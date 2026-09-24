/*
 * NEON SLICE - rendering: palette/themes, pseudo-3D scene, cubes, text.
 */
#include <graphx.h>
#include <string.h>
#include "neon.h"

int view_x = VP_X, view_y = HORIZON;
uint8_t pal_fade = 255;
uint8_t pal_dim = 255;
uint8_t pal_dim_end = 63;
uint8_t lane_fx[4];
uint8_t red_flash;
int block_drop;

static uint8_t pal[256][3];         /* working palette, committed each frame */
static uint16_t depth_tab[W_MAX - W_MIN + 1];

/* ------------------------------------------------------------------------ */
/* Themes                                                                   */
/* ------------------------------------------------------------------------ */
typedef struct {
    uint8_t sky_top[3], sky_bot[3], flash[3];
    uint8_t floor[3], grid[3];
    uint8_t road[3], road_grid[3], edge[3], hit[3];
    uint8_t sun_top[3], sun_bot[3];
    uint8_t mount[3], mount_edge[3];
    uint8_t col_a[3], col_b[3];
} theme_t;

static const theme_t themes[6] = {
    { /* 0: title - synthwave pink/cyan */
        {8, 0, 26}, {80, 10, 100}, {150, 50, 190},
        {14, 2, 30}, {210, 30, 170},
        {4, 0, 16}, {0, 170, 230}, {0, 240, 255}, {255, 255, 255},
        {255, 230, 60}, {255, 30, 150},
        {26, 4, 50}, {255, 60, 210},
        {255, 30, 160}, {0, 220, 255} },
    { /* 1: Warm Up - cyan dawn */
        {0, 4, 26}, {0, 56, 120}, {40, 130, 220},
        {0, 10, 32}, {0, 120, 220},
        {0, 4, 18}, {0, 200, 255}, {70, 255, 255}, {220, 255, 255},
        {190, 255, 255}, {0, 110, 255},
        {0, 20, 56}, {0, 210, 255},
        {0, 230, 255}, {190, 80, 255} },
    { /* 2: Groove - lime pulse */
        {0, 14, 8}, {0, 76, 46}, {70, 180, 50},
        {2, 18, 8}, {60, 210, 40},
        {0, 8, 4}, {170, 255, 40}, {190, 255, 70}, {235, 255, 190},
        {255, 255, 120}, {40, 210, 60},
        {4, 34, 14}, {150, 255, 40},
        {150, 255, 30}, {255, 220, 30} },
    { /* 3: Overdrive - sunset pink */
        {22, 0, 44}, {150, 20, 96}, {230, 70, 130},
        {28, 0, 32}, {255, 40, 160},
        {10, 0, 18}, {255, 130, 40}, {255, 60, 190}, {255, 225, 245},
        {255, 225, 40}, {255, 40, 130},
        {44, 4, 54}, {255, 110, 60},
        {255, 40, 170}, {255, 150, 20} },
    { /* 4: Hyperbeat - violet storm */
        {8, 0, 32}, {64, 10, 140}, {130, 70, 255},
        {12, 0, 34}, {150, 60, 255},
        {4, 0, 18}, {0, 220, 255}, {180, 90, 255}, {235, 210, 255},
        {0, 255, 255}, {170, 40, 255},
        {26, 6, 66}, {0, 220, 255},
        {170, 70, 255}, {0, 230, 255} },
    { /* 5: Insane - red alert */
        {22, 0, 2}, {120, 0, 24}, {255, 50, 50},
        {24, 0, 6}, {255, 30, 50},
        {12, 0, 4}, {255, 130, 30}, {255, 50, 70}, {255, 235, 210},
        {255, 210, 40}, {255, 0, 50},
        {48, 0, 10}, {255, 70, 40},
        {255, 40, 50}, {255, 40, 210} },
};

static const theme_t *cur_theme = &themes[0];

static const uint8_t ui_colors[16][3] = {
    {0, 0, 0}, {255, 255, 255}, {255, 30, 160}, {0, 235, 255},
    {170, 70, 255}, {140, 255, 40}, {255, 230, 50}, {255, 140, 30},
    {255, 40, 50}, {150, 150, 185}, {64, 58, 96}, {10, 6, 28},
    {30, 18, 64}, {110, 10, 76}, {0, 64, 106}, {255, 200, 40},
};

static const uint8_t white[3] = {255, 255, 255};
static const uint8_t black[3] = {0, 0, 0};
static const uint8_t red[3] = {255, 20, 40};

static const uint8_t rainbow[8][3] = {
    {255, 40, 60}, {255, 150, 20}, {255, 240, 40}, {120, 255, 40},
    {0, 240, 255}, {60, 110, 255}, {180, 60, 255}, {255, 40, 190},
};

/* d = a + (b - a) * t / 256 */
static void mix(uint8_t *d, const uint8_t *a, const uint8_t *b, uint8_t t)
{
    uint8_t k;
    for (k = 0; k < 3; k++)
        d[k] = (uint8_t)(a[k] + ((((int)b[k] - (int)a[k]) * t) >> 8));
}

static void scale3(uint8_t *d, const uint8_t *a, uint8_t t)
{
    mix(d, black, a, t);
}

/* 8-step fade ramp: [0] hot, [1] base, [2..7] darker */
static void make_ramp(uint8_t base, const uint8_t *c)
{
    static const uint8_t lv[6] = {220, 176, 136, 100, 66, 36};
    uint8_t i;
    mix(pal[base], c, white, 150);
    memcpy(pal[base + 1], c, 3);
    for (i = 0; i < 6; i++)
        scale3(pal[base + 2 + i], c, lv[i]);
}

