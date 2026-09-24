/*
 * Level loading from the DREADL1..DREADL5 AppVars (format documented in
 * tools/levels.py). The map is copied into a fixed 64x64 grid; everything
 * outside the level rectangle is filled with solid wall so rays and
 * movement never need bounds checks.
 */
#include <string.h>
#include <fileioc.h>

#include "dread.h"
#include "render.h"

#define LEVEL_HEADER 40
#define LEVEL_VERSION 1

uint8_t level_map[MAP_SIZE * MAP_SIZE];
level_info_t level;

bool level_load(uint8_t num)
{
    char name[9] = "DREADL0";
    const uint8_t *d;
    uint8_t h;
    uint8_t y, w, ht;

    name[6] = (char)('0' + num);
    h = ti_Open(name, "r");
    if (!h)
        return false;
    d = (const uint8_t *)ti_GetDataPtr(h);
    if (ti_GetSize(h) < LEVEL_HEADER || d[0] != 'D' || d[1] != 'R' ||
        d[2] != 'L' || d[3] != LEVEL_VERSION) {
        ti_Close(h);
        return false;
    }

    w = d[4];
    ht = d[5];
    if (w > MAP_SIZE || ht > MAP_SIZE ||
        ti_GetSize(h) < LEVEL_HEADER + (unsigned)w * ht) {
        ti_Close(h);
        return false;
    }

    memset(level_map, 1 + TEX_TECH_PANEL, sizeof level_map);
    for (y = 0; y < ht; y++)
        memcpy(&level_map[(unsigned)y << MAP_SHIFT], d + LEVEL_HEADER + (unsigned)y * w, w);

    memset(&level, 0, sizeof level);
    level.number = num;
    level.width = w;
    level.height = ht;
    player.x = (d[6] << FRAC_BITS) + TILE_UNITS / 2;
    player.y = (d[7] << FRAC_BITS) + TILE_UNITS / 2;
    player.angle = (d[8] * 4) & ANG_MASK;
    player.turn_held = 0;
    level.ceiling_color = d[9];
    level.floor_color = d[10];
    level.ambient = d[11];
    level.falloff = d[12];
    level.boss = d[13] & 1;
    memcpy(level.name, d + 16, sizeof level.name);
    level.name[sizeof level.name - 1] = '\0';

    things_spawn(d + LEVEL_HEADER + (unsigned)w * ht + 1, d[LEVEL_HEADER + (unsigned)w * ht]);
    ti_Close(h);

    doors_init();
    sound_init();
    render_set_level();
    return true;
}
