/*
 * NEON SLICE - gameplay: song clock, judging, scoring, HUD and pause menu.
 */
#include <graphx.h>
#include <keypadc.h>
#include <string.h>
#include <time.h>
#include "neon.h"

result_t last_result;

enum { PH_INTRO, PH_PLAY, PH_CLEAR, PH_FAIL };

#define LEAD_BEATS      4
#define HEALTH_MAX      100
#define HEALTH_START    60

static struct {
    const level_t *lv;
    chart_info_t info;
    uint16_t nnotes, head, draw_head;
    int song;                   /* song time in subticks (negative = lead-in) */
    int end_song;
    unsigned approach;          /* flight time of a block in subticks */
    int win_p, win_gr, win_go;  /* judgement windows in subticks */
    uint32_t clock0, pause_clock;
    uint8_t phase, timer;
    bool paused;
    uint8_t pause_sel;
    int health;
    uint32_t score;
    uint16_t combo, max_combo;
    uint8_t mult, mult_prog, mult_flash;
    uint16_t counts[4];
    uint16_t bombs_hit;
    uint32_t acc_sum;
} g;

static unsigned ms_to_sub(unsigned ms)
{
    /* subticks = ms * bpm * 768 / 60000 */
    return (unsigned)(((uint32_t)ms * g.lv->bpm * 16) / 1250);
}

static int song_at(uint32_t clk)
{
    int32_t el = (int32_t)(clk - g.clock0);
    if (el < 0) el = 0;
    return (int)(((uint32_t)el * g.lv->bpm) / 2560) - LEAD_BEATS * SUB_BEAT;
}

static int note_time(const note_t *n)
{
    return (int)n->tick << SUB_SHIFT;
}

/* depth index of a note at the current song time */
static int note_w(const note_t *n)
{
    return ((note_time(n) - g.song) * 256) / (int)g.approach;
}

void game_enter(void)
{
    memset(&g, 0, sizeof g);
    g.lv = &levels[sel_level];
    g.nnotes = chart_load(sel_level, &g.info);
    g.approach = ms_to_sub(g.lv->approach_ms);
    g.win_p = (int)ms_to_sub(g.lv->win_perfect);
    g.win_gr = (int)ms_to_sub(g.lv->win_great);
    g.win_go = (int)ms_to_sub(g.lv->win_good);
    g.end_song = ((int)g.info.end_tick << SUB_SHIFT) + 2 * SUB_BEAT;
    g.health = HEALTH_START;
    g.mult = 1;
    theme_apply(g.lv->theme);
    fx_reset();
    block_drop = 0;
    pal_dim = 255;
    pal_dim_end = 63;
    pq_len = 0;
    g.clock0 = clock();
    g.song = song_at(g.clock0);
}

/* ---- judging ----------------------------------------------------------- */
static void break_combo(void)
{
    g.combo = 0;
    if (g.mult > 1) g.mult >>= 1;
    g.mult_prog = 0;
}

static void hit_block(note_t *n, uint8_t j)
{
    static const uint8_t points[3] = {100, 70, 40};
    static const uint8_t heal[3] = {3, 2, 1};

    n->state = NS_HIT;
    g.counts[j]++;
    g.acc_sum += points[j];
    if (++g.combo > g.max_combo) g.max_combo = g.combo;
    /* multiplier doubles after 4, 8 and 16 more hits */
    if (g.mult < 8 && ++g.mult_prog >= g.mult * 4) {
        g.mult <<= 1;
        g.mult_prog = 0;
        g.mult_flash = 24;
    }
    g.score += (uint32_t)points[j] * g.mult;
    g.health += heal[j];
    if (g.health > HEALTH_MAX) g.health = HEALTH_MAX;

    fx_slice(n->lane, note_w(n), n->lane <= LANE_DOWN ? P_BLK_A : P_BLK_B);
    fx_judge(j);
    if (g.combo == 25 || g.combo % 50 == 0)
        fx_combo(g.combo);
}

