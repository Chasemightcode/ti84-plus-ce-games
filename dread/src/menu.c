/*
 * Menus and full-screen pages: title, difficulty, pause, level tally,
 * victory, best times, messages.
 *
 * Every loop redraws its page into the back buffer and swaps, so each page
 * stays correct in both buffers. Keys act when they go down (Up/Down move,
 * 2nd or Enter select, Clear quits DREAD from anywhere), and a key already
 * held when a page opens is ignored until it is released.
 */
#include <graphx.h>
#include <keypadc.h>

#include "dread.h"
#include "hud.h"

enum { IN_NONE, IN_UP, IN_DOWN, IN_SELECT, IN_QUIT };

static uint8_t keys_prev;

static void input_reset(void)
{
    keys_prev = 0xFF;                   /* wait for a release first */
}

static uint8_t input(void)
{
    uint8_t now = 0, fresh;

    kb_Scan();
    if (kb_Data[7] & kb_Up)
        now |= 1;
    if (kb_Data[7] & kb_Down)
        now |= 2;
    if ((kb_Data[1] & kb_2nd) || (kb_Data[6] & kb_Enter))
        now |= 4;
    if (kb_Data[6] & kb_Clear)
        now |= 8;
    fresh = now & ~keys_prev;
    keys_prev = now;
    if (fresh & 8)
        return IN_QUIT;
    if (fresh & 4)
        return IN_SELECT;
    if (fresh & 1)
        return IN_UP;
    if (fresh & 2)
        return IN_DOWN;
    return IN_NONE;
}

static void text_at(const char *s, int x, int y, uint8_t color, uint8_t scale)
{
    gfx_SetTextScale(scale, scale);
    gfx_SetTextFGColor(color);
    gfx_PrintStringXY(s, x, y);
}

static void text_centered(const char *s, int y, uint8_t color, uint8_t scale)
{
    gfx_SetTextScale(scale, scale);
    gfx_SetTextFGColor(color);
    gfx_PrintStringXY(s, (SCREEN_W - (int)gfx_GetStringWidth(s)) / 2, y);
}

/* "m:ss" into buf (at least 8 bytes). */
static char *time_text(char *buf, unsigned secs)
{
    char *p = buf;
    unsigned m = secs / 60;

    if (m >= 100)
        m = 99;
    if (m >= 10)
        *p++ = (char)('0' + m / 10);
    *p++ = (char)('0' + m % 10);
    *p++ = ':';
    *p++ = (char)('0' + secs % 60 / 10);
    *p++ = (char)('0' + secs % 10);
    *p = 0;
    return buf;
}

/* Dark red bands behind every page. */
static void backdrop(void)
{
    static const uint8_t shade[15] = { 15, 15, 14, 14, 13, 13, 12, 12, 13, 13, 14, 14, 15, 15, 15 };
    uint8_t i;

    for (i = 0; i < 15; i++) {
        gfx_SetColor(PAL(RED, shade[i]));
        gfx_FillRectangle_NoClip(0, i * 16, SCREEN_W, 16);
    }
    gfx_SetTextBGColor(0);
    gfx_SetTextTransparentColor(0);
}

static void logo(int y)
{
    gfx_SetTextScale(7, 7);
    gfx_SetTextFGColor(PAL(GRAY, 15));
    gfx_PrintStringXY("DREAD", (SCREEN_W - (int)gfx_GetStringWidth("DREAD")) / 2 + 4, y + 4);
    gfx_SetTextFGColor(PAL(RED, 2));
    gfx_PrintStringXY("DREAD", (SCREEN_W - (int)gfx_GetStringWidth("DREAD")) / 2, y);
}

/* A vertical list of choices; disabled entries are skipped. */
static void draw_items(const char *const *items, uint8_t n, uint8_t sel, uint8_t disabled, int y)
{
    uint8_t i;

    for (i = 0; i < n; i++, y += 20) {
        uint8_t color = (disabled >> i) & 1 ? PAL(GRAY, 10) : i == sel ? PAL(YELLOW, 0)
                                                                         : PAL(GRAY, 3);
        text_centered(items[i], y, color, 2);
        if (i == sel) {
            text_at(">", 40, y, PAL(YELLOW, 0), 2);
            text_at("<", SCREEN_W - 56, y, PAL(YELLOW, 0), 2);
        }
    }
}

static uint8_t move_sel(uint8_t sel, uint8_t n, uint8_t disabled, uint8_t key)
{
    uint8_t k;

    for (k = 0; k < n; k++) {
        sel = key == IN_UP ? (sel ? sel - 1 : n - 1) : (sel + 1 < n ? sel + 1 : 0);
        if (!((disabled >> sel) & 1))
            break;
    }
    return sel;
}

