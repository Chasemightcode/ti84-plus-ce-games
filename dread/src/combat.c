/*
 * Combat: the player's weapons (a small state machine per weapon: ready,
 * fire frame, recoil, plus lowering and raising when switching), hitscan
 * and melee attacks, damage to the player, missiles, short-lived effects
 * and the first-person weapon sprite.
 *
 * Hitscan uses what the renderer saw last frame: rc_seen (depth and lateral
 * offset of every thing in view) and the per-column wall depth in zbuffer.
 * A shot down a column hits the nearest shootable thing whose sprite covers
 * that column and that stands in front of the wall there.
 *
 * Rockets are missiles like the imps' fireballs, but they hit monsters and
 * solid decorations, and their blast damages everything in its radius that
 * it has a clear line to, the player included.
 */
#include <fileioc.h>
#include <graphx.h>
#include <stdlib.h>

#include "dread.h"
#include "gen/weapons.h"
#include "gfx/dreaden.h"
#include "gfx/dreadwp.h"
#include "hud.h"
#include "render.h"

#define NO_AMMO        0xFF
#define HEAR_EVERY     18         /* tics between gunfire wake-ups */
#define MISSILE_LIFE   140        /* tics (4 s) */
#define MISSILE_HIT    102        /* 0.4 tile: missile against the player */
#define EFFECT_TICS    4          /* tics per effect frame */
#define SWITCH_STEP    10         /* logical px per tic when lowering/raising */
#define SWITCH_DROP    60
#define BOB_STEP       36         /* bob phase, angle units per tic */
#define BOB_MAX        8          /* bob amplitude, logical px */
#define PUNCH_SLACK    5          /* extra columns either side for fists */
#define ROCKET_START   96         /* spawned this far in front of the player */
#define MISSILE_RADIUS 16         /* rocket against solid things (+80) */
#define SPLASH_MAX     128        /* blast damage at the center */
#define FLASH_TICS     2          /* muzzle light at the start of a shot */

enum { WS_READY, WS_FIRE, WS_RECOIL, WS_LOWER, WS_RAISE };

typedef struct {
    uint8_t ammo;                   /* AMMO_* or NO_AMMO */
    uint8_t idle, recoil;           /* WF_* frames */
    uint8_t fire[2];                /* fire frames, alternating shot by shot */
    uint8_t fire_tics, recoil_tics; /* fire_tics + recoil_tics = refire time */
    uint8_t dmg_min, dmg_max;       /* per pellet, or a rocket's direct hit */
    uint8_t pellets;
    uint8_t spread;                 /* max column offset of inaccurate shots */
    uint8_t missile;                /* TH_* it launches, 0 for hitscan */
    uint16_t melee;                 /* reach, Q8; 0 for guns */
    uint8_t pad;                    /* 16 bytes: indexing is a shift */
} weapon_t;

static const weapon_t weapon_info[NUM_WEAPONS] = {
    /* fists */
    { NO_AMMO, WF_FISTS_IDLE, WF_FISTS_IDLE, { WF_FISTS_PUNCH, WF_FISTS_PUNCH },
      8, 8, 2, 20, 1, 0, 0, 282, 0 },
    /* pistol: the first shot of a burst is exact */
    { AMMO_BULLETS, WF_PISTOL_IDLE, WF_PISTOL_RECOIL, { WF_PISTOL_FIRE, WF_PISTOL_FIRE },
      4, 8, 5, 15, 1, 3, 0, 0, 0 },
    /* scattergun: 7 pellets over a wide cone, devastating up close */
    { AMMO_SHELLS, WF_SHOTGUN_IDLE, WF_SHOTGUN_PUMP, { WF_SHOTGUN_FIRE, WF_SHOTGUN_FIRE },
      6, 20, 5, 15, 7, 9, 0, 0, 0 },
    /* rotary cannon: a shot every 4 tics, barrels turning shot by shot */
    { AMMO_BULLETS, WF_CANNON_IDLE, WF_CANNON_IDLE, { WF_CANNON_FIRE0, WF_CANNON_FIRE1 },
      4, 0, 5, 15, 1, 4, 0, 0, 0 },
    /* launcher: rockets with splash damage */
    { AMMO_ROCKETS, WF_LAUNCHER_IDLE, WF_LAUNCHER_RECOIL, { WF_LAUNCHER_FIRE, WF_LAUNCHER_FIRE },
      6, 14, 30, 80, 1, 0, TH_ROCKETFLY, 0, 0 },
};
_Static_assert(sizeof(weapon_t) == 16, "weapon_t is indexed by shifting");