static void miss_block(note_t *n)
{
    n->state = NS_MISSED;
    g.counts[J_MISS]++;
    break_combo();
    g.health -= g.lv->miss_dmg;
    fx_judge(J_MISS);
    fx_shake(2);
}

static void hit_bomb(note_t *n)
{
    n->state = NS_BOMBED;
    g.bombs_hit++;
    break_combo();
    g.health -= g.lv->bomb_dmg;
    fx_explode(n->lane, note_w(n));
    fx_judge(J_BOMB);
}

/* A lane key went down at song time t: slice the closest block in reach. */
static void judge_press(uint8_t lane, int t)
{
    uint16_t i;
    lane_fx[lane] = 255;
    for (i = g.head; i < g.nnotes; i++) {
        note_t *n = &notes[i];
        int dt = t - note_time(n);
        int ad = dt < 0 ? -dt : dt;
        if (-dt > g.win_go) break;
        if (n->state != NS_PENDING || n->lane != lane) continue;
        if (n->type == NT_BOMB) {
            if (ad <= g.win_gr) {
                hit_bomb(n);
                return;
            }
            continue;
        }
        if (ad > g.win_go) continue;
        hit_block(n, ad <= g.win_p ? J_PERFECT : ad <= g.win_gr ? J_GREAT : J_GOOD);
        return;
    }
}

/* Blocks that slipped past the window are misses; bombs are dodged. */
static void update_notes(void)
{
    uint16_t i;
    for (i = g.head; i < g.nnotes; i++) {
        note_t *n = &notes[i];
        if (g.song - note_time(n) <= g.win_go) break;
        if (n->state == NS_PENDING) {
            if (n->type == NT_BOMB)
                n->state = NS_PASSED;
            else
                miss_block(n);
        }
    }
    while (g.head < g.nnotes && notes[g.head].state != NS_PENDING)
        g.head++;
}

static void finish(bool cleared)
{
    result_t *r = &last_result;
    uint32_t total = (uint32_t)g.info.blocks + g.bombs_hit;

    memset(r, 0, sizeof *r);
    r->level = sel_level;
    r->score = g.score;
    r->max_combo = g.max_combo;
    memcpy(r->counts, g.counts, sizeof r->counts);
    r->bombs_hit = g.bombs_hit;
    r->acc = total ? (uint16_t)((g.acc_sum * 10) / total) : 0;
    r->cleared = cleared;
    r->full_combo = cleared && !g.counts[J_MISS] && !g.bombs_hit;
    r->has_bombs = g.info.bombs > 0;
    if (!cleared) r->grade = GRADE_D;
    else if (r->acc >= 950) r->grade = GRADE_S;
    else if (r->acc >= 880) r->grade = GRADE_A;
    else if (r->acc >= 780) r->grade = GRADE_B;
    else if (r->acc >= 650) r->grade = GRADE_C;
    else r->grade = GRADE_D;
    state_goto(ST_RESULTS);
}

/* ---- pause menu -------------------------------------------------------- */
static void pause_update(void)
{
    if (key_pressed(7, kb_Up)) g.pause_sel = (uint8_t)((g.pause_sel + 2) % 3);
    if (key_pressed(7, kb_Down)) g.pause_sel = (uint8_t)((g.pause_sel + 1) % 3);
    if (!confirm_pressed()) return;
    if (g.pause_sel == 0) {
        g.clock0 += clock() - g.pause_clock;    /* the song clock stood still */
        g.paused = false;
        pal_dim = 255;
        pal_dim_end = 63;
        pq_len = 0;
    } else {
        state_goto(g.pause_sel == 1 ? ST_PLAY : ST_SELECT);
    }
}

static void draw_pause(void)
{
    static const char *const items[3] = {"RESUME", "RESTART", "QUIT"};
    uint8_t i;
    panel(92, 66, 136, 108, C_PINK);
    text_center("PAUSED", 76, 2, C_PINK, C_DPINK);
    for (i = 0; i < 3; i++) {
        int y = 104 + i * 22;
        bool sel = (i == g.pause_sel);
        if (sel) {
            gfx_SetColor(C_PANEL2);
            gfx_FillRectangle(98, y - 3, 124, 21);
            text_at(">", 102 + ((frame_count >> 2) & 1), y, 2, C_CYAN, NOSH);
        }
        text_center(items[i], y, 2, sel ? C_WHITE : C_GRAY, sel ? C_DCYAN : NOSH);
    }
    text_center("CLEAR: EXIT GAME", 182, 1, C_GRAY, C_BLACK);
}