static void make_block_set(uint8_t base, const uint8_t *c)
{
    static const uint8_t fog[4] = {0, 90, 160, 210};
    uint8_t f, s;
    uint8_t shade[SH_COUNT][3];

    mix(shade[SH_TOP], c, white, 110);
    memcpy(shade[SH_FRONT], c, 3);
    scale3(shade[SH_SIDE], c, 120);
    mix(shade[SH_EDGE], c, white, 190);
    memcpy(shade[SH_ARROW], white, 3);
    for (f = 0; f < 4; f++)
        for (s = 0; s < SH_COUNT; s++)
            mix(pal[base + f * SH_COUNT + s], shade[s], cur_theme->road, fog[f]);
}

static void make_bomb_set(void)
{
    static const uint8_t fog[4] = {0, 90, 160, 210};
    static const uint8_t shade[BS_COUNT][3] = {
        {40, 32, 56}, {130, 120, 165}, {170, 160, 195}, {255, 40, 80}
    };
    uint8_t f, s;
    for (f = 0; f < 4; f++)
        for (s = 0; s < BS_COUNT; s++)
            mix(pal[P_BOMB + f * BS_COUNT + s], shade[s], cur_theme->road, fog[f]);
}

void theme_apply(uint8_t theme)
{
    uint8_t i;
    cur_theme = &themes[theme];
    make_block_set(P_BLK_A, cur_theme->col_a);
    make_block_set(P_BLK_B, cur_theme->col_b);
    make_bomb_set();
    make_ramp(R_A, cur_theme->col_a);
    make_ramp(R_B, cur_theme->col_b);
    for (i = 0; i < 8; i++)
        mix(pal[P_SUN + i], cur_theme->sun_top, cur_theme->sun_bot, (uint8_t)(i * 36));
}

/* Per-frame palette animation: the whole scene pulses with the beat. */
void pal_animate(uint8_t pulse)
{
    const theme_t *t = cur_theme;
    uint8_t tmp[3];
    uint8_t i;

    for (i = 0; i < 16; i++) {
        mix(tmp, t->sky_top, t->sky_bot, (uint8_t)(i * 17));
        mix(pal[P_SKY + i], tmp, t->flash, (uint8_t)((pulse * (6 + i)) >> 6));
        if (red_flash)
            mix(pal[P_SKY + i], pal[P_SKY + i], red, red_flash);
    }
    mix(pal[P_FLOOR], t->floor, t->flash, pulse >> 3);
    if (red_flash)
        mix(pal[P_FLOOR], pal[P_FLOOR], red, red_flash >> 1);
    memcpy(pal[P_ROAD], t->road, 3);

    mix(pal[P_GRID], t->grid, white, pulse >> 2);
    scale3(pal[P_GRID_DIM], pal[P_GRID], 110);
    scale3(tmp, t->road_grid, 150);
    mix(pal[P_RGRID], tmp, t->road_grid, pulse);
    mix(pal[P_RGRID_HI], t->road_grid, white, (uint8_t)(60 + (pulse >> 1)));

    mix(pal[P_EDGE], t->edge, white, pulse >> 1);
    scale3(pal[P_EDGE_GL], t->edge, (uint8_t)(90 + (pulse >> 1)));
    scale3(tmp, t->road_grid, 110);
    mix(pal[P_LANE], tmp, t->edge, pulse);

    mix(pal[P_HIT], t->edge, white, (uint8_t)(90 + ((pulse * 165) >> 8)));
    scale3(pal[P_HIT_GL], t->edge, (uint8_t)(110 + (pulse >> 1)));
    scale3(tmp, t->road_grid, 120);
    mix(pal[P_TARGET], tmp, t->hit, pulse >> 1);
    scale3(pal[P_SHADOW], t->road, 60);

    memcpy(pal[P_MOUNT], t->mount, 3);
    mix(pal[P_MOUNT_E], t->mount_edge, white, pulse >> 2);

    scale3(pal[P_STAR0], white, 90);
    scale3(pal[P_STAR1], white, 170);
    memcpy(pal[P_STAR2], white, 3);
    mix(pal[P_HALO], t->sky_bot, t->sun_bot, (uint8_t)(90 + (pulse >> 2)));

    for (i = 0; i < 4; i++)
        mix(pal[P_LANEFX + i], t->road, t->edge, lane_fx[i]);
    mix(pal[P_LINK], t->edge, white, 160);

    /* bomb cores throb quickly */
    for (i = 0; i < 4; i++) {
        uint8_t k = (uint8_t)((frame_count & 7) << 5);
        if (frame_count & 8) k = (uint8_t)(255 - k);
        mix(pal[P_BOMB + i * BS_COUNT + BS_CORE], ui_colors[C_DPINK], red, k);
    }
    for (i = 0; i < 8; i++)
        memcpy(pal[P_RAINBOW + i], rainbow[(i + (frame_count >> 1)) & 7], 3);
}

void pal_commit(void)
{
    uint16_t *hw = gfx_palette;
    unsigned i;
    for (i = 0; i < 255; i++) {
        unsigned f = pal_fade;
        uint8_t r, g, b;
        if (i >= 16 && i <= pal_dim_end)
            f = (f * pal_dim) >> 8;
        r = (uint8_t)((pal[i][0] * f) >> 8);
        g = (uint8_t)((pal[i][1] * f) >> 8);
        b = (uint8_t)((pal[i][2] * f) >> 8);
        hw[i] = gfx_RGBTo1555(r, g, b);
    }
}

/* ------------------------------------------------------------------------ */
/* Fast trapezoid fills                                                     */
/*                                                                          */
/* GraphX's triangle filler divides twice per scanline, which is slow on    */
/* the eZ80.  Every filled shape in the game is a trapezoid with either     */
/* horizontal or vertical parallel sides, so these step their edges in      */
/* 8.8 fixed point with one division per shape.                             */
/* ------------------------------------------------------------------------ */

