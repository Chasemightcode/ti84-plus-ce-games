"""Address-level hotspot profile of _render_view for a few fixed poses.

    python tools/emu/hotspots.py [window_bytes] [top_n]
"""
import bisect
import collections
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

from ce import Exit  # noqa: E402
from test_render import boot  # noqa: E402

POSES = [(3 * 256 + 128, 5 * 256 + 128, 0), (8 * 256 + 100, 20 * 256, 300),
         (25 * 256, 33 * 256, 700), (40 * 256, 8 * 256, 512), (10 * 256, 14 * 256, 128)]


def main():
    win = int(sys.argv[1]) if len(sys.argv) > 1 else 32
    top = int(sys.argv[2]) if len(sys.argv) > 2 else 16
    m = boot()
    sym, mem, cpu = m.symbols, m.bus.mem, m.cpu
    hist = collections.Counter()
    for x8, y8, a in POSES:
        pl = sym["_player"]
        mem[pl:pl + 3] = x8.to_bytes(3, "little")
        mem[pl + 3:pl + 6] = y8.to_bytes(3, "little")
        mem[pl + 6:pl + 9] = a.to_bytes(3, "little")
        cpu.sp = 0xD1A87E
        cpu.push(0xFFFFF0)
        cpu.pc = sym["_render_view"]
        try:
            while True:
                pc, c0 = cpu.pc, cpu.cycles
                cpu.step()
                hist[pc] += cpu.cycles - c0
        except Exit:
            pass
    objdump = os.path.join(os.path.expanduser("~"), "CEdev", "binutils", "bin", "z80-none-elf-objdump.exe")
    dis = subprocess.run([objdump, "-d", os.path.join(ROOT, "bin", "DREAD.obj")], capture_output=True, text=True).stdout
    text = {}
    for line in dis.splitlines():
        mm = re.match(r"\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*(.*)", line)
        if mm:
            text[int(mm.group(1), 16)] = mm.group(2).strip()
    regions = collections.Counter()
    for a, c in hist.items():
        regions[a // win * win] += c
    total = sum(hist.values()) / len(POSES)
    print("render_view total %.0f cycles/pose" % total)
    for a, c in sorted(regions.items(), key=lambda kv: -kv[1])[:top]:
        ins = " | ".join(text[b] for b in range(a, a + win) if b in text)
        print("%06X %7d %4.1f%%  %s" % (a, c / len(POSES), 100 * c / len(POSES) / total, ins[:140]))


if __name__ == "__main__":
    main()
