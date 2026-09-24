"""Profile cycles per function over a few frames of a DREAD build.

    python tools/emu/profile.py [frames] [script]

Cycles spent in runtime helpers (__imulu etc.) are reported both on their
own and charged to the calling DREAD function ("incl" column).
"""

import bisect
import collections
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

from ce import CPU_HZ, Machine  # noqa: E402
from run import parse_script  # noqa: E402


def main():
    frames = int(sys.argv[1]) if len(sys.argv) > 1 else 2
    script = sys.argv[2] if len(sys.argv) > 2 else "100 none"
    m = Machine(os.path.join(ROOT, "bin", "DREAD.obj"), os.path.join(ROOT, "bin", "DREAD.map"))
    m.load_game_vars(ROOT)
    steps = parse_script(script)
    m.set_keys({})

    syms = sorted((a, n) for n, a in m.symbols.items() if a >= 0xD1A000)
    addrs = [a for a, _ in syms]
    cpu = m.cpu
    self_cyc = collections.Counter()
    incl_cyc = collections.Counter()
    state = {"profiling": False, "done": False, "menu": 0, "played": 0}

    def on_swap(mach):
        if not mach.in_game():               # title and difficulty pages
            mach.set_keys(mach.menu_keys(state["menu"]))
            state["menu"] += 1
            return
        state["played"] += 1
        if state["played"] == 1:
            state["profiling"] = True
            mach.set_keys(steps[0][1])
        elif state["played"] > frames:
            state["done"] = True

    m.on_swap = on_swap
    cpu.sp = 0xD1A87E
    cpu.push(0xFFFFF0)
    cpu.pc = m.symbols["_main"]
    stack_owner = []   # last non-helper function seen, approximated per step
    owner = "_main"
    while not state["done"]:
        pc = cpu.pc
        j = bisect.bisect_right(addrs, pc) - 1
        fn = syms[j][1] if j >= 0 else "?"
        before = cpu.cycles
        cpu.step()
        if state["profiling"]:
            d = cpu.cycles - before
            self_cyc[fn] += d
            if not fn.startswith("__") and fn not in ("_memcpy", "_atomic_load_32"):
                owner = fn
            incl_cyc[owner] += d
    total = sum(self_cyc.values())
    print("%d frames, %.0f cycles/frame -> %.1f fps" % (frames, total / frames, CPU_HZ * frames / total))
    print("\nself cycles per frame:")
    for fn, c in self_cyc.most_common(18):
        print("  %-34s %9d  %5.1f%%" % (fn, c / frames, 100.0 * c / total))
    print("\nincluding helpers (by calling function):")
    for fn, c in incl_cyc.most_common(10):
        print("  %-34s %9d  %5.1f%%" % (fn, c / frames, 100.0 * c / total))


if __name__ == "__main__":
    main()
