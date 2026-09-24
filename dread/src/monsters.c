/*
 * Monster AI.
 *
 * Each creature idles until it sees the player (grid line of sight) or
 * hears gunfire (see sound_init below), then chases: it steps
 * toward the player in one of 8 directions, picking a new direction when
 * blocked or after a few steps, the way DOOM's monsters do. When it has a
 * clear shot it winds up and attacks; hits make it flinch; at 0 health it
 * plays its death frames and stays as a corpse.
 *
 * Sound travels through regions: sound_init() splits the level's open
 * cells into areas bounded by walls and doors when the level loads, and
 * gunfire reaches every area joined to the player's through doors that
 * are at least half open.
 */
#include <stdlib.h>
#include <string.h>

#include "dread.h"
#include "hud.h"
#include "render.h"

enum { ATK_HITSCAN, ATK_MISSILE, ATK_MELEE };
/* frame offsets within a creature's 10 sprites (tools/enemies.py) */
enum { FR_WALK0, FR_WALK1, FR_WALK2, FR_WINDUP, FR_ATTACK, FR_PAIN,
       FR_DIE0, FR_DIE1, FR_DIE2, FR_CORPSE };

#define DIR_NONE      8
#define SIGHT_RANGE   (24 * TILE_UNITS)
#define PLAYER_RADIUS 72
#define MELEE_FLAG    0x80           /* in thing_t.shots: this attack is melee */
#define region_map    (arena + ARENA_REGIONS)

typedef struct {
    uint16_t hp;        /* first: a uint16_t is 2-byte aligned */
    uint8_t base;       /* SPR_ of the creature's first frame */
    uint8_t speed;      /* Q8 per tic */
    uint8_t charge;     /* speed while charging (0 = never charges) */
    uint8_t pain;       /* flinch chance out of 256 */
    uint8_t attack;     /* ATK_* */
    uint8_t dmg_min, dmg_max;
    uint8_t shots;      /* per attack */
    uint8_t windup;     /* tics aiming before the first shot */
    uint8_t reload;     /* tics before it may attack again */
    uint8_t radius;     /* Q8 */
    uint8_t claws;      /* melee damage when adjacent (ranged attackers), 0 = none */
    uint8_t missile;    /* TH_* thrown by ATK_MISSILE */
    uint8_t pad;        /* 16 bytes: indexing is a shift, not a multiply */
} monclass_t;
_Static_assert(sizeof(monclass_t) == 16, "monclass_t is indexed by shifting");

/* indexed by the thing's arg in tools/things.py */
static const monclass_t classes[] = {
    /* grunt */ { 20, SPR_GRUNT_WALK0, 10, 0, 200, ATK_HITSCAN, 3, 12, 1, 10, 40, 64, 0, 0, 0 },
    /* heavy */ { 50, SPR_HEAVY_WALK0, 9, 0, 120, ATK_HITSCAN, 3, 12, 3, 12, 60, 72, 0, 0, 0 },
    /* imp */   { 40, SPR_IMP_WALK0, 12, 0, 150, ATK_MISSILE, 6, 18, 1, 12, 45, 64, 12,
                  TH_FIREBALL, 0 },
    /* brute */ { 150, SPR_BRUTE_WALK0, 7, 22, 60, ATK_MELEE, 15, 35, 1, 8, 25, 96, 0, 0, 0 },
    /* the Warden: volleys of three rockets, stomps anyone next to it */
    /* boss */  { 1200, SPR_BOSS_WALK0, 8, 0, 16, ATK_MISSILE, 20, 45, 3, 16, 50, 120, 25,
                  TH_BOSSROCKET, 0 },
};

