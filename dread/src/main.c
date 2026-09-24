/*
 * DREAD - a raycasting shooter for the TI-84 Plus CE.
 *
 * Title menu -> difficulty -> the five-level campaign (a tally and a save
 * after each level) -> victory. A level runs the main loop: read the
 * keypad, run as many fixed 35 Hz game tics as real time demands, render
 * one frame (3D view or automap) into the back buffer, swap.
 */
#include <graphx.h>
#include <keypadc.h>
#include <ti/getcsc.h>
#include <time.h>

#include "dread.h"
#include "hud.h"
#include "render.h"

#define EXIT_DELAY    18         /* tics between the switch and the tally */
#define BOSS_DELAY    70         /* tics between the boss's death and the end */
#define RESPAWN_DELAY TIC_RATE   /* tics after death before 2nd restarts */

enum { END_EXIT, END_TITLE, END_QUIT };

uint8_t game_skill = SKILL_NORMAL;
uint8_t game_mode;

static void fatal(const char *msg)
{
    /* graphx's default palette is still loaded here: 0 black, 255 white. */
    gfx_SetDrawScreen();
    gfx_FillScreen(0);
    gfx_SetTextBGColor(0);
    gfx_SetTextFGColor(255);
    gfx_SetTextTransparentColor(0);
    gfx_PrintStringXY("DREAD cannot start:", 8, 8);
    gfx_PrintStringXY(msg, 8, 24);
    gfx_PrintStringXY("Send every DREAD*.8xv file from the", 8, 48);
    gfx_PrintStringXY("release folder and keep 30 KB RAM free.", 8, 60);
    gfx_PrintStringXY("Press any key.", 8, 84);
    while (!os_GetCSC())
        ;
    gfx_End();
}

/* Keys that act once per press rather than while held. */
typedef struct {
    bool alpha, mode, second, enter, yequ;
    uint8_t weapon;             /* bit per number key 1..5 */
} edges_t;

typedef struct {
    bool quit, fps, pause, map;
} events_t;

static edges_t held;
static bool pending_use;        /* a use press waiting for the next tic */
static bool fire_held;          /* 2nd: fire while held */
static bool fire_pressed;       /* 2nd went down this frame */
static player_t level_start;    /* loadout to restart the level with */
static unsigned fps10;
static unsigned run_tics;       /* the whole campaign, for the victory page */

static void read_input(ticcmd_t *cmd, events_t *ev)
{
    kb_key_t arrows, g1, g2, g6;
    uint8_t numbers, i;
    int8_t lr;

    kb_Scan();
    arrows = kb_Data[7];
    g1 = kb_Data[1];
    g2 = kb_Data[2];
    g6 = kb_Data[6];
    ev->quit = (g6 & kb_Clear) != 0;

    fire_held = (g1 & kb_2nd) != 0;
    fire_pressed = fire_held && !held.second;
    held.second = fire_held;

    /* number keys 1..5 pick a weapon */
    numbers = ((kb_Data[3] & kb_1) ? 0x01 : 0) | ((kb_Data[4] & kb_2) ? 0x02 : 0) |
              ((kb_Data[5] & kb_3) ? 0x04 : 0) | ((kb_Data[3] & kb_4) ? 0x08 : 0) |
              ((kb_Data[4] & kb_5) ? 0x10 : 0);
    for (i = 0; i < NUM_WEAPONS; i++) {
        if ((numbers & ~held.weapon) & (1 << i))
            weapon_select(i);
    }
    held.weapon = numbers;

    cmd->forward = (arrows & kb_Up) ? 1 : (arrows & kb_Down) ? -1 : 0;
    lr = (int8_t)(((arrows & kb_Right) ? 1 : 0) - ((arrows & kb_Left) ? 1 : 0));
    if (g2 & kb_Alpha) {
        cmd->strafe = lr;
        cmd->turn = 0;
    } else {
        cmd->strafe = 0;
        cmd->turn = lr;
    }
    if ((g2 & kb_Alpha) && !held.alpha)
        pending_use = true;
    held.alpha = (g2 & kb_Alpha) != 0;
    ev->fps = (g1 & kb_Mode) && !held.mode;
    held.mode = (g1 & kb_Mode) != 0;
    ev->pause = (g6 & kb_Enter) && !held.enter;
    held.enter = (g6 & kb_Enter) != 0;
    ev->map = (g1 & kb_Yequ) && !held.yequ;
    held.yequ = (g1 & kb_Yequ) != 0;
}

/* Loads level num; with a loadout, the player gets that health, armor,
 * ammo and weapons (a restart), otherwise keeps what they carry. */
static bool begin_level(uint8_t num, const player_t *loadout)
{
    uint8_t i;

    if (!level_load(num))
        return false;
    if (loadout) {
        player.health = loadout->health;
        player.armor = loadout->armor;
        player.armor_class = loadout->armor_class;
        player.weapons = loadout->weapons;
        player.weapon = loadout->weapon;
        for (i = 0; i < NUM_AMMO; i++)
            player.ammo[i] = loadout->ammo[i];
    }
    player.keys = 0;
    combat_reset();
    automap_reset();
    fx_reset();
    hud_dirty(HUD_ALL);
    return true;
}

