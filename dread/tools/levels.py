"""ASCII level files -> DREAD binary level format.

Level file layout (see levels/*.txt):

    name: Hangar Nine
    ceiling: steel 9          # ramp name + shade of the ceiling base color
    floor: brown 6
    light: 0 18               # ambient (light levels) and falloff (1/16 level per tile)
    boss: yes                 # optional: the level ends when the boss dies
    legend:                   # optional per-level overrides
      $ = secret stone_gray
    map:
    ##########
    #>.......#
    ##########

Tile byte encoding (shared with src/level.h):
    0x00        empty floor
    0x01..0x3F  solid wall, texture id = tile - 1
    0x40..0x7F  secret door: a flush wall with texture (tile & 0x3F) - 1
                that slides open when used
    0x80..0x8F  doors (0x80 normal, 0x81 red, 0x82 blue, 0x83 yellow)
The game renumbers every door and secret door to 0x80 | door index when
it loads a level.

Things: lowercase letters are items and decorations (tools/things.py).
Monsters are digits and symbols, which also pick the skill levels they
appear on (Easy, Normal, Hard):
    1 grunt   2 heavy   3 imp   4 brute     every skill
    5 grunt   6 heavy   7 imp   8 brute     Normal and Hard
    ! grunt   @ heavy   & imp   * brute     Hard only
    9 the boss

Binary layout (little endian), version 1:
    0   'D' 'R' 'L' version
    4   width, height
    6   start_x, start_y, start_angle (0..255, one unit = 4 engine angle units)
    9   ceiling color, floor color, ambient, falloff
    13  flags (bit 0: boss level), 2 reserved bytes
    16  name, 24 bytes, zero padded
    40  tiles[width * height], row major
    ..  thing_count, then thing_count * {x, y, type, flags}
"""

import os
import struct

import palette as P
import textures as T
import things as TH

VERSION = 1

TILE_DOOR = 0x80
TILE_DOOR_RED = 0x81
TILE_DOOR_BLUE = 0x82
TILE_DOOR_YELLOW = 0x83
TILE_SECRET = 0x40

# Default character legend. Walls are uppercase letters or symbols.
DEFAULT_LEGEND = {
    "#": "tech_panel",
    "C": "tech_computer",
    "V": "tech_vent",
    "I": "tech_light",
    "S": "stone_gray",
    "O": "stone_brown",
    "B": "brick_red",
    "M": "metal_plate",
    "Z": "metal_hazard",
    "Q": "metal_pipes",
    "F": "hell_flesh",
    "L": "hell_rock",
    "K": "hell_bone",
    "G": "hell_sigil",
    "J": "door_jamb",
    "X": "exit_off",
    "D": "door",
    "R": "door red",
    "U": "door blue",
    "Y": "door yellow",
}

START_DIRS = {">": 0, "v": 64, "<": 128, "^": 192}  # east, south, west, north

# Thing types are placed with lowercase letters and digits (see
# tools/things.py); SKILL_CHARS adds skill-limited monsters.
THING_CHARS = TH.CHARS
SKILL_EASY, SKILL_NORMAL, SKILL_HARD = 0x01, 0x02, 0x04
SKILL_ALL = SKILL_EASY | SKILL_NORMAL | SKILL_HARD
SKILL_CHARS = {
    "5": ("grunt", SKILL_NORMAL | SKILL_HARD), "6": ("heavy", SKILL_NORMAL | SKILL_HARD),
    "7": ("imp", SKILL_NORMAL | SKILL_HARD), "8": ("brute", SKILL_NORMAL | SKILL_HARD),
    "!": ("grunt", SKILL_HARD), "@": ("heavy", SKILL_HARD),
    "&": ("imp", SKILL_HARD), "*": ("brute", SKILL_HARD),
}
LEVEL_BOSS = 0x01


def tex_id(name):
    return T.TEXTURE_NAMES.index(name)