/* when the current weapon runs dry, switch to the first of these with ammo */
static const uint8_t preference[NUM_WEAPONS] = {
    WP_CHAINGUN, WP_SHOTGUN, WP_PISTOL, WP_LAUNCHER, WP_FISTS
};

bool player_dead;

static gfx_rletsprite_t *wframe[NUM_WEAPON_FRAMES];
static const int8_t frame_dx[NUM_WEAPON_FRAMES] = WEAPON_FRAME_DX;
static uint8_t wstate, wtics;
static uint8_t pending;             /* weapon to switch to */
static uint8_t drop;                /* px the weapon is lowered while switching */
static bool refire;                 /* this shot follows the last with 2nd held */
static uint8_t shots_fired;         /* picks the alternating fire frame */
static uint8_t hear_wait;
static unsigned bob_phase;
static uint8_t bob_amp;
static int last_x, last_y;

static const char *check_var(const char *name, unsigned size, const char *missing,
                             const char *wrong)
{
    uint8_t h = ti_Open(name, "r");

    if (!h)
        return missing;
    if (ti_GetSize(h) != size) {
        ti_Close(h);
        return wrong;
    }
    ti_Close(h);
    return NULL;
}

const char *combat_load(void)
{
    const char *err = check_var("DREADEN", DREADEN_appvar_size, "DREADEN appvar missing",
                                "DREADEN is the wrong version");

    if (!err)
        err = check_var("DREADWP", DREADWP_appvar_size, "DREADWP appvar missing",
                        "DREADWP is the wrong version");
    if (err)
        return err;
    if (!DREADEN_init() || !DREADWP_init())
        return "DREADEN/DREADWP unreadable";
    WEAPON_FRAME_BIND(wframe);
    return NULL;
}

void combat_reset(void)
{
    player_dead = false;
    wstate = WS_READY;
    wtics = 0;
    pending = player.weapon;
    drop = 0;
    refire = false;
    hear_wait = 0;
    bob_amp = 0;
    last_x = player.x;
    last_y = player.y;
}

static bool usable(uint8_t wp)
{
    return wp < NUM_WEAPONS && (player.weapons & (1 << wp));
}

void weapon_select(uint8_t wp)
{
    if (usable(wp))
        pending = wp;
}

static bool has_ammo(uint8_t wp)
{
    uint8_t a = weapon_info[wp].ammo;

    return a == NO_AMMO || player.ammo[a] > 0;
}

static int roll(uint8_t lo, uint8_t hi)
{
    return lo + (int)(((unsigned)random8() * (unsigned)(hi - lo + 1)) >> 8);
}

static int approx_dist(int dx, int dy)
{
    dx = abs(dx);
    dy = abs(dy);
    return dx > dy ? dx + dy / 2 : dy + dx / 2;
}

bool weapon_flash(void)
{
    const weapon_t *w = &weapon_info[player.weapon];

    return wstate == WS_FIRE && !w->melee && wtics + FLASH_TICS > w->fire_tics;
}

/* ------------------------------------------------------------ attacks */

/* Nearest shootable thing whose sprite covers column col, nearer than
 * max_depth (Q8). */
static thing_t *target_at(int col, unsigned max_depth)
{
    thing_t *best = NULL;
    const seen_t *sn = rc_seen;
    uint8_t i;

    for (i = rc_nseen; i; i--, sn++) {
        thing_t *t = sn->thing;
        const spritebox_t *box;
        int depth = sn->depth, size, x0, y_cell;

        if ((unsigned)depth >= max_depth || !(t->flags & THING_ACTIVE) ||
            !(thingdefs[t->type].flags & TF_SHOOT) || t->state >= MS_DYING)
            continue;
        size = (int)sprite_project(sn, &x0, &y_cell);      /* as drawn */
        box = &sprite_box[t->sprite];
        if (col < x0 + ((box->left * size) >> 5) - 1 || col > x0 + ((box->right * size) >> 5))
            continue;
        best = t;
        max_depth = (unsigned)depth;
    }
    return best;
}

