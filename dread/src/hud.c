/*
 * Status bar, message line and screen flashes.
 *
 * The bar's graphics live in the DREADSP AppVar. Every number, the face and
 * the key icons are drawn opaque over dark wells that share one color, so
 * a changed value is simply redrawn in place. With double buffering each
 * change is drawn on two consecutive frames, once into each buffer.
 */
#include <fileioc.h>
#include <graphx.h>

#include "dread.h"
#include "gen/hud_layout.h"
#include "gfx/dreadsp.h"
#include "hud.h"

#define MSG_TICS     (TIC_RATE * 2)
#define LOOK_MIN     35
#define LOOK_RANGE   50
#define PAIN_TICS    18
#define GRIN_TICS    35

bool hud_show_fps = true;

static uint8_t dirty_now, dirty_prev;
static gfx_sprite_t *big[11], *small[10], *arm_off[5], *arm_sel[5], *keyspr[4];
static gfx_sprite_t *faces[5 * FACE_LOOKS + 1];
static const char *msg;
static uint8_t msg_tics;
static uint8_t look, look_tics, pain_tics, grin_tics;
static uint8_t face_shown = 0xFF;

const char *hud_load(void)
{
    uint8_t h = ti_Open("DREADSP", "r");

    if (!h)
        return "DREADSP appvar missing";
    if (ti_GetSize(h) != DREADSP_appvar_size) {
        ti_Close(h);
        return "DREADSP is the wrong version";
    }
    ti_Close(h);
    if (!DREADSP_init())
        return "DREADSP appvar missing";

    big[0] = hud_big_0; big[1] = hud_big_1; big[2] = hud_big_2; big[3] = hud_big_3;
    big[4] = hud_big_4; big[5] = hud_big_5; big[6] = hud_big_6; big[7] = hud_big_7;
    big[8] = hud_big_8; big[9] = hud_big_9; big[10] = hud_big_pct;
    small[0] = hud_small_0; small[1] = hud_small_1; small[2] = hud_small_2;
    small[3] = hud_small_3; small[4] = hud_small_4; small[5] = hud_small_5;
    small[6] = hud_small_6; small[7] = hud_small_7; small[8] = hud_small_8;
    small[9] = hud_small_9;
    arm_off[0] = hud_arm_off_1; arm_off[1] = hud_arm_off_2; arm_off[2] = hud_arm_off_3;
    arm_off[3] = hud_arm_off_4; arm_off[4] = hud_arm_off_5;
    arm_sel[0] = hud_arm_sel_1; arm_sel[1] = hud_arm_sel_2; arm_sel[2] = hud_arm_sel_3;
    arm_sel[3] = hud_arm_sel_4; arm_sel[4] = hud_arm_sel_5;
    keyspr[0] = hud_key_red; keyspr[1] = hud_key_blue; keyspr[2] = hud_key_yellow;
    keyspr[3] = hud_key_none;
    faces[0] = hud_face_0_center; faces[1] = hud_face_0_left;
    faces[2] = hud_face_0_right; faces[3] = hud_face_0_pain;
    faces[4] = hud_face_1_center; faces[5] = hud_face_1_left;
    faces[6] = hud_face_1_right; faces[7] = hud_face_1_pain;
    faces[8] = hud_face_2_center; faces[9] = hud_face_2_left;
    faces[10] = hud_face_2_right; faces[11] = hud_face_2_pain;
    faces[12] = hud_face_3_center; faces[13] = hud_face_3_left;
    faces[14] = hud_face_3_right; faces[15] = hud_face_3_pain;
    faces[16] = hud_face_4_center; faces[17] = hud_face_4_left;
    faces[18] = hud_face_4_right; faces[19] = hud_face_4_pain;
    faces[20] = hud_face_dead;
    return NULL;
}

void hud_dirty(uint8_t parts)
{
    dirty_now |= parts;
}

void hud_message(const char *m)
{
    if (m && *m) {
        msg = m;
        msg_tics = MSG_TICS;
    }
}

void hud_pain(void)
{
    pain_tics = PAIN_TICS;
}

void hud_grin(void)
{
    grin_tics = GRIN_TICS;
}

static uint8_t face_index(void)
{
    int h = player.health;
    uint8_t band;

    if (h <= 0)
        return 5 * FACE_LOOKS;
    band = h >= 80 ? 0 : h >= 60 ? 1 : h >= 40 ? 2 : h >= 20 ? 3 : 4;
    if (pain_tics)
        return band * FACE_LOOKS + 3;
    /* A grin reuses the center look until stage 3 adds the art. */
    return band * FACE_LOOKS + (grin_tics ? 0 : look);
}

