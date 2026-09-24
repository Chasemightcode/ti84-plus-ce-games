"""Generate the 16x16 Cesium icon (icon.png): a neon cube sliced in two."""
import struct
import zlib
from pathlib import Path

BG = (12, 4, 30)
PINK = (255, 30, 160)
PINK_TOP = (255, 130, 210)
PINK_SIDE = (130, 10, 80)
CYAN = (0, 220, 255)
CYAN_TOP = (140, 245, 255)
CYAN_SIDE = (0, 100, 140)
WHITE = (255, 255, 255)

px = [[BG] * 16 for _ in range(16)]


def put(x, y, c):
    if 0 <= x < 16 and 0 <= y < 16:
        px[y][x] = c


# left half (pink) of a cube, shifted up-left; right half (cyan) down-right
for y in range(4, 13):
    for x in range(2, 7):
        put(x, y - 1, PINK)
    for x in range(8, 13):
        put(x + 1, y + 1, CYAN)
for x in range(3, 7):
    put(x, 2, PINK_TOP)
for x in range(9, 14):
    put(x + 1, 4, CYAN_TOP)
for y in range(5, 14):
    put(14, y, CYAN_SIDE)
for y in range(3, 12):
    put(7, y, PINK_SIDE)
# slash
for i in range(16):
    put(i // 2 + 4, 15 - i, WHITE)


def png(pixels):
    raw = b"".join(b"\x00" + bytes(v for p in row for v in p) for row in pixels)

    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", 16, 16, 8, 2, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


out = Path(__file__).resolve().parent.parent / "icon.png"
out.write_bytes(png(px))
print("wrote", out)
