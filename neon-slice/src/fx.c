/*
 * NEON SLICE - effects: particles, sliced cube halves, slash streaks,
 * pop-up text and screen shake.
 */
#include <graphx.h>
#include <string.h>
#include "neon.h"

static uint16_t rng_state = 0xACE1;

uint16_t rnd(void)
{
    uint16_t x = rng_state;
    x ^= x << 7;
    x ^= x >> 9;
    x ^= x << 8;
    return rng_state = x;
}

/* 16 unit directions scaled by 64 */
static const int8_t dir16[16][2] = {
    {64, 0}, {59, 24}, {45, 45}, {24, 59}, {0, 64}, {-24, 59}, {-45, 45}, {-59, 24},
    {-64, 0}, {-59, -24}, {-45, -45}, {-24, -59}, {0, -64}, {24, -59}, {45, -45}, {59, -24},
};

/* ---- particles (positions in 1/16 pixel) -------------------------------- */
#define MAX_PARTS 96
typedef struct {
    int16_t x, y, vx, vy;
    uint8_t life, max, ramp;
} part_t;
static part_t parts[MAX_PARTS];
static uint8_t part_next;

static void part_add(int x, int y, int vx, int vy, uint8_t life, uint8_t ramp)
{
    part_t *p = &parts[part_next];
    if (++part_next >= MAX_PARTS) part_next = 0;
    p->x = (int16_t)(x << 4);
    p->y = (int16_t)(y << 4);
    p->vx = (int16_t)vx;
    p->vy = (int16_t)vy;
    p->life = p->max = life;
    p->ramp = ramp;
}

void fx_burst(int x, int y, uint8_t count, uint8_t ramp, uint8_t speed)
{
    while (count--) {
        uint16_t r = rnd();
        const int8_t *d = dir16[r & 15];
        int spd = 12 + ((r >> 4) % speed);
        part_add(x, y, (d[0] * spd) >> 6, ((d[1] * spd) >> 6) - 14,
                 (uint8_t)(10 + ((r >> 9) & 15)), ramp);
    }
}

/* ---- sliced halves ----------------------------------------------------- */
#define MAX_DEBRIS 16
typedef struct {
    int16_t x, y, vx, vy;           /* 1/16 pixel */
    uint8_t w, h, life, set;
    bool horiz_cut;
    int8_t side;                    /* -1 first half, +1 second half */
} debris_t;
static debris_t debris[MAX_DEBRIS];
static uint8_t debris_next;

#define MAX_SLASH 8
typedef struct { int16_t x0, y0, x1, y1; uint8_t life; } slash_t;
static slash_t slashes[MAX_SLASH];
static uint8_t slash_next;

#define DEBRIS_LIFE 12

static void debris_add(int x, int y, int w, int h, int vx, int vy, uint8_t set,
                       bool horiz, int8_t side)
{
    debris_t *d = &debris[debris_next];
    if (++debris_next >= MAX_DEBRIS) debris_next = 0;
    if (w < 2) w = 2;
    if (h < 2) h = 2;
    if (w > 120) w = 120;
    if (h > 120) h = 120;
    d->x = (int16_t)(x << 4);
    d->y = (int16_t)(y << 4);
    d->vx = (int16_t)vx;
    d->vy = (int16_t)vy;
    d->w = (uint8_t)w;
    d->h = (uint8_t)h;
    d->life = DEBRIS_LIFE;
    d->set = set;
    d->horiz_cut = horiz;
    d->side = side;
}

