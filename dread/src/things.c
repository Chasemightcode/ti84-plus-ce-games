/*
 * Things: pickups, decorations, monsters, missiles and effects. Behavior
 * comes from thingdefs[] (generated from tools/things.py); monsters are in
 * monsters.c, missiles and effects in combat.c.
 *
 * Solid things are registered in a blockmap (a list of things per map
 * cell) so a collision test only looks at the 3x3 cells around a position.
 * Pickups sit in a similar item map, so a tic only looks at the cells the
 * player touches, and only things that act on their own (monsters,
 * missiles, effects) are on the thinker list that things_tic walks.
 */
#include <string.h>

#include "dread.h"
#include "hud.h"

#define PICKUP_RANGE  (TILE_UNITS / 2)
#define THING_RADIUS  80          /* solid things, Q8 */

thing_t things[MAX_THINGS];
uint8_t num_things;
static uint8_t blockmap[MAP_SIZE * MAP_SIZE];     /* first thing index + 1, or 0 */
#define itemmap (arena + ARENA_ITEMS)                /* the same for pickups */
static thing_t *thinkers[MAX_THINGS];
static thing_t **thinkers_end;

void blockmap_clear(void)
{
    memset(blockmap, 0, sizeof blockmap);
}

static unsigned cell_of(int x, int y)
{
    return (CELL(y) << MAP_SHIFT) + CELL(x);
}

/* Each cell heads a list of the solid things standing in it, linked
 * through thing_t.bnext (index + 1, 0 ends the list). */
static void blockmap_add(thing_t *t)
{
    unsigned c = cell_of(t->x, t->y);

    t->bnext = blockmap[c];
    blockmap[c] = (uint8_t)(t - things + 1);
}

static void blockmap_remove(thing_t *t)
{
    uint8_t id = (uint8_t)(t - things + 1);
    uint8_t *link = &blockmap[cell_of(t->x, t->y)];

    while (*link) {
        if (*link == id) {
            *link = t->bnext;
            return;
        }
        link = &things[*link - 1].bnext;
    }
}

void blockmap_move(thing_t *t, int nx, int ny)
{
    bool solid = t->flags & THING_SOLID;

    if (solid && cell_of(nx, ny) == cell_of(t->x, t->y)) {
        t->x = nx;
        t->y = ny;
        return;
    }
    if (solid)
        blockmap_remove(t);
    t->x = nx;
    t->y = ny;
    if (solid)
        blockmap_add(t);
}

void thing_unsolid(thing_t *t)
{
    if (t->flags & THING_SOLID)
        blockmap_remove(t);
    t->flags &= ~THING_SOLID;
}

void things_spawn(const uint8_t *data, uint8_t count)
{
    thing_t *t = things;
    uint8_t i;

    num_things = 0;
    blockmap_clear();
    memset(itemmap, 0, MAP_SIZE * MAP_SIZE);
    memset(things, 0, sizeof things);
    thinkers_end = thinkers;
    for (i = 0; i < count; i++, data += 4) {
        uint8_t type = data[2];
        const thingdef_t *def;

        if (type >= NUM_THING_TYPES || !(data[3] & (1 << game_skill)) || num_things == MAX_THINGS)
            continue;
        def = &thingdefs[type];
        t->x = (data[0] << FRAC_BITS) + TILE_UNITS / 2;
        t->y = (data[1] << FRAC_BITS) + TILE_UNITS / 2;
        t->type = type;
        t->flags = THING_ACTIVE;
        t->sprite = def->sprite;
        if (def->flags & TF_ITEM)
            level.items_total++;
        if (def->flags & TF_KILL)
            level.kills_total++;
        if (def->kind == KIND_MONSTER) {
            monster_init(t);
            *thinkers_end++ = t;
        } else if (def->kind != KIND_DECOR) {
            /* a pickup: items never move, and none are solid, so bnext
             * links the item map's lists */
            uint8_t *head = &itemmap[cell_of(t->x, t->y)];
            t->bnext = *head;
            *head = (uint8_t)(num_things + 1);
        }
        if (def->flags & TF_BLOCK) {
            t->flags |= THING_SOLID;
            blockmap_add(t);
        }
        t++;
        num_things++;
    }
}

thing_t *thing_spawn(uint8_t type, int x, int y)
{
    thing_t *t = things;
    uint8_t i;

    for (i = 0; i < num_things && (t->flags & THING_ACTIVE); i++, t++)
        ;
    if (i == num_things) {
        if (num_things == MAX_THINGS)
            return NULL;
        num_things++;
    }
    memset(t, 0, sizeof *t);
    t->x = x;
    t->y = y;
    t->type = type;
    t->flags = THING_ACTIVE;
    t->sprite = thingdefs[type].sprite;
    *thinkers_end++ = t;            /* spawned things are missiles and effects */
    return t;
}

static bool give_ammo(uint8_t type, unsigned amount)
{
    if (player.ammo[type] >= max_ammo[type])
        return false;
    if (game_skill == SKILL_EASY)
        amount *= 2;
    player.ammo[type] += amount;
    if (player.ammo[type] > max_ammo[type])
        player.ammo[type] = max_ammo[type];
    return true;
}

