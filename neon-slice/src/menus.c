/*
 * NEON SLICE - title screen, level select and results.
 */
#include <graphx.h>
#include <keypadc.h>
#include <string.h>
#include <time.h>
#include "neon.h"

/* Menus run a steady 120 BPM beat of their own; 64 beats at 120 BPM are
   exactly 2^20 clock ticks, which keeps the clock wrap simple. */
#define MENU_BPM        120
#define MENU_WRAP_BEATS 64
#define MENU_WRAP_CLK   1048576UL
#define MENU_APPROACH   1300

static uint32_t menu_clock0;

static int menu_song(void)
{
    int song = (int)(((clock() - menu_clock0) * MENU_BPM) / 2560);
    if (song >= MENU_WRAP_BEATS * SUB_BEAT) {
        menu_clock0 += MENU_WRAP_CLK;
        song -= MENU_WRAP_BEATS * SUB_BEAT;
    }
    return song;
}

static void menu_reset(uint8_t theme)
{
    menu_clock0 = clock();
    theme_apply(theme);
    fx_reset();
    block_drop = 0;
    pal_dim = 255;
    pal_dim_end = 63;
}

static void menu_background(int song, bool road)
{
    int pulse = beat_pulse(song, NULL);
    fx_apply_shake();
    pal_animate((uint8_t)pulse);
    scene_draw(song, MENU_APPROACH, road);
    stars_step((uint8_t)(4 + (pulse >> 5)));
}

/* Attract mode: demo blocks fly in and get sliced right on the beat. */
static const uint8_t demo[16] = {1, 8, 2, 4, 1, 4, 2, 8, 9, 0, 6, 0, 1, 2, 4, 8};
static int attract_last_beat;

static void attract(int song)
{
    int beat, b;
    uint8_t l;

    beat_pulse(song, &beat);
    fx_update();
    if (beat != attract_last_beat) {
        uint8_t m = demo[beat & 15];
        attract_last_beat = beat;
        for (l = 0; l < 4; l++) {
            if (m & (1 << l)) {
                fx_slice(l, 0, l <= LANE_DOWN ? P_BLK_A : P_BLK_B);
                lane_fx[l] = 255;
            }
        }
    }
    fx_draw_debris();
    for (b = beat + 3; b > beat; b--) {
        int w = ((b * SUB_BEAT - song) * 256) / MENU_APPROACH;
        uint8_t m = demo[b & 15];
        if (w > W_SPAWN) continue;
        for (l = 0; l < 4; l++)
            if (m & (1 << l)) draw_block(l, w, fog_for(w), false);
    }
    fx_draw_particles();
}

/* ------------------------------------------------------------------------ */
/* Title                                                                    */
/* ------------------------------------------------------------------------ */
static uint8_t title_flicker;

void title_enter(void)
{
    menu_reset(0);
    attract_last_beat = 0;
    title_flicker = 0;
}

void title_update(void)
{
    if (confirm_pressed())
        state_goto(ST_SELECT);
}

void title_draw(void)
{
    int song = menu_song(), ph, cut, xs, xn;
    unsigned wn = neon_word_width("NEON", 6), ws = neon_word_width("SLICE", 6);

    menu_background(song, true);
    attract(song);

    /* neon sign flicker */
    if (!title_flicker && (rnd() & 63) == 0) title_flicker = 5;
    if (title_flicker) title_flicker--;
    logo_glow((title_flicker & 1) ? 110 : 255, 255);

    /* logo: NEON on top, SLICE cut in two, halves sliding on each beat */
    ph = song % SUB_BEAT;
    cut = 2 + (ph < SUB_BEAT / 2 ? ((SUB_BEAT / 2 - ph) * 7) / (SUB_BEAT / 2) : 0);
    xn = (SCR_W - (int)wn) / 2;
    xs = (SCR_W - (int)ws) / 2;
    neon_word("NEON", xn, 18, 6, P_LOGO_A, P_LOGO_GA, 0, 0);
    neon_word("SLICE", xs, 74, 6, P_LOGO_B, P_LOGO_GB, 95, cut);
    gfx_SetColor(P_LOGO_GA);
    gfx_FillRectangle(xs - 22 - cut, 94, (int)ws + 44 + 2 * cut, 3);
    gfx_SetColor(ph < SUB_BEAT / 4 ? P_HOT : C_PINK);
    gfx_HorizLine(xs - 18 - cut, 95, (int)ws + 36 + 2 * cut);

    text_center("SLICE TO THE BEAT", 126, 1, C_WHITE, C_DPINK);
    if (frame_count & 16)
        text_center("PRESS 2ND OR ENTER", 214, 1, C_WHITE, C_BLACK);
    text_at("V1.0", 4, 230, 1, C_GRAY, NOSH);
    text_right("CLEAR: QUIT", 316, 230, 1, C_GRAY, NOSH);
}

