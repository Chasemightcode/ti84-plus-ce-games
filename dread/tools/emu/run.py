"""Run a DREAD build headlessly with scripted input.

    python tools/emu/run.py [script] [--shots DIR]

A script is a list of steps "<frames> <keys>" separated by commas, where
keys is any of: up down left right alpha 2nd clear enter yequ 1..5 none.
Example: "10 none, 20 up, 8 right, 15 up alpha left"

Screenshots of the last frame of every step are written to --shots, and
per-frame cycle counts are summarized as estimated frames per second.
"""

import argparse
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

from ce import CPU_HZ, Machine  # noqa: E402

KEYS = {
    "down": (7, 0x01), "left": (7, 0x02), "right": (7, 0x04), "up": (7, 0x08),
    "alpha": (2, 0x80), "2nd": (1, 0x20), "yequ": (1, 0x10), "mode": (1, 0x40),
    "clear": (6, 0x40), "enter": (6, 0x01),
    "1": (3, 0x02), "4": (3, 0x04), "2": (4, 0x02), "5": (4, 0x04), "3": (5, 0x02),
}


def parse_script(text):
    steps = []
    for part in text.split(","):
        words = part.split()
        if not words:
            continue
        groups = {}
        for k in words[1:]:
            if k == "none":
                continue
            g, bit = KEYS[k]
            groups[g] = groups.get(g, 0) | bit
        steps.append((int(words[0]), groups, " ".join(words[1:]) or "none"))
    return steps


class Done(Exception):
    pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("script", nargs="?", default="3 none, 12 up, 10 right, 12 up")
    ap.add_argument("--shots", default=os.path.join(ROOT, "build", "shots"))
    ap.add_argument("--prefix", default="frame")
    ap.add_argument("--every", action="store_true", help="save every frame")
    args = ap.parse_args()
    os.makedirs(args.shots, exist_ok=True)

    m = Machine(os.path.join(ROOT, "bin", "DREAD.obj"), os.path.join(ROOT, "bin", "DREAD.map"))
    m.load_game_vars(ROOT)

    steps = parse_script(args.script)
    bounds = []
    total = 0
    for n, groups, label in steps:
        total += n
        bounds.append((total, groups, label))
    shots = []

    def keys_for(frame):
        for end, groups, _ in bounds:
            if frame < end:
                return groups
        return {6: 0x40}  # clear: quit once the script is over

    menu = {"n": 0, "start": None}

    def on_swap(mach):
        if not mach.in_game():               # title and difficulty pages
            mach.set_keys(mach.menu_keys(menu["n"]))
            menu["n"] += 1
            return
        if menu["start"] is None:
            menu["start"] = mach.frames - 1
        f = mach.frames - menu["start"]      # frames of play completed
        for i, (end, _, label) in enumerate(bounds):
            if f == end or args.every:
                path = os.path.join(args.shots, "%s_%03d.png" % (args.prefix, f))
                mach.screenshot().save(path)
                if f == end:
                    shots.append((path, label))
                break
        mach.set_keys(keys_for(f))

    m.on_swap = on_swap
    m.set_keys({})
    t0 = time.time()
    ret = m.call("_main")
    dt = time.time() - t0

    fc = m.frame_cycles[(menu["start"] or 0) + 1:] or m.frame_cycles
    avg = sum(fc) / len(fc)
    print("main returned %s after %d frames, %d instructions, %.1fs host time"
          % (ret, m.frames, m.cpu.instructions, dt))
    print("cycles/frame: avg %d  min %d  max %d  -> est. %.1f fps (%.1f ms)"
          % (avg, min(fc), max(fc), CPU_HZ / avg, avg / CPU_HZ * 1000))
    for path, label in shots:
        print("  shot:", os.path.relpath(path, ROOT), "-", label)


if __name__ == "__main__":
    main()