/* Rows y0..y1: spans [xl0, xr0] at y0 and [xl1, xr1] at y1. */
void fill_trap_h(int y0, int y1, int xl0, int xr0, int xl1, int xr1)
{
    int n = y1 - y0, xl, xr, dl = 0, dr = 0, y;
    if (n < 0 || y0 >= SCR_H || y1 < 0) return;
    if (n) {
        dl = ((xl1 - xl0) << 8) / n;
        dr = ((xr1 - xr0) << 8) / n;
    }
    xl = (xl0 << 8) + 128;
    xr = (xr0 << 8) + 128;
    if (y0 < 0) {
        xl += (int)((int32_t)dl * -y0);
        xr += (int)((int32_t)dr * -y0);
        y0 = 0;
    }
    if (y1 >= SCR_H) y1 = SCR_H - 1;
    for (y = y0; y <= y1; y++) {
        int a = xl >> 8, b = xr >> 8;
        if (a > b) { int t = a; a = b; b = t; }
        gfx_HorizLine(a, y, b - a + 1);
        xl += dl;
        xr += dr;
    }
}

/* Columns x0..x1: spans [yt0, yb0] at x0 and [yt1, yb1] at x1. */
void fill_trap_v(int x0, int x1, int yt0, int yb0, int yt1, int yb1)
{
    int n = x1 - x0, yt, yb, dt = 0, db = 0, x;
    if (n < 0 || x0 >= SCR_W || x1 < 0) return;
    if (n) {
        dt = ((yt1 - yt0) << 8) / n;
        db = ((yb1 - yb0) << 8) / n;
    }
    yt = (yt0 << 8) + 128;
    yb = (yb0 << 8) + 128;
    if (x0 < 0) {
        yt += (int)((int32_t)dt * -x0);
        yb += (int)((int32_t)db * -x0);
        x0 = 0;
    }
    if (x1 >= SCR_W) x1 = SCR_W - 1;
    for (x = x0; x <= x1; x++) {
        int a = yt >> 8, b = yb >> 8;
        if (a > b) { int t = a; a = b; b = t; }
        gfx_VertLine(x, a, b - a + 1);
        yt += dt;
        yb += db;
    }
}

/* ------------------------------------------------------------------------ */
/* Init                                                                     */
/* ------------------------------------------------------------------------ */
#define SUN_R   36
#define HALO_R  (SUN_R + 4)
#define SKY_H   HORIZON

/* The sky (gradient, striped sun, mountains) never moves relative to the
   horizon and only ever uses fixed palette slots, so it is drawn once into
   two sprites and blitted each frame; the beat pulse still animates it
   through the palette. */
gfx_UninitedSprite(sky_left, 160, SKY_H);
gfx_UninitedSprite(sky_right, 160, SKY_H);

#define NSTARS  44
static int16_t st_x[NSTARS], st_y[NSTARS], st_z[NSTARS];

static void star_reset(uint8_t i, bool far)
{
    st_x[i] = (int16_t)(rnd() % 2400) - 1200;
    st_y[i] = (int16_t)(rnd() % 900) + 24;
    st_z[i] = far ? 1024 : (int16_t)(rnd() % 1000) + 24;
}

static uint8_t isqrt(unsigned v)
{
    unsigned x = 0;
    while ((x + 1) * (x + 1) <= v) x++;
    return (uint8_t)x;
}

static const int8_t mount_h[19] = {
    18, 10, 26, 12, 30, 16, 7, 15, 5, 0, 5, 14, 8, 21, 33, 13, 24, 11, 20
};

static void sky_build(void)
{
    /* rows (counted up from the horizon) cut out of the sun */
    static const uint32_t gaps = (7UL << 2) | (3UL << 8) | (3UL << 13) | (1UL << 18) | (1UL << 23);
    int i, y, y0 = 0, cy = SKY_H + 6, top = cy - SUN_R;

    for (i = 0; i < 16; i++) {
        int y1 = (SKY_H * (i + 1)) >> 4;
        gfx_SetColor(P_SKY + i);
        gfx_FillRectangle_NoClip(0, y0, SCR_W, y1 - y0);
        y0 = y1;
    }
    for (y = cy - HALO_R; y < SKY_H; y++) {
        int dy = cy - y, h = SKY_H - y, hw = isqrt(HALO_R * HALO_R - dy * dy);
        gfx_SetColor(P_HALO);
        gfx_HorizLine_NoClip(VP_X - hw, y, 2 * hw + 1);
        if (dy > SUN_R || (h < 32 && (gaps & (1UL << h)))) continue;
        hw = isqrt(SUN_R * SUN_R - dy * dy);
        gfx_SetColor(P_SUN + ((y - top) * 8) / (SKY_H - top + 1));
        gfx_HorizLine_NoClip(VP_X - hw, y, 2 * hw + 1);
    }
    gfx_SetColor(P_MOUNT);
    for (i = 0; i < 16; i++)
        fill_trap_v(i * 20, i * 20 + 20, SKY_H - mount_h[i + 1], SKY_H - 1,
                    SKY_H - mount_h[i + 2], SKY_H - 1);
    gfx_SetColor(P_MOUNT_E);
    for (i = 0; i < 16; i++)
        gfx_Line(i * 20, SKY_H - mount_h[i + 1], i * 20 + 20, SKY_H - mount_h[i + 2]);

    sky_left->width = sky_right->width = 160;
    sky_left->height = sky_right->height = SKY_H;
    gfx_GetSprite(sky_left, 0, 0);
    gfx_GetSprite(sky_right, 160, 0);
}