static const char *const skill_names[3] = { "EASY", "NORMAL", "HARD" };

static void best_times_page(void)
{
    input_reset();
    for (;;) {
        uint8_t n, s, key;
        char buf[8];

        backdrop();
        text_centered("BEST TIMES", 16, PAL(RED, 2), 3);
        for (s = 0; s < 3; s++)
            text_at(skill_names[s], 112 + s * 72, 64, PAL(STEEL, 3), 1);
        for (n = 0; n < NUM_LEVELS; n++) {
            buf[0] = 'M';
            buf[1] = 'A';
            buf[2] = 'P';
            buf[3] = ' ';
            buf[4] = (char)('1' + n);
            buf[5] = 0;
            text_at(buf, 40, 84 + n * 16, PAL(GRAY, 3), 1);
            for (s = 0; s < 3; s++) {
                unsigned t = save.best[s][n];
                text_at(t ? time_text(buf, t) : "--:--", 112 + s * 72, 84 + n * 16,
                        t ? PAL(YELLOW, 1) : PAL(GRAY, 10), 1);
            }
        }
        for (s = 0; s < 3; s++) {
            if (save.won & (1 << s))
                text_at("WON", 112 + s * 72, 84 + NUM_LEVELS * 16 + 8, PAL(GREEN, 2), 1);
        }
        text_centered("PRESS 2ND TO GO BACK", 216, PAL(GRAY, 6), 1);
        gfx_SwapDraw();
        key = input();
        if (key == IN_SELECT || key == IN_QUIT)
            return;
    }
}

uint8_t title_menu(void)
{
    static char cont[] = "CONTINUE: MAP 1";
    static const char *const items[4] = { "NEW GAME", cont, "BEST TIMES", "QUIT" };
    uint8_t disabled = save.level ? 0 : 0x02, sel = save.level ? 1 : 0;

    cont[8] = ':';
    if (save.level)
        cont[sizeof cont - 2] = (char)('0' + save.level);
    else
        cont[8] = 0;                    /* just "CONTINUE", grayed out */
    input_reset();
    for (;;) {
        uint8_t key;

        backdrop();
        logo(18);
        text_centered("EPISODE 1: THE WARDEN", 80, PAL(TAN, 4), 1);
        draw_items(items, 4, sel, disabled, 112);
        if (save.level)
            text_centered(skill_names[save.skill], 196, PAL(GRAY, 7), 1);
        text_centered("ARROWS: MOVE  2ND: SELECT  CLEAR: QUIT", 224, PAL(GRAY, 7), 1);
        gfx_SwapDraw();
        key = input();
        if (key == IN_QUIT)
            return TITLE_QUIT;
        if (key == IN_UP || key == IN_DOWN)
            sel = move_sel(sel, 4, disabled, key);
        if (key == IN_SELECT) {
            if (sel == 2) {
                best_times_page();
                input_reset();
            } else {
                return sel == 0 ? TITLE_NEW : sel == 1 ? TITLE_CONTINUE : TITLE_QUIT;
            }
        }
    }
}

uint8_t skill_menu(void)
{
    static const char *const notes[3] = {
        "FEWER FOES, HALF DAMAGE, DOUBLE AMMO",
        "THE WAY IT WAS MEANT TO BE",
        "MORE MONSTERS, THEY HIT HARDER",
    };
    uint8_t sel = SKILL_NORMAL;

    input_reset();
    for (;;) {
        uint8_t key;

        backdrop();
        text_centered("CHOOSE YOUR FATE", 40, PAL(RED, 2), 2);
        draw_items(skill_names, 3, sel, 0, 96);
        text_centered(notes[sel], 176, PAL(TAN, 4), 1);
        gfx_SwapDraw();
        key = input();
        if (key == IN_QUIT)
            return MENU_QUIT;
        if (key == IN_UP || key == IN_DOWN)
            sel = move_sel(sel, 3, 0, key);
        if (key == IN_SELECT)
            return sel;
    }
}

uint8_t pause_menu(void)
{
    static const char *const items[3] = { "RESUME", "RESTART MAP", "QUIT TO TITLE" };
    uint8_t sel = 0;

    input_reset();
    for (;;) {
        uint8_t key;

        gfx_SetColor(PAL(GRAY, 15));
        gfx_FillRectangle_NoClip(32, 40, SCREEN_W - 64, 124);
        gfx_SetColor(PAL(RED, 6));
        gfx_FillRectangle_NoClip(32, 40, SCREEN_W - 64, 2);
        gfx_FillRectangle_NoClip(32, 162, SCREEN_W - 64, 2);
        gfx_SetTextBGColor(0);
        gfx_SetTextTransparentColor(0);
        text_centered("PAUSED", 52, PAL(RED, 2), 2);
        draw_items(items, 3, sel, 0, 84);
        gfx_SwapDraw();
        key = input();
        if (key == IN_QUIT)
            return PAUSE_QUIT;
        if (key == IN_UP || key == IN_DOWN)
            sel = move_sel(sel, 3, 0, key);
        if (key == IN_SELECT)
            return sel == 0 ? PAUSE_RESUME : sel == 1 ? PAUSE_RESTART : PAUSE_TITLE;
    }
}