/* ---- per-frame update -------------------------------------------------- */
void game_update(void)
{
    uint8_t i, l;

    if (g.paused) {
        pause_update();
        return;
    }
    if (key_pressed(6, kb_Enter) && g.phase <= PH_PLAY) {
        g.paused = true;
        g.pause_sel = 0;
        g.pause_clock = clock();
        pal_dim = 70;
        pal_dim_end = 254;
        return;
    }

    if (g.phase != PH_FAIL)
        g.song = song_at(clock());

    if (g.phase <= PH_PLAY) {
        for (i = 0; i < pq_len; i++) {
            int t = song_at(pq_time[i]);
            for (l = 0; l < 4; l++)
                if (pq_lanes[i] & (1 << l)) judge_press(l, t);
        }
        update_notes();
        if (g.phase == PH_INTRO && g.song >= 0)
            g.phase = PH_PLAY;
        if (g.health <= 0) {
            g.health = 0;
            g.phase = PH_FAIL;
            g.timer = 0;
            fx_shake(10);
            red_flash = 255;
        } else if (g.head >= g.nnotes && g.song >= g.end_song) {
            g.phase = PH_CLEAR;
            g.timer = 0;
        }
    } else {
        for (i = 0; i < pq_len; i++)
            for (l = 0; l < 4; l++)
                if (pq_lanes[i] & (1 << l)) lane_fx[l] = 255;
        g.timer++;
        if (g.phase == PH_CLEAR) {
            if (g.timer % 7 == 1) {
                uint16_t r = rnd();
                static const uint8_t ramps[4] = {R_PINK, R_CYAN, R_GOLD, R_GREAT};
                fx_burst(40 + (int)(r % 240), 30 + (int)((r >> 8) % 70), 18, ramps[r & 3], 50);
            }
            if (g.timer == 95) finish(true);
        } else {
            block_drop = (g.timer * g.timer) / 2;
            if (g.timer < 30) red_flash = 180;
            if (g.timer == 75) finish(false);
        }
    }
    pq_len = 0;
    if (g.mult_flash) g.mult_flash--;
    fx_update();
    stars_step((uint8_t)(5 + (beat_pulse(g.song, NULL) >> 5)));
}

/* ---- drawing ----------------------------------------------------------- */
static void draw_notes(void)
{
    static uint16_t vis[64];
    uint8_t nv = 0;
    uint16_t i;

    /* compare times (not depth) so long rests can't overflow 24-bit math */
    while (g.draw_head < g.nnotes &&
           note_time(&notes[g.draw_head]) - g.song < -(int)(g.approach / 2) - 1)
        g.draw_head++;
    for (i = g.draw_head; i < g.nnotes && nv < 64; i++) {
        if (note_time(&notes[i]) - g.song > (int)g.approach) break;
        vis[nv++] = i;
    }
    /* far to near */
    while (nv--) {
        note_t *n = &notes[vis[nv]];
        int w = note_w(n);
        if (n->state == NS_HIT || n->state == NS_BOMBED) continue;
        /* dodged bombs and missed cubes vanish before they fill the screen */
        if (n->state != NS_PENDING && w < -56) continue;
        if (n->type == NT_BOMB) {
            draw_bomb(n->lane, w, fog_for(w));
            continue;
        }
        if (n->state == NS_MISSED) {
            draw_block(n->lane, w, 3, true);
            continue;
        }
        if (n->link < 0 && notes[vis[nv] + n->link].state == NS_PENDING)
            draw_link(n->lane, notes[vis[nv] + n->link].lane, w);
        draw_block(n->lane, w, fog_for(w), false);
    }
}

