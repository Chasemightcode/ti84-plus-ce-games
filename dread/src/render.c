/*
 * DREAD renderer: per-level and per-frame setup around the assembly core
 * in raycast.s.
 *
 * The 3D view is 160x100 logical pixels, each drawn as a 2x2 block over
 * screen rows 0..199. The core renders even screen rows only; rc_dup then
 * copies each one to the odd row below it.
 *
 * Fixed-point formats shared with raycast.s:
 *   positions        Q8 tiles
 *   ray direction    Q22 accumulators (Q14 after dropping the low byte)
 *   distances        Q8 tiles along the view direction
 */
#include <string.h>
#include <graphx.h>

#include "dread.h"
#include "render.h"

#define MAX_LIGHT  14        /* +1 for x-facing walls stays < NUM_LIGHT */
#define FLASH_LIGHT 3        /* levels brighter while a gun fires */

/* 256-byte alignment lets the assembly form addresses by replacing only
 * the low byte: colormap pages are indexed by color, and a 32-byte texture
 * column never crosses a page. */
uint8_t colormap[NUM_LIGHT][256] __attribute__((aligned(256)));
uint8_t textures[NUM_TEXTURES][TEX_SIZE * TEX_SIZE] __attribute__((aligned(256)));
uint16_t zbuffer[VIEW_W];

/* Inputs to raycast.s. */
const uint8_t *rc_tile_tex[256];
uint8_t rc_light_tab[LIGHT_TAB];
uint8_t rc_row_color[VIEW_H];
unsigned int rc_row_ofs[VIEW_H];
int rc_ray_x, rc_ray_y, rc_ray_dx, rc_ray_dy;
uint8_t rc_jamb_tile;        /* wall tile drawn on the sides of a doorway */

/* Normal and muzzle-flash lighting; the active set is copied into
 * rc_light_tab / rc_row_color when it changes. */
static uint8_t light_base[LIGHT_TAB], light_lit[LIGHT_TAB];
static uint8_t rows_base[VIEW_H], rows_lit[VIEW_H];
static bool lit;

/* Reciprocal mantissas: entry i is 2^27 / (16388 + 8i), the center of the
 * i-th bucket of divisors normalized to [2^14, 2^15). See rc_recip. */
#define RECIP_ENTRIES 2048
uint16_t rc_recip_tab[RECIP_ENTRIES];

void rc_cast(uint8_t *fb);
void rc_draw(uint8_t *fb);
void rc_dup(uint8_t *fb);

const char *render_init(void)
{
    unsigned l, c, y;

    /* The assembly steps colormap pages by changing only the middle
     * address byte, so the 4 KB table must not straddle a 64 KB bank. */
    if (((uintptr_t)colormap >> 16) != (((uintptr_t)colormap + sizeof colormap - 1) >> 16))
        return "colormap crosses a 64K bank";
    if (((uintptr_t)colormap & 0xFF) || ((uintptr_t)textures & 0xFF))
        return "renderer tables misaligned";

    /* Palette ramps are 16 shades from bright to black, so darkening is a
     * shift toward the end of the ramp. */
    for (l = 0; l < NUM_LIGHT; l++) {
        for (c = 0; c < 256; c++) {
            unsigned s = (c & 15) + l;
            colormap[l][c] = (uint8_t)((c & 0xF0) | (s > 15 ? 15 : s));
        }
    }
    for (y = 0; y < VIEW_H; y++)
        rc_row_ofs[y] = y * (SCREEN_W * 2);
    for (c = 0; c < RECIP_ENTRIES; c++) {
        unsigned long center = 16388UL + 8UL * c;
        rc_recip_tab[c] = (uint16_t)((134217728UL + center / 2) / center);
    }
    return NULL;
}

void render_set_level(void)
{
    unsigned i, y;

    for (i = 0; i < 256; i++)
        rc_tile_tex[i] = textures[TEX_TECH_PANEL];
    for (i = 1; i <= NUM_TEXTURES && i < 0x40; i++) {
        rc_tile_tex[i] = textures[i - 1];
        rc_tile_tex[i | TILE_SECRET] = textures[i - 1];
    }
    /* doors were renumbered to TILE_DOOR | index by doors_init() */
    for (i = 0; i < num_doors; i++)
        rc_tile_tex[TILE_DOOR | i] = textures[doors[i].tex];
    rc_jamb_tile = TEX_DOOR_JAMB + 1;

    /* Distance d = i/8 tiles gets ambient + d * falloff/16 levels. */
    for (i = 0; i < LIGHT_TAB; i++) {
        unsigned l = level.ambient + ((i * level.falloff) >> 7);
        if (l > MAX_LIGHT)
            l = MAX_LIGHT;
        light_base[i] = (uint8_t)l;
        light_lit[i] = (uint8_t)(l > FLASH_LIGHT ? l - FLASH_LIGHT : 0);
    }

    /* Ceiling/floor rows: a plane half a tile from the eye is seen at row
     * offset dy at distance (FOCAL/2)/dy tiles. With dy = (2k-1)/2 for the
     * k-th row from the horizon, that is 960/(2k-1) in 1/8 tiles. */
    for (y = 0; y < HORIZON; y++) {
        unsigned k = HORIZON - y;
        unsigned li = 960u / (2 * k - 1);
        uint8_t l, b;
        if (li >= LIGHT_TAB)
            li = LIGHT_TAB - 1;
        l = light_base[li];
        b = light_lit[li];
        rows_base[y] = colormap[l][level.ceiling_color];
        rows_base[VIEW_H - 1 - y] = colormap[l][level.floor_color];
        rows_lit[y] = colormap[b][level.ceiling_color];
        rows_lit[VIEW_H - 1 - y] = colormap[b][level.floor_color];
    }
    memcpy(rc_light_tab, light_base, LIGHT_TAB);
    memcpy(rc_row_color, rows_base, VIEW_H);
    lit = false;
}

void render_view(void)
{
    uint8_t *fb = (uint8_t *)gfx_vbuffer;
    long dir_x = fcos(player.angle);
    long dir_y = fsin(player.angle);
    /* Camera plane = direction rotated +90 degrees and scaled so the view
     * spans VIEW_W/2 px at FOCAL px. Everything here is Q22 (Q14 << 8). */
    int plane_x = (int)(-dir_y * 256 * (VIEW_W / 2) / FOCAL);
    int plane_y = (int)(dir_x * 256 * (VIEW_W / 2) / FOCAL);

    rc_ray_dx = plane_x / (VIEW_W / 2);
    rc_ray_dy = plane_y / (VIEW_W / 2);
    /* Column centers: camera x runs from -1 + 1/VIEW_W to 1 - 1/VIEW_W. */
    rc_ray_x = (int)(dir_x * 256) - plane_x + rc_ray_dx / 2;
    rc_ray_y = (int)(dir_y * 256) - plane_y + rc_ray_dy / 2;

    if (weapon_flash() != lit) {
        lit = !lit;
        memcpy(rc_light_tab, lit ? light_lit : light_base, LIGHT_TAB);
        memcpy(rc_row_color, lit ? rows_lit : rows_base, VIEW_H);
    }

    rc_cast(fb);
    gfx_Wait();      /* the draw buffer may still be on screen until now */
    rc_draw(fb);
    sprites_draw(fb);
    weapon_draw(fb);
    rc_dup(fb);
}
