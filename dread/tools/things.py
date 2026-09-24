"""Thing definitions: the single source for the level compiler and the game.

build_assets.py turns this table into src/gen/things.h and
src/gen/thingdefs.c, so map characters, sprite ids and pickup behavior can
never drift apart between Python and C.
"""

import enemies as E
import sprites as S

# kinds
DECOR, HEALTH, MEGA, ARMOR, AMMO, KEY, WEAPON, MONSTER, MISSILE, EFFECT = range(10)
KIND_NAMES = ["DECOR", "HEALTH", "MEGA", "ARMOR", "AMMO", "KEY", "WEAPON",
              "MONSTER", "MISSILE", "EFFECT"]

# ammo types and weapons (weapon number = key 1..5 minus one)
AMMO_TYPES = ["BULLETS", "SHELLS", "ROCKETS"]
BULLETS, SHELLS, ROCKETS = range(3)
WEAPONS = ["FISTS", "PISTOL", "SHOTGUN", "CHAINGUN", "LAUNCHER"]

# flags
F_BLOCK = 0x01        # blocks movement
F_BRIGHT = 0x02       # drawn at full brightness
F_ITEM = 0x04         # counts toward the level's item total
F_SHOOT = 0x08        # can be hit by the player's attacks
F_KILL = 0x10         # counts toward the level's kill total
F_BIG = 0x20          # drawn two tiles tall (the boss)

#   char  name            sprite             kind    arg       amount flags            message
THINGS = [
    ("s", "stim",         "stim",            HEALTH, 0,        10, F_ITEM,          "Picked up a stimpack."),
    ("m", "medkit",       "medkit",          HEALTH, 0,        25, F_ITEM,          "Picked up a medkit."),
    ("o", "orb",          "orb",             MEGA,   0,        100, F_ITEM | F_BRIGHT, "Vital orb! +100 health."),
    ("a", "armor",        "armor",           ARMOR,  1,        100, F_ITEM,         "Picked up the combat vest."),
    ("b", "heavy_armor",  "heavy_armor",     ARMOR,  2,        200, F_ITEM,         "Picked up the heavy plate!"),
    ("c", "clip",         "clip",            AMMO,   BULLETS,  10, F_ITEM,          "Picked up a clip."),
    ("n", "bullet_box",   "bullet_box",      AMMO,   BULLETS,  50, F_ITEM,          "Picked up a box of bullets."),
    ("e", "shells",       "shells",          AMMO,   SHELLS,   4, F_ITEM,           "Picked up 4 shells."),
    ("f", "shell_box",    "shell_box",       AMMO,   SHELLS,   20, F_ITEM,          "Picked up a box of shells."),
    ("r", "rocket",       "rocket",          AMMO,   ROCKETS,  1, F_ITEM,           "Picked up a rocket."),
    ("q", "rocket_box",   "rocket_box",      AMMO,   ROCKETS,  5, F_ITEM,           "Picked up a crate of rockets."),
    ("k", "key_red",      "key_red",         KEY,    0,        0, F_ITEM | F_BRIGHT, "Picked up the red keycard."),
    ("j", "key_blue",     "key_blue",        KEY,    1,        0, F_ITEM | F_BRIGHT, "Picked up the blue keycard."),
    ("y", "key_yellow",   "key_yellow",      KEY,    2,        0, F_ITEM | F_BRIGHT, "Picked up the yellow keycard."),
    ("g", "shotgun",      "shotgun_pickup",  WEAPON, 2,        8, F_ITEM,           "You got the scattergun!"),
    ("u", "chaingun",     "chaingun_pickup", WEAPON, 3,        20, F_ITEM,          "You got the rotary cannon!"),
    ("l", "launcher",     "launcher_pickup", WEAPON, 4,        2, F_ITEM,           "You got the rocket launcher!"),
    ("t", "lamp",         "lamp",            DECOR,  0,        0, F_BLOCK | F_BRIGHT, ""),
    ("p", "pillar",       "pillar",          DECOR,  0,        0, F_BLOCK,          ""),
    ("w", "barrel",       "barrel",          DECOR,  0,        0, F_BLOCK,          ""),
    ("d", "blood",        "blood",           DECOR,  0,        0, 0,                ""),
    ("z", "bones",        "bones",           DECOR,  0,        0, 0,                ""),
    ("i", "brazier",      "brazier",         DECOR,  0,        0, F_BLOCK | F_BRIGHT, ""),
    # monsters: arg = class in src/monsters.c
    ("1", "grunt",        "grunt_walk0",     MONSTER, 0,       0, F_BLOCK | F_SHOOT | F_KILL, ""),
    ("2", "heavy",        "heavy_walk0",     MONSTER, 1,       0, F_BLOCK | F_SHOOT | F_KILL, ""),
    ("3", "imp",          "imp_walk0",       MONSTER, 2,       0, F_BLOCK | F_SHOOT | F_KILL, ""),
    ("4", "brute",        "brute_walk0",     MONSTER, 3,       0, F_BLOCK | F_SHOOT | F_KILL, ""),
    ("9", "boss",         "boss_walk0",      MONSTER, 4,       0, F_BLOCK | F_SHOOT | F_KILL | F_BIG, ""),
    # spawned at run time only (no map character); a missile's arg is its
    # splash radius in 1/4 tiles (0: none), its amount the speed (Q8/tic)
    ("",  "fireball",     "fireball0",       MISSILE, 0,       36, F_BRIGHT,        ""),
    ("",  "puff",         "puff0",           EFFECT, 0,        0, F_BRIGHT,         ""),
    ("",  "bloodhit",     "bloodhit0",       EFFECT, 0,        0, 0,                ""),
    ("",  "boom",         "boom0",           EFFECT, 0,        0, F_BRIGHT,         ""),
    ("",  "rocketfly",    "rocketfly0",      MISSILE, 7,       56, F_BRIGHT,        ""),
    ("",  "bossrocket",   "rocketfly0",      MISSILE, 6,       40, F_BRIGHT,        ""),
]

