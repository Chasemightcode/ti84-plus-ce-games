/*
 * DREAD - shared engine definitions.
 *
 * Integer sizes matter here: on the eZ80, int is 24 bits (range +-8.3M),
 * short is 16 and long is 32 (slow library math). All hot-path arithmetic
 * is kept inside 24 bits; comments call out the worst-case magnitudes.
 */
#ifndef DREAD_H
#define DREAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gen/assets.h"
#include "gen/things.h"

/* ---- screen layout ---------------------------------------------------- */
#define SCREEN_W   320
#define SCREEN_H   240
#define VIEW_W     160            /* logical columns, drawn 2 px wide */
#define VIEW_H     100            /* logical rows, drawn 2 px tall */
#define HORIZON    (VIEW_H / 2)
#define FOCAL      120            /* projection distance, logical px */
#define HUD_Y      (VIEW_H * 2)   /* status bar starts at this screen row */
#define HUD_H      (SCREEN_H - HUD_Y)

/* ---- fixed point ------------------------------------------------------ */
/* World positions are Q8: 256 units per map tile. */
#define FRAC_BITS  8
#define TILE_UNITS (1 << FRAC_BITS)
/* Map cell of a world coordinate. Positions are never negative, and an
 * unsigned shift is cheaper than a signed one (both are library calls at
 * -Oz; a cast to uint8_t doesn't help once the result feeds more math). */
#define CELL(v)    ((unsigned)(v) >> FRAC_BITS)

/* 1024 angle units per turn; 0 = east, increasing turns right (map y is
 * down). sin/cos are Q14 (16384 = 1.0). */
#define ANG_COUNT  1024
#define ANG_MASK   (ANG_COUNT - 1)
#define ANG_90     (ANG_COUNT / 4)
extern const int16_t sin_table[ANG_COUNT + ANG_COUNT / 4];
#define fsin(a) ((int)sin_table[(a) & ANG_MASK])
#define fcos(a) ((int)sin_table[((a) & ANG_MASK) + ANG_90])

/* ---- map -------------------------------------------------------------- */
/* Levels are copied into a fixed 64x64 grid so a cell index is a shift. */
#define MAP_SHIFT  6
#define MAP_SIZE   (1 << MAP_SHIFT)
extern uint8_t level_map[MAP_SIZE * MAP_SIZE];
#define MAP_AT(tx, ty) level_map[((unsigned)(ty) << MAP_SHIFT) + (unsigned)(tx)]

/* Tile codes in level files (see tools/levels.py). */
#define TILE_EMPTY       0x00
#define TILE_SECRET      0x40   /* secret door: texture (tile & 0x3F) - 1 */
#define TILE_DOOR        0x80
#define TILE_DOOR_RED    0x81
#define TILE_DOOR_BLUE   0x82
#define TILE_DOOR_YELLOW 0x83
/* In memory every door is renumbered to TILE_DOOR | index into doors[]. */
#define IS_DOOR(t)       ((t) & 0x80)
#define DOOR_INDEX(t)    ((t) & 0x3F)
#define TILE_EXIT_OFF    (TEX_EXIT_OFF + 1)
#define TILE_EXIT_ON     (TEX_EXIT_ON + 1)

/* ---- doors ------------------------------------------------------------ */
/* Layout is read by raycast.s: keep 8 bytes, open first, flags second. */
#define MAX_DOORS        64
#define DOOR_VERTICAL    0x01   /* plane at x = cell + 1/2 (passage runs E-W) */
#define DOOR_FLUSH       0x02   /* secret door: plane on the entry face */
enum { DS_CLOSED, DS_OPENING, DS_OPEN, DS_CLOSING };

typedef struct {
    uint8_t open;      /* 0 closed .. 255 fully open */
    uint8_t flags;
    uint8_t state;
    uint8_t timer;     /* tics until an open door tries to close */
    uint8_t x, y;
    uint8_t lock;      /* 0 none, 1 red, 2 blue, 3 yellow keycard */
    uint8_t tex;       /* texture id */
} door_t;
_Static_assert(sizeof(door_t) == 8, "raycast.s indexes doors[] in 8-byte steps");

extern door_t doors[MAX_DOORS];
extern uint8_t num_doors;

void doors_init(void);                  /* after the map is loaded */
void doors_tic(void);
bool door_use(uint8_t index);           /* true if the door reacted */
void door_open_quiet(uint8_t index);    /* monsters: plain doors only */
bool door_blocks(uint8_t index);