void fx_slice(uint8_t lane, int w, uint8_t set)
{
    int x, y, bw, bh, cx, cy;
    slash_t *s = &slashes[slash_next];
    bool horiz = (lane == LANE_LEFT || lane == LANE_RIGHT);

    if (++slash_next >= MAX_SLASH) slash_next = 0;
    block_rect(lane, w, &x, &y, &bw, &bh);
    cx = x + bw / 2;
    cy = y + bh / 2;

    if (horiz) {
        /* cut along the arrow: top half flies up, bottom half drops */
        int dir = lane == LANE_LEFT ? -30 : 30;
        debris_add(x, y, bw, bh / 2, dir, -60, set, true, -1);
        debris_add(x, y + bh / 2, bw, bh - bh / 2, dir, 22, set, true, 1);
        s->x0 = (int16_t)(x - bw / 2); s->x1 = (int16_t)(x + bw + bw / 2);
        s->y0 = (int16_t)(cy + 3); s->y1 = (int16_t)(cy - 3);
    } else {
        int up = lane == LANE_UP ? -44 : -14;
        debris_add(x, y, bw / 2, bh, -60, up, set, false, -1);
        debris_add(x + bw / 2, y, bw - bw / 2, bh, 60, up, set, false, 1);
        s->x0 = (int16_t)(cx - 3); s->x1 = (int16_t)(cx + 3);
        s->y0 = (int16_t)(y + bh + bh / 2); s->y1 = (int16_t)(y - bh / 2);
    }
    s->life = 6;

    fx_burst(cx, cy, 12, (set == P_BLK_A) ? R_A : R_B, 44);
    fx_burst(cx, cy, 8, R_WHITE, 36);
}

void fx_explode(uint8_t lane, int w)
{
    int x, y, bw, bh;
    block_rect(lane, w, &x, &y, &bw, &bh);
    fx_burst(x + bw / 2, y + bh / 2, 22, R_FIRE, 60);
    fx_burst(x + bw / 2, y + bh / 2, 8, R_MISS, 40);
    fx_shake(9);
    red_flash = 200;
}

/* ---- screen shake ------------------------------------------------------ */
static uint8_t shake_amt;

void fx_shake(uint8_t amount)
{
    if (amount > shake_amt) shake_amt = amount;
}

void fx_apply_shake(void)
{
    view_x = VP_X;
    view_y = HORIZON;
    if (shake_amt) {
        uint16_t r = rnd();
        view_x += (int)(r % (2 * shake_amt + 1)) - shake_amt;
        view_y += (int)((r >> 8) % (2 * shake_amt + 1)) - shake_amt;
    }
}

/* ---- pop-up text ------------------------------------------------------- */
typedef struct {
    char text[14];
    uint8_t ramp, life, max, scale;
} popup_t;
static popup_t pop_judge, pop_combo;

static const char *const judge_text[5] = {"PERFECT", "GREAT", "GOOD", "MISS", "BOMB!"};
static const uint8_t judge_ramp[5] = {R_PERFECT, R_GREAT, R_GOOD, R_MISS, R_FIRE};

void fx_judge(uint8_t j)
{
    strcpy(pop_judge.text, judge_text[j]);
    pop_judge.ramp = judge_ramp[j];
    pop_judge.life = pop_judge.max = 14;
    pop_judge.scale = 2;
}

void fx_combo(uint16_t combo)
{
    char *p = fmt_uint(pop_combo.text, combo, 1);
    strcpy(p + strlen(p), " COMBO!");
    pop_combo.ramp = P_RAINBOW;
    pop_combo.life = pop_combo.max = 45;
    pop_combo.scale = 3;
    fx_shake(combo >= 100 ? 6 : 4);
    fx_burst(VP_X - 90, HIT_Y - 20, 14, R_PINK, 60);
    fx_burst(VP_X + 90, HIT_Y - 20, 14, R_CYAN, 60);
    fx_burst(VP_X, HIT_Y - 30, 10, R_GOLD, 60);
}

/* Scaled text is costly on the calculator, so only the rare combo banner
   gets a drop shadow; the judgement pops one size larger for two frames. */
static void popup_draw(popup_t *p, int y, uint8_t shadow)
{
    uint8_t age, color, scale;
    if (!p->life) return;
    age = (uint8_t)(p->max - p->life);
    scale = (uint8_t)(p->scale + (age < 2 && shadow == NOSH ? 1 : 0));
    if (p->ramp == P_RAINBOW) {
        color = (uint8_t)(P_RAINBOW + (age & 7));
        if (p->life < 8) color = (uint8_t)(R_WHITE + 8 - p->life);
    } else if (age < 2) {
        color = p->ramp;
    } else {
        uint8_t fade = p->life < 6 ? (uint8_t)(7 - p->life) : 1;
        color = (uint8_t)(p->ramp + fade);
    }
    text_center(p->text, y - (scale - p->scale) * 4 - (age >> 2), scale, color, shadow);
}

void fx_draw_popups(void)
{
    popup_draw(&pop_judge, 210, NOSH);
    popup_draw(&pop_combo, 24, C_BLACK);
}