void render_init(void)
{
    int w;
    uint8_t i;

    /*
     * Depth table: perspective scale (x.12) for a depth index w, where
     * w = 256 * time_until_hit / approach_time.  s(w) = 1/(1 + 2.25u + 6.75u^2)
     * with u = w/256: blocks spawn at s = 0.1, reach s = 1 at the hit line
     * and keep a readable speed near the end.  Past the hit line (w < 0) the
     * scale keeps growing linearly so missed blocks fly past the camera.
     */
    for (w = W_MIN; w <= W_MAX; w++) {
        uint32_t s;
        if (w < 0) {
            s = (uint32_t)(S_ONE + 36 * -w);
        } else {
            uint32_t d = 4096UL + 36UL * (uint32_t)w + (27UL * (uint32_t)w * (uint32_t)w) / 64;
            s = 16777216UL / d;
        }
        depth_tab[w - W_MIN] = (uint16_t)s;
    }

    for (i = 0; i < NSTARS; i++)
        star_reset(i, false);

    for (i = 0; i < 16; i++)
        memcpy(pal[i], ui_colors[i], 3);
    make_ramp(R_FIRE, (const uint8_t[3]){255, 150, 30});
    mix(pal[R_FIRE], white, (const uint8_t[3]){255, 255, 120}, 128);
    make_ramp(R_WHITE, (const uint8_t[3]){210, 210, 255});
    make_ramp(R_PERFECT, ui_colors[C_CYAN]);
    make_ramp(R_GREAT, ui_colors[C_LIME]);
    make_ramp(R_GOOD, ui_colors[C_YELLOW]);
    make_ramp(R_MISS, ui_colors[C_RED]);
    make_ramp(R_GOLD, ui_colors[C_GOLD]);
    make_ramp(R_PINK, ui_colors[C_PINK]);
    make_ramp(R_CYAN, ui_colors[C_CYAN]);
    memcpy(pal[P_LOGO_A], ui_colors[C_PINK], 3);
    memcpy(pal[P_LOGO_GA], ui_colors[C_DPINK], 3);
    memcpy(pal[P_LOGO_B], ui_colors[C_CYAN], 3);
    memcpy(pal[P_LOGO_GB], ui_colors[C_DCYAN], 3);
    memcpy(pal[P_HOT], white, 3);

    theme_apply(0);
    pal_animate(0);
    sky_build();
}

/* The logo flickers like a real neon sign: set its tube brightness. */
void logo_glow(uint8_t a, uint8_t b)
{
    scale3(pal[P_LOGO_A], ui_colors[C_PINK], a);
    scale3(pal[P_LOGO_GA], ui_colors[C_DPINK], a);
    scale3(pal[P_LOGO_B], ui_colors[C_CYAN], b);
    scale3(pal[P_LOGO_GB], ui_colors[C_DCYAN], b);
}

/* ------------------------------------------------------------------------ */
/* Projection helpers                                                       */
/* ------------------------------------------------------------------------ */
unsigned depth_scale(int w)
{
    if (w < W_MIN) w = W_MIN;
    if (w > W_MAX) w = W_MAX;
    return depth_tab[w - W_MIN];
}

static int proj_x(int x, unsigned s)
{
    return view_x + ((x * (int)s) >> S_SHIFT);
}

static int proj_y(int h, unsigned s)
{
    return view_y + (((CAM_H - h) * (int)s) >> S_SHIFT);
}

static const int8_t lane_x[4] = {-84, -28, 28, 84};

uint8_t fog_for(int w)
{
    if (w > 190) return 3;
    if (w > 130) return 2;
    if (w > 70) return 1;
    return 0;
}

/* Beat pulse 0..255: sharp flash on each beat, strongest on the downbeat. */
int beat_pulse(int song, int *beat_out)
{
    int beat, ph, v, p;
    if (song >= 0) {
        beat = song / SUB_BEAT;
    } else {
        beat = -((-song + SUB_BEAT - 1) / SUB_BEAT);
    }
    ph = song - beat * SUB_BEAT;
    if (beat_out) *beat_out = beat;
    if (ph >= SUB_BEAT / 2) return 0;
    v = SUB_BEAT / 2 - ph;
    p = (v * v) / 579;
    if (beat & 3) p = (p * 5) >> 3;
    return p;
}

/* ------------------------------------------------------------------------ */
/* Background scene                                                         */
/* ------------------------------------------------------------------------ */
void stars_step(uint8_t speed)
{
    uint8_t i;
    for (i = 0; i < NSTARS; i++) {
        st_z[i] -= speed;
        if (st_z[i] < 24) star_reset(i, true);
    }
}

static void draw_sky(void)
{
    int dx = view_x - VP_X, dy = view_y - HORIZON;
    uint8_t i;

    if (!dx && !dy) {
        gfx_Sprite_NoClip(sky_left, 0, 0);
        gfx_Sprite_NoClip(sky_right, 160, 0);
    } else {
        /* shaking: cover the strips the shifted sky leaves open */
        gfx_SetColor(P_SKY);
        if (dy > 0) gfx_FillRectangle_NoClip(0, 0, SCR_W, dy);
        if (dx > 0) gfx_FillRectangle(0, 0, dx, view_y);
        if (dx < 0) gfx_FillRectangle(SCR_W + dx, 0, -dx, view_y);
        gfx_Sprite(sky_left, dx, dy);
        gfx_Sprite(sky_right, 160 + dx, dy);
    }

    /* stars fly out from the vanishing point, behind the sun and mountains */
    for (i = 0; i < NSTARS; i++) {
        int z = st_z[i];
        int sx = view_x + (st_x[i] * 64) / z;
        int sy = view_y - (st_y[i] * 64) / z;
        uint8_t under;
        if (sx < 1 || sx >= SCR_W - 2 || sy < 1 || sy >= view_y - 1) {
            star_reset(i, true);
            continue;
        }
        under = gfx_GetPixel(sx, sy);
        if (under < P_SKY || under >= P_SKY + 16) continue;
        if (z < 170) {
            int z2 = z + 70;
            gfx_SetColor(P_STAR1);
            gfx_Line(view_x + (st_x[i] * 64) / z2, view_y - (st_y[i] * 64) / z2, sx, sy);
            gfx_SetColor(P_STAR2);
            gfx_FillRectangle_NoClip(sx, sy, 2, 2);
        } else {
            gfx_SetColor(z > 600 ? P_STAR0 : P_STAR1);
            gfx_SetPixel(sx, sy);
        }
    }
}

