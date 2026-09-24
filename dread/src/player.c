/*
 * Player movement and interaction: one call per 35 Hz game tic.
 *
 * Collision uses an axis-aligned box of +-PLAYER_RADIUS around the player.
 * X and Y are resolved separately so the player slides along walls, and a
 * blocked axis snaps flush against the wall instead of stopping short.
 */
#include "dread.h"
#include "hud.h"

#define PLAYER_RADIUS 72     /* Q8: a bit over a quarter tile */
#define MOVE_SPEED    28     /* Q8 per tic: ~3.8 tiles/s */
#define BACK_SPEED    22
#define STRAFE_SPEED  24
#define TURN_SLOW     6      /* angle units per tic for the first few tics */
#define TURN_FAST     15     /* ~185 degrees/s once the key is held */
#define TURN_RAMP     5      /* tics before switching to the fast rate */
#define USE_REACH     (TILE_UNITS * 3 / 4)

player_t player;
const unsigned max_ammo[NUM_AMMO] = { 200, 50, 50 };

void player_reset(void)
{
    uint8_t i;

    player.health = 100;
    player.armor = 0;
    player.armor_class = 0;
    for (i = 0; i < NUM_AMMO; i++)
        player.ammo[i] = 0;
    player.ammo[AMMO_BULLETS] = 50;
    player.weapons = (1 << WP_FISTS) | (1 << WP_PISTOL);
    player.weapon = WP_PISTOL;
    player.keys = 0;
}

static bool cell_blocks(int cx, int cy)
{
    uint8_t t = MAP_AT(cx, cy);

    if (t == TILE_EMPTY)
        return false;
    if (IS_DOOR(t))
        return door_blocks(DOOR_INDEX(t));
    return true;
}

static bool solid_at(int x, int y)
{
    unsigned x0 = CELL(x - PLAYER_RADIUS), x1 = CELL(x + PLAYER_RADIUS);
    unsigned y0 = CELL(y - PLAYER_RADIUS), y1 = CELL(y + PLAYER_RADIUS);

    /* The box is under one tile wide, so its corners cover every cell. */
    return cell_blocks(x0, y0) || cell_blocks(x1, y0) || cell_blocks(x0, y1) ||
           cell_blocks(x1, y1) || thing_blocks_at(x, y, PLAYER_RADIUS, NULL);
}

bool player_overlaps_cell(uint8_t cx, uint8_t cy)
{
    unsigned x0 = CELL(player.x - PLAYER_RADIUS), x1 = CELL(player.x + PLAYER_RADIUS);
    unsigned y0 = CELL(player.y - PLAYER_RADIUS), y1 = CELL(player.y + PLAYER_RADIUS);

    return cx >= x0 && cx <= x1 && cy >= y0 && cy <= y1;
}

static void try_move(int dx, int dy)
{
    int nx, ny;

    if (dx) {
        nx = player.x + dx;
        if (solid_at(nx, player.y)) {
            /* Snap flush to the blocking cell's edge. */
            if (dx > 0)
                nx = (((nx + PLAYER_RADIUS) >> FRAC_BITS) << FRAC_BITS) - PLAYER_RADIUS - 1;
            else
                nx = ((((nx - PLAYER_RADIUS) >> FRAC_BITS) + 1) << FRAC_BITS) + PLAYER_RADIUS;
            if (solid_at(nx, player.y))
                nx = player.x;
        }
        player.x = nx;
    }
    if (dy) {
        ny = player.y + dy;
        if (solid_at(player.x, ny)) {
            if (dy > 0)
                ny = (((ny + PLAYER_RADIUS) >> FRAC_BITS) << FRAC_BITS) - PLAYER_RADIUS - 1;
            else
                ny = ((((ny - PLAYER_RADIUS) >> FRAC_BITS) + 1) << FRAC_BITS) + PLAYER_RADIUS;
            if (solid_at(player.x, ny))
                ny = player.y;
        }
        player.y = ny;
    }
}

/* Use whatever is directly ahead: doors, secret doors, the exit switch. */
static void use_ahead(void)
{
    int c = fcos(player.angle), s = fsin(player.angle);
    uint8_t step;

    /* Probe a quarter tile at a time out to USE_REACH past the player's edge. */
    for (step = 1; step <= 4; step++) {
        int reach = PLAYER_RADIUS / 2 + (USE_REACH * step) / 4;
        int x = player.x + ((c * reach) >> 14);
        int y = player.y + ((s * reach) >> 14);
        uint8_t t = MAP_AT(x >> FRAC_BITS, y >> FRAC_BITS);

        if (t == TILE_EMPTY)
            continue;
        if (IS_DOOR(t))
            door_use(DOOR_INDEX(t));
        else if (t == TILE_EXIT_OFF) {
            MAP_AT(x >> FRAC_BITS, y >> FRAC_BITS) = TILE_EXIT_ON;
            level.exiting = true;
        }
        return;
    }
}

void player_tic(const ticcmd_t *cmd)
{
    int dx = 0, dy = 0;
    int c, s;

    if (player_dead)
        return;
    if (cmd->turn) {
        int rate = player.turn_held < TURN_RAMP ? TURN_SLOW : TURN_FAST;
        if (player.turn_held < 255)
            player.turn_held++;
        player.angle = (player.angle + cmd->turn * rate) & ANG_MASK;
    } else {
        player.turn_held = 0;
    }

    c = fcos(player.angle);
    s = fsin(player.angle);
    /* speed (<= 28) * Q14 trig (<= 16384) stays under 2^19. */
    if (cmd->forward > 0) {
        dx += (c * MOVE_SPEED) >> 14;
        dy += (s * MOVE_SPEED) >> 14;
    } else if (cmd->forward < 0) {
        dx -= (c * BACK_SPEED) >> 14;
        dy -= (s * BACK_SPEED) >> 14;
    }
    if (cmd->strafe) {
        /* Right of the view direction is (-sin, cos). */
        dx -= (s * STRAFE_SPEED * cmd->strafe) >> 14;
        dy += (c * STRAFE_SPEED * cmd->strafe) >> 14;
    }
    try_move(dx, dy);

    if (cmd->use)
        use_ahead();
}
