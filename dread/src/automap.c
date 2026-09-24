/*
 * Automap (Y=): a top-down plan of what the player has explored, drawn over
 * the 3D view area. raycast.s marks every wall and door cell a ray hits
 * (1); the cells the player walks through are marked here (2). The whole
 * level is scaled to fit 320x200, built one map row at a time into a line
 * buffer that is copied down to the row's screen lines.
 */
#include <graphx.h>
#include <string.h>

#include "dread.h"
#include "hud.h"

#define SEEN_WALL   1
#define SEEN_FLOOR  2
#define MAP_AREA_H  (VIEW_H * 2)
#define BACKGROUND  PAL(GRAY, 15)       /* black (index 0 is the brightest gray) */

#define automap (arena + ARENA_AUTOMAP)

static uint8_t line[SCREEN_W];

void automap_reset(void)
{
    memset(automap, 0, MAP_SIZE * MAP_SIZE);
}

void automap_visit(void)
{
    automap[((unsigned)player.y >> FRAC_BITS << MAP_SHIFT) + ((unsigned)player.x >> FRAC_BITS)] =
        SEEN_FLOOR;
}

/* Wall colors by texture family (texture ids from gen/assets.h). */
static uint8_t wall_color(uint8_t tile)
{
    uint8_t tex = (tile & 0x3F) - 1;

    if (tile == TILE_EXIT_OFF || tile == TILE_EXIT_ON)
        return PAL(GREEN, 2);
    if (tex >= TEX_HELL_FLESH && tex <= TEX_HELL_SIGIL)
        return PAL(RUST, 5);
    if (tex == TEX_BRICK_RED)
        return PAL(RED, 6);
    if (tex >= TEX_STONE_GRAY && tex <= TEX_STONE_BROWN)
        return PAL(GRAY, 6);
    return PAL(STEEL, 5);
}

static uint8_t cell_color(unsigned c)
{
    uint8_t seen = automap[c], tile = level_map[c];

    if (!seen)
        return BACKGROUND;
    if (IS_DOOR(tile)) {
        const door_t *d = &doors[DOOR_INDEX(tile)];
        static const uint8_t lock_color[4] = {
            PAL(TAN, 3), PAL(RED, 2), PAL(BLUE, 2), PAL(YELLOW, 1)
        };
        if (d->flags & DOOR_FLUSH)                  /* a secret: a wall until found */
            return d->state == DS_CLOSED ? wall_color(d->tex + 1) : PAL(GRAY, 11);
        return lock_color[d->lock];
    }
    if (tile == TILE_EMPTY)
        return PAL(GRAY, 11);
    return wall_color(tile);
}

static void plot(uint8_t *fb, int x, int y, uint8_t c)
{
    if (x >= 0 && x < SCREEN_W && y >= 0 && y < MAP_AREA_H)
        fb[(unsigned)y * SCREEN_W + (unsigned)x] = c;
}

static void print_stat(char *p, char tag, unsigned got, unsigned total)
{
    *p++ = tag;
    *p++ = ' ';
    if (got >= 10)
        *p++ = (char)('0' + got / 10 % 10);
    *p++ = (char)('0' + got % 10);
    *p++ = '/';
    if (total >= 10)
        *p++ = (char)('0' + total / 10 % 10);
    *p++ = (char)('0' + total % 10);
    *p++ = ' ';
    *p++ = ' ';
    *p = 0;
}

void automap_draw(uint8_t *fb)
{
    unsigned s = SCREEN_W / level.width, sy = MAP_AREA_H / level.height;
    unsigned ox, oy, x, y, k;
    uint8_t *row = fb;
    int px, py, c, sn;
    char text[48];
    unsigned secs = level.tics / TIC_RATE;

    if (sy < s)
        s = sy;
    ox = (SCREEN_W - level.width * s) / 2;
    oy = (MAP_AREA_H - level.height * s) / 2;

    memset(fb, BACKGROUND, oy * SCREEN_W);
    row = fb + oy * SCREEN_W;
    for (y = 0; y < level.height; y++) {
        unsigned c0 = (unsigned)y << MAP_SHIFT;
        uint8_t *lp = line + ox;
        memset(line, BACKGROUND, sizeof line);
        for (x = 0; x < level.width; x++, lp += s) {
            uint8_t col = cell_color(c0 + x);
            if (col != BACKGROUND)
                memset(lp, col, s);
        }
        for (k = 0; k < s; k++, row += SCREEN_W)
            memcpy(row, line, SCREEN_W);
    }
    memset(row, BACKGROUND, (unsigned)(fb + MAP_AREA_H * SCREEN_W - row));

    /* the player: a dot with a line showing the view direction */
    px = (int)ox + (int)(((unsigned)player.x * s) >> FRAC_BITS);
    py = (int)oy + (int)(((unsigned)player.y * s) >> FRAC_BITS);
    c = fcos(player.angle);
    sn = fsin(player.angle);
    for (k = 0; k <= 3 * s; k++)
        plot(fb, px + ((c * (int)k) >> 14), py + ((sn * (int)k) >> 14), PAL(BONE, 0));
    for (y = 0; y < 3; y++)
        for (x = 0; x < 3; x++)
            plot(fb, px - 1 + (int)x, py - 1 + (int)y, PAL(YELLOW, 0));

    /* the level's name and tally along the bottom */
    gfx_SetTextBGColor(0);
    gfx_SetTextTransparentColor(0);
    gfx_SetTextFGColor(PAL(RED, 3));
    gfx_PrintStringXY(level.name, 4, MAP_AREA_H - 20);
    print_stat(text, 'K', level.kills, level.kills_total);
    print_stat(text + strlen(text), 'I', level.items, level.items_total);
    print_stat(text + strlen(text), 'S', level.secrets, level.secrets_total);
    k = strlen(text);
    text[k++] = (char)('0' + secs / 600 % 10);
    text[k++] = (char)('0' + secs / 60 % 10);
    text[k++] = ':';
    text[k++] = (char)('0' + secs % 60 / 10);
    text[k++] = (char)('0' + secs % 10);
    text[k] = 0;
    gfx_SetTextFGColor(PAL(YELLOW, 2));
    gfx_PrintStringXY(text, 4, MAP_AREA_H - 10);
}