static void hit_thing(thing_t *t, int damage)
{
    /* the blood goes a quarter tile in front so it sorts over the sprite */
    spawn_effect(TH_BLOODHIT, t->x - (fcos(player.angle) >> 8), t->y - (fsin(player.angle) >> 8));
    monster_damage(t, damage);
}

/* A bullet down view column col. */
static void shoot_column(int col, int damage)
{
    unsigned wall = zbuffer[col];
    thing_t *t = target_at(col, wall);
    int d, lp, c9, s9, kx, ky;

    if (t) {
        hit_thing(t, damage);
        return;
    }
    /* puff where the ray meets the wall, pulled back 1/8 tile: the point at
     * perpendicular depth d along the column's ray is
     * player + d * (dir + right * lp / FOCAL), right = (-sin, cos) */
    d = (int)wall - TILE_UNITS / 8;
    if (d > 16000)
        d = 16000;
    if (d < 0)
        d = 0;
    lp = col - VIEW_W / 2;
    c9 = fcos(player.angle) >> 5;
    s9 = fsin(player.angle) >> 5;
    kx = c9 - s9 * lp / FOCAL;
    ky = s9 + c9 * lp / FOCAL;
    d >>= 1;                               /* (d/2) * k stays under 2^23 */
    spawn_effect(TH_PUFF, player.x + ((d * kx) >> 8), player.y + ((d * ky) >> 8));
}

static void punch(const weapon_t *w)
{
    static const int8_t cols[3] = { 0, -PUNCH_SLACK, PUNCH_SLACK };
    uint8_t i;

    for (i = 0; i < 3; i++) {
        int col = VIEW_W / 2 + cols[i];
        unsigned reach = zbuffer[col] < w->melee ? zbuffer[col] : w->melee;
        thing_t *t = target_at(col, reach);
        if (t) {
            hit_thing(t, roll(w->dmg_min, w->dmg_max));
            return;
        }
    }
}

/* The rocket starts a little in front of the player, or at the player
 * when that is already inside a wall (then it goes off right away). */
static void fire_rocket(const weapon_t *w)
{
    int c = fcos(player.angle), s = fsin(player.angle);
    int x = player.x + ((c * ROCKET_START) >> 14), y = player.y + ((s * ROCKET_START) >> 14);
    uint8_t tile = MAP_AT((unsigned)x >> FRAC_BITS, (unsigned)y >> FRAC_BITS);
    thing_t *t;

    if (tile != TILE_EMPTY && !(IS_DOOR(tile) && !door_blocks(DOOR_INDEX(tile)))) {
        x = player.x;
        y = player.y;
    }
    t = thing_spawn(w->missile, x, y);
    if (!t)
        return;
    t->dx = (c * thingdefs[w->missile].amount) >> 14;      /* amount: speed */
    t->dy = (s * thingdefs[w->missile].amount) >> 14;
    t->hp = roll(w->dmg_min, w->dmg_max);
    t->tics = MISSILE_LIFE;
    t->shots = 1;                       /* the player's: it hits monsters */
}

static void fire_weapon(void)
{
    const weapon_t *w = &weapon_info[player.weapon];
    uint8_t i;

    wstate = WS_FIRE;
    wtics = w->fire_tics;
    shots_fired++;
    if (w->melee) {
        punch(w);
        return;
    }
    player.ammo[w->ammo]--;
    hud_dirty(HUD_AMMO | HUD_TABLE);
    if (!hear_wait) {
        monsters_hear();
        hear_wait = HEAR_EVERY;
    }
    if (w->missile) {
        fire_rocket(w);
        return;
    }
    for (i = 0; i < w->pellets; i++) {
        int col = VIEW_W / 2;
        if (w->spread && (refire || w->pellets > 1))
            col += (int)(((unsigned)random8() * (2u * w->spread + 1)) >> 8) - w->spread;
        shoot_column(col, roll(w->dmg_min, w->dmg_max));
    }
}

static void switch_if_dry(void)
{
    uint8_t i;

    for (i = 0; i < NUM_WEAPONS; i++) {
        uint8_t wp = preference[i];
        if (usable(wp) && has_ammo(wp)) {
            pending = wp;
            return;
        }
    }
}