# Weapon pickups give ammo of the weapon's type.
WEAPON_AMMO = {2: SHELLS, 3: BULLETS, 4: ROCKETS}

CHARS = {t[0]: i for i, t in enumerate(THINGS) if t[0]}
# every sprite id: world sprites (DREADSP) first, then enemy frames and
# effects (DREADEN)
ENEMY_SPRITES = [n for n, _ in E.build_all()]
ALL_SPRITES = S.SPRITE_NAMES + ENEMY_SPRITES
NAMES = [t[1] for t in THINGS]


def c_header():
    lines = ["/* Generated by tools/build_assets.py from tools/things.py -- do not edit. */",
             "#ifndef GEN_THINGS_H", "#define GEN_THINGS_H", "", "#include <stdint.h>", ""]
    for i, n in enumerate(KIND_NAMES):
        lines.append("#define KIND_%s %d" % (n, i))
    lines.append("")
    for i, n in enumerate(AMMO_TYPES):
        lines.append("#define AMMO_%s %d" % (n, i))
    lines.append("#define NUM_AMMO %d" % len(AMMO_TYPES))
    for i, n in enumerate(WEAPONS):
        lines.append("#define WP_%s %d" % (n, i))
    lines.append("#define NUM_WEAPONS %d" % len(WEAPONS))
    lines.append("")
    lines += ["#define TF_BLOCK  0x%02X" % F_BLOCK,
              "#define TF_BRIGHT 0x%02X" % F_BRIGHT,
              "#define TF_ITEM   0x%02X" % F_ITEM,
              "#define TF_SHOOT  0x%02X" % F_SHOOT,
              "#define TF_KILL   0x%02X" % F_KILL,
              "#define TF_BIG    0x%02X" % F_BIG, ""]
    for i, t in enumerate(THINGS):
        lines.append("#define TH_%-12s %d" % (t[1].upper(), i))
    lines.append("#define NUM_THING_TYPES %d" % len(THINGS))
    lines.append("")
    for i, n in enumerate(ALL_SPRITES):
        lines.append("#define SPR_%-16s %d" % (n.upper(), i))
    lines.append("#define NUM_WORLD_SPRITES %d  /* in DREADSP; the rest are in DREADEN */"
                 % len(S.SPRITE_NAMES))
    lines.append("#define NUM_SPRITES %d" % len(ALL_SPRITES))
    lines += ["", "typedef struct {",
              "    uint8_t sprite;", "    uint8_t kind;", "    uint8_t arg;",
              "    uint8_t amount;", "    uint8_t flags;", "    const char *msg;",
              "} thingdef_t;", "",
              "/* Opaque bounding box of each sprite: top row, first and last+1 column. */",
              "typedef struct {", "    uint8_t top, left, right;", "} spritebox_t;", "",
              "extern const thingdef_t thingdefs[NUM_THING_TYPES];",
              "extern const spritebox_t sprite_box[NUM_SPRITES];", "", "#endif", ""]
    return "\n".join(lines)


def c_source(sprite_arrays):
    lines = ["/* Generated by tools/build_assets.py from tools/things.py -- do not edit. */",
             '#include "things.h"', "",
             "const spritebox_t sprite_box[NUM_SPRITES] = {"]
    for name, a in zip(ALL_SPRITES, sprite_arrays):
        rows = [y for y in range(a.shape[0]) if a[y].any()] or [31]
        cols = [x for x in range(a.shape[1]) if a[:, x].any()] or [15]
        lines.append("    { %d, %d, %d },  /* %s */" % (rows[0], cols[0], cols[-1] + 1, name))
    lines += ["};", "", "const thingdef_t thingdefs[NUM_THING_TYPES] = {"]
    for t in THINGS:
        char, name, sprite, kind, arg, amount, flags, msg = t
        lines.append('    { SPR_%s, KIND_%s, %d, %d, 0x%02X, %s },  /* %s */'
                     % (sprite.upper(), KIND_NAMES[kind], arg, amount, flags,
                        '"%s"' % msg if msg else "0", name))
    lines += ["};", ""]
    return "\n".join(lines)
