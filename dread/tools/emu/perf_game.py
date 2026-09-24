"""Frame cost of the real main loop in a live fight.

    python tools/emu/perf_game.py [x,y,deg] [frames] [weapon 1-5]

Boots DREAD, gives the player every weapon, selects one (default 2, the
pistol), puts the player at (x, y) facing deg, then runs the given number of
frames holding 2nd (which wakes every monster in earshot, and the player is
kept alive) and reports cycles per frame, the estimated frame rate and the
most expensive functions.
"""

import bisect
import collections
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

from ce import CPU_HZ, Exit, Machine  # noqa: E402
from run import KEYS  # noqa: E402


class Done(Exception):
    pass


def main():
    pose = sys.argv[1] if len(sys.argv) > 1 else "40.5,7.5,180"
    frames = int(sys.argv[2]) if len(sys.argv) > 2 else 60
    weapon = sys.argv[3] if len(sys.argv) > 3 else "2"
    warm = 18                            # frames to switch weapons
    x, y, deg = (float(v) for v in pose.split(","))
    m = Machine(os.path.join(ROOT, "bin", "DREAD.obj"), os.path.join(ROOT, "bin", "DREAD.map"))
    m.load_game_vars(ROOT)
    sym, mem, cpu = m.symbols, m.bus.mem, m.cpu
    pl = sym["_player"]

    def place():
        mem[pl:pl + 3] = int(x * 256).to_bytes(3, "little")
        mem[pl + 3:pl + 6] = int(y * 256).to_bytes(3, "little")
        mem[pl + 6:pl + 9] = (int(round(deg / 360 * 1024)) & 1023).to_bytes(3, "little")
        mem[pl + 10:pl + 13] = (100).to_bytes(3, "little")      # health
        mem[pl + 26] = 0x1F                                     # every weapon
        for k, v in enumerate((200, 50, 50)):
            mem[pl + 17 + 3 * k:pl + 20 + 3 * k] = v.to_bytes(3, "little")

    fire = {KEYS["2nd"][0]: KEYS["2nd"][1]}
    select = {KEYS[weapon][0]: KEYS[weapon][1]}
    state = {"n": 0, "start": 0}

    menu = {"n": 0}

    def on_swap(mach):
        if not mach.in_game():
            mach.set_keys(mach.menu_keys(menu["n"]))
            menu["n"] += 1
            return
        state["n"] += 1
        n = state["n"]
        place()
        if n == warm:
            state["start"] = len(mach.frame_cycles)
        if n >= warm + frames:
            raise Done()
        mach.set_keys(select if n == 2 else fire if n >= warm - 1 else {})

    m.on_swap = on_swap
    m.set_keys({})
    syms = sorted((a, n) for n, a in sym.items() if a >= 0xD1A000)
    addrs = [a for a, _ in syms]
    hist = collections.Counter()
    helpers = {sym[h]: h for h in os.environ.get("CALLERS", "").split(",") if h in sym}
    callers = collections.Counter()
    counting = False
    cpu.sp = 0xD1A87E
    cpu.push(0xFFFFF0)
    cpu.pc = sym["_main"]
    cpu.iy = 0xD00080
    try:
        while True:
            if not counting and state["n"] >= warm:
                counting = True
            pc, cc = cpu.pc, cpu.cycles
            if counting and pc in helpers:
                ret = cpu.r24(cpu.sp)
                j = bisect.bisect_right(addrs, ret) - 1
                callers[(helpers[pc], syms[j][1] if j >= 0 else "?")] += 1
            cpu.step()
            if counting:
                j = bisect.bisect_right(addrs, pc) - 1
                hist[syms[j][1] if j >= 0 else "?"] += cpu.cycles - cc
    except (Done, Exit):
        pass
    fc = m.frame_cycles[state["start"] + 1:]
    avg = sum(fc) / max(1, len(fc))
    awake = 0
    for i in range(mem[sym["_num_things"]]):
        a = sym["_things"] + 32 * i
        if 23 <= mem[a + 6] <= 26 and mem[a + 9] not in (0, 0xFF):
            awake += 1
    print("pose %s: %d frames, avg %d cycles (%.1f fps), max %d; %d monsters awake"
          % (pose, len(fc), avg, CPU_HZ / avg, max(fc), awake))
    tot = sum(hist.values())
    for n, c in hist.most_common(int(os.environ.get("TOP", "14"))):
        print("    %-34s %8d  %4.1f%%" % (n, c // max(1, len(fc)), 100.0 * c / tot))
    if callers:
        print("  calls per frame (CALLERS):")
        for (h, c), n in callers.most_common(16):
            print("    %-16s from %-26s %6.1f" % (h, c, n / max(1, len(fc))))
    m.screenshot().save(os.path.join(ROOT, "build", "shots", "perf.png"))


if __name__ == "__main__":
    main()