/* Lane glow beams when a key is pressed. */
static void draw_lane_fx(void)
{
    uint8_t l;
    unsigned sf = depth_scale(56);
    int yf = proj_y(0, sf), yn = view_y + CAM_H;
    for (l = 0; l < 4; l++) {
        int xl = lane_x[l] - LANE_W / 2, xr = lane_x[l] + LANE_W / 2;
        if (!lane_fx[l]) continue;
        gfx_SetColor(P_LANEFX + l);
        fill_trap_h(yf, yn, proj_x(xl, sf), proj_x(xr, sf), view_x + xl, view_x + xr);
    }
}

void scene_draw(int song, unsigned approach, bool road)
{
    int hy = view_y, yb = SCR_H - 1;
    unsigned sb = (unsigned)(((yb - hy) << S_SHIFT) / CAM_H);
    int i, dt, dt0, ph, beat;
    static const int16_t radial[6] = {150, 200, 270, 370, 520, 760};

    draw_sky();

    /* floor + converging grid lines */
    gfx_SetColor(P_FLOOR);
    gfx_FillRectangle_NoClip(0, hy, SCR_W, SCR_H - hy);
    {
        unsigned s0 = depth_scale(520);
        int ys = proj_y(0, s0);
        gfx_SetColor(P_GRID_DIM);
        for (i = 0; i < 6; i++) {
            int xb = (radial[i] * (int)sb) >> S_SHIFT;
            int xs = (radial[i] * (int)s0) >> S_SHIFT;
            gfx_Line(view_x - xs, ys, view_x - xb, yb);
            gfx_Line(view_x + xs, ys, view_x + xb, yb);
        }
    }

    if (road) {
        int xr = (ROAD_HALF * (int)sb) >> S_SHIFT;
        gfx_SetColor(P_ROAD);
        fill_trap_h(hy, yb, view_x, view_x, view_x - xr, view_x + xr);
    }

    /* light streaks racing along the floor beside the road, twice as fast
       as the beat grid */
    {
        static const int16_t sx[6] = {-150, 135, -205, 180, -128, 250};
        uint8_t k;
        gfx_SetColor(P_EDGE);
        for (k = 0; k < 6; k++) {
            int cyc = 2 * SUB_BEAT;
            int t = (song * 2 + k * (cyc / 6)) % cyc;
            int w0, w1;
            if (t < 0) t += cyc;
            w0 = ((cyc - t) * 2 * 256) / (int)approach - 60;
            w1 = w0 + 70;
            if (w0 > W_MAX || w1 < W_MIN) continue;
            gfx_Line(view_x + ((sx[k] * (int)depth_scale(w1)) >> S_SHIFT), proj_y(0, depth_scale(w1)),
                     view_x + ((sx[k] * (int)depth_scale(w0)) >> S_SHIFT), proj_y(0, depth_scale(w0)));
        }
    }

    /* scrolling cross lines, one per half beat: a bright one crosses the
       hit line exactly on every beat */
    ph = song % (SUB_BEAT / 2);
    if (ph < 0) ph += SUB_BEAT / 2;
    dt0 = ph ? SUB_BEAT / 2 - ph : 0;
    for (dt = dt0 - SUB_BEAT; ; dt += SUB_BEAT / 2) {
        int w = (dt * 256) / (int)approach;
        unsigned s;
        int y, rx;
        bool on_beat, down;
        if (w < W_MIN) continue;
        if (w > W_MAX) break;
        s = depth_tab[w - W_MIN];
        y = proj_y(0, s);
        if (y > yb) continue;
        beat = song + dt;
        on_beat = ((beat % SUB_BEAT) + SUB_BEAT) % SUB_BEAT == 0;
        down = on_beat && ((((beat / SUB_BEAT) % 4) + 4) % 4 == 0);
        rx = road ? (ROAD_HALF * (int)s) >> S_SHIFT : 0;
        gfx_SetColor(on_beat ? P_GRID : P_GRID_DIM);
        if (view_x - rx > 0) gfx_HorizLine_NoClip(0, y, view_x - rx);
        if (view_x + rx < SCR_W) gfx_HorizLine_NoClip(view_x + rx, y, SCR_W - view_x - rx);
        if (road && w <= W_SPAWN + 64) {
            gfx_SetColor(down ? P_RGRID_HI : on_beat ? P_RGRID : P_GRID_DIM);
            gfx_HorizLine(view_x - rx, y, 2 * rx);
        }
    }

    if (!road) return;

    draw_lane_fx();

    /* lane dividers and glowing road edges */
    {
        unsigned s0 = depth_scale(W_SPAWN + 80);
        int ys = proj_y(0, s0);
        gfx_SetColor(P_LANE);
        for (i = -1; i <= 1; i++) {
            int x = i * LANE_W;
            gfx_Line(proj_x(x, s0), ys, view_x + ((x * (int)sb) >> S_SHIFT), yb);
        }
        gfx_SetColor(P_EDGE_GL);
        for (i = -1; i <= 1; i += 2) {
            int x = i * (ROAD_HALF + 5);
            gfx_Line(view_x, hy, view_x + ((x * (int)sb) >> S_SHIFT), yb);
            x = i * (ROAD_HALF - 4);
            gfx_Line(proj_x(x, s0), ys, view_x + ((x * (int)sb) >> S_SHIFT), yb);
        }
        gfx_SetColor(P_EDGE);
        for (i = -1; i <= 1; i += 2) {
            int xb = view_x + ((i * ROAD_HALF * (int)sb) >> S_SHIFT);
            gfx_Line(view_x, hy, xb, yb);
            gfx_Line(view_x + i, hy, xb + i, yb);
        }
    }

    /* target brackets where the cubes land */
    {
        int yt = view_y + CAM_H - BLK_TOP, ybt = view_y + CAM_H - BLK_BOT - 1;
        uint8_t l;
        gfx_SetColor(P_TARGET);
        for (l = 0; l < 4; l++) {
            int x0 = view_x + lane_x[l] - BLK_HALF, x1 = view_x + lane_x[l] + BLK_HALF - 1;
            gfx_HorizLine(x0, yt, 7);
            gfx_HorizLine(x1 - 6, yt, 7);
            gfx_HorizLine(x0, ybt, 7);
            gfx_HorizLine(x1 - 6, ybt, 7);
            gfx_VertLine(x0, yt, 7);
            gfx_VertLine(x1, yt, 7);
            gfx_VertLine(x0, ybt - 6, 7);
            gfx_VertLine(x1, ybt - 6, 7);
        }
    }

    /* hit line: throbs on every beat */
    {
        int y = view_y + CAM_H;
        int th = 2 + (beat_pulse(song, NULL) >> 6);
        gfx_SetColor(P_HIT_GL);
        gfx_FillRectangle(view_x - ROAD_HALF - 6, y - th, 2 * ROAD_HALF + 13, 2 * th + 1);
        gfx_SetColor(P_HIT);
        gfx_FillRectangle(view_x - ROAD_HALF - 3, y - 1, 2 * ROAD_HALF + 7, 3);
        gfx_FillRectangle(view_x - ROAD_HALF - 6, y - th - 1, 4, 2 * th + 3);
        gfx_FillRectangle(view_x + ROAD_HALF + 3, y - th - 1, 4, 2 * th + 3);
    }
}