/* End of a shot: fire again right away while 2nd is held. */
static void shot_done(bool fire)
{
    wstate = WS_READY;
    if (fire && pending == player.weapon && has_ammo(player.weapon)) {
        refire = true;
        fire_weapon();
    }
}

void combat_tic(bool fire)
{
    bool moving = player.x != last_x || player.y != last_y;

    last_x = player.x;
    last_y = player.y;
    if (moving && !player_dead) {
        bob_phase += BOB_STEP;
        if (bob_amp < BOB_MAX)
            bob_amp++;
    } else if (bob_amp) {
        bob_amp--;
    }
    if (hear_wait)
        hear_wait--;
    if (player_dead)
        return;

    switch (wstate) {
    case WS_READY:
        if (pending != player.weapon) {
            wstate = WS_LOWER;
        } else if (!fire) {
            refire = false;
        } else if (has_ammo(player.weapon)) {
            fire_weapon();
        } else {
            switch_if_dry();
        }
        break;
    case WS_FIRE:
        if (--wtics == 0) {
            wtics = weapon_info[player.weapon].recoil_tics;
            if (wtics)
                wstate = WS_RECOIL;
            else
                shot_done(fire);
        }
        break;
    case WS_RECOIL:
        if (--wtics == 0)
            shot_done(fire);
        break;
    case WS_LOWER:
        if (pending == player.weapon) {
            wstate = WS_RAISE;           /* changed its mind */
        } else if (drop >= SWITCH_DROP - SWITCH_STEP) {
            drop = SWITCH_DROP;
            player.weapon = pending;
            hud_dirty(HUD_ARMS | HUD_AMMO);
            wstate = WS_RAISE;
        } else {
            drop += SWITCH_STEP;
        }
        break;
    case WS_RAISE:
        if (drop <= SWITCH_STEP) {
            drop = 0;
            wstate = WS_READY;
        } else {
            drop -= SWITCH_STEP;
        }
        break;
    }
}

/* ------------------------------------------------------- player damage */

void player_damage(int amount)
{
    if (player_dead || amount <= 0)
        return;
    if (game_skill == SKILL_EASY)
        amount = (amount + 1) / 2;
    else if (game_skill == SKILL_HARD)
        amount += amount / 4;
    if (player.armor_class) {
        /* the vest stops a third, the plate half, while its points last */
        int saved = player.armor_class == 1 ? amount / 3 : amount / 2;
        if (saved >= player.armor) {
            saved = player.armor;
            player.armor_class = 0;
        }
        player.armor -= saved;
        amount -= saved;
    }
    player.health -= amount;
    fx_damage(amount);
    hud_dirty(HUD_HEALTH | HUD_ARMOR | HUD_FACE);
    if (player.health <= 0) {
        player.health = 0;
        player_dead = true;
        hud_message("You died. Press 2nd to try again.");
    }
}

/* -------------------------------------------------- missiles, effects */

/* Blast damage: full at the center, falling to 0 at radius r (Q8), for
 * the player and (for the player's own rockets) every shootable thing with
 * a clear line to (x, y). Monsters don't hurt each other. */
static void splash(int x, int y, int r, bool players)
{
    thing_t *t = things;
    uint8_t i;
    int d;

    for (i = players ? num_things : 0; i; i--, t++) {
        if (!(t->flags & THING_ACTIVE) || !(thingdefs[t->type].flags & TF_SHOOT) ||
            t->state >= MS_DYING)
            continue;
        d = approx_dist(t->x - x, t->y - y) - TILE_UNITS / 4;   /* body width */
        if (d < 0)
            d = 0;
        if (d < r && line_clear(x, y, t->x, t->y))
            monster_damage(t, SPLASH_MAX * (r - d) / r);
    }
    if (player_dead)
        return;
    d = approx_dist(player.x - x, player.y - y) - TILE_UNITS / 4;
    if (d < 0)
        d = 0;
    if (d < r && line_clear(x, y, player.x, player.y))
        player_damage(SPLASH_MAX * (r - d) / r);
}

static void explode(thing_t *t)
{
    t->type = TH_BOOM;
    t->sprite = SPR_BOOM0;
    t->tics = EFFECT_TICS;
    t->anim = 0;
}