/* Runs the loaded level until it is finished or left. */
static uint8_t play_level(void)
{
    ticcmd_t cmd;
    events_t ev;
    bool automap_on = false;
    uint32_t last, now, acc = 0, fps_start;
    unsigned frames = 0;
    uint8_t exit_timer = 0, dead_tics = 0;

    game_mode = 1;
    held.second = held.enter = held.yequ = true;   /* ignore keys from the menus */
    last = fps_start = clock();

    for (;;) {
        uint8_t tics = 0;

        read_input(&cmd, &ev);
        if (ev.quit)
            return END_QUIT;
        if (ev.fps)
            hud_show_fps = !hud_show_fps;
        if (ev.map)
            automap_on = !automap_on;
        if (ev.pause) {
            uint8_t r;
            fx_reset();
            r = pause_menu();
            if (r == PAUSE_QUIT)
                return END_QUIT;
            if (r == PAUSE_TITLE)
                return END_TITLE;
            if (r == PAUSE_RESTART && !begin_level(level.number, &level_start))
                return END_TITLE;
            if (r == PAUSE_RESTART)
                exit_timer = dead_tics = 0;
            hud_dirty(HUD_ALL);
            held.second = held.enter = true;
            last = clock();
            acc = 0;
            continue;
        }

        now = clock();
        acc += now - last;
        last = now;
        while (acc >= TIC_TICKS && tics < MAX_TICS_PER_FRAME) {
            acc -= TIC_TICKS;
            tics++;
        }
        if (tics == MAX_TICS_PER_FRAME)
            acc = 0;   /* running slow: drop time instead of spiraling */
        while (tics--) {
            cmd.use = pending_use;         /* one use per key press */
            pending_use = false;
            player_tic(&cmd);
            combat_tic(fire_held);
            doors_tic();
            things_tic();
            hud_tic();
            fx_tic();
            automap_visit();
            level.tics++;
            if (player_dead && dead_tics < 255)
                dead_tics++;
            if (level.exiting && ++exit_timer >= (level.boss ? BOSS_DELAY : EXIT_DELAY))
                break;
        }

        if (player_dead && dead_tics >= RESPAWN_DELAY && fire_pressed) {
            /* start the level over with what the player had coming in */
            dead_tics = exit_timer = 0;
            if (!begin_level(level.number, &level_start))
                return END_TITLE;
            last = clock();
            acc = 0;
            continue;
        }
        if (level.exiting && exit_timer >= (level.boss ? BOSS_DELAY : EXIT_DELAY))
            return END_EXIT;

        if (automap_on)
            automap_draw((uint8_t *)gfx_vbuffer);
        else
            render_view();
        hud_overlay(fps10);
        hud_draw();
        fx_apply();
        gfx_SwapDraw();

        frames++;
        if (now - fps_start >= TIMER_HZ) {
            fps10 = (unsigned)((uint32_t)frames * (TIMER_HZ * 10UL) / (now - fps_start));
            frames = 0;
            fps_start = now;
        }
    }
}

/* Plays from level n to the end of the episode (or until the player leaves).
 * Sets *dirty when the save changed. */
static uint8_t campaign(uint8_t n, bool *dirty)
{
    run_tics = 0;
    for (;;) {
        uint8_t r;
        unsigned secs;
        uint16_t *best;
        bool record;

        if (!begin_level(n, NULL)) {
            game_mode = 0;
            message_screen("A level AppVar is missing.", "Send DREADL1 to DREADL5.");
            return END_TITLE;
        }
        level_start = player;
        r = play_level();
        game_mode = 0;
        fx_reset();
        if (r != END_EXIT)
            return r;

        run_tics += level.tics;
        secs = level.tics / TIC_RATE;
        if (!secs)
            secs = 1;
        best = &save.best[game_skill][n - 1];
        record = !*best || secs < *best;
        if (record)
            *best = (uint16_t)secs;
        *dirty = true;
        if (n == NUM_LEVELS) {
            save.level = 0;
            save.won |= 1 << game_skill;
            save_write(false);
            return victory_screen(run_tics / TIC_RATE) ? END_TITLE : END_QUIT;
        }
        save_loadout(&player, n + 1);
        save_write(false);
        if (!intermission(*best, record))
            return END_QUIT;
        n++;
    }
}

int main(void)
{
    const char *err;
    bool dirty = false;

    gfx_Begin();

    err = render_init();
    if (!err)
        err = arena_init();
    if (!err)
        err = assets_load();
    if (!err)
        err = hud_load();
    if (!err)
        err = combat_load();
    if (err) {
        arena_free();
        fatal(err);
        return 1;
    }

    fx_init();
    gfx_SetDrawBuffer();
    save_load();

    for (;;) {
        uint8_t r, first = 1;

        game_mode = 0;
        fx_reset();
        r = title_menu();
        if (r == TITLE_QUIT)
            break;
        if (r == TITLE_NEW) {
            uint8_t s = skill_menu();
            if (s == MENU_QUIT)
                break;
            game_skill = s;
            player_reset();
        } else {
            player_reset();
            load_loadout(&player);
            first = save.level;
        }
        if (campaign(first, &dirty) == END_QUIT)
            break;
    }

    /* archiving can make the OS ask to garbage-collect: leave graphx first */
    gfx_End();
    if (dirty)
        save_write(true);
    arena_free();
    return 0;
}