/* ------------------------------------------------------------------------ */
/* Cubes, bombs, arrows                                                     */
/* ------------------------------------------------------------------------ */

/* Arrow made of a triangular head and a stem, pointing in lane direction. */
void draw_arrow(uint8_t dir, int cx, int cy, int size, uint8_t color)
{
    int hu = (size * 3) / 10;       /* tip distance from the centre */
    int bu = size / 16;             /* head base, just behind the centre */
    int su = (size * 3) / 10;       /* stem tail */
    int hv = (size * 3) / 10;       /* half width of the head */
    int sv = size / 9;              /* half width of the stem */

    if (sv < 1) sv = 1;
    gfx_SetColor(color);
    switch (dir) {
        case LANE_UP:
            fill_trap_h(cy - hu, cy + bu, cx, cx, cx - hv, cx + hv);
            gfx_FillRectangle(cx - sv, cy + bu, 2 * sv + 1, su - bu + 1);
            break;
        case LANE_DOWN:
            fill_trap_h(cy - bu, cy + hu, cx - hv, cx + hv, cx, cx);
            gfx_FillRectangle(cx - sv, cy - su, 2 * sv + 1, su - bu + 1);
            break;
        case LANE_LEFT:
            fill_trap_v(cx - hu, cx + bu, cy, cy, cy - hv, cy + hv);
            gfx_FillRectangle(cx + bu, cy - sv, su - bu + 1, 2 * sv + 1);
            break;
        default:
            fill_trap_v(cx - bu, cx + hu, cy - hv, cy + hv, cy, cy);
            gfx_FillRectangle(cx - su, cy - sv, su - bu + 1, 2 * sv + 1);
            break;
    }
}

/* Screen rectangle of a cube's front face. */
void block_rect(uint8_t lane, int w, int *x, int *y, int *wd, int *ht)
{
    unsigned s = depth_scale(w);
    int x0 = proj_x(lane_x[lane] - BLK_HALF, s);
    int y0 = proj_y(BLK_TOP, s) + block_drop;
    *x = x0;
    *y = y0;
    *wd = proj_x(lane_x[lane] + BLK_HALF, s) - x0;
    *ht = proj_y(BLK_BOT, s) + block_drop - y0;
}

void draw_block(uint8_t lane, int w, uint8_t fog, bool dead)
{
    unsigned s1 = depth_scale(w), s2;
    int xc = lane_x[lane];
    int x0 = proj_x(xc - BLK_HALF, s1), x1 = proj_x(xc + BLK_HALF, s1);
    int yt = proj_y(BLK_TOP, s1) + block_drop, yb = proj_y(BLK_BOT, s1) + block_drop;
    int fw = x1 - x0, fy = proj_y(0, s1);
    int bx0, bx1, byt, byb;
    uint8_t set = (uint8_t)(((lane == LANE_LEFT || lane == LANE_DOWN) ? P_BLK_A : P_BLK_B)
                            + (dead ? 3 : fog) * SH_COUNT);

    if (yt >= SCR_H) return;

    /* soft shadow on the floor */
    if (!block_drop && fw > 3) {
        gfx_SetColor(P_SHADOW);
        gfx_FillRectangle(x0 + fw / 8, fy - (fw >> 4), fw - fw / 4, (fw >> 3) + 1);
    }
    if (fw < 5) {
        gfx_SetColor(set + SH_FRONT);
        gfx_FillRectangle(x0, yt, fw > 0 ? fw : 1, yb - yt > 0 ? yb - yt : 1);
        return;
    }

    /* back face scale: the cube is BLK_DEPTH units deep */
    s2 = (s1 * 256) / (256 + ((BLK_DEPTH * s1) >> S_SHIFT));
    bx0 = proj_x(xc - BLK_HALF, s2);
    bx1 = proj_x(xc + BLK_HALF, s2);
    byt = proj_y(BLK_TOP, s2) + block_drop;
    byb = proj_y(BLK_BOT, s2) + block_drop;

    /* lit top face, shaded side facing the centre, bright front */
    gfx_SetColor(set + SH_TOP);
    fill_trap_h(byt, yt, bx0, bx1 - 1, x0, x1 - 1);
    gfx_SetColor(set + SH_SIDE);
    if (xc < 0)
        fill_trap_v(x1 - 1, bx1 - 1, yt, yb - 1, byt, byb - 1);
    else
        fill_trap_v(bx0, x0, byt, byb - 1, yt, yb - 1);
    gfx_SetColor(set + SH_FRONT);
    gfx_FillRectangle(x0, yt, fw, yb - yt);

    /* neon edges */
    gfx_SetColor(set + SH_EDGE);
    gfx_Rectangle(x0, yt, fw, yb - yt);
    gfx_HorizLine(bx0, byt, bx1 - bx0);
    gfx_Line(x0, yt, bx0, byt);
    gfx_Line(x1 - 1, yt, bx1 - 1, byt);
    if (xc < 0)
        gfx_VertLine(bx1 - 1, byt, byb - byt);
    else
        gfx_VertLine(bx0, byt, byb - byt);

    if (fw >= 9 && !dead)
        draw_arrow(lane, x0 + fw / 2, (yt + yb) / 2, fw, set + SH_ARROW);
}