static const int8_t dir_x[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };   /* E SE S SW W NW N NE */
static const int8_t dir_y[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
static const uint8_t walk_cycle[4] = { FR_WALK0, FR_WALK1, FR_WALK2, FR_WALK1 };

static uint16_t rng = 0x1D2F;

uint8_t random8(void)
{
    rng = rng * 25173u + 13849u;
    return (uint8_t)(rng >> 8);
}

static const monclass_t *class_of(const thing_t *t)
{
    return &classes[thingdefs[t->type].arg];
}

static void set_frame(thing_t *t, uint8_t frame)
{
    t->sprite = class_of(t)->base + frame;
}

static int approx_dist(int dx, int dy)
{
    if (dx < 0)
        dx = -dx;
    if (dy < 0)
        dy = -dy;
    return dx > dy ? dx + dy / 2 : dy + dx / 2;
}

static int roll(uint8_t lo, uint8_t hi)
{
    return lo + (int)(((unsigned)random8() * (unsigned)(hi - lo + 1)) >> 8);
}

void monster_init(thing_t *t)
{
    const monclass_t *mc = class_of(t);

    t->hp = mc->hp;
    t->state = MS_IDLE;
    t->dir = DIR_NONE;
    t->anim = random8();
    t->sprite = mc->base;
}

/* Grid walk between two points in Q4 (1/16 tile) so every term stays well
 * inside 24 bits; walls and doors that aren't at least half open block. */
bool line_clear(int x0, int y0, int x1, int y1)
{
    int ax = (int)((unsigned)x0 >> 4), ay = (int)((unsigned)y0 >> 4);
    int bx = (int)((unsigned)x1 >> 4), by = (int)((unsigned)y1 >> 4);
    int cx = ax >> 4, cy = ay >> 4, tx = bx >> 4, ty = by >> 4;
    int dx = bx - ax, dy = by - ay;
    int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1;
    int adx = abs(dx), ady = abs(dy);
    /* compare distances to the next x and y lines, scaled by the other axis */
    int ex = (sx > 0 ? 16 - (ax & 15) : (ax & 15)) * ady;
    int ey = (sy > 0 ? 16 - (ay & 15) : (ay & 15)) * adx;
    int stepx = 16 * ady, stepy = 16 * adx;
    uint8_t guard = 0;

    while ((cx != tx || cy != ty) && ++guard < 100) {
        uint8_t t;
        if (ex < ey) {
            cx += sx;
            ex += stepx;
        } else {
            cy += sy;
            ey += stepy;
        }
        t = MAP_AT(cx, cy);
        if (t == TILE_EMPTY)
            continue;
        if (IS_DOOR(t) && doors[DOOR_INDEX(t)].open >= 128)
            continue;
        return false;
    }
    return true;
}

static void wake(thing_t *t)
{
    if (t->state == MS_IDLE) {
        t->state = MS_CHASE;
        t->reload = 10 + (random8() & 31);   /* reaction time */
        t->movecount = 0;
    }
}

/* Moves one step if nothing is in the way. Bumping a closed, unlocked
 * sliding door opens it. */
static bool try_step(thing_t *t, uint8_t dir, uint8_t speed)
{
    const monclass_t *mc = class_of(t);
    int step = (dir & 1) ? (speed * 181) >> 8 : speed;   /* diagonals: /sqrt 2 */
    int nx = t->x, ny = t->y;
    int r = mc->radius;
    unsigned x0, x1, y0, y1, cx, cy;

    if (dir_x[dir] > 0)
        nx += step;
    else if (dir_x[dir] < 0)
        nx -= step;
    if (dir_y[dir] > 0)
        ny += step;
    else if (dir_y[dir] < 0)
        ny -= step;
    x0 = CELL(nx - r);
    x1 = CELL(nx + r);
    y0 = CELL(ny - r);
    y1 = CELL(ny + r);

    for (cy = y0; cy <= y1; cy++) {
        for (cx = x0; cx <= x1; cx++) {
            uint8_t tile = MAP_AT(cx, cy);
            if (tile == TILE_EMPTY)
                continue;
            if (IS_DOOR(tile)) {
                if (!door_blocks(DOOR_INDEX(tile)))
                    continue;
                door_open_quiet(DOOR_INDEX(tile));
            }
            return false;
        }
    }
    if (!player_dead) {
        int dx = player.x - nx, dy = player.y - ny;
        int reach = r + PLAYER_RADIUS;
        if (dx > -reach && dx < reach && dy > -reach && dy < reach)
            return false;
    }
    if (thing_blocks_at(nx, ny, r - 16, t))
        return false;
    blockmap_move(t, nx, ny);
    return true;
}

/* Blocked while heading along axis direction dir: if the wall ahead holds
 * an unlocked sliding door, straight ahead or one cell to the side, line up
 * with the doorway (a side step toward its row or column), or wait there
 * while it opens. Without this a monster pressing against the wall next to
 * a door only finds the way through by chance. */
static bool seek_door(thing_t *t, uint8_t dir, uint8_t speed)
{
    int8_t ddx = dir_x[dir], ddy = dir_y[dir];
    unsigned ax = CELL(t->x) + ddx, ay = CELL(t->y) + ddy;
    uint8_t across = ddx ? (uint8_t)t->y : (uint8_t)t->x;   /* position in the cell */
    uint8_t slack = TILE_UNITS / 2 - class_of(t)->radius;
    int8_t k;

    for (k = -1; k <= 1; k++) {
        uint8_t tile = MAP_AT(ax + (ddy ? k : 0), ay + (ddx ? k : 0));
        const door_t *d;
        uint8_t side;

        if (!IS_DOOR(tile))
            continue;
        d = &doors[DOOR_INDEX(tile)];
        if (d->lock || (d->flags & DOOR_FLUSH))
            continue;
        if (k == 0 && across >= TILE_UNITS / 2 - slack && across <= TILE_UNITS / 2 + slack) {
            door_open_quiet(DOOR_INDEX(tile));   /* lined up: wait for it */
            t->movecount = 0;
            return true;
        }
        /* toward the doorway's row (moving east/west) or column */
        if (k)
            side = ddx ? (k > 0 ? 2 : 6) : (k > 0 ? 0 : 4);
        else
            side = ddx ? (across < TILE_UNITS / 2 ? 2 : 6) : (across < TILE_UNITS / 2 ? 0 : 4);
        if (try_step(t, side, speed)) {
            t->dir = side;
            t->movecount = 0;                    /* look again next think */
            return true;
        }
    }
    return false;
}

/* DOOM-style: try the direct diagonal, then each axis (larger first), then
 * a doorway in the way, then the old direction, then anything but turning
 * around. */
static void new_chase_dir(thing_t *t, uint8_t speed)
{
    int dx = player.x - t->x, dy = player.y - t->y;
    uint8_t old = t->dir;
    uint8_t turnaround = old == DIR_NONE ? DIR_NONE : (old + 4) & 7;
    uint8_t d1 = dx > 64 ? 0 : dx < -64 ? 4 : DIR_NONE;
    uint8_t d2 = dy > 64 ? 2 : dy < -64 ? 6 : DIR_NONE;
    uint8_t d, k, start;

    if (d1 != DIR_NONE && d2 != DIR_NONE) {
        d = d1 == 0 ? (d2 == 2 ? 1 : 7) : (d2 == 2 ? 3 : 5);
        if (d != turnaround && try_step(t, d, speed))
            goto found;
    }
    if (random8() > 200 || abs(dy) > abs(dx)) {
        d = d1;
        d1 = d2;
        d2 = d;
    }
    if (d1 != DIR_NONE && d1 != turnaround && try_step(t, d1, speed)) {
        d = d1;
        goto found;
    }
    if (d2 != DIR_NONE && d2 != turnaround && try_step(t, d2, speed)) {
        d = d2;
        goto found;
    }
    if ((d1 != DIR_NONE && seek_door(t, d1, speed)) || (d2 != DIR_NONE && seek_door(t, d2, speed)))
        return;
    if (old != DIR_NONE && try_step(t, old, speed)) {
        d = old;
        goto found;
    }
    start = random8() & 7;
    for (k = 0; k < 8; k++) {
        d = (start + k) & 7;
        if (d != turnaround && try_step(t, d, speed))
            goto found;
    }
    if (turnaround != DIR_NONE && try_step(t, turnaround, speed)) {
        d = turnaround;
        goto found;
    }
    t->dir = DIR_NONE;
    return;
found:
    t->dir = d;
    t->movecount = 4 + (random8() & 15);
}

static void start_attack(thing_t *t, const monclass_t *mc, bool melee)
{
    t->state = MS_WINDUP;
    t->tics = mc->windup;
    t->shots = melee ? (MELEE_FLAG | 1) : mc->shots;
    set_frame(t, FR_WINDUP);
}

static int melee_reach(const monclass_t *mc)
{
    return mc->radius + PLAYER_RADIUS + 48;
}

static void fire(thing_t *t, const monclass_t *mc)
{
    int dist = approx_dist(player.x - t->x, player.y - t->y);

    t->state = MS_ATTACK;
    t->tics = 6;
    set_frame(t, FR_ATTACK);
    if (player_dead)
        return;
    if (t->shots & MELEE_FLAG) {
        if (dist < melee_reach(mc) + 32)
            player_damage(mc->attack == ATK_MELEE ? roll(mc->dmg_min, mc->dmg_max)
                                                  : roll(mc->claws / 2, mc->claws));
        return;
    }
    if (mc->attack == ATK_MISSILE) {
        spawn_missile(mc->missile, t, roll(mc->dmg_min, mc->dmg_max));
        return;
    }
    /* hitscan: needs a clear line now; farther shots miss more */
    if (line_clear(t->x, t->y, player.x, player.y)) {
        int tiles = (int)CELL(dist);
        int chance = 230 - tiles * 10;
        if (chance < 50)
            chance = 50;
        if (random8() < chance)
            player_damage(roll(mc->dmg_min, mc->dmg_max));
    }
}

/* A chasing monster thinks on every other tic (alternating between
 * monsters) and makes up for it with double steps and attack chances: the
 * AI is the most expensive C code in a fight. */
static void chase(thing_t *t, const monclass_t *mc, uint8_t index)
{
    int dx, dy, dist;
    uint8_t speed = mc->speed * 2;

    if (t->reload)
        t->reload--;
    t->anim++;
    if ((((uint8_t)level.tics + index) & 1) != 0)
        return;
    dx = player.x - t->x;
    dy = player.y - t->y;
    dist = approx_dist(dx, dy);
    if ((((uint8_t)level.tics + index) & 3) == 0)
        t->los = !player_dead && dist < SIGHT_RANGE && line_clear(t->x, t->y, player.x, player.y);

    if (!t->reload && !player_dead) {
        bool can_melee = mc->attack == ATK_MELEE || mc->claws;
        if (can_melee && dist < melee_reach(mc)) {
            start_attack(t, mc, true);
            return;
        }
        if (mc->attack != ATK_MELEE && t->los) {
            int tiles = (int)CELL(dist);
            int chance = tiles < 16 ? 80 - tiles * 4 : 16;
            if (random8() < chance) {
                start_attack(t, mc, false);
                return;
            }
        }
    }
    if (mc->charge && t->los && dist < 6 * TILE_UNITS)
        speed = mc->charge * 2;
    if (t->dir == DIR_NONE || t->movecount == 0 || !try_step(t, t->dir, speed))
        new_chase_dir(t, speed);
    else
        t->movecount--;
    set_frame(t, walk_cycle[(t->anim >> 3) & 3]);
}

void monster_tic(thing_t *t, uint8_t index)
{
    const monclass_t *mc = class_of(t);

    switch (t->state) {
    case MS_IDLE:
        if ((((uint8_t)level.tics + index) & 7) == 0 && !player_dead &&
            approx_dist(player.x - t->x, player.y - t->y) < SIGHT_RANGE &&
            line_clear(t->x, t->y, player.x, player.y))
            wake(t);
        break;
    case MS_CHASE:
        chase(t, mc, index);
        break;
    case MS_WINDUP:
        if (--t->tics == 0)
            fire(t, mc);
        break;
    case MS_ATTACK:
        if (--t->tics == 0) {
            if (--t->shots & ~MELEE_FLAG) {
                t->state = MS_WINDUP;       /* next shot of a burst */
                t->tics = 5;
                set_frame(t, FR_WINDUP);
            } else {
                t->state = MS_CHASE;
                t->reload = mc->reload;
                set_frame(t, FR_WALK0);
            }
        }
        break;
    case MS_PAIN:
        if (--t->tics == 0) {
            t->state = MS_CHASE;
            set_frame(t, FR_WALK0);
        }
        break;
    case MS_DYING:
        if (--t->tics == 0) {
            uint8_t frame = t->sprite - mc->base + 1;
            bool boss = thingdefs[t->type].flags & TF_BIG;
            set_frame(t, frame);
            if (frame == FR_CORPSE) {
                t->state = MS_DEAD;
                if (boss && level.boss)
                    level.exiting = true;       /* the episode is won */
            } else {
                t->tics = boss ? 12 : 5;        /* the boss dies slowly */
            }
        }
        break;
    }
}

void monster_damage(thing_t *t, int damage)
{
    const monclass_t *mc = class_of(t);

    if (t->state >= MS_DYING)
        return;
    t->hp -= damage;
    if (t->hp <= 0) {
        t->state = MS_DYING;
        t->tics = 5;
        set_frame(t, FR_DIE0);
        thing_unsolid(t);
        level.kills++;
        return;
    }
    wake(t);
    if (random8() < mc->pain) {
        t->state = MS_PAIN;
        t->tics = 6;
        set_frame(t, FR_PAIN);
    }
}

static const int8_t neighbor[4] = { 1, -1, MAP_SIZE, -MAP_SIZE };
static uint8_t door_side[MAX_DOORS][2];   /* the regions a door joins */
static uint8_t num_regions;

/* Labels every open cell with a region number (1..255) by flood fill, and
 * records which two regions each door separates. Uses the sprite cache
 * slots as the fill queue (8 KB: room for every cell). */
void sound_init(void)
{
    uint16_t *queue = (uint16_t *)(arena + ARENA_SPRITES);
    const door_t *dp = doors;
    unsigned c;
    uint8_t d;

    memset(region_map, 0, MAP_SIZE * MAP_SIZE);
    num_regions = 0;
    for (c = MAP_SIZE; c < MAP_SIZE * (MAP_SIZE - 1) && num_regions < 255; c++) {
        uint16_t *head = queue, *tail = queue;

        if (level_map[c] != TILE_EMPTY || region_map[c])
            continue;
        region_map[c] = ++num_regions;
        *tail++ = (uint16_t)c;
        while (head < tail) {
            unsigned n = *head++;
            uint8_t k;
            for (k = 0; k < 4; k++) {
                unsigned m = n + neighbor[k];
                if (level_map[m] == TILE_EMPTY && !region_map[m]) {
                    region_map[m] = num_regions;
                    *tail++ = (uint16_t)m;
                }
            }
        }
    }
    sprites_flush();
    for (d = 0; d < num_doors; d++, dp++) {
        uint8_t a = 0, b = 0, k;
        c = ((unsigned)dp->y << MAP_SHIFT) + dp->x;
        for (k = 0; k < 4; k++) {
            uint8_t r = region_map[c + neighbor[k]];
            if (r && !a)
                a = r;
            else if (r && r != a)
                b = r;
        }
        door_side[d][0] = a;
        door_side[d][1] = b ? b : a;
    }
}

/* Gunfire wakes every idle monster in a region the sound reaches. */
void monsters_hear(void)
{
    static uint8_t heard[256];
    unsigned pc = (CELL(player.y) << MAP_SHIFT) + CELL(player.x);
    uint8_t tile = level_map[pc], i;
    bool spread;
    thing_t *t;

    memset(heard, 0, sizeof heard);
    heard[region_map[pc]] = 1;
    if (IS_DOOR(tile)) {                    /* standing in a doorway */
        heard[door_side[DOOR_INDEX(tile)][0]] = 1;
        heard[door_side[DOOR_INDEX(tile)][1]] = 1;
    }
    heard[0] = 0;
    do {
        const door_t *dp = doors;
        const uint8_t *side = door_side[0];
        spread = false;
        for (i = num_doors; i; i--, dp++, side += 2) {
            if (dp->open >= 128 && heard[side[0]] != heard[side[1]]) {
                heard[side[0]] = heard[side[1]] = 1;
                spread = true;
            }
        }
    } while (spread);
    for (i = num_things, t = things; i; i--, t++) {
        if ((t->flags & THING_ACTIVE) && t->state == MS_IDLE &&
            thingdefs[t->type].kind == KIND_MONSTER &&
            heard[region_map[(CELL(t->y) << MAP_SHIFT) + CELL(t->x)]])
            wake(t);
    }
}
