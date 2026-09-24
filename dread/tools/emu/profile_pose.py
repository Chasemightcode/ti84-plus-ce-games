"""Cycles per function for _render_view at given poses.

    python tools/emu/profile_pose.py "x,y,deg[,label]" ...
"""
import bisect
import collections
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

from ce import CPU_HZ, Exit  # noqa: E402
from test_render import boot  # noqa: E402


def main():
    m = boot()
    sym, mem, cpu = m.symbols, m.bus.mem, m.cpu
    syms = sorted((a, n) for n, a in sym.items() if a >= 0xD1A000 or n.startswith(("_gfx", "_os")))
    addrs = [a for a, _ in syms]
    for arg in sys.argv[1:]:
        parts = arg.split(",")
        x, y, deg = float(parts[0]), float(parts[1]), float(parts[2])
        label = parts[3] if len(parts) > 3 else arg
        pl = sym["_player"]
        mem[pl:pl + 3] = int(x * 256).to_bytes(3, "little")
        mem[pl + 3:pl + 6] = int(y * 256).to_bytes(3, "little")
        mem[pl + 6:pl + 9] = (int(round(deg / 360 * 1024)) & 1023).to_bytes(3, "little")
        hist = collections.Counter()
        cpu.sp = 0xD1A87E
        cpu.push(0xFFFFF0)
        cpu.pc = sym["_render_view"]
        c0 = cpu.cycles
        try:
            while True:
                pc, cc = cpu.pc, cpu.cycles
                cpu.step()
                j = bisect.bisect_right(addrs, pc) - 1
                hist[syms[j][1]] += cpu.cycles - cc
        except Exit:
            pass
        tot = cpu.cycles - c0
        print("%-10s render_view %8d cycles (%.1f ms)" % (label, tot, tot / CPU_HZ * 1000))
        for n, c in hist.most_common(int(os.environ.get("TOP", "6"))):
            print("    %-30s %8d" % (n, c))
        m.screenshot(m.draw_base()).save(os.path.join(ROOT, "build", "shots", "prof_%s.png" % label))


if __name__ == "__main__":
    main()