void draw_bomb(uint8_t lane, int w, uint8_t fog)
{
    static const int8_t spike[8][2] = {
        {64, 0}, {45, 45}, {0, 64}, {-45, 45}, {-64, 0}, {-45, -45}, {0, -64}, {45, -45}
    };
    unsigned s = depth_scale(w);
    int cx = proj_x(lane_x[lane], s);
    int cy = proj_y(BLK_BOT + BLK_HALF, s) + block_drop;
    int r = (18 * (int)s) >> S_SHIFT, fy = proj_y(0, s);
    uint8_t set = (uint8_t)(P_BOMB + fog * BS_COUNT), k, rot;

    if (cy - r >= SCR_H) return;
    if (!block_drop && r > 1) {
        gfx_SetColor(P_SHADOW);
        gfx_FillRectangle(cx - r, fy - (r >> 3), 2 * r, (r >> 2) + 1);
    }
    if (r < 2) {
        gfx_SetColor(set + BS_CORE);
        gfx_FillRectangle(cx - 1, cy - 1, 2, 2);
        return;
    }
    /* rotating spikes */
    rot = (uint8_t)((frame_count >> 2) & 1);
    gfx_SetColor(set + BS_SPIKE);
    for (k = 0; k < 4; k++) {
        const int8_t *d = spike[(k * 2 + rot) & 7];
        int l = r + r / 2;
        gfx_Line(cx - (d[0] * l) / 64, cy - (d[1] * l) / 64,
                 cx + (d[0] * l) / 64, cy + (d[1] * l) / 64);
    }
    gfx_SetColor(set + BS_BODY);
    gfx_FillCircle(cx, cy, r);
    gfx_SetColor(set + BS_SPIKE);
    gfx_Circle(cx, cy, r);
    gfx_SetColor(set + BS_SHINE);
    gfx_FillCircle(cx - r / 3, cy - r / 3, r / 4 + 1);
    gfx_SetColor(set + BS_CORE);
    gfx_FillCircle(cx, cy + r / 6, r / 3 + 1);
}

void draw_link(uint8_t lane_a, uint8_t lane_b, int w)
{
    unsigned s = depth_scale(w);
    int y = proj_y(BLK_BOT + BLK_HALF, s) + block_drop;
    int xa = proj_x(lane_x[lane_a], s), xb = proj_x(lane_x[lane_b], s);
    int t = (int)(s >> 11) + 1;
    if (xa > xb) { int tmp = xa; xa = xb; xb = tmp; }
    gfx_SetColor(P_LINK);
    gfx_FillRectangle(xa, y - t / 2, xb - xa, t);
}

/* A cube for menus, drawn with a simple oblique projection. */
void ui_cube(int cx, int cy, int size, uint8_t set, uint8_t dir)
{
    int x0 = cx - size / 2, y0 = cy - size / 2, d = size / 4;
    gfx_SetColor(set + SH_TOP);
    fill_trap_h(y0 - d, y0 - 1, x0 + d, x0 + size + d - 1, x0 + 1, x0 + size);
    gfx_SetColor(set + SH_SIDE);
    fill_trap_v(x0 + size, x0 + size + d - 1, y0, y0 + size - 1, y0 - d + 1, y0 + size - d);
    gfx_SetColor(set + SH_FRONT);
    gfx_FillRectangle(x0, y0, size, size);
    gfx_SetColor(set + SH_EDGE);
    gfx_Rectangle(x0, y0, size, size);
    gfx_Line(x0, y0, x0 + d, y0 - d);
    gfx_Line(x0 + size - 1, y0, x0 + size + d - 1, y0 - d);
    gfx_HorizLine(x0 + d, y0 - d, size);
    gfx_VertLine(x0 + size + d - 1, y0 - d, size);
    draw_arrow(dir, cx, cy, size, set + SH_ARROW);
}

/* ------------------------------------------------------------------------ */
/* Text                                                                     */
/* ------------------------------------------------------------------------ */
unsigned text_width(const char *s, uint8_t scale)
{
    gfx_SetTextScale(scale, scale);
    return gfx_GetStringWidth(s);
}

/* GraphX does not clip scaled text, so refuse anything off-screen. */
bool text_at(const char *s, int x, int y, uint8_t scale, uint8_t color, uint8_t shadow)
{
    int w = (int)text_width(s, scale);
    int off = scale > 1 ? scale / 2 + 1 : 1;
    if (x < 0 || y < 0 || x + w + off > SCR_W || y + 8 * scale + off > SCR_H)
        return false;
    if (shadow != NOSH) {
        gfx_SetTextFGColor(shadow);
        gfx_PrintStringXY(s, x + off, y + off);
    }
    gfx_SetTextFGColor(color);
    gfx_PrintStringXY(s, x, y);
    return true;
}

void text_center(const char *s, int y, uint8_t scale, uint8_t color, uint8_t shadow)
{
    text_at(s, (SCR_W - (int)text_width(s, scale)) / 2, y, scale, color, shadow);
}

void text_right(const char *s, int xr, int y, uint8_t scale, uint8_t color, uint8_t shadow)
{
    text_at(s, xr - (int)text_width(s, scale), y, scale, color, shadow);
}