static void draw_hud(void)
{
    char buf[12];
    int prog, i, segs;
    uint8_t c;

    /* song progress */
    prog = g.song <= 0 ? 0 : (int)(((int32_t)g.song * SCR_W) / g.end_song);
    if (prog > SCR_W) prog = SCR_W;
    gfx_SetColor(C_DGRAY);
    gfx_FillRectangle_NoClip(0, 0, SCR_W, 2);
    if (prog > 0) {
        gfx_SetColor(g.lv->ui_color);
        gfx_FillRectangle_NoClip(0, 0, prog, 2);
    }

    /* score */
    text_at(fmt_uint(buf, g.score > 9999999 ? 9999999 : g.score, 7), 4, 5, 2, C_WHITE, C_DPINK);

    /* energy bar */
    text_at("HP", 196, 7, 1, C_GRAY, NOSH);
    gfx_SetColor(C_GRAY);
    gfx_Rectangle_NoClip(212, 5, 104, 10);
    segs = (g.health * 20 + 99) / 100;
    c = g.health > 50 ? C_LIME : g.health > 25 ? C_YELLOW : (frame_count & 4) ? C_RED : C_DPINK;
    gfx_SetColor(c);
    for (i = 0; i < segs; i++)
        gfx_FillRectangle_NoClip(214 + i * 5, 7, 4, 6);

    /* combo */
    if (g.combo >= 2) {
        text_at("COMBO", 6, 26, 1, C_GRAY, NOSH);
        text_at(fmt_uint(buf, g.combo, 1), 6, 36, 2, C_WHITE, NOSH);
    }

    /* multiplier */
    buf[0] = 'x';
    fmt_uint(buf + 1, g.mult, 1);
    c = g.mult_flash ? (uint8_t)(P_RAINBOW + (frame_count & 7)) :
        g.mult == 8 ? C_PINK : g.mult == 4 ? C_LIME : g.mult == 2 ? C_CYAN : C_GRAY;
    text_right(buf, 314, 24, 3, c, NOSH);
    gfx_SetColor(C_DGRAY);
    gfx_FillRectangle_NoClip(270, 51, 44, 3);
    i = g.mult == 8 ? 44 : (g.mult_prog * 44) / (g.mult * 4);
    if (i > 0) {
        gfx_SetColor(c);
        gfx_FillRectangle_NoClip(270, 51, i, 3);
    }
}

void game_draw(void)
{
    int pulse = g.paused ? 0 : beat_pulse(g.song, NULL);

    if (g.paused) {
        view_x = VP_X;
        view_y = HORIZON;
    } else {
        fx_apply_shake();
    }
    pal_animate((uint8_t)pulse);
    scene_draw(g.song, g.approach, true);
    fx_draw_debris();
    draw_notes();
    fx_draw_particles();
    fx_draw_popups();
    draw_hud();

    if (g.phase == PH_INTRO && !g.paused) {
        text_center(g.lv->name, 92, 2, g.lv->ui_color, C_BLACK);
        if (g.song < -SUB_BEAT)
            text_center("READY?", 116, 3, C_WHITE, C_DPINK);
        else
            text_center("GO!", 112, 4, P_RAINBOW + (frame_count & 7), C_BLACK);
    } else if (g.phase == PH_CLEAR) {
        text_center("STAGE CLEAR", 96, 3, P_RAINBOW + ((frame_count >> 1) & 7), C_BLACK);
        if (!g.counts[J_MISS] && !g.bombs_hit)
            text_center("FULL COMBO!", 128, 2, C_GOLD, C_BLACK);
    } else if (g.phase == PH_FAIL) {
        text_center("FAILED", 100, 4, C_RED, C_BLACK);
    }
    if (g.paused) draw_pause();
}

#ifdef NS_HOST
/* read-only hooks for the PC preview's autoplay bot (not in the .8xp) */
int host_song(void) { return g.song; }
int host_frame_sub(void) { return g.lv ? (int)((uint32_t)FRAME_CLK * g.lv->bpm / 2560) : 0; }
uint16_t host_note_count(void) { return g.nnotes; }
bool host_paused(void) { return g.paused || g.phase > PH_PLAY; }
#endif