static unsigned percent(unsigned got, unsigned total)
{
    return total ? got * 100 / total : 100;
}

static void stat_line(const char *label, unsigned value, bool pct, int y)
{
    char buf[8];
    uint8_t i = 0;

    text_at(label, 72, y, PAL(STEEL, 2), 2);
    if (value >= 100)
        buf[i++] = (char)('0' + value / 100 % 10);
    if (value >= 10)
        buf[i++] = (char)('0' + value / 10 % 10);
    buf[i++] = (char)('0' + value % 10);
    if (pct)
        buf[i++] = '%';
    buf[i] = 0;
    text_at(buf, 200, y, PAL(YELLOW, 1), 2);
}

bool intermission(unsigned best, bool record)
{
    unsigned target[3], shown = 0;
    unsigned secs = level.tics < TIC_RATE ? 1 : level.tics / TIC_RATE;   /* as recorded */
    uint8_t frame = 0;
    char buf[8];

    target[0] = percent(level.kills, level.kills_total);
    target[1] = percent(level.items, level.items_total);
    target[2] = percent(level.secrets, level.secrets_total);
    input_reset();
    for (;;) {
        uint8_t key;
        unsigned v[3], i;

        for (i = 0; i < 3; i++)
            v[i] = shown < target[i] ? shown : target[i];
        backdrop();
        text_centered("MAP COMPLETE", 24, PAL(RED, 2), 3);
        text_centered(level.name, 56, PAL(TAN, 3), 1);
        stat_line("KILLS", v[0], true, 84);
        stat_line("ITEMS", v[1], true, 108);
        stat_line("SECRETS", v[2], true, 132);
        text_at("TIME", 72, 160, PAL(STEEL, 2), 2);
        text_at(time_text(buf, secs), 200, 160, PAL(YELLOW, 1), 2);
        text_at("BEST", 72, 184, PAL(STEEL, 2), 2);
        text_at(time_text(buf, best), 200, 184, PAL(YELLOW, 1), 2);
        if (record && (frame & 8))
            text_centered("NEW RECORD!", 206, PAL(GREEN, 1), 1);
        text_centered("PRESS 2ND TO CONTINUE", 224, PAL(GRAY, 6), 1);
        gfx_SwapDraw();
        frame++;
        if (shown < 100)
            shown += 4;
        key = input();
        if (key == IN_QUIT)
            return false;
        if (key == IN_SELECT) {
            if (shown >= 100)
                return true;
            shown = 100;                  /* first press skips the count */
        }
    }
}

bool victory_screen(unsigned run_secs)
{
    char buf[8];

    input_reset();
    for (;;) {
        uint8_t key;

        backdrop();
        text_centered("THE WARDEN IS DEAD", 36, PAL(RED, 2), 2);
        text_centered("THE PIT FALLS SILENT.", 76, PAL(TAN, 3), 1);
        text_centered("THE OUTPOST IS YOURS, FOR NOW...", 90, PAL(TAN, 3), 1);
        text_centered("SOMETHING DEEPER IS STIRRING.", 104, PAL(TAN, 3), 1);
        text_centered("EPISODE 1 COMPLETE", 128, PAL(YELLOW, 1), 2);
        text_centered(skill_names[game_skill], 152, PAL(STEEL, 2), 1);
        text_at("TOTAL TIME", 72, 176, PAL(STEEL, 2), 1);
        text_at(time_text(buf, run_secs), 200, 176, PAL(YELLOW, 1), 1);
        text_centered("PRESS 2ND", 224, PAL(GRAY, 6), 1);
        gfx_SwapDraw();
        key = input();
        if (key == IN_QUIT)
            return false;
        if (key == IN_SELECT)
            return true;
    }
}

void message_screen(const char *line1, const char *line2)
{
    input_reset();
    for (;;) {
        uint8_t key;

        backdrop();
        text_centered(line1, 96, PAL(YELLOW, 1), 1);
        text_centered(line2, 112, PAL(TAN, 3), 1);
        text_centered("PRESS 2ND", 224, PAL(GRAY, 6), 1);
        gfx_SwapDraw();
        key = input();
        if (key == IN_SELECT || key == IN_QUIT)
            return;
    }
}
