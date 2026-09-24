/*
 * NEON SLICE - a neon rhythm game for the TI-84 Plus CE
 *
 * Main loop: fixed 30 fps pacing on the 32768 Hz hardware clock, keypad
 * polling (lane presses are time-stamped even while waiting for the next
 * frame, so judging is finer than the frame rate) and screen transitions.
 */
#include <graphx.h>
#include <keypadc.h>
#include <string.h>
#include <time.h>
#include "neon.h"

uint8_t key_edge[8];
uint8_t key_held[8];
uint8_t lanes_held;
uint32_t pq_time[PQ_SIZE];
uint8_t pq_lanes[PQ_SIZE];
uint8_t pq_len;
uint8_t sel_level;
uint24_t frame_count;

static uint8_t state, next_state;
static int8_t fade_dir;             /* -1 fading out, 1 fading in, 0 idle */

#define FADE_STEP 51

void input_poll(void)
{
    uint8_t g, v, lanes, fresh;

    kb_Scan();
    for (g = 1; g < 8; g++) {
        v = kb_Data[g];
        key_edge[g] |= (uint8_t)(v & ~key_held[g]);
        key_held[g] = v;
    }
    v = key_held[7];
    lanes = (uint8_t)(((v & kb_Left) ? 1 : 0) | ((v & kb_Down) ? 2 : 0) |
                      ((v & kb_Up) ? 4 : 0) | ((v & kb_Right) ? 8 : 0));
    fresh = (uint8_t)(lanes & ~lanes_held);
    lanes_held = lanes;
    if (fresh && pq_len < PQ_SIZE) {
        pq_time[pq_len] = clock();
        pq_lanes[pq_len++] = fresh;
    }
}

bool key_pressed(uint8_t group, uint8_t mask)
{
    return (key_edge[group] & mask) != 0;
}

bool confirm_pressed(void)
{
    return key_pressed(1, kb_2nd) || key_pressed(6, kb_Enter);
}

void state_goto(uint8_t st)
{
    if (fade_dir < 0) return;
    next_state = st;
    fade_dir = -1;
}

static void state_enter(uint8_t st)
{
    state = st;
    switch (st) {
        case ST_TITLE:   title_enter();   break;
        case ST_SELECT:  select_enter();  break;
        case ST_PLAY:    game_enter();    break;
        case ST_RESULTS: results_enter(); break;
    }
}

static void state_update(void)
{
    switch (state) {
        case ST_TITLE:   title_update();   break;
        case ST_SELECT:  select_update();  break;
        case ST_PLAY:    game_update();    break;
        case ST_RESULTS: results_update(); break;
    }
}

static void state_draw(void)
{
    switch (state) {
        case ST_TITLE:   title_draw();   break;
        case ST_SELECT:  select_draw();  break;
        case ST_PLAY:    game_draw();    break;
        case ST_RESULTS: results_draw(); break;
    }
}

#ifdef NS_HOST
uint8_t host_state(void) { return state; }
#endif

int main(void)
{
    uint32_t next;

    gfx_Begin();
    gfx_SetDrawBuffer();
    gfx_SetTransparentColor(C_TRANSP);
    gfx_SetTextBGColor(C_TRANSP);
    gfx_SetTextTransparentColor(C_TRANSP);

    save_load();
    render_init();
    pal_fade = 0;
    pal_commit();
    state_enter(ST_TITLE);
    fade_dir = 1;

    /* keys still held from launching the program must not count as presses */
    input_poll();
    memset(key_edge, 0, sizeof key_edge);
    pq_len = 0;

    next = clock();
    for (;;) {
        input_poll();
        if (key_pressed(6, kb_Clear)) break;        /* CLEAR quits from anywhere */
        if (fade_dir >= 0) state_update();
        memset(key_edge, 0, sizeof key_edge);
        if (state != ST_PLAY) pq_len = 0;

        if (fade_dir < 0) {
            if (pal_fade > FADE_STEP) {
                pal_fade -= FADE_STEP;
            } else {
                pal_fade = 0;
                state_enter(next_state);
                fade_dir = 1;
            }
        } else if (fade_dir > 0) {
            if (pal_fade < 255 - FADE_STEP) {
                pal_fade += FADE_STEP;
            } else {
                pal_fade = 255;
                fade_dir = 0;
            }
        }

        state_draw();
        pal_commit();
        gfx_SwapDraw();
        frame_count++;

        /* hold a steady 30 fps; keep sampling the keypad meanwhile */
        next += FRAME_CLK;
        while ((int32_t)(clock() - next) < 0)
            input_poll();
        if ((int32_t)(clock() - next) > 3 * FRAME_CLK)
            next = clock();
    }

    gfx_End();
    save_write(true);
    return 0;
}
