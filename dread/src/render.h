#ifndef DREAD_RENDER_H
#define DREAD_RENDER_H

#include <stddef.h>
#include <stdint.h>

#include "dread.h"

#define NUM_LIGHT 16
#define LIGHT_TAB 512        /* perp >> 5 (1/8 tile steps) -> light level */

/* colormap[L][c] is palette color c darkened by L light levels. */
extern uint8_t colormap[NUM_LIGHT][256];

/* Perpendicular wall distance per logical column (Q8), for sprite clipping. */
extern uint16_t zbuffer[VIEW_W];
extern uint8_t rc_light_tab[LIGHT_TAB];

/* One sprite for rc_sprite (raycast.s reads this layout). */
typedef struct {
    const uint8_t *tex;      /* 256-aligned column-major 32x32 texels */
    uint8_t *dst;            /* first pixel of the first visible column */
    uint16_t *zb;            /* zbuffer entry of the first visible column */
    unsigned tx;             /* texture x of that column, 8.8 */
    uint16_t step;           /* texels per logical pixel, 8.8 */
    uint16_t tp0;            /* texture y of the first visible row, 8.8 */
    uint16_t depth;          /* Q8 */
    uint8_t cols, rows;
    uint8_t cmap;            /* colormap page */
} sprdesc_t;
_Static_assert(offsetof(sprdesc_t, step) == 12 && offsetof(sprdesc_t, depth) == 16 &&
               offsetof(sprdesc_t, cmap) == 20, "sprdesc_t layout is fixed by raycast.s");

/* Things in view from the last frame's rc_gather (raycast.s writes this
 * layout): combat.c uses it for hitscan. */
#define MAX_VIS 32
typedef struct {
    uint16_t depth;          /* Q8 along the view */
    int lat;                 /* Q8 to the right */
    uint8_t index;           /* into things[] */
    thing_t *thing;          /* &things[index] */
    uint8_t pad;             /* a uint16_t member rounds the size up to even */
} seen_t;
_Static_assert(sizeof(seen_t) == 10, "raycast.s writes 10-byte seen_t records");

extern seen_t rc_seen[MAX_VIS];
extern uint8_t rc_nseen;
unsigned sprite_project(const seen_t *sn, int *x0, int *y_cell);

/* Draw a convimg RLET sprite at logical (x, y) into the 3D view's even
 * rows, 2 bytes per texel, stopping at the bottom of the view. The caller
 * keeps it inside the view horizontally. */
void rc_weapon(uint8_t *fb, const void *rlet, int x, int y);

/* Returns NULL on success or a message describing the problem. */
const char *render_init(void);

/* Rebuild texture lookup and lighting for the loaded level. */
void render_set_level(void);

/* Draw the 3D view (walls, then sprites) into rows 0..HUD_Y-1 of the
 * current draw buffer. */
void render_view(void);

/* Things as billboards, far to near, clipped by zbuffer (sprites.c). */
void sprites_draw(uint8_t *fb);
void sprites_flush(void);       /* forget cached texels (arena moved) */

#endif