/* ---- timing ----------------------------------------------------------- */
#define TIMER_HZ   32768           /* clock() rate (CLOCKS_PER_SEC) */
#define TIC_RATE   35
#define TIC_TICKS  (TIMER_HZ / TIC_RATE)   /* 936 timer ticks per game tic */
#define MAX_TICS_PER_FRAME 4

/* ---- player ----------------------------------------------------------- */
#define KEY_RED    0x01
#define KEY_BLUE   0x02
#define KEY_YELLOW 0x04

typedef struct {
    int x, y;          /* Q8 world position (raycast.s reads x, y, angle) */
    int angle;         /* 0..ANG_MASK */
    uint8_t turn_held; /* tics the turn key has been held (turn acceleration) */
    int health;        /* 0..200 */
    int armor;         /* 0..200 */
    uint8_t armor_class;               /* 0 none, 1 vest, 2 plate */
    unsigned ammo[NUM_AMMO];
    uint8_t weapons;   /* bit per WP_* owned */
    uint8_t weapon;    /* selected WP_* */
    uint8_t keys;      /* KEY_* bits */
} player_t;
_Static_assert(offsetof(player_t, y) == 3 && offsetof(player_t, angle) == 6,
               "raycast.s reads player x, y and angle");

extern player_t player;
extern const unsigned max_ammo[NUM_AMMO];

/* One tic of player intent, built from the keypad each frame. */
typedef struct {
    int8_t forward;    /* -1 back, 0, +1 forward */
    int8_t strafe;     /* -1 left, 0, +1 right */
    int8_t turn;       /* -1 left, 0, +1 right */
    bool use;          /* use key went down this frame */
} ticcmd_t;

void player_reset(void);                /* new game: health, ammo, weapons */
void player_tic(const ticcmd_t *cmd);
bool player_overlaps_cell(uint8_t cx, uint8_t cy);

/* ---- things ----------------------------------------------------------- */
#define MAX_THINGS 160
#define THING_ACTIVE   0x01    /* raycast.s tests this bit */
#define THING_SOLID    0x02    /* currently blocks movement */

/* raycast.s reads x, y and flags at their offsets: keep them first. */
typedef struct {
    int x, y;          /* Q8 */
    uint8_t type;      /* index into thingdefs[] */
    uint8_t flags;     /* THING_* */
    uint8_t sprite;    /* SPR_* currently shown */
    /* monsters, missiles and effects */
    uint8_t state;
    uint8_t tics;      /* tics left in the current state or frame */
    int hp;
    int dx, dy;        /* missile velocity, Q8 per tic */
    uint8_t dir;       /* chase direction 0..7 */
    uint8_t movecount; /* steps before choosing a new direction */
    uint8_t reload;    /* tics before it may attack again */
    uint8_t shots;     /* shots left in the current attack */
    uint8_t anim;      /* walk animation clock */
    uint8_t los;       /* 1 if it could see the player at the last check */
    uint8_t bnext;     /* next solid thing in the same blockmap cell, index + 1 */
    uint8_t pad[5];    /* 32 bytes: things[i] is a shift, not a multiply */
} thing_t;
_Static_assert(offsetof(thing_t, y) == 3 && offsetof(thing_t, flags) == 7,
               "raycast.s reads thing_t x, y and flags");
_Static_assert(sizeof(thing_t) == 32, "thing_t is indexed by shifting");

extern thing_t things[MAX_THINGS];
extern uint8_t num_things;

void things_spawn(const uint8_t *data, uint8_t count);
void things_tic(void);                  /* pickups, monsters, missiles, effects */
thing_t *thing_spawn(uint8_t type, int x, int y);   /* NULL if full */
thing_t *thing_at(int x, int y, int radius, const thing_t *self);   /* a solid thing */
#define thing_blocks_at(x, y, radius, self) (thing_at(x, y, radius, self) != NULL)
bool thing_in_cell(uint8_t cx, uint8_t cy);
void blockmap_clear(void);
void blockmap_move(thing_t *t, int nx, int ny);
void thing_unsolid(thing_t *t);

/* ---- monsters (monsters.c) --------------------------------------------- */
enum { MS_IDLE, MS_CHASE, MS_WINDUP, MS_ATTACK, MS_PAIN, MS_DYING, MS_DEAD };

void monster_init(thing_t *t);
void monster_tic(thing_t *t, uint8_t index);
void monster_damage(thing_t *t, int damage);
void sound_init(void);                   /* after doors_init: regions for hearing */
void monsters_hear(void);                /* wake monsters the player can be heard by */
bool line_clear(int x0, int y0, int x1, int y1);
uint8_t random8(void);