/* A missile hits a wall or, for the player's rockets, a solid thing. */
static void detonate(thing_t *t, thing_t *hit)
{
    uint8_t radius = thingdefs[t->type].arg;           /* quarter tiles */

    if (hit && (thingdefs[hit->type].flags & TF_SHOOT))
        monster_damage(hit, t->hp);
    if (radius)
        splash(t->x, t->y, radius * (TILE_UNITS / 4), t->shots != 0);
    explode(t);
}

void missile_tic(thing_t *t)
{
    int nx = t->x + t->dx, ny = t->y + t->dy;
    uint8_t tile = MAP_AT((unsigned)nx >> FRAC_BITS, (unsigned)ny >> FRAC_BITS);
    thing_t *hit;

    if (--t->tics == 0) {
        t->flags &= ~THING_ACTIVE;
        return;
    }
    t->anim++;
    t->sprite = thingdefs[t->type].sprite + ((t->anim >> 2) & 1);
    if (tile != TILE_EMPTY && !(IS_DOOR(tile) && !door_blocks(DOOR_INDEX(tile)))) {
        detonate(t, NULL);               /* stays just in front of the wall */
        return;
    }
    if (t->shots && (hit = thing_at(nx, ny, MISSILE_RADIUS, NULL)) != NULL) {
        detonate(t, hit);
        return;
    }
    t->x = nx;
    t->y = ny;
    if (!t->shots && !player_dead && abs(player.x - nx) < MISSILE_HIT &&
        abs(player.y - ny) < MISSILE_HIT) {
        /* no explosion sprite in the player's face: it would fill the whole
         * view (and the frame time); the red flash shows the hit */
        player_damage(t->hp);
        t->flags &= ~THING_ACTIVE;
    }
}

void effect_tic(thing_t *t)
{
    uint8_t frames = t->type == TH_BOOM ? 3 : 2;

    if (--t->tics)
        return;
    if (++t->anim >= frames) {
        t->flags &= ~THING_ACTIVE;
        return;
    }
    t->tics = EFFECT_TICS;
    t->sprite = thingdefs[t->type].sprite + t->anim;
}

void spawn_effect(uint8_t type, int x, int y)
{
    thing_t *t = thing_spawn(type, x, y);

    if (t)
        t->tics = EFFECT_TICS;
}

void spawn_missile(uint8_t type, const thing_t *from, int damage)
{
    int dx = player.x - from->x, dy = player.y - from->y;
    int ax = abs(dx), ay = abs(dy);
    int dist = ax > ay ? ax + ay / 2 : ay + ax / 2;
    thing_t *t;

    if (dist < 1)
        dist = 1;
    t = thing_spawn(type, from->x, from->y);
    if (!t)
        return;
    t->dx = dx * thingdefs[type].amount / dist;           /* amount: speed */
    t->dy = dy * thingdefs[type].amount / dist;
    t->hp = damage;
    t->tics = MISSILE_LIFE;
    /* start in front of the thrower rather than inside it */
    t->x += t->dx * 2;
    t->y += t->dy * 2;
}

/* ------------------------------------------------------ weapon sprite */

void weapon_draw(uint8_t *fb)
{
    const weapon_t *w = &weapon_info[player.weapon];
    const gfx_rletsprite_t *spr;
    uint8_t f;
    int x, y;

    if (player_dead)
        return;
    f = wstate == WS_FIRE ? w->fire[shots_fired & 1] : wstate == WS_RECOIL ? w->recoil : w->idle;
    spr = wframe[f];
    x = VIEW_W / 2 - spr->width / 2 + frame_dx[f];
    y = VIEW_H - spr->height + drop;
    if (wstate != WS_FIRE && wstate != WS_RECOIL && bob_amp) {
        /* a U-shaped sway: side to side, lowest in the middle */
        x += (bob_amp * fcos(bob_phase)) >> 14;
        y += (bob_amp * abs(fsin(bob_phase))) >> 15;
    }
    if (x < 0)
        x = 0;
    if (x > VIEW_W - spr->width)
        x = VIEW_W - spr->width;
    if (y < 0)
        y = 0;
    if (y >= VIEW_H)
        return;
    rc_weapon(fb, spr, x, y);
}