void hud_tic(void)
{
    uint8_t f;

    if (msg_tics && --msg_tics == 0)
        msg = 0;
    if (pain_tics)
        pain_tics--;
    if (grin_tics)
        grin_tics--;
    if (look_tics) {
        look_tics--;
    } else {
        /* glance around now and then, mostly straight ahead */
        uint8_t r = (uint8_t)(level.tics * 73 + player.x);
        look = (r & 3) == 1 ? 1 : (r & 3) == 2 ? 2 : 0;
        look_tics = LOOK_MIN + (r % LOOK_RANGE);
    }
    f = face_index();
    if (f != face_shown) {
        face_shown = f;
        hud_dirty(HUD_FACE);
    }
}

/* Right-aligned big number with an optional percent sign. */
static void big_number(int value, int x, int y, bool pct)
{
    int digits[3];
    uint8_t i;

    if (value < 0)
        value = 0;
    if (value > 999)
        value = 999;
    digits[0] = value / 100;
    digits[1] = value / 10 % 10;
    digits[2] = value % 10;
    for (i = 0; i < 3; i++) {
        bool blank = (i == 0 && value < 100) || (i == 1 && value < 10);
        if (blank) {
            gfx_SetColor(HUD_WELL_COLOR);
            gfx_FillRectangle_NoClip(x + i * BIG_W, y, BIG_W, BIG_H);
        } else {
            gfx_Sprite_NoClip(big[digits[i]], x + i * BIG_W, y);
        }
    }
    if (pct)
        gfx_Sprite_NoClip(big[10], x + 3 * BIG_W, y);
}

static void small_number(unsigned value, int x, int y)
{
    gfx_Sprite_NoClip(small[value / 100 % 10], x, y);
    gfx_Sprite_NoClip(small[value / 10 % 10], x + SMALL_W, y);
    gfx_Sprite_NoClip(small[value % 10], x + 2 * SMALL_W, y);
}

static unsigned current_ammo(void)
{
    static const uint8_t uses[NUM_WEAPONS] = {
        0xFF, AMMO_BULLETS, AMMO_SHELLS, AMMO_BULLETS, AMMO_ROCKETS
    };
    uint8_t a = uses[player.weapon];

    return a == 0xFF ? 0 : player.ammo[a];
}

void hud_draw(void)
{
    uint8_t parts = dirty_now | dirty_prev;
    uint8_t i;

    dirty_prev = dirty_now;
    dirty_now = 0;
    if (!parts)
        return;

    if (parts & HUD_PANEL) {
        gfx_Sprite_NoClip(hud_panel_l, 0, HUD_Y);
        gfx_Sprite_NoClip(hud_panel_r, 160, HUD_Y);
        parts = HUD_ALL;
    }
    if (parts & HUD_AMMO) {
        if (player.weapon == WP_FISTS) {
            gfx_SetColor(HUD_WELL_COLOR);
            gfx_FillRectangle_NoClip(WELL_AMMO_X + 2, HUD_Y + WELL_AMMO_Y + 2, 3 * BIG_W, BIG_H);
        } else {
            big_number(current_ammo(), WELL_AMMO_X + 2, HUD_Y + WELL_AMMO_Y + 2, false);
        }
    }
    if (parts & HUD_HEALTH)
        big_number(player.health, WELL_HEALTH_X + 2, HUD_Y + WELL_HEALTH_Y + 2, true);
    if (parts & HUD_ARMOR)
        big_number(player.armor, WELL_ARMOR_X + 2, HUD_Y + WELL_ARMOR_Y + 2, true);
    if (parts & HUD_ARMS) {
        for (i = 0; i < NUM_WEAPONS; i++) {
            int x = WELL_ARMS_X + 4 + (i % 3) * 12;
            int y = HUD_Y + WELL_ARMS_Y + 3 + (i / 3) * 9;
            gfx_sprite_t *spr = i == player.weapon ? arm_sel[i]
                              : (player.weapons & (1 << i)) ? small[i + 1] : arm_off[i];
            gfx_Sprite_NoClip(spr, x, y);
        }
    }
    if (parts & HUD_FACE)
        gfx_Sprite_NoClip(faces[face_shown == 0xFF ? 0 : face_shown],
                          WELL_FACE_X + 2, HUD_Y + WELL_FACE_Y + 2);
    if (parts & HUD_KEYS) {
        for (i = 0; i < 3; i++)
            gfx_Sprite_NoClip(keyspr[(player.keys & (1 << i)) ? i : 3],
                              WELL_KEYS_X + 1, HUD_Y + WELL_KEYS_Y + 1 + i * 10);
    }
    if (parts & HUD_TABLE) {
        static const uint8_t row_y[NUM_AMMO] = { TABLE_ROW0_Y, TABLE_ROW1_Y, TABLE_ROW2_Y };
        for (i = 0; i < NUM_AMMO; i++) {
            int y = HUD_Y + WELL_TABLE_Y + row_y[i];
            small_number(player.ammo[i], WELL_TABLE_X + TABLE_NUM_X, y);
            small_number(max_ammo[i], WELL_TABLE_X + TABLE_MAX_X, y);
        }
    }
}