/* ---- combat (combat.c) ------------------------------------------------- */
const char *combat_load(void);           /* NULL on success, else what is missing */
void combat_reset(void);
void combat_tic(bool fire);
void weapon_select(uint8_t weapon);      /* WP_*, if owned and usable */
void player_damage(int amount);
void missile_tic(thing_t *t);
void effect_tic(thing_t *t);
void spawn_effect(uint8_t type, int x, int y);
void spawn_missile(uint8_t type, const thing_t *from, int damage);
void weapon_draw(uint8_t *fb);           /* first-person weapon, before rc_dup */
bool weapon_flash(void);                 /* muzzle light this frame */
extern bool player_dead;

/* ---- level ------------------------------------------------------------ */
typedef struct {
    char name[24];
    uint8_t width, height;
    uint8_t ceiling_color, floor_color;
    uint8_t ambient, falloff;
    uint8_t number;
    uint8_t items, items_total;
    uint8_t secrets, secrets_total;
    uint8_t kills, kills_total;
    unsigned tics;                      /* time on this level */
    bool exiting;
    bool boss;                          /* ends when the boss dies */
} level_info_t;

extern level_info_t level;

#define NUM_LEVELS 5

bool level_load(uint8_t num);

/* ---- game state (main.c) ------------------------------------------------ */
enum { SKILL_EASY, SKILL_NORMAL, SKILL_HARD };
extern uint8_t game_skill;
extern uint8_t game_mode;               /* 0 menus, 1 playing (tests read it) */

/* ---- RAM arena (arena.c) ---------------------------------------------- */
/* 256-aligned buffers in free user RAM; offsets are multiples of 256. */
#define SPRITE_CACHE_SLOTS 8
#define ARENA_SPRITES      0                            /* 1 KB per slot */
#define ARENA_TINTS        (SPRITE_CACHE_SLOTS * 1024)  /* 2 x 4 palettes */
#define ARENA_ITEMS        (ARENA_TINTS + 2 * 4 * 512)  /* pickups per map cell */
#define ARENA_REGIONS      (ARENA_ITEMS + MAP_SIZE * MAP_SIZE)  /* sound areas */
#define ARENA_AUTOMAP      (ARENA_REGIONS + MAP_SIZE * MAP_SIZE) /* seen cells */
#define ARENA_SIZE         (ARENA_AUTOMAP + MAP_SIZE * MAP_SIZE)

extern uint8_t *arena;

const char *arena_init(void);
extern uint8_t *rc_automap;     /* arena + ARENA_AUTOMAP, for raycast.s */

/* ---- automap (automap.c) ------------------------------------------------- */
void automap_reset(void);       /* new level: nothing seen */
void automap_visit(void);       /* each tic: the player's cell */
void automap_draw(uint8_t *fb); /* over the 3D view area */

/* ---- saves (save.c) ------------------------------------------------------ */
typedef struct {
    char magic[4];
    uint8_t skill;                      /* of the run in progress */
    uint8_t level;                      /* level to continue at, 0 = none */
    int health, armor;                  /* loadout at the start of that level */
    uint8_t armor_class, weapons, weapon;
    uint16_t ammo[NUM_AMMO];
    uint16_t best[3][NUM_LEVELS];       /* best time per skill, seconds (0: none) */
    uint8_t won;                        /* bit per skill: episode finished */
} save_t;

extern save_t save;
void save_load(void);
bool save_write(bool archive);          /* archive: only when quitting */
void save_loadout(const player_t *p, uint8_t level_num);
void load_loadout(player_t *p);

/* ---- menus and screens (menu.c) ------------------------------------------ */
enum { TITLE_NEW, TITLE_CONTINUE, TITLE_QUIT };
enum { PAUSE_RESUME, PAUSE_RESTART, PAUSE_TITLE, PAUSE_QUIT };
#define MENU_QUIT 0xFF                  /* Clear, from any menu */
uint8_t title_menu(void);               /* TITLE_* */
uint8_t skill_menu(void);               /* SKILL_*, or MENU_QUIT */
uint8_t pause_menu(void);               /* PAUSE_* */
bool intermission(unsigned best, bool record);   /* false: Clear */
bool victory_screen(unsigned run_secs);          /* false: Clear */
void message_screen(const char *line1, const char *line2);
void arena_rebind(void);        /* after other variables were modified */
void arena_free(void);

/* ---- assets ----------------------------------------------------------- */
#define TEX_SIZE   32
extern uint8_t textures[NUM_TEXTURES][TEX_SIZE * TEX_SIZE];
extern uint16_t base_palette[256];

/* Returns NULL on success or a message naming what is missing. */
const char *assets_load(void);

#endif
