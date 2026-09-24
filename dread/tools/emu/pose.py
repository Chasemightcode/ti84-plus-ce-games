"""Render specific poses from the real binary and save a contact sheet.

    python tools/emu/pose.py out.png "x,y,deg" ["x,y,deg" ...]

x, y in tiles; deg is the view direction (0 = east, 90 = south).
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from PIL import Image  # noqa: E402

from test_render import boot  # noqa: E402


def main():
    out = sys.argv[1]
    poses = [tuple(float(v) for v in p.split(",")) for p in sys.argv[2:]]
    m = boot()
    sym, mem = m.symbols, m.bus.mem
    shots = []
    for x, y, deg in poses:
        pl = sym["_player"]
        mem[pl:pl + 3] = int(x * 256).to_bytes(3, "little")
        mem[pl + 3:pl + 6] = int(y * 256).to_bytes(3, "little")
        mem[pl + 6:pl + 9] = (int(round(deg / 360 * 1024)) & 1023).to_bytes(3, "little")
        m.call("_render_view")
        shots.append(m.screenshot(m.draw_base()).crop((0, 0, 320, 200)))
    cols = 2 if len(shots) > 1 else 1
    rows = (len(shots) + cols - 1) // cols
    sheet = Image.new("RGB", (320 * cols, 200 * rows))
    for i, im in enumerate(shots):
        sheet.paste(im, ((i % cols) * 320, (i // cols) * 200))
    sheet.save(out)


if __name__ == "__main__":
    main()