/* ------------------------------------------------------------------------ */
/* Level select                                                             */
/* ------------------------------------------------------------------------ */
static chart_info_t sel_info;
static uint8_t sel_anim;

static const char grade_chars[] = "-DCBAS";

static uint8_t grade_color(uint8_t grade)
{
    static const uint8_t col[6] = {C_DGRAY, C_RED, C_YELLOW, C_LIME, C_CYAN, C_GOLD};
    if (grade == GRADE_S) return (uint8_t)(P_RAINBOW + ((frame_count >> 1) & 7));
    return col[grade];
}

static void select_load(void)
{
    chart_load(sel_level, &sel_info);
    theme_apply(levels[sel_level].theme);
    sel_anim = 0;
}

void select_enter(void)
{
    menu_reset(levels[sel_level].theme);
    attract_last_beat = 0;
    select_load();
}

void select_update(void)
{
    uint8_t old = sel_level;
    if (key_pressed(7, kb_Up)) sel_level = (uint8_t)((sel_level + NUM_LEVELS - 1) % NUM_LEVELS);
    if (key_pressed(7, kb_Down)) sel_level = (uint8_t)((sel_level + 1) % NUM_LEVELS);
    if (sel_level != old) select_load();
    if (confirm_pressed()) state_goto(ST_PLAY);
    else if (key_pressed(1, kb_Del) || key_pressed(1, kb_Mode)) state_goto(ST_TITLE);
    if (sel_anim < 255) sel_anim++;
}

static void label_value(const char *label, const char *value, int x, int xr, int y, uint8_t vc)
{
    text_at(label, x, y, 1, C_GRAY, NOSH);
    text_right(value, xr, y, 1, vc, NOSH);
}

