/*
 * Asset loading: the DREADTX AppVar holds the palette and wall textures.
 * Textures are copied into RAM because the renderer reads them for every
 * wall pixel and archive (flash) reads are slower than RAM.
 */
#include <string.h>
#include <fileioc.h>

#include "dread.h"
#include "gfx/dreadtx.h"

uint16_t base_palette[256];

const char *assets_load(void)
{
    uint8_t h;
    uint8_t i;

    /* Reject a DREADTX from a different build before trusting its layout. */
    h = ti_Open("DREADTX", "r");
    if (!h)
        return "DREADTX appvar missing";
    if (ti_GetSize(h) != DREADTX_appvar_size) {
        ti_Close(h);
        return "DREADTX is the wrong version";
    }
    ti_Close(h);

    if (!DREADTX_init())
        return "DREADTX appvar missing";
    if (walls_tiles_num != NUM_TEXTURES)
        return "DREADTX texture count mismatch";

    memcpy(base_palette, dread_pal, sizeof base_palette);
    for (i = 0; i < NUM_TEXTURES; i++)
        memcpy(textures[i], walls_tiles_data[i], TEX_SIZE * TEX_SIZE);
    return NULL;
}
