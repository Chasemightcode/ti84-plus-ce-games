/*
 * Doors.
 *
 * Normal doors are Wolfenstein-style slabs through the middle of their cell
 * that slide sideways into the wall. Secret doors are flush with the wall
 * around them and use that wall's texture, so they only give themselves
 * away when they move. raycast.s draws both from doors[] (see DOOR_* in
 * dread.h); this file runs their state machines.
 */
#include <string.h>

#include "dread.h"
#include "hud.h"
#include "render.h"

#define DOOR_SPEED    15      /* open amount per tic: ~0.5 s to open */
#define DOOR_WAIT     105     /* 3 s open before trying to close */
#define DOOR_RETRY    35

door_t doors[MAX_DOORS];
uint8_t num_doors;

static const char *const need_key[] = {
    "", "You need the red keycard.", "You need the blue keycard.",
    "You need the yellow keycard.",
};

static bool solid_file_tile(uint8_t t)
{
    return t != TILE_EMPTY && !IS_DOOR(t);
}

void doors_init(void)
{
    uint8_t x, y;

    num_doors = 0;
    memset(doors, 0, sizeof doors);
    for (y = 1; y < MAP_SIZE - 1; y++) {
        for (x = 1; x < MAP_SIZE - 1; x++) {
            uint8_t t = MAP_AT(x, y);
            door_t *d;

            if (!IS_DOOR(t) && !(t & TILE_SECRET))
                continue;
            if (num_doors == MAX_DOORS) {
                MAP_AT(x, y) = t & 0x3F ? t & 0x3F : 1;   /* turn extras into walls */
                continue;
            }
            d = &doors[num_doors];
            d->x = x;
            d->y = y;
            if (IS_DOOR(t)) {
                d->lock = t & 0x03;
                d->tex = TEX_DOOR_TECH + d->lock;
                /* walls north and south: the passage runs east-west, so the
                 * slab stands on the vertical line through the cell center */
                if (solid_file_tile(MAP_AT(x, y - 1)) && solid_file_tile(MAP_AT(x, y + 1)))
                    d->flags = DOOR_VERTICAL;
            } else {
                d->flags = DOOR_FLUSH;
                d->tex = (t & 0x3F) - 1;
                level.secrets_total++;
            }
            MAP_AT(x, y) = TILE_DOOR | num_doors;
            num_doors++;
        }
    }
}

bool door_blocks(uint8_t index)
{
    return doors[index].open != 255;
}

static bool door_occupied(const door_t *d)
{
    return player_overlaps_cell(d->x, d->y) || thing_in_cell(d->x, d->y);
}

/* Monsters bumping into a plain closed door open it; locked doors and
 * secret walls stay shut. */
void door_open_quiet(uint8_t index)
{
    door_t *d = &doors[index];

    if (!d->lock && !(d->flags & DOOR_FLUSH) &&
        (d->state == DS_CLOSED || d->state == DS_CLOSING))
        d->state = DS_OPENING;
}

bool door_use(uint8_t index)
{
    door_t *d = &doors[index];

    if (d->lock && !(player.keys & (1 << (d->lock - 1)))) {
        hud_message(need_key[d->lock]);
        return true;
    }
    switch (d->state) {
    case DS_CLOSED:
        if (d->flags & DOOR_FLUSH) {
            level.secrets++;
            hud_message("You found a secret!");
        }
        /* fall through */
    case DS_CLOSING:
        d->state = DS_OPENING;
        return true;
    case DS_OPEN:
        if (!(d->flags & DOOR_FLUSH) && !door_occupied(d))
            d->state = DS_CLOSING;
        return true;
    default:
        return false;
    }
}

void doors_tic(void)
{
    door_t *d = doors;
    uint8_t i;

    for (i = 0; i < num_doors; i++, d++) {
        switch (d->state) {
        case DS_OPENING:
            if (d->open >= 255 - DOOR_SPEED) {
                d->open = 255;
                d->state = DS_OPEN;
                d->timer = (d->flags & DOOR_FLUSH) ? 0 : DOOR_WAIT;
            } else {
                d->open += DOOR_SPEED;
            }
            break;
        case DS_OPEN:
            /* secret doors (timer 0) stay open for good */
            if (d->timer && --d->timer == 0) {
                if (door_occupied(d))
                    d->timer = DOOR_RETRY;
                else
                    d->state = DS_CLOSING;
            }
            break;
        case DS_CLOSING:
            if (player_overlaps_cell(d->x, d->y)) {
                d->state = DS_OPENING;        /* don't crush the player */
            } else if (d->open <= DOOR_SPEED) {
                d->open = 0;
                d->state = DS_CLOSED;
            } else {
                d->open -= DOOR_SPEED;
            }
            break;
        }
    }
}
