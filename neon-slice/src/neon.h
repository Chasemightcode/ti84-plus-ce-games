/*
 * NEON SLICE - a neon rhythm game for the TI-84 Plus CE
 * Shared definitions for all modules.
 *
 * Note: on the eZ80 an `int` is 24 bits (range +-8,388,607), so any math
 * that can grow past that uses the 32-bit int32_t/uint32_t types.
 */
#ifndef NEON_H
#define NEON_H

#include <stdint.h>
#include <stdbool.h>

/* ---- screen & pseudo-3D projection ------------------------------------ */
#define SCR_W       320
#define SCR_H       240
#define VP_X        160                 /* vanishing point x */
#define HORIZON     66                  /* vanishing point / horizon y */
#define HIT_Y       202                 /* floor y of the hit line */
#define CAM_H       (HIT_Y - HORIZON)   /* camera height in world units */
#define LANE_W      56                  /* lane width at the hit line */
#define ROAD_HALF   (2 * LANE_W)
#define BLK_HALF    20                  /* half size of a cube */
#define BLK_DEPTH   40
#define BLK_BOT     6                   /* cubes hover above the floor */
#define BLK_TOP     (BLK_BOT + 2 * BLK_HALF)
#define S_SHIFT     12                  /* perspective scale is x.12 fixed */
#define S_ONE       (1 << S_SHIFT)
#define W_MIN       (-128)              /* depth index: w = 256*time_left/approach */
#define W_MAX       767
#define W_SPAWN     256                 /* blocks appear at w = 256 */

/* ---- timing ------------------------------------------------------------ */
#define TPB         48                  /* chart ticks per beat */
#define TPBAR       (4 * TPB)           /* ticks per 4/4 bar */
#define SUB_SHIFT   4                   /* song time is kept in 1/16 ticks */
#define SUB_BEAT    (TPB << SUB_SHIFT)  /* 768 subticks per beat */
#define FRAME_CLK   1092                /* 32768 Hz clock / 30 fps */

/* ---- palette layout ------------------------------------------------------
 * 0-15   fixed UI colors (never dimmed by the scene dimmer)
 * 16-63  background scene, animated every frame for the beat pulse
 * 64-119 block & bomb shades (4 fog levels each)
 * 120+   8-step fade ramps for particles and pop-up text
 */
enum {
    C_BLACK, C_WHITE, C_PINK, C_CYAN, C_PURPLE, C_LIME, C_YELLOW, C_ORANGE,
    C_RED, C_GRAY, C_DGRAY, C_PANEL, C_PANEL2, C_DPINK, C_DCYAN, C_GOLD,

    P_SKY = 16,                         /* 16 gradient entries */
    P_FLOOR = 32, P_ROAD, P_GRID, P_GRID_DIM, P_RGRID, P_RGRID_HI,
    P_EDGE, P_EDGE_GL, P_LANE, P_HIT, P_HIT_GL, P_TARGET, P_SHADOW,
    P_MOUNT, P_MOUNT_E, P_STAR0, P_STAR1, P_STAR2, P_HALO,
    P_SUN = 51,                         /* 8 gradient entries */
    P_LANEFX = 59,                      /* 4 entries, one per lane */
    P_LINK = 63,

    P_BLK_A = 64,                       /* 4 fog levels x 5 shades */
    P_BLK_B = 84,
    P_BOMB = 104,                       /* 4 fog levels x 4 shades */

    R_A = 120, R_B = 128, R_FIRE = 136, R_WHITE = 144,
    R_PERFECT = 152, R_GREAT = 160, R_GOOD = 168, R_MISS = 176,
    R_GOLD = 184, R_PINK = 192, R_CYAN = 200,

    P_LOGO_A = 208, P_LOGO_GA, P_LOGO_B, P_LOGO_GB, P_HOT,
    P_RAINBOW = 216,                    /* 8 cycling hues */

    C_TRANSP = 255                      /* never drawn (text background) */
};

/* shade offsets inside a block color set */
enum { SH_TOP, SH_FRONT, SH_SIDE, SH_EDGE, SH_ARROW, SH_COUNT };
/* shade offsets inside a bomb color set */
enum { BS_BODY, BS_SHINE, BS_SPIKE, BS_CORE, BS_COUNT };

/* ---- lanes / keys ------------------------------------------------------ */
enum { LANE_LEFT, LANE_DOWN, LANE_UP, LANE_RIGHT };

/* ---- chart notes ------------------------------------------------------- */
enum { NT_BLOCK, NT_BOMB };
enum { NS_PENDING, NS_HIT, NS_MISSED, NS_BOMBED, NS_PASSED };

typedef struct {
    uint16_t tick;          /* time in chart ticks (1/48 beat) */
    uint8_t lane;
    uint8_t type;           /* NT_BLOCK / NT_BOMB */
    uint8_t state;          /* NS_* */
    int8_t link;            /* relative index of the partner of a double */
} note_t;

#define MAX_NOTES   720
#define NUM_LEVELS  5

typedef struct {
    const char *name;
    const char *chart;
    uint8_t bpm;
    uint8_t stars;          /* difficulty 1..5 */
    uint16_t approach_ms;   /* time a block takes to fly in */
    uint8_t win_perfect;    /* timing windows in ms (+/-) */
    uint8_t win_great;
    uint8_t win_good;
    uint8_t miss_dmg;       /* health lost on a miss */
    uint8_t bomb_dmg;       /* health lost when slicing a bomb */
    uint8_t theme;
    uint8_t ui_color;       /* fixed UI color for menus */
} level_t;

typedef struct {
    uint16_t notes;         /* total objects */
    uint16_t blocks;        /* sliceable blocks */
    uint16_t bombs;
    uint16_t doubles;
    uint16_t end_tick;      /* tick of the last object */
} chart_info_t;

