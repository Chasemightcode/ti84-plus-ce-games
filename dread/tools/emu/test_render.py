"""Check the raycaster in the real binary against a floating-point reference.

Boots DREAD in the emulator until its first frame, then places the player
at many positions/angles, calls _render_view directly and compares every
column's depth-buffer entry with an exact float DDA over the same map.

    python tools/emu/test_render.py [--shots N]
"""

import math
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

from ce import CPU_HZ, Machine  # noqa: E402

VIEW_W, FOCAL = 160, 120


class FirstFrame(Exception):
    pass


def boot():
    """Runs DREAD through the menus (New Game, Normal) to its first frame of
    play and stops there; main is abandoned, the level stays loaded."""
    m = Machine(os.path.join(ROOT, "bin", "DREAD.obj"), os.path.join(ROOT, "bin", "DREAD.map"))
    m.load_game_vars(ROOT)
    m.set_keys({})
    menu = {"n": 0}

    def stop(mach):
        if mach.in_game():
            raise FirstFrame()
        mach.set_keys(mach.menu_keys(menu["n"]))
        menu["n"] += 1
    m.on_swap = stop
    try:
        m.call("_main")
    except FirstFrame:
        pass
    m.on_swap = None
    return m


DOORS = {}      # door index -> (flags, open), filled by main() from memory


def reference(mapdata, px, py, angle, cam_eps=0.0, columns=None):
    """Float DDA: per column (perpendicular distance, tile) for the same
    camera model as render.c (plane = 80/120 of the direction). Doors
    (tile & 0x80) are slabs through the cell center (flag 1: vertical) or
    flush with the face the ray enters (flag 2: secret)."""
    a = angle * 2 * math.pi / 1024
    dx, dy = math.cos(a), math.sin(a)
    k = (VIEW_W / 2) / FOCAL
    plx, ply = -dy * k, dx * k
    out = []
    for x in (range(VIEW_W) if columns is None else columns):
        cam = (2 * x + 1) / VIEW_W - 1 + cam_eps
        rx, ry = dx + plx * cam, dy + ply * cam
        mx, my = int(px), int(py)
        ddx = abs(1 / rx) if rx else 1e30
        ddy = abs(1 / ry) if ry else 1e30
        if rx < 0:
            sx, sdx = -1, (px - mx) * ddx
        else:
            sx, sdx = 1, (mx + 1 - px) * ddx
        if ry < 0:
            sy, sdy = -1, (py - my) * ddy
        else:
            sy, sdy = 1, (my + 1 - py) * ddy
        while True:
            if sdx < sdy:
                mx += sx
                d, sdx = sdx, sdx + ddx
                xside = True
            else:
                my += sy
                d, sdy = sdy, sdy + ddy
                xside = False
            t = mapdata[my * 64 + mx]
            if t & 0x80:
                flags, opened = DOORS[t & 0x3F]
                if flags & 2:
                    dp = d
                elif bool(flags & 1) == xside:
                    dp = d + (ddx if xside else ddy) / 2
                    if dp >= (sdy if xside else sdx):
                        continue        # leaves through the side of the cell
                else:
                    continue
                u = (py + dp * ry) if xside else (px + dp * rx)
                if opened < 255 and (u - math.floor(u)) * 256 >= opened:
                    out.append((dp, t))
                    break
                continue
            if t:
                out.append((d, t))
                break
    return out


def main():
    shots = int(sys.argv[sys.argv.index("--shots") + 1]) if "--shots" in sys.argv else 6
    m = boot()
    sym = m.symbols
    mem = m.bus.mem
    lm = sym["_level_map"]
    mapdata = bytes(mem[lm:lm + 4096])
    for i in range(mem[sym["_num_doors"]]):
        a = sym["_doors"] + 8 * i
        DOORS[i] = (mem[a + 1], mem[a])
    rng = random.Random(1)

    open_cells = [(x, y) for y in range(64) for x in range(64)
                  if mapdata[y * 64 + x] == 0 and all(
                      mapdata[(y + j) * 64 + x + i] == 0 or True for i in (0,) for j in (0,))]
    poses = []
    for _ in range(60):
        cx, cy = rng.choice(open_cells)
        # stay at least 0.3 tiles from any wall like the collision code does
        fx = rng.uniform(0.3, 0.7)
        fy = rng.uniform(0.3, 0.7)
        poses.append((cx + fx, cy + fy, rng.randrange(1024)))
    # axis-aligned and diagonal views stress the DDA's tie handling
    for ang in (0, 128, 256, 384, 512, 640, 768, 896, 1, 1023, 255, 257):
        poses.append((3.5, 5.5, ang))

    os.makedirs(os.path.join(ROOT, "build", "shots"), exist_ok=True)
    worst = 0.0
    grazing = 0
    bad_cols = 0
    total_cols = 0
    cycles = []
    for i, (px, py, ang) in enumerate(poses):
        x8, y8 = int(px * 256), int(py * 256)
        pl = sym["_player"]
        mem[pl:pl + 3] = x8.to_bytes(3, "little")
        mem[pl + 3:pl + 6] = y8.to_bytes(3, "little")
        mem[pl + 6:pl + 9] = ang.to_bytes(3, "little")
        c0 = m.cpu.cycles
        m.call("_render_view")
        cycles.append(m.cpu.cycles - c0)
        zb = sym["_zbuffer"]
        ref = reference(mapdata, x8 / 256, y8 / 256, ang)
        for x in range(VIEW_W):
            got = (mem[zb + 2 * x] | mem[zb + 2 * x + 1] << 8) / 256
            want = max(ref[x][0], 16 / 256)
            err = abs(got - want) / want
            total_cols += 1
            if err > 0.02 and abs(got - want) > 2 / 256:
                # A ray that grazes a block corner can legitimately go either
                # way; accept the result if a nudge of 1/2000 of the view
                # width reproduces it.
                if any(abs(reference(mapdata, x8 / 256, y8 / 256, ang, e, [x])[0][0] - got) / got < 0.02
                       for e in (-1e-3, -5e-4, 5e-4, 1e-3)):
                    grazing += 1
                    continue
                bad_cols += 1
                if bad_cols <= 10:
                    print("pose %d (%.3f,%.3f,a=%d) col %d: got %.4f want %.4f" % (i, px, py, ang, x, got, want))
            worst = max(worst, err if abs(got - want) > 2 / 256 else 0)
        if i < shots:
            draw = m.draw_base()
            m.screenshot(draw).save(os.path.join(ROOT, "build", "shots", "test_%02d.png" % i))
    avg = sum(cycles) / len(cycles)
    print("%d poses, %d columns: %d wrong, %d corner-grazing rays within tolerance"
          % (len(poses), total_cols, bad_cols, grazing))
    print("render_view: avg %d cycles (%.1f ms), max %d" % (avg, avg / CPU_HZ * 1000, max(cycles)))
    sys.exit(1 if bad_cols else 0)


if __name__ == "__main__":
    main()