/* ---- update / draw ----------------------------------------------------- */
void fx_reset(void)
{
    memset(parts, 0, sizeof parts);
    memset(debris, 0, sizeof debris);
    memset(slashes, 0, sizeof slashes);
    pop_judge.life = pop_combo.life = 0;
    shake_amt = 0;
    red_flash = 0;
    memset(lane_fx, 0, sizeof lane_fx);
}

void fx_update(void)
{
    uint8_t i;
    for (i = 0; i < MAX_PARTS; i++) {
        part_t *p = &parts[i];
        if (!p->life) continue;
        p->life--;
        p->x += p->vx;
        p->y += p->vy;
        p->vy += 2;
    }
    for (i = 0; i < MAX_DEBRIS; i++) {
        debris_t *d = &debris[i];
        if (!d->life) continue;
        d->life--;
        d->x += d->vx;
        d->y += d->vy;
        d->vy += 5;
    }
    for (i = 0; i < MAX_SLASH; i++)
        if (slashes[i].life) slashes[i].life--;
    if (pop_judge.life) pop_judge.life--;
    if (pop_combo.life) pop_combo.life--;
    if (shake_amt) shake_amt--;
    if (red_flash) red_flash = red_flash > 24 ? red_flash - 24 : 0;
    for (i = 0; i < 4; i++) {
        uint8_t v = lane_fx[i];
        if (lanes_held & (1 << i)) {
            if (v < 110) lane_fx[i] = 110;
        } else {
            lane_fx[i] = v > 40 ? v - 40 : 0;
        }
    }
}

/* Sliced halves and slash streaks: drawn behind the blocks so they never
   hide the next incoming cube. */
void fx_draw_debris(void)
{
    uint8_t i;

    for (i = 0; i < MAX_DEBRIS; i++) {
        debris_t *d = &debris[i];
        int x, y, w, h;
        uint8_t fog, set;
        if (!d->life) continue;
        /* halves shrink as they fly off, and only fade at the very end */
        w = (d->w * (d->life + 6)) / (DEBRIS_LIFE + 6);
        h = (d->h * (d->life + 6)) / (DEBRIS_LIFE + 6);
        x = (d->x >> 4) + (d->w - w) / 2;
        y = (d->y >> 4) + (d->h - h) / 2;
        fog = d->life > 2 ? 0 : 1;
        set = (uint8_t)(d->set + fog * SH_COUNT);
        if (x >= SCR_W || y >= SCR_H || x + w <= 0 || y + h <= 0) continue;
        gfx_SetColor(set + SH_FRONT);
        gfx_FillRectangle(x, y, w, h);
        gfx_SetColor(set + SH_EDGE);
        gfx_Rectangle(x, y, w, h);
        /* glowing cut face */
        gfx_SetColor(R_WHITE);
        if (d->horiz_cut)
            gfx_HorizLine(x, d->side < 0 ? y + h - 1 : y, w);
        else
            gfx_VertLine(d->side < 0 ? x + w - 1 : x, y, h);
    }

    for (i = 0; i < MAX_SLASH; i++) {
        slash_t *s = &slashes[i];
        if (!s->life) continue;
        gfx_SetColor(s->life > 3 ? R_WHITE : R_WHITE + 7 - s->life);
        gfx_Line(s->x0, s->y0, s->x1, s->y1);
        gfx_Line(s->x0 + 1, s->y0, s->x1 + 1, s->y1);
        gfx_Line(s->x0, s->y0 + 1, s->x1, s->y1 + 1);
    }
}

void fx_draw_particles(void)
{
    uint8_t i;

    for (i = 0; i < MAX_PARTS; i++) {
        part_t *p = &parts[i];
        int x, y;
        if (!p->life) continue;
        x = p->x >> 4;
        y = p->y >> 4;
        if (x < 0 || y < 0 || x > SCR_W - 3 || y > SCR_H - 3) {
            p->life = 0;
            continue;
        }
        gfx_SetColor(p->ramp + ((p->max - p->life) * 8) / (p->max + 1));
        if (p->life * 3 > p->max * 2)
            gfx_FillRectangle_NoClip(x, y, 3, 3);
        else if (p->life * 3 > p->max)
            gfx_FillRectangle_NoClip(x, y, 2, 2);
        else
            gfx_SetPixel(x, y);
    }
}