void select_draw(void)
{
    const level_t *lv = &levels[sel_level];
    const record_t *rec = &records[sel_level];
    char buf[16];
    uint8_t i, k;
    int song = menu_song();
    unsigned secs;

    pal_dim = 150;
    menu_background(song, true);
    attract(song);

    text_center("SELECT LEVEL", 5, 2, C_PINK, C_DPINK);

    /* level list */
    for (i = 0; i < NUM_LEVELS; i++) {
        const level_t *l = &levels[i];
        bool sel = (i == sel_level);
        int x = sel ? 10 : 6, y = 30 + i * 28;
        char num[2] = {(char)('1' + i), 0};
        char g[2] = {grade_chars[records[i].grade], 0};

        panel(x, y, 138, 25, sel ? l->ui_color : C_DGRAY);
        if (sel) {
            gfx_SetColor(l->ui_color);
            gfx_Rectangle(x - 1, y - 1, 140, 27);
            text_at(">", 1 + ((frame_count >> 2) & 1), y + 9, 1, l->ui_color, NOSH);
        }
        text_at(num, x + 6, y + 5, 2, l->ui_color, C_BLACK);
        text_at(l->name, x + 26, y + 4, 1, sel ? C_WHITE : C_GRAY, NOSH);
        for (k = 0; k < 5; k++) {
            gfx_SetColor(k < l->stars ? l->ui_color : C_DGRAY);
            gfx_FillRectangle(x + 26 + k * 8, y + 15, 6, 4);
        }
        text_at(g, x + 116, y + 5, 2, grade_color(records[i].grade), C_BLACK);
    }

    /* info panel for the highlighted level */
    panel(152, 30, 162, 136, lv->ui_color);
    text_at(lv->name, 152 + (162 - (int)text_width(lv->name, 2)) / 2, 37, 2, lv->ui_color, C_BLACK);

    text_at("DIFFICULTY", 160, 58, 1, C_GRAY, NOSH);
    for (k = 0; k < 5; k++) {
        int h = 4 + k * 2;
        gfx_SetColor(k < lv->stars ? lv->ui_color : C_DGRAY);
        gfx_FillRectangle(254 + k * 10, 66 - h, 7, h);
    }
    secs = (unsigned)(((uint32_t)sel_info.end_tick / TPB + 1) * 60 / lv->bpm);
    buf[0] = (char)('0' + secs / 60);
    buf[1] = ':';
    fmt_uint(buf + 2, secs % 60, 2);
    label_value("BPM", fmt_uint(buf + 6, lv->bpm, 1), 160, 236, 72, C_WHITE);
    label_value("TIME", buf, 242, 306, 72, C_WHITE);
    label_value("BLOCKS", fmt_uint(buf, sel_info.blocks, 1), 160, 236, 84, C_WHITE);
    if (sel_info.bombs)
        label_value("BOMBS", fmt_uint(buf, sel_info.bombs, 1), 242, 306, 84, C_ORANGE);
    else if (sel_info.doubles)
        label_value("DBLS", fmt_uint(buf, sel_info.doubles, 1), 242, 306, 84, C_WHITE);

    gfx_SetColor(C_PANEL2);
    gfx_HorizLine(158, 97, 150);

    /* theme preview cubes */
    ui_cube(170, 126, 20, P_BLK_A, LANE_LEFT);
    ui_cube(197, 126, 20, P_BLK_B, LANE_RIGHT);
    if (sel_info.bombs) {
        gfx_SetColor(P_BOMB + BS_BODY);
        gfx_FillCircle(170, 152, 6);
        gfx_SetColor(P_BOMB + BS_CORE);
        gfx_FillCircle(170, 153, 2);
    }

    label_value("BEST", fmt_uint(buf, rec->best, 1), 222, 308, 104, C_WHITE);
    label_value("COMBO", fmt_uint(buf, rec->combo, 1), 222, 308, 116, C_WHITE);
    text_at("GRADE", 222, 132, 1, C_GRAY, NOSH);
    buf[0] = grade_chars[rec->grade];
    buf[1] = 0;
    text_right(buf, 306, 129, 3, grade_color(rec->grade), C_BLACK);
    if (rec->flags & REC_CLEARED) {
        fmt_uint(buf, rec->acc / 10, 1);
        k = (uint8_t)strlen(buf);
        buf[k] = '.';
        buf[k + 1] = (char)('0' + rec->acc % 10);
        buf[k + 2] = '%';
        buf[k + 3] = 0;
        text_at(buf, 222, 144, 1, C_WHITE, NOSH);
    }
    if (rec->flags & REC_FULLCOMBO)
        text_at("FULL COMBO", 226, 156, 1, C_GOLD, NOSH);

    text_center("\x18\x19 CHOOSE   2ND PLAY   DEL BACK", 228, 1, C_GRAY, C_BLACK);
}

/* ------------------------------------------------------------------------ */
/* Results                                                                  */
/* ------------------------------------------------------------------------ */
static uint8_t res_t, res_sel;

void results_enter(void)
{
    result_t *r = &last_result;
    record_t *rec = &records[r->level];

    rec->flags |= REC_PLAYED;
    r->new_best = r->score > rec->best;
    if (r->new_best) rec->best = r->score;
    if (r->cleared) {
        rec->flags |= REC_CLEARED;
        if (r->grade > rec->grade) rec->grade = r->grade;
        if (r->acc > rec->acc) rec->acc = r->acc;
        if (r->full_combo) rec->flags |= REC_FULLCOMBO;
    }
    if (r->max_combo > rec->combo) rec->combo = r->max_combo;
    save_write(false);

    menu_reset(levels[r->level].theme);
    res_t = 0;
    res_sel = 0;
}

void results_update(void)
{
    if (key_pressed(7, kb_Left) || key_pressed(7, kb_Right)) res_sel ^= 1;
    if (confirm_pressed()) {
        if (res_t < 60) res_t = 60;         /* skip the count-up */
        else state_goto(res_sel ? ST_SELECT : ST_PLAY);
    }
    if (res_t < 255) res_t++;
    if (res_t == 62) {
        fx_burst(260, 106, 26, last_result.grade >= GRADE_A ? R_GOLD : R_CYAN, 70);
        fx_burst(260, 106, 12, R_WHITE, 50);
        fx_shake(last_result.grade == GRADE_S ? 7 : 4);
    }
    fx_update();
}