char *fmt_uint(char *buf, uint32_t v, uint8_t min_digits)
{
    char tmp[12];
    uint8_t n = 0, i = 0;
    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v || n < min_digits);
    while (n) buf[i++] = tmp[--n];
    buf[i] = 0;
    return buf;
}

void panel(int x, int y, int w, int h, uint8_t border)
{
    gfx_SetColor(C_PANEL);
    gfx_FillRectangle(x, y, w, h);
    gfx_SetColor(border);
    gfx_Rectangle(x, y, w, h);
    gfx_SetColor(C_PANEL2);
    gfx_Rectangle(x + 2, y + 2, w - 4, h - 4);
}

/* ------------------------------------------------------------------------ */
/* Neon tube lettering for the logo and grades                              */
/* ------------------------------------------------------------------------ */
typedef struct { int8_t x, y, w, h; } stroke_t;     /* h == 0: N diagonal */

static const stroke_t g_N[] = {{0,0,1,7},{4,0,1,7},{1,0,3,0}};
static const stroke_t g_E[] = {{0,0,1,7},{0,0,5,1},{0,3,4,1},{0,6,5,1}};
static const stroke_t g_O[] = {{0,0,5,1},{0,6,5,1},{0,0,1,7},{4,0,1,7}};
static const stroke_t g_S[] = {{0,0,5,1},{0,0,1,4},{0,3,5,1},{4,3,1,4},{0,6,5,1}};
static const stroke_t g_L[] = {{0,0,1,7},{0,6,5,1}};
static const stroke_t g_I[] = {{1,0,1,7},{0,0,3,1},{0,6,3,1}};
static const stroke_t g_C[] = {{0,0,5,1},{0,0,1,7},{0,6,5,1}};
static const stroke_t g_A[] = {{0,0,5,1},{0,0,1,7},{4,0,1,7},{0,3,5,1}};
static const stroke_t g_B[] = {{0,0,1,7},{0,0,4,1},{0,3,4,1},{0,6,4,1},{4,1,1,2},{4,4,1,2}};
static const stroke_t g_D[] = {{0,0,1,7},{0,0,4,1},{0,6,4,1},{4,1,1,5}};

static const stroke_t *glyph(char c, uint8_t *count, uint8_t *width)
{
    *width = 5;
    switch (c) {
        case 'A': *count = 4; return g_A;
        case 'B': *count = 6; return g_B;
        case 'D': *count = 4; return g_D;
        case 'N': *count = 3; return g_N;
        case 'E': *count = 4; return g_E;
        case 'O': *count = 4; return g_O;
        case 'S': *count = 5; return g_S;
        case 'L': *count = 2; return g_L;
        case 'I': *count = 3; *width = 3; return g_I;
        case 'C': *count = 3; return g_C;
    }
    *count = 0;
    return g_N;
}

unsigned neon_word_width(const char *word, uint8_t unit)
{
    unsigned w = 0;
    uint8_t n, gw;
    while (*word) {
        glyph(*word++, &n, &gw);
        w += (gw + 2) * unit;
    }
    return w - 2 * unit;
}

/* rectangle, split at cut_y: the upper part slides left, the lower right */
static void cut_rect(int x, int y, int w, int h, int cut_y, int dx)
{
    if (dx && y < cut_y && y + h > cut_y) {
        gfx_FillRectangle(x - dx, y, w, cut_y - y);
        gfx_FillRectangle(x + dx, cut_y, w, y + h - cut_y);
    } else if (dx && y + h <= cut_y) {
        gfx_FillRectangle(x - dx, y, w, h);
    } else if (dx) {
        gfx_FillRectangle(x + dx, y, w, h);
    } else {
        gfx_FillRectangle(x, y, w, h);
    }
}

void neon_word(const char *word, int x, int y, uint8_t unit, uint8_t core,
               uint8_t glow, int cut_y, int cut_dx)
{
    uint8_t pass;
    for (pass = 0; pass < 3; pass++) {
        const char *p = word;
        int cx = x;
        while (*p) {
            uint8_t n, gw, i;
            const stroke_t *st = glyph(*p++, &n, &gw);
            for (i = 0; i < n; i++) {
                int sx = cx + st[i].x * unit, sy = y + st[i].y * unit;
                int sw = st[i].w * unit, sh = st[i].h * unit;
                if (st[i].h == 0) {
                    /* diagonal of the N: a parallelogram from the top-left
                       to the bottom-right corner */
                    int xb = cx + unit * 3 / 2, xc = cx + 5 * unit;
                    int xd = cx + unit * 7 / 2, yb = y + 7 * unit - 1;
                    if (pass == 0) {
                        gfx_SetColor(glow);
                        fill_trap_h(y - 2, yb + 2, cx - 2, xb + 2, xd - 2, xc + 2);
                    } else if (pass == 1) {
                        gfx_SetColor(core);
                        fill_trap_h(y, yb, cx, xb - 1, xd, xc - 1);
                    } else {
                        gfx_SetColor(P_HOT);
                        gfx_Line(cx + unit * 3 / 4, y + unit / 2, cx + unit * 17 / 4, yb - unit / 2);
                    }
                    continue;
                }
                if (pass == 0) {
                    gfx_SetColor(glow);
                    cut_rect(sx - 2, sy - 2, sw + 4, sh + 4, cut_y, cut_dx);
                } else if (pass == 1) {
                    gfx_SetColor(core);
                    cut_rect(sx, sy, sw, sh, cut_y, cut_dx);
                } else {
                    gfx_SetColor(P_HOT);
                    if (sw > sh)
                        cut_rect(sx + unit / 2, sy + unit / 2, sw - unit, 1, cut_y, cut_dx);
                    else
                        cut_rect(sx + unit / 2, sy + unit / 2, 1, sh - unit, cut_y, cut_dx);
                }
            }
            cx += (gw + 2) * unit;
        }
    }
}