/* Applies a pickup; false if the player can't use it right now. */
static bool try_pickup(const thingdef_t *def)
{
    switch (def->kind) {
    case KIND_HEALTH:
        if (player.health >= 100)
            return false;
        player.health += def->amount;
        if (player.health > 100)
            player.health = 100;
        hud_dirty(HUD_HEALTH | HUD_FACE);
        return true;
    case KIND_MEGA:
        if (player.health >= 200)
            return false;
        player.health += def->amount;
        if (player.health > 200)
            player.health = 200;
        hud_dirty(HUD_HEALTH | HUD_FACE);
        return true;
    case KIND_ARMOR:
        if (player.armor >= def->amount)
            return false;
        player.armor = def->amount;
        player.armor_class = def->arg;
        hud_dirty(HUD_ARMOR);
        return true;
    case KIND_AMMO:
        if (!give_ammo(def->arg, def->amount))
            return false;
        hud_dirty(HUD_AMMO | HUD_TABLE);
        return true;
    case KIND_KEY:
        player.keys |= 1 << def->arg;
        hud_dirty(HUD_KEYS);
        return true;
    case KIND_WEAPON: {
        static const uint8_t weapon_ammo[NUM_WEAPONS] = {
            0, AMMO_BULLETS, AMMO_SHELLS, AMMO_BULLETS, AMMO_ROCKETS
        };
        uint8_t bit = 1 << def->arg;
        bool fresh = !(player.weapons & bit);
        bool ammo = give_ammo(weapon_ammo[def->arg], def->amount);

        if (!fresh && !ammo)
            return false;
        if (fresh) {
            player.weapons |= bit;
            weapon_select(def->arg);           /* switch to it */
            hud_grin();
        }
        hud_dirty(HUD_ARMS | HUD_AMMO | HUD_TABLE);
        return true;
    }
    default:
        return false;
    }
}

static void take(thing_t *t)
{
    const thingdef_t *def = &thingdefs[t->type];

    t->flags &= ~THING_ACTIVE;
    if (def->flags & TF_ITEM)
        level.items++;
    hud_message(def->msg);
    fx_pickup();
}

/* Pickups in the cells within PICKUP_RANGE of the player. */
static void pickups(void)
{
    unsigned x0 = CELL(player.x - PICKUP_RANGE), x1 = CELL(player.x + PICKUP_RANGE);
    unsigned y0 = CELL(player.y - PICKUP_RANGE), y1 = CELL(player.y + PICKUP_RANGE);
    unsigned cx, cy;

    for (cy = y0; cy <= y1; cy++) {
        for (cx = x0; cx <= x1; cx++) {
            uint8_t *link = &itemmap[(cy << MAP_SHIFT) + cx];

            while (*link) {
                thing_t *t = &things[*link - 1];
                int dx = t->x - player.x, dy = t->y - player.y;

                if (dx >= -PICKUP_RANGE && dx <= PICKUP_RANGE && dy >= -PICKUP_RANGE &&
                    dy <= PICKUP_RANGE && try_pickup(&thingdefs[t->type])) {
                    *link = t->bnext;           /* gone from the map */
                    take(t);
                } else {
                    link = &t->bnext;
                }
            }
        }
    }
}

/* Runs every thinker, dropping the ones that are finished (spent effects,
 * corpses) from the list. Things spawned during the walk are appended and
 * get their first tic in the same pass. */
void things_tic(void)
{
    thing_t **p = thinkers, **keep = thinkers;
    uint8_t n = 0;

    while (p < thinkers_end) {
        thing_t *t = *p++;

        switch (thingdefs[t->type].kind) {
        case KIND_MONSTER:
            monster_tic(t, n++);
            if (t->state == MS_DEAD)
                continue;
            break;
        case KIND_MISSILE:
            missile_tic(t);
            break;
        default:
            effect_tic(t);
            break;
        }
        if (t->flags & THING_ACTIVE)
            *keep++ = t;
    }
    thinkers_end = keep;
    if (!player_dead)
        pickups();
}

/* A solid thing (other than self) within radius + THING_RADIUS of (x, y)
 * on both axes, or NULL. */
thing_t *thing_at(int x, int y, int radius, const thing_t *self)
{
    int reach = radius + THING_RADIUS;
    unsigned cx = CELL(x), cy = CELL(y);
    unsigned i, j;

    for (j = cy - 1; j <= cy + 1; j++) {
        for (i = cx - 1; i <= cx + 1; i++) {
            uint8_t b = blockmap[(j << MAP_SHIFT) + i];

            while (b) {
                thing_t *t = &things[b - 1];
                int dx = t->x - x, dy = t->y - y;

                b = t->bnext;
                if (t != self && dx > -reach && dx < reach && dy > -reach && dy < reach)
                    return t;
            }
        }
    }
    return NULL;
}

bool thing_in_cell(uint8_t cx, uint8_t cy)
{
    return blockmap[((unsigned)cy << MAP_SHIFT) + cx] != 0;
}
