/*
 * Billboard sprites for things.
 *
 * Each visible thing is transformed into camera space, projected like a
 * one-tile-tall wall slice standing on the floor, and handed to rc_sprite
 * far to near. Sprite texels live in the archived DREADSP (items and
 * decorations) and DREADEN (creatures and effects) AppVars; the ones
 * in use are copied into 256-aligned RAM slots so the assembly can address
 * a texture column by replacing only the low address byte, like walls.
 */
#include <string.h>

#include "dread.h"
#include "gfx/dreaden.h"
#include "gfx/dreadsp.h"
#include "render.h"

#define CACHE_SLOTS  SPRITE_CACHE_SLOTS

/* raycast.s: rc_gather transforms and culls things into rc_seen. */
seen_t rc_seen[MAX_VIS];
uint8_t rc_nseen;
int rc_c9, rc_s9;                  /* Q9 cos/sin of the view angle */
unsigned rc_thing_stride = sizeof(thing_t);

void rc_gather(void);
void rc_sprite(const sprdesc_t *d);

/* A sprite that survived culling: its visible column span and size. */
typedef struct {
    uint16_t depth;
    int x0;                        /* left edge of the 32x32 cell, logical px */
    uint8_t xs, xe;                /* visible columns [xs, xe) */
    unsigned size;                 /* cell width = height, logical px */
    int y_cell;                    /* top row of the cell */
    uint16_t step;                 /* texels per logical px, 8.8 */
    const spritebox_t *box;
    uint8_t sprite;
    uint8_t bright;
} vis_t;

/* CACHE_SLOTS 1 KB slots at the start of the RAM arena (256-aligned). */
#define CACHE_SLOT(i) (arena + ARENA_SPRITES + (unsigned)(i) * (TEX_SIZE * TEX_SIZE))
static uint8_t cache_key[CACHE_SLOTS];
static uint8_t cache_age[CACHE_SLOTS];
static uint8_t cache_clock;
static bool cache_ready;

static vis_t vis[MAX_VIS];
static vis_t *order[MAX_VIS];

void sprites_flush(void)
{
    cache_ready = false;
}

static const uint8_t *sprite_texels(uint8_t id)
{
    uint8_t i, victim = 0;

    if (!cache_ready) {
        memset(cache_key, 0xFF, sizeof cache_key);
        cache_ready = true;
    }
    cache_clock++;
    for (i = 0; i < CACHE_SLOTS; i++) {
        if (cache_key[i] == id) {
            cache_age[i] = cache_clock;
            return CACHE_SLOT(i);
        }
        if ((uint8_t)(cache_clock - cache_age[i]) > (uint8_t)(cache_clock - cache_age[victim]))
            victim = i;
    }
    memcpy(CACHE_SLOT(victim), id < NUM_WORLD_SPRITES ? wsprites_tiles_data[id]
                               : esprites_tiles_data[id - NUM_WORLD_SPRITES],
           TEX_SIZE * TEX_SIZE);
    cache_key[victim] = id;
    cache_age[victim] = cache_clock;
    return CACHE_SLOT(victim);
}

/* Screen cell of a seen thing: returns its size (width = height, logical
 * px) and sets its top-left corner. Things stand on the floor, one tile
 * tall, or two for TF_BIG (the boss). */
unsigned sprite_project(const seen_t *sn, int *x0, int *y_cell)
{
    int depth = sn->depth;
    unsigned tile = (unsigned)(FOCAL * TILE_UNITS) / (unsigned)depth;
    int cx = VIEW_W / 2 + (FOCAL * sn->lat) / depth;

    if (thingdefs[sn->thing->type].flags & TF_BIG) {
        *x0 = cx - (int)tile;
        *y_cell = HORIZON + (int)(tile >> 1) - 2 * (int)tile;
        return tile * 2;
    }
    *x0 = cx - (int)(tile >> 1);
    *y_cell = HORIZON - (int)(tile >> 1);
    return tile;
}

/* Indexing arrays of odd-sized structs costs a library multiply at -Oz,
 * so both loops walk pointers. */
void sprites_draw(uint8_t *fb)
{
    const seen_t *sn = rc_seen;
    vis_t *v = vis;
    vis_t **end = order, **p;
    uint8_t i;

    rc_c9 = fcos(player.angle) >> 5;
    rc_s9 = fsin(player.angle) >> 5;
    rc_gather();

    for (i = rc_nseen; i; i--, sn++) {
        const thing_t *t = sn->thing;
        int depth = sn->depth, x0, y_cell;
        unsigned size = sprite_project(sn, &x0, &y_cell);
        const spritebox_t *box;
        const uint16_t *zl, *zr;
        int xs, xe;

        if (!size || x0 >= VIEW_W || x0 + (int)size <= 0)
            continue;
        box = &sprite_box[t->sprite];
        /* screen extent of the opaque part of the 32x32 cell */
        xs = x0 + (int)((box->left * size) >> 5);
        xe = x0 + (int)((box->right * size + 31) >> 5);
        if (xs < 0)
            xs = 0;
        if (xe > VIEW_W)
            xe = VIEW_W;
        if (xs >= xe)
            continue;
        /* trim columns where a wall is nearer; drop it if none are left */
        zl = &zbuffer[xs];
        zr = &zbuffer[xe];
        while (zl < zr && *zl <= (unsigned)depth)
            zl++;
        if (zl == zr)
            continue;
        while (zr[-1] <= (unsigned)depth)
            zr--;
        v->depth = (uint16_t)depth;
        v->x0 = x0;
        v->xs = (uint8_t)(zl - zbuffer);
        v->xe = (uint8_t)(zr - zbuffer);
        v->size = size;
        v->y_cell = y_cell;
        /* 8192 / size: 32 texels over size px */
        v->step = (uint16_t)(((unsigned)depth * 273u) >> (thingdefs[t->type].flags & TF_BIG ? 11 : 10));
        v->box = box;
        v->sprite = t->sprite;
        v->bright = (thingdefs[t->type].flags & TF_BRIGHT) != 0;
        /* insertion sort, far first */
        for (p = end; p > order && p[-1]->depth < v->depth; p--)
            *p = p[-1];
        *p = v;
        end++;
        v++;
    }

    for (p = order; p < end; p++) {
        const vis_t *vv = *p;
        const spritebox_t *box = vv->box;
        sprdesc_t d;
        unsigned size = vv->size;
        unsigned step = vv->step;
        int xs = vv->xs, xe = vv->xe;
        int y_cell = vv->y_cell;
        int ys = y_cell + (int)((box->top * size) >> 5);
        int ye = y_cell + (int)size;
        uint8_t light;

        if (ys < 0)
            ys = 0;
        if (ye > VIEW_H)
            ye = VIEW_H;
        if (ys >= ye)
            continue;
        if (vv->bright) {
            light = 0;
        } else {
            unsigned li = vv->depth >> 5;
            light = rc_light_tab[li < LIGHT_TAB ? li : LIGHT_TAB - 1];
        }
        d.tex = sprite_texels(vv->sprite);
        d.dst = fb + (unsigned)ys * (SCREEN_W * 2) + (unsigned)xs * 2;
        d.zb = &zbuffer[xs];
        d.tx = (unsigned)(xs - vv->x0) * step;
        d.step = (uint16_t)step;
        d.tp0 = (uint16_t)((unsigned)(ys - y_cell) * step);
        d.depth = vv->depth;
        d.cols = (uint8_t)(xe - xs);
        d.rows = (uint8_t)(ye - ys);
        d.cmap = (uint8_t)(((uintptr_t)colormap[light]) >> 8);
        rc_sprite(&d);
    }
}
