"""Stage the README screenshots on the real binary.

    python tools/emu/screenshots.py [outdir]

Plays the release build (campaign levels, not the test map) through the
menus, then poses the player in each level and saves 640x480 palette PNGs
(each calculator pixel drawn 2x2) to screenshots/ by default. Levels after
the first are reached with a save that continues there with every weapon.
Monsters are frozen for posed shots so they stay where the level puts them.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

from PIL import Image  # noqa: E402

from ce import Exit  # noqa: E402
from test_game import (P_HEALTH, T_STATE, T_TYPE, MONSTERS, MS_DYING, Done, Game,  # noqa: E402
                       press)

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "screenshots")
FROZEN = 0xFF


def save_for(level, weapon):
    """A 54-byte DREADSV continuing at `level` on Normal with every weapon."""
    sv = bytearray(54)
    sv[0:4] = b"DSV1"
    sv[4] = 1                                    # Normal
    sv[5] = level
    sv[6:9] = (200).to_bytes(3, "little")        # health
    sv[9:12] = (200).to_bytes(3, "little")       # armor
    sv[12] = 2                                   # heavy plate
    sv[13] = 0x1F                                # every weapon
    sv[14] = weapon
    for k, v in enumerate((200, 50, 50)):
        sv[16 + 2 * k:18 + 2 * k] = v.to_bytes(2, "little")
    return bytes(sv)


def shot(g, name):
    im = g.m.screenshot(indexed=True)
    im = im.resize((640, 480), Image.NEAREST)
    path = os.path.join(OUT, name + ".png")
    im.save(path, optimize=True)
    print("  %s (%d KB)" % (os.path.relpath(path, ROOT), os.path.getsize(path) // 1024))


def freeze(g):
    for i in g.things():
        if g.thing(i, T_TYPE) in MONSTERS and g.thing(i, T_STATE) < MS_DYING:
            g.set_thing(i, T_STATE, FROZEN)


def hold(g, x, y, deg, frames=3, keys=None):
    g.mem[g.sym["_hud_show_fps"]] = 0            # no FPS counter in the pictures
    for _ in range(frames):
        g.place(x, y, deg)
        g.set24("_player", P_HEALTH, 200)
        yield keys or {}


def run(g, scenario):
    steps = scenario(g)

    def advance(mach):
        try:
            mach.set_keys(next(steps))
        except StopIteration:
            raise Done()
    g.m.on_swap = advance
    g.m.set_keys({})
    try:
        g.m.call("_main")
    except (Done, Exit):
        pass


def menus_and_map1(g):
    yield {}
    yield {}
    shot(g, "title")
    yield press("2nd")
    yield {}
    yield {}
    shot(g, "difficulty")
    yield press("2nd")
    while not g.m.in_game():
        yield {}
    yield {}
    freeze(g)
    yield from hold(g, 20.5, 13.2, 270, 4)
    shot(g, "map1_hall")
    yield press("enter")
    yield {}
    yield {}
    shot(g, "pause")
    yield press("2nd")
    yield {}
    # the exit room, then the switch, with a plausible tally
    yield from hold(g, 30.3, 22.5, 0, 3)
    lv = g.sym["_level"]
    g.mem[lv + 35] = g.mem[lv + 36] - 2          # kills
    g.mem[lv + 31] = g.mem[lv + 32] - 4          # items
    g.mem[lv + 33] = 1                           # secrets
    g.mem[lv + 37:lv + 40] = (35 * 187).to_bytes(3, "little")   # 3:07
    yield press("alpha")
    while g.m.in_game():
        yield {}
    for _ in range(30):
        yield {}
    shot(g, "tally")


def continue_to(g):
    yield {}
    yield {}
    yield press("2nd")                   # CONTINUE is selected when there is a save
    while not g.m.in_game():
        yield {}
    yield {}


def map2_automap(g):
    yield from continue_to(g)
    freeze(g)
    # show the whole map as explored: floor walked (2), and every wall that
    # faces a floor cell seen (1), as if the player had been everywhere
    auto = int.from_bytes(g.mem[g.sym["_rc_automap"]:g.sym["_rc_automap"] + 3], "little")
    lm = g.sym["_level_map"]
    for c in range(64, 64 * 63):
        if g.mem[lm + c] == 0:
            g.mem[auto + c] = 2
            for n in (c - 65, c - 64, c - 63, c - 1, c + 1, c + 63, c + 64, c + 65):
                if g.mem[lm + n] != 0:
                    g.mem[auto + n] = 1
    yield from hold(g, 22.5, 28.5, 270, 2)
    yield press("yequ")
    yield from hold(g, 22.5, 28.5, 270, 3)
    shot(g, "automap")


def map3_keep(g):
    yield from continue_to(g)
    freeze(g)
    yield from hold(g, 13.5, 21.5, 40, 4)
    shot(g, "map3_courtyard")


def map4_fight(g):
    yield from continue_to(g)
    yield from hold(g, 28.5, 39.5, 270, 40, press("2nd"))   # wake them, keep firing
    for n in range(6):
        yield from hold(g, 28.5, 39.5, 270, 1, press("2nd"))
        if n == 3:
            shot(g, "map4_cannon")


def map5_boss(g):
    yield from continue_to(g)
    freeze(g)
    yield from hold(g, 27.8, 11.0, 280, 4)
    shot(g, "map5_warden")


def main():
    os.makedirs(OUT, exist_ok=True)
    print("screenshots ->", os.path.relpath(OUT, ROOT))
    run(Game(test_level=False), menus_and_map1)
    for level, weapon, scenario in ((2, 3, map2_automap), (3, 2, map3_keep),
                                    (4, 3, map4_fight), (5, 4, map5_boss)):
        g = Game(test_level=False)
        g.m.add_appvar("DREADSV", save_for(level, weapon))
        run(g, scenario)


if __name__ == "__main__":
    main()