extern note_t notes[MAX_NOTES];
extern const level_t levels[NUM_LEVELS];
uint16_t chart_load(uint8_t level, chart_info_t *info);

/* ---- save data --------------------------------------------------------- */
enum { GRADE_NONE, GRADE_D, GRADE_C, GRADE_B, GRADE_A, GRADE_S };
#define REC_PLAYED      1
#define REC_CLEARED     2
#define REC_FULLCOMBO   4

typedef struct {
    uint32_t best;          /* best score */
    uint16_t acc;           /* best accuracy in 0.1% */
    uint16_t combo;         /* best max combo */
    uint8_t grade;          /* best grade (cleared runs only) */
    uint8_t flags;
} record_t;

extern record_t records[NUM_LEVELS];
void save_load(void);
void save_write(bool archive);

/* ---- input (main.c) ---------------------------------------------------- */
extern uint8_t key_edge[8];         /* keys newly pressed this frame, by group */
extern uint8_t key_held[8];
extern uint8_t lanes_held;          /* bit per lane currently held */
#define PQ_SIZE 16
extern uint32_t pq_time[PQ_SIZE];   /* timestamped lane presses */
extern uint8_t pq_lanes[PQ_SIZE];
extern uint8_t pq_len;
void input_poll(void);
bool key_pressed(uint8_t group, uint8_t mask);
bool confirm_pressed(void);         /* 2nd or enter */

/* ---- state machine (main.c) -------------------------------------------- */
enum { ST_TITLE, ST_SELECT, ST_PLAY, ST_RESULTS };
void state_goto(uint8_t st);
extern uint8_t sel_level;
extern uint24_t frame_count;

/* ---- rendering (render.c) ---------------------------------------------- */
extern int view_x, view_y;          /* vanishing point incl. screen shake */
extern uint8_t pal_fade;            /* global brightness (fades) */
extern uint8_t pal_dim;             /* scene dimmer for 16..pal_dim_end */
extern uint8_t pal_dim_end;
extern uint8_t lane_fx[4];          /* lane glow intensities */
extern uint8_t red_flash;           /* red sky flash intensity */
extern int block_drop;              /* extra y offset for falling blocks */

void render_init(void);
void theme_apply(uint8_t theme);
void logo_glow(uint8_t pink, uint8_t cyan);
void pal_animate(uint8_t pulse);
void pal_commit(void);
unsigned depth_scale(int w);
int beat_pulse(int song, int *beat_out);
void scene_draw(int song, unsigned approach, bool road);
void draw_block(uint8_t lane, int w, uint8_t fog, bool dead);
void draw_bomb(uint8_t lane, int w, uint8_t fog);
void draw_link(uint8_t lane_a, uint8_t lane_b, int w);
void draw_arrow(uint8_t dir, int cx, int cy, int size, uint8_t color);
void fill_trap_h(int y0, int y1, int xl0, int xr0, int xl1, int xr1);
void fill_trap_v(int x0, int x1, int yt0, int yb0, int yt1, int yb1);
void ui_cube(int cx, int cy, int size, uint8_t set, uint8_t dir);
void block_rect(uint8_t lane, int w, int *x, int *y, int *wd, int *ht);
void stars_step(uint8_t speed);
uint8_t fog_for(int w);

/* text helpers: return false if the text would not fit on screen;
   pass NOSH as the shadow color for no drop shadow */
#define NOSH C_TRANSP
unsigned text_width(const char *s, uint8_t scale);
bool text_at(const char *s, int x, int y, uint8_t scale, uint8_t color, uint8_t shadow);
void text_center(const char *s, int y, uint8_t scale, uint8_t color, uint8_t shadow);
void text_right(const char *s, int xr, int y, uint8_t scale, uint8_t color, uint8_t shadow);
char *fmt_uint(char *buf, uint32_t v, uint8_t min_digits);
void panel(int x, int y, int w, int h, uint8_t border);
void neon_word(const char *word, int x, int y, uint8_t unit, uint8_t core,
               uint8_t glow, int cut_y, int cut_dx);
unsigned neon_word_width(const char *word, uint8_t unit);

/* ---- effects (fx.c) ---------------------------------------------------- */
enum { J_PERFECT, J_GREAT, J_GOOD, J_MISS, J_BOMB };
void fx_reset(void);
void fx_update(void);
void fx_draw_debris(void);
void fx_draw_particles(void);
void fx_shake(uint8_t amount);
void fx_apply_shake(void);
void fx_burst(int x, int y, uint8_t count, uint8_t ramp, uint8_t speed);
void fx_slice(uint8_t lane, int w, uint8_t set);
void fx_explode(uint8_t lane, int w);
void fx_judge(uint8_t judgement);
void fx_combo(uint16_t combo);
void fx_draw_popups(void);
uint16_t rnd(void);

/* ---- gameplay (game.c) ------------------------------------------------- */
void game_enter(void);
void game_update(void);
void game_draw(void);

typedef struct {
    uint32_t score;
    uint16_t max_combo;
    uint16_t counts[4];     /* perfect, great, good, miss */
    uint16_t bombs_hit;
    uint16_t acc;           /* accuracy in 0.1% */
    uint8_t grade;
    bool cleared;
    bool full_combo;
    bool new_best;
    uint8_t level;
    bool has_bombs;
} result_t;
extern result_t last_result;

/* ---- menus (menus.c) --------------------------------------------------- */
void title_enter(void);
void title_update(void);
void title_draw(void);
void select_enter(void);
void select_update(void);
void select_draw(void);
void results_enter(void);
void results_update(void);
void results_draw(void);

#endif