static void shadow_text(const char *s, int x, int y, uint8_t color)
{
    gfx_SetTextFGColor(PAL(GRAY, 15));
    gfx_PrintStringXY(s, x + 1, y + 1);
    gfx_SetTextFGColor(color);
    gfx_PrintStringXY(s, x, y);
}

void hud_overlay(unsigned fps10)
{
    gfx_SetTextBGColor(0);
    gfx_SetTextTransparentColor(0);
    if (msg)
        shadow_text(msg, 4, 4, PAL(YELLOW, 1));
    if (hud_show_fps) {
        char buf[8];
        unsigned whole = fps10 / 10;
        uint8_t n = 0;
        if (whole >= 10)
            buf[n++] = (char)('0' + whole / 10 % 10);
        buf[n++] = (char)('0' + whole % 10);
        buf[n++] = '.';
        buf[n++] = (char)('0' + fps10 % 10);
        buf[n] = 0;
        shadow_text(buf, SCREEN_W - 8 * n - 4, 4, PAL(GREEN, 1));
    }
}

/* ---------------------------------------------------------------- flashes */

#define TINT_LEVELS 4

/* [yellow/red][level-1][color], in the RAM arena */
#define tinted ((uint16_t (*)[TINT_LEVELS][256])(arena + ARENA_TINTS))
static uint8_t flash_yellow, flash_red;
static uint8_t tint_shown;                      /* 0 none, else kind*8 + level */

static uint16_t mix(uint16_t c, uint8_t tr, uint8_t tg, uint8_t tb, uint8_t amount)
{
    /* 1555 with bit 15 as green's low bit: unpack to 5/6/5, lerp, repack */
    unsigned r = (c >> 10) & 31;
    unsigned g = (((c >> 5) & 31) << 1) | (c >> 15);
    unsigned b = c & 31;

    r += ((int)tr - (int)r) * amount / 16;
    g += ((int)tg - (int)g) * amount / 16;
    b += ((int)tb - (int)b) * amount / 16;
    return (uint16_t)((r << 10) | ((g >> 1) << 5) | b | ((g & 1) << 15));
}

void fx_init(void)
{
    unsigned i;
    uint8_t l;

    for (l = 0; l < TINT_LEVELS; l++) {
        for (i = 0; i < 256; i++) {
            tinted[0][l][i] = mix(base_palette[i], 31, 58, 8, 2 + l * 2);
            tinted[1][l][i] = mix(base_palette[i], 31, 0, 0, 3 + l * 3);
        }
    }
    fx_reset();
}

void fx_reset(void)
{
    flash_yellow = flash_red = 0;
    tint_shown = 0;
    gfx_SetPalette(base_palette, sizeof base_palette, 0);
}

void fx_pickup(void)
{
    flash_yellow = 8;
}

void fx_damage(int amount)
{
    int f = flash_red + amount / 2 + 4;
    flash_red = (uint8_t)(f > 24 ? 24 : f);
    hud_pain();
}

void fx_tic(void)
{
    if (flash_yellow)
        flash_yellow--;
    if (flash_red)
        flash_red--;
}

void fx_apply(void)
{
    uint8_t want = 0;

    if (flash_red)
        want = 8 + (flash_red + 5) / 6;         /* 1..4 */
    else if (flash_yellow)
        want = (flash_yellow + 3) / 4;          /* 1..2 */
    if (want == tint_shown)
        return;
    tint_shown = want;
    if (!want)
        gfx_SetPalette(base_palette, sizeof base_palette, 0);
    else
        gfx_SetPalette(tinted[want >> 3][(want & 7) - 1], sizeof base_palette, 0);
}