static void stat_row(const char *label, uint32_t v, int y, uint8_t color)
{
    char buf[12];
    text_at(label, 14, y, 1, color, NOSH);
    text_right(fmt_uint(buf, v, 1), 190, y, 1, C_WHITE, NOSH);
}

void results_draw(void)
{
    const result_t *r = &last_result;
    const level_t *lv = &levels[r->level];
    char buf[12];
    uint8_t k, t = res_t > 40 ? 40 : res_t;
    int song = menu_song();

    pal_dim = 130;
    menu_background(song, false);

    if (r->cleared)
        text_center("STAGE CLEAR", 6, 2, C_LIME, C_BLACK);
    else
        text_center("STAGE FAILED", 6, 2, C_RED, C_BLACK);
    text_center(lv->name, 26, 1, lv->ui_color, C_BLACK);

    panel(6, 38, 190, 138, C_PINK);
    text_at("SCORE", 14, 46, 1, C_GRAY, NOSH);
    text_at(fmt_uint(buf, (r->score * t) / 40, 7), 14, 57, 2, C_WHITE, C_DPINK);
    stat_row("MAX COMBO", r->max_combo, 82, C_GRAY);
    fmt_uint(buf, ((uint32_t)r->acc * t / 40) / 10, 1);
    k = (uint8_t)strlen(buf);
    buf[k] = '.';
    buf[k + 1] = (char)('0' + (r->acc * t / 40) % 10);
    buf[k + 2] = '%';
    buf[k + 3] = 0;
    text_at("ACCURACY", 14, 95, 1, C_GRAY, NOSH);
    text_right(buf, 190, 95, 1, C_WHITE, NOSH);
    stat_row("PERFECT", r->counts[J_PERFECT], 114, C_CYAN);
    stat_row("GREAT", r->counts[J_GREAT], 126, C_LIME);
    stat_row("GOOD", r->counts[J_GOOD], 138, C_YELLOW);
    stat_row("MISS", r->counts[J_MISS], 150, C_RED);
    if (r->has_bombs)
        stat_row("BOMBS HIT", r->bombs_hit, 162, C_ORANGE);

    panel(202, 38, 112, 138, C_CYAN);
    text_at("GRADE", 258 - (int)text_width("GRADE", 1) / 2, 46, 1, C_GRAY, NOSH);
    if (res_t >= 60) {
        /* the grade in neon tube lettering, popping in slightly larger */
        static const uint8_t glow[6] = {C_DGRAY, R_MISS + 5, R_GOOD + 5, R_GREAT + 5, C_DCYAN, C_DPINK};
        uint8_t gc = grade_color(r->grade);
        char g[2] = {grade_chars[r->grade], 0};
        uint8_t u = res_t < 63 ? 9 : 8;
        int gw = (int)neon_word_width(g, u);
        gfx_SetColor(gc);
        gfx_Rectangle(218, 60, 80, 80);
        gfx_Rectangle(220, 62, 76, 76);
        neon_word(g, 258 - gw / 2, 100 - (7 * u) / 2, u, gc, glow[r->grade], 0, 0);
    }
    if (res_t >= 60 && r->new_best && (frame_count & 8))
        text_at("NEW BEST!", 258 - (int)text_width("NEW BEST!", 1) / 2, 148, 1, C_GOLD, C_BLACK);
    if (res_t >= 60 && r->full_combo)
        text_at("FULL COMBO", 258 - (int)text_width("FULL COMBO", 1) / 2, 160, 1,
                P_RAINBOW + (frame_count & 7), C_BLACK);

    fx_draw_particles();

    for (k = 0; k < 2; k++) {
        static const char *const opt[2] = {"RETRY", "LEVELS"};
        int x = k ? 166 : 46;
        bool sel = (k == res_sel);
        panel(x, 186, 108, 28, sel ? C_PINK : C_DGRAY);
        text_at(opt[k], x + (108 - (int)text_width(opt[k], 2)) / 2, 193, 2,
                sel ? C_WHITE : C_GRAY, sel ? C_DPINK : NOSH);
    }
    text_center("\x1b\x1a CHOOSE   2ND OK   CLEAR QUIT", 226, 1, C_GRAY, C_BLACK);
}