def tile_for(spec):
    parts = spec.split()
    if parts[0] == "door":
        color = parts[1] if len(parts) > 1 else None
        return {None: TILE_DOOR, "red": TILE_DOOR_RED,
                "blue": TILE_DOOR_BLUE, "yellow": TILE_DOOR_YELLOW}[color]
    if parts[0] == "secret":
        return TILE_SECRET | (tex_id(parts[1]) + 1)
    return tex_id(parts[0]) + 1


def parse(path):
    with open(path, "r") as f:
        lines = [ln.rstrip("\n") for ln in f]
    info = {"name": os.path.splitext(os.path.basename(path))[0],
            "ceiling": ("steel", 8), "floor": ("brown", 7), "light": (0, 16), "boss": False}
    legend = dict(DEFAULT_LEGEND)
    rows = []
    mode = None
    for ln in lines:
        if mode == "map":
            if ln.strip() == "" or ln.startswith(";"):
                continue
            rows.append(ln)
            continue
        s = ln.split(";", 1)[0].rstrip()
        if not s.strip():
            continue
        if s.strip() == "legend:":
            mode = "legend"
            continue
        if s.strip() == "map:":
            mode = "map"
            continue
        if mode == "legend" and s.startswith(" "):
            ch, spec = s.strip().split("=", 1)
            legend[ch.strip()] = spec.strip()
            continue
        mode = None
        key, val = s.split(":", 1)
        key, val = key.strip(), val.strip()
        if key == "name":
            info["name"] = val
        elif key in ("ceiling", "floor"):
            r, sh = val.split()
            info[key] = (r, int(sh))
        elif key == "light":
            a, fo = val.split()
            info["light"] = (int(a), int(fo))
        elif key == "boss":
            info["boss"] = val.lower() in ("yes", "true", "1")
        else:
            raise ValueError("%s: unknown key %r" % (path, key))
    return info, legend, rows


def validate_doors(path, tiles, w, h):
    """Sliding doors need walls on both sides along exactly one axis; the
    engine derives the door's orientation from that. Secret doors are
    flush walls and only need to be inside the map."""
    def solid(x, y):
        t = tiles[y * w + x]
        return t != 0 and not (t & 0x80)
    for y in range(h):
        for x in range(w):
            t = tiles[y * w + x]
            if not (t & 0x80):
                continue
            ew = solid(x - 1, y) and solid(x + 1, y)
            ns = solid(x, y - 1) and solid(x, y + 1)
            if ew == ns:
                raise ValueError("%s: door at %d,%d needs walls on exactly one axis (E-W or N-S)"
                                 % (path, x, y))


def compile_level(path):
    info, legend, rows = parse(path)
    h = len(rows)
    w = max(len(r) for r in rows)
    if w > 64 or h > 64:
        raise ValueError("%s: map is %dx%d, max is 64x64" % (path, w, h))
    tiles = bytearray(w * h)
    things = []
    start = None
    for y, row in enumerate(rows):
        row = row.ljust(w, " ")
        for x, ch in enumerate(row):
            t = 0
            if ch in (".", " "):
                t = 0
            elif ch in START_DIRS:
                start = (x, y, START_DIRS[ch])
            elif ch in THING_CHARS:
                things.append((x, y, THING_CHARS[ch], SKILL_ALL))
            elif ch in SKILL_CHARS:
                name, skills = SKILL_CHARS[ch]
                things.append((x, y, TH.NAMES.index(name), skills))
            elif ch in legend:
                t = tile_for(legend[ch])
            else:
                raise ValueError("%s:%d: unknown map char %r" % (path, y + 1, ch))
            # The engine never checks bounds: the outer ring must be solid.
            if (x == 0 or y == 0 or x == w - 1 or y == h - 1) and t == 0:
                raise ValueError("%s: open tile on the map border at %d,%d" % (path, x, y))
            tiles[y * w + x] = t
    if start is None:
        raise ValueError("%s: no player start (use > v < ^)" % path)
    validate_doors(path, tiles, w, h)
    if len(things) > 255:
        raise ValueError("%s: %d things, max 255" % (path, len(things)))

    name = info["name"].encode("ascii")[:23]
    ceil_c = P.idx(*info["ceiling"])
    floor_c = P.idx(*info["floor"])
    ambient, falloff = info["light"]
    out = bytearray(b"DRL" + bytes([VERSION]))
    out += bytes([w, h, start[0], start[1], start[2]])
    out += bytes([ceil_c, floor_c, ambient & 0xFF, falloff & 0xFF])
    out += bytes([LEVEL_BOSS if info["boss"] else 0, 0, 0])
    out += name + bytes(24 - len(name))
    assert len(out) == 40
    out += tiles
    out += bytes([len(things)])
    for t in things:
        out += struct.pack("<BBBB", *t)
    info["things"] = things
    info["start"] = start
    info["size"] = (w, h)
    check_level(path, info, tiles, w, h)
    return info, bytes(out)


