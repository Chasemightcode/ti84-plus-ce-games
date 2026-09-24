"""Check rc_gather (thing transform + culling) against float math.

For random poses, runs _render_view on the real binary and compares every
entry of rc_seen with the exact camera-space depth and lateral offset, and
checks that every thing clearly inside the view was gathered.

    python tools/emu/test_sprites.py
"""

import math
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from test_render import boot  # noqa: E402

THING_SIZE = 32         # sizeof(thing_t)
SEEN_SIZE = 10


def s24(b):
    v = int.from_bytes(b, "little")
    return v - (1 << 24) if v & 0x800000 else v


def main():
    m = boot()
    sym, mem = m.symbols, m.bus.mem
    n_things = mem[sym["_num_things"]]
    things = []
    for i in range(n_things):
        a = sym["_things"] + i * THING_SIZE
        things.append((s24(mem[a:a + 3]), s24(mem[a + 3:a + 6]), mem[a + 7] & 1))
    lm = sym["_level_map"]
    mapdata = bytes(mem[lm:lm + 4096])
    open_cells = [(x, y) for y in range(64) for x in range(64) if mapdata[y * 64 + x] == 0]
    rng = random.Random(7)
    worst_depth = worst_lat = 0.0
    missing = 0
    total = 0
    for trial in range(40):
        cx, cy = rng.choice(open_cells)
        px, py = cx * 256 + rng.randrange(60, 196), cy * 256 + rng.randrange(60, 196)
        ang = rng.randrange(1024)
        pl = sym["_player"]
        mem[pl:pl + 3] = px.to_bytes(3, "little")
        mem[pl + 3:pl + 6] = py.to_bytes(3, "little")
        mem[pl + 6:pl + 9] = ang.to_bytes(3, "little")
        m.call("_render_view")
        a = ang * 2 * math.pi / 1024
        c, s = math.cos(a), math.sin(a)
        seen = {}
        for k in range(mem[sym["_rc_nseen"]]):
            e = sym["_rc_seen"] + k * SEEN_SIZE
            depth = mem[e] | mem[e + 1] << 8
            lat = s24(mem[e + 2:e + 5])
            seen[mem[e + 5]] = (depth, lat)
        for i, (tx, ty, active) in enumerate(things):
            dx, dy = tx - px, ty - py
            d = dx * c + dy * s
            lt = dy * c - dx * s
            if i in seen:
                total += 1
                gd, gl = seen[i]
                # error relative to distance (in tiles, at least 1): what
                # matters on screen is the angular error
                dist = max(1.0, math.hypot(dx, dy) / 256)
                worst_depth = max(worst_depth, abs(gd - d) / 256 / dist)
                worst_lat = max(worst_lat, abs(gl - lt) / 256 / dist)
            elif active and d > 96 and abs(lt) < d * 0.6 and abs(dx) < 23.5 * 256 and abs(dy) < 23.5 * 256:
                missing += 1
                print("missing thing %d at pose %d: depth %.2f lat %.2f" % (i, trial, d / 256, lt / 256))
    print("%d gathered entries checked; worst error per tile of distance: depth %.4f, "
          "lateral %.4f; %d missing" % (total, worst_depth, worst_lat, missing))
    ok = worst_depth < 0.005 and worst_lat < 0.005 and missing == 0 and total > 0
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