KEY_THINGS = {"key_red": 1, "key_blue": 2, "key_yellow": 3}


def check_level(path, info, tiles, w, h):
    """Play the level as a flood fill: from the start, walk through open
    cells, secret doors and every door whose keycard has been reached,
    picking keycards up on the way. The exit (or, on a boss level, the
    boss) and every monster and item must be reachable, and every keycard
    door must open eventually. Raises ValueError, and fills in
    info["stats"]: things per skill."""
    exit_tile = tex_id("exit_off") + 1
    keys_at = {}
    for x, y, typ, skills in info["things"]:
        name = TH.NAMES[typ]
        if name in KEY_THINGS:
            keys_at[(x, y)] = KEY_THINGS[name]

    def passable(x, y, keys):
        t = tiles[y * w + x]
        if t == 0 or (0x40 <= t < 0x80):
            return True
        if t & 0x80:
            lock = t & 0x03
            return lock == 0 or lock in keys
        return False

    keys = set()
    order = []
    while True:
        seen = {info["start"][:2]}
        todo = [info["start"][:2]]
        while todo:
            x, y = todo.pop()
            for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                if (nx, ny) not in seen and 0 <= nx < w and 0 <= ny < h and passable(nx, ny, keys):
                    seen.add((nx, ny))
                    todo.append((nx, ny))
        new = {k for pos, k in keys_at.items() if pos in seen} - keys
        if not new:
            break
        keys |= new
        order += sorted(new)
    problems = []
    for x, y, typ, skills in info["things"]:
        if (x, y) not in seen:
            problems.append("%s at %d,%d is unreachable" % (TH.NAMES[typ], x, y))
    for y in range(h):
        for x in range(w):
            t = tiles[y * w + x]
            if t & 0x80 and (t & 3) and (t & 3) not in keys:
                problems.append("a %s door at %d,%d never opens"
                                % (["", "red", "blue", "yellow"][t & 3], x, y))
    if info["boss"]:
        if not any(TH.NAMES[t[2]] == "boss" for t in info["things"]):
            problems.append("boss level without a boss")
    else:
        exits = [(x, y) for y in range(h) for x in range(w) if tiles[y * w + x] == exit_tile]
        if not any((x + dx, y + dy) in seen for x, y in exits
                   for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
            problems.append("no reachable exit switch")
    if problems:
        raise ValueError("%s:\n  " % path + "\n  ".join(problems))
    stats = {}
    for label, bit in (("easy", SKILL_EASY), ("normal", SKILL_NORMAL), ("hard", SKILL_HARD)):
        n = [TH.NAMES[t[2]] for t in info["things"] if t[3] & bit and TH.THINGS[t[2]][3] == TH.MONSTER]
        stats[label] = len(n)
    info["stats"] = stats
    info["key_order"] = order
