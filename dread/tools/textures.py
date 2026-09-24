"""Procedural 32x32 wall textures for DREAD.

Textures are authored directly in palette space: every generator returns a
ramp map R and a floating-point shade map S (0 = bright, 15 = black). The
final texel is R * 16 + dithered(S). Arrays are indexed [y][x].
"""

import numpy as np

from palette import ramp as RAMP

N = 32

# Order here defines texture ids (tile value in a map = id + 1).
TEXTURE_NAMES = [
    "tech_panel", "tech_computer", "tech_vent", "tech_light",
    "stone_gray", "stone_brown", "brick_red",
    "metal_plate", "metal_hazard", "metal_pipes",
    "hell_flesh", "hell_rock", "hell_bone", "hell_sigil",
    "door_tech", "door_red", "door_blue", "door_yellow", "door_jamb",
    "exit_off", "exit_on",
]

BAYER4 = (np.array([[0, 8, 2, 10],
                    [12, 4, 14, 6],
                    [3, 11, 1, 9],
                    [15, 7, 13, 5]]) + 0.5) / 16.0 - 0.5


# ---------------------------------------------------------------- helpers

def rng(seed):
    return np.random.default_rng(seed)


def value_noise(seed, cell):
    """Tileable value noise in [0,1] with the given lattice cell size."""
    g = N // cell
    r = rng(seed).random((g, g))
    ys, xs = np.mgrid[0:N, 0:N].astype(float)
    fx, fy = xs / cell, ys / cell
    x0, y0 = np.floor(fx).astype(int), np.floor(fy).astype(int)
    tx, ty = fx - x0, fy - y0
    tx = tx * tx * (3 - 2 * tx)
    ty = ty * ty * (3 - 2 * ty)
    x1, y1 = (x0 + 1) % g, (y0 + 1) % g
    x0, y0 = x0 % g, y0 % g
    a = r[y0, x0] * (1 - tx) + r[y0, x1] * tx
    b = r[y1, x0] * (1 - tx) + r[y1, x1] * tx
    return a * (1 - ty) + b * ty


def fbm(seed, cells=(16, 8, 4, 2), weights=(0.5, 0.25, 0.15, 0.10)):
    out = np.zeros((N, N))
    for i, (c, w) in enumerate(zip(cells, weights)):
        out += value_noise(seed + i * 101, c) * w
    return out / sum(weights)


def voronoi(seed, count):
    """Tileable Voronoi: returns (d1, d2, cell_id) arrays."""
    pts = rng(seed).random((count, 2)) * N
    ys, xs = np.mgrid[0:N, 0:N].astype(float)
    d = np.empty((count, N, N))
    for i, (px, py) in enumerate(pts):
        dx = np.abs(xs - px)
        dy = np.abs(ys - py)
        dx = np.minimum(dx, N - dx)
        dy = np.minimum(dy, N - dy)
        d[i] = np.sqrt(dx * dx + dy * dy)
    order = np.argsort(d, axis=0)
    ds = np.take_along_axis(d, order, axis=0)
    return ds[0], ds[1], order[0]


def blank(ramp_name, shade):
    return np.full((N, N), RAMP(ramp_name)), np.full((N, N), float(shade))


def bevel_rect(S, x0, y0, x1, y1, hi=-2.0, lo=2.5):
    """Raise a rectangle (inclusive coords): light top/left, dark bottom/right."""
    S[y0, x0:x1 + 1] += hi
    S[y0:y1 + 1, x0] += hi
    S[y1, x0:x1 + 1] += lo
    S[y0:y1 + 1, x1] += lo


def rivet(S, x, y):
    S[y, x] = 1
    S[y, x + 1] = 3
    S[y + 1, x] = 4
    S[y + 1, x + 1] = 9


def paint(R, S, mask, ramp_name, shade):
    R[mask] = RAMP(ramp_name)
    if np.isscalar(shade):
        S[mask] = shade
    else:
        S[mask] = shade[mask]


def rect_mask(x0, y0, x1, y1):
    m = np.zeros((N, N), bool)
    m[y0:y1 + 1, x0:x1 + 1] = True
    return m


def finalize(R, S, lo=0, hi=13, dither=True):
    """Quantize the shade field with ordered dithering into palette indices."""
    s = S.copy()
    if dither:
        s = s + np.tile(BAYER4, (N // 4, N // 4)) * 0.9
    s = np.clip(np.round(s), lo, hi).astype(int)
    out = R.astype(int) * 16 + s
    out[out == 0] = 1  # index 0 is reserved for transparency
    return out


# --------------------------------------------------------------- font 3x5

FONT3x5 = {
    "E": ["###", "#..", "##.", "#..", "###"],
    "X": ["#.#", "#.#", ".#.", "#.#", "#.#"],
    "I": ["###", ".#.", ".#.", ".#.", "###"],
    "T": ["###", ".#.", ".#.", ".#.", ".#."],
}


def text_mask(text, x, y):
    m = np.zeros((N, N), bool)
    for ch in text:
        glyph = FONT3x5[ch]
        for gy, row in enumerate(glyph):
            for gx, c in enumerate(row):
                if c == "#":
                    m[y + gy, x + gx] = True
        x += 4
    return m


# ------------------------------------------------------------- generators

def tech_panel():
    R, S = blank("steel", 5.0)
    S += (fbm(11) - 0.5) * 1.6
    S += np.linspace(-0.6, 0.6, N)[:, None]
    bevel_rect(S, 0, 0, 31, 15)
    bevel_rect(S, 0, 16, 31, 31)
    for (x, y) in [(2, 2), (28, 2), (2, 12), (28, 12), (2, 18), (28, 18), (2, 28), (28, 28)]:
        rivet(S, x, y)
    # Recessed light groove with a cyan strip and glow.
    S[5, 5:27] = 11
    S[10, 5:27] = 3
    paint(R, S, rect_mask(6, 6, 25, 9), "cyan", 6.0)
    paint(R, S, rect_mask(6, 7, 25, 8), "cyan", 0.0)
    S[7, 6:26] = np.where(np.arange(20) % 5 == 0, 2.0, 0.0)
    # Two slot vents on the lower panel.
    for x0 in (8, 19):
        for y in range(20, 28, 2):
            S[y, x0:x0 + 5] = 12
            S[y + 1, x0:x0 + 5] = 4
    return finalize(R, S)


def tech_computer():
    R, S = blank("steel", 5.5)
    S += (fbm(21) - 0.5) * 1.2
    bevel_rect(S, 0, 0, 31, 31)
    bevel_rect(S, 3, 3, 28, 20, hi=2.5, lo=-2.0)  # recessed screen bezel
    screen = rect_mask(4, 4, 27, 19)
    paint(R, S, screen, "cyan", 12.5)
    g = rng(22)
    for y in range(6, 19, 2):
        x = 6
        while x < 26:
            ln = int(g.integers(2, 7))
            ln = min(ln, 26 - x)
            ramp_name = "cyan" if g.random() < 0.7 else "green"
            R[y, x:x + ln] = RAMP(ramp_name)
            S[y, x:x + ln] = g.choice([1.0, 2.0, 3.0])
            x += ln + int(g.integers(1, 4))
    # Scanline glow near the top of the screen.
    S[4, 4:28] -= 1.5
    # Button strip.
    for i, name in enumerate(["red", "yellow", "green", "blue", "red", "cyan"]):
        x = 5 + i * 4
        paint(R, S, rect_mask(x, 23, x + 1, 24), name, 1.0)
        S[25, x:x + 2] = 10
    for y in range(27, 30):
        S[y, 5:27] = 9 if y % 2 else 6
    return finalize(R, S)


def tech_vent():
    R, S = blank("steel", 5.0)
    S += (fbm(31) - 0.5) * 1.0
    bevel_rect(S, 0, 0, 31, 31)
    for (x, y) in [(2, 2), (28, 2), (2, 28), (28, 28)]:
        rivet(S, x, y)
    for y in range(5, 27):
        k = (y - 5) % 4
        S[y, 5:27] = [3.0, 5.5, 7.5, 13.0][k]
    bevel_rect(S, 4, 4, 27, 27, hi=3.0, lo=-1.5)
    return finalize(R, S)


def tech_light():
    R, S = blank("steel", 5.5)
    S += (fbm(41) - 0.5) * 1.2
    bevel_rect(S, 0, 0, 31, 31)
    ys, xs = np.mgrid[0:N, 0:N]
    # Glow falls off with distance from the lamp.
    d = np.maximum(np.abs(xs - 15.5) - 4, 0) + np.maximum(np.abs(ys - 15.5) - 12, 0)
    S -= np.clip(3.0 - d * 0.6, 0, 3.0)
    lamp = rect_mask(11, 3, 20, 28)
    paint(R, S, lamp, "bone", 0.0)
    S[lamp] += (np.abs(xs[lamp] - 15.5) > 3) * 1.0
    # Protective cage bars.
    for y in range(5, 28, 4):
        paint(R, S, rect_mask(11, y, 20, y), "steel", 7.0)
    paint(R, S, rect_mask(15, 3, 16, 28), "steel", 6.0)
    bevel_rect(S, 10, 2, 21, 29, hi=-1.0, lo=3.0)
    return finalize(R, S)


def _brick_rows(seed, heights, min_w, max_w):
    """Yield (y0, y1, [(x0, w), ...]) block layouts that wrap horizontally."""
    g = rng(seed)
    y = 0
    rows = []
    for h in heights:
        off = int(g.integers(0, N))
        blocks = []
        x = 0
        while x < N:
            w = int(g.integers(min_w, max_w + 1))
            if N - x - w < min_w:
                w = N - x
            blocks.append(((x + off) % N, w))
            x += w
        rows.append((y, y + h - 1, blocks))
        y += h
    return rows


def stone_gray():
    R, S = blank("gray", 5.5)
    n = fbm(51)
    g = rng(52)
    S += (n - 0.5) * 3.0
    for (y0, y1, blocks) in _brick_rows(53, [11, 10, 11], 10, 18):
        for (x0, w) in blocks:
            tone = g.uniform(-1.2, 1.2)
            cols = [(x0 + i) % N for i in range(w)]
            for i, cx in enumerate(cols):
                S[y0:y1 + 1, cx] += tone
                S[y0, cx] -= 1.5
                S[y1, cx] = 12.0
                if i == 0:
                    S[y0:y1 + 1, cx] = 11.5
                elif i == 1:
                    S[y0:y1 + 1, cx] -= 1.0
    # A few cracks.
    for _ in range(3):
        x, y = int(g.integers(0, N)), int(g.integers(0, N))
        for _ in range(int(g.integers(4, 9))):
            S[y % N, x % N] += 3.0
            x += int(g.integers(-1, 2))
            y += 1
    return finalize(R, S)


def stone_brown():
    R, S = blank("brown", 5.0)
    d1, d2, cid = voronoi(61, 12)
    g = rng(62)
    tones = g.uniform(-1.0, 1.5, 12)
    ramps = np.where(g.random(12) < 0.3, RAMP("tan"), RAMP("brown"))
    n = fbm(63)
    edge = (d2 - d1) < 1.6
    S = 3.5 + d1 * 0.35 + tones[cid] + (n - 0.5) * 2.5
    R = ramps[cid]
    S[edge] = 12.5
    # Light from the top-left: darken pixels just below/right of an edge.
    shadow = np.roll(edge, 1, axis=0) | np.roll(edge, 1, axis=1)
    S[shadow & ~edge] += 1.5
    return finalize(np.array(R), S)


def brick_red():
    R, S = blank("rust", 5.0)
    g = rng(71)
    S += (fbm(72) - 0.5) * 1.8
    for row in range(4):
        y0 = row * 8
        off = 0 if row % 2 == 0 else 8
        for b in range(2):
            x0 = (off + b * 16) % N
            tone = g.uniform(-1.0, 1.2)
            for i in range(16):
                cx = (x0 + i) % N
                S[y0:y0 + 8, cx] += tone
                S[y0 + 1, cx] -= 1.0
            # Mortar: left column of each brick.
            paint(R, S, rect_mask(x0, y0, x0, y0 + 7), "gray", 8.5)
        paint(R, S, rect_mask(0, y0 + 7, 31, y0 + 7), "gray", 9.0)
        S[y0, :] += np.where(R[y0, :] == RAMP("gray"), 0, 0.5)
    return finalize(R, S)


def metal_plate():
    R, S = blank("steel", 5.5)
    S += (fbm(81) - 0.5) * 1.4
    ys, xs = np.mgrid[0:N, 0:N]
    # Diamond tread: small raised diagonal dashes in a staggered grid.
    u = (xs + ys) % 8
    v = (xs - ys) % 8
    raised = ((u == 0) | (u == 1)) & (v < 3)
    S[raised] -= 2.0
    S[np.roll(raised, 1, axis=0) & ~raised] += 1.5
    bevel_rect(S, 0, 0, 31, 31)
    for (x, y) in [(2, 2), (28, 2), (2, 28), (28, 28)]:
        rivet(S, x, y)
    # Thin rust streaks running down from the top rivets.
    g = rng(82)
    for x0 in (2, 3, 28, 29):
        length = int(g.integers(6, 16))
        for y in range(4, 4 + length):
            if g.random() < 0.8:
                R[y, x0] = RAMP("rust")
                S[y, x0] = 5.0 + (y - 4) * 0.35
    return finalize(R, S)


def metal_hazard():
    R, S = blank("steel", 5.0)
    S += (fbm(91) - 0.5) * 1.4
    bevel_rect(S, 0, 0, 31, 9)
    bevel_rect(S, 0, 22, 31, 31)
    for (x, y) in [(3, 3), (27, 3), (3, 25), (27, 25)]:
        rivet(S, x, y)
    ys, xs = np.mgrid[0:N, 0:N]
    band = (ys >= 11) & (ys <= 20)
    stripe = ((xs + ys) // 4) % 2 == 0
    paint(R, S, band & stripe, "yellow", 2.0)
    paint(R, S, band & ~stripe, "gray", 12.0)
    S[band] += (fbm(92)[band] - 0.5) * 1.5
    paint(R, S, rect_mask(0, 10, 31, 10), "steel", 10.0)
    paint(R, S, rect_mask(0, 21, 31, 21), "steel", 3.0)
    return finalize(R, S)


def metal_pipes():
    R, S = blank("steel", 9.5)
    S += (fbm(101) - 0.5) * 1.0
    profile = [6.5, 4.0, 2.0, 1.5, 3.0, 5.5, 8.0]
    for x0, name in ((2, "steel"), (12, "rust"), (22, "steel")):
        for i, s in enumerate(profile):
            R[:, x0 + i] = RAMP(name)
            S[:, x0 + i] = s + (fbm(102 + x0)[:, x0 + i] - 0.5) * 1.2
        S[:, x0 + 7] = 11.5  # cast shadow on the wall
    for y0 in (5, 21):
        paint(R, S, rect_mask(0, y0, 31, y0 + 2), "gray", 6.0)
        S[y0, :] = 3.5
        S[y0 + 2, :] = 9.0
    return finalize(R, S)


def hell_flesh():
    R, S = blank("flesh", 5.0)
    n1 = fbm(111)
    n2 = value_noise(112, 8)
    ridges = np.abs(n2 - 0.5) * 2  # 0 on the crease
    S = 3.5 + (n1 - 0.5) * 3.0 + (1 - ridges) ** 3 * 5.0
    g = rng(113)
    # Veins: meandering red lines with a dark rim.
    for _ in range(4):
        x, y = float(g.integers(0, N)), 0.0
        dx = g.uniform(-0.6, 0.6)
        for _ in range(N):
            ix, iy = int(x) % N, int(y) % N
            R[iy, ix] = RAMP("red")
            S[iy, ix] = 3.0
            S[iy, (ix + 1) % N] += 2.0
            x += dx + g.uniform(-0.8, 0.8)
            y += 1.0
    for _ in range(6):
        px, py = int(g.integers(0, N)), int(g.integers(0, N))
        S[py, px] = 12.0
        S[(py + 1) % N, px] = 10.0
    return finalize(R, S)


def hell_rock():
    R, S = blank("brown", 9.0)
    d1, d2, cid = voronoi(121, 9)
    n = fbm(122)
    S = 7.5 + d1 * 0.25 + (n - 0.5) * 3.0
    R = np.full((N, N), RAMP("brown"))
    crack = (d2 - d1) < 0.9
    glow = ((d2 - d1) < 2.0) & ~crack
    R[glow] = RAMP("red")
    S[glow] = 7.0 + (d2 - d1)[glow] * 1.2
    R[crack] = RAMP("orange")
    S[crack] = 0.5 + (n[crack] * 2.0)
    return finalize(R, S)


def hell_bone():
    R, S = blank("flesh", 11.0)
    S += (fbm(131) - 0.5) * 2.0
    ys, xs = np.mgrid[0:N, 0:N].astype(float)
    for cy in (0, 16):
        for cx in (0, 16):
            ox = cx + (4 if cy == 16 else 0)
            lx = (xs - ox) % N
            ly = ys - cy
            cranium = ((lx - 8) / 6.3) ** 2 + ((ly - 6.5) / 5.8) ** 2 <= 1
            jaw = (lx >= 5) & (lx <= 11) & (ly >= 10) & (ly <= 14)
            skull = (cranium | jaw) & (ly >= 0) & (ly < 16)
            R[skull] = RAMP("bone")
            shade = 2.0 + np.sqrt(((lx - 6) ** 2 + (ly - 3) ** 2)) * 0.45
            S[skull] = shade[skull]
            for ex in (5.5, 10.5):
                eye = ((lx - ex) ** 2 / 3.2 + (ly - 7.5) ** 2 / 2.4) <= 1
                R[eye & skull] = RAMP("flesh")
                S[eye & skull] = 13.0
            nose = (np.abs(lx - 8) <= (ly - 9.5)) & (ly <= 11) & (ly >= 9.5)
            S[nose & skull] = 12.0
            teeth = jaw & (ly >= 12) & (ly <= 13) & ((lx.astype(int) % 2) == 0)
            S[teeth & skull] = 9.0
            S[jaw & (ly == 14)] = 8.0
    return finalize(R, S)


def hell_sigil():
    R, S = blank("purple", 10.5)
    n = fbm(141)
    ys, xs = np.mgrid[0:N, 0:N].astype(float)
    marble = np.sin((xs + ys * 0.6) * 0.35 + n * 6.0)
    S = 10.5 + (n - 0.5) * 2.0 - np.clip(marble, 0.6, 1.0) * 3.0 + 1.8
    r = np.sqrt((xs - 15.5) ** 2 + (ys - 15.5) ** 2)
    glow = (r > 8) & (r < 13)
    R[glow] = RAMP("red")
    S[glow] = 8.0 + np.abs(r[glow] - 10.5) * 1.8
    ring = np.abs(r - 10.5) < 0.8
    R[ring] = RAMP("orange")
    S[ring] = 1.0
    # An almond eye with a slit pupil inside the ring.
    eye = (np.abs(xs - 15.5) / 7.0) ** 2 + (np.abs(ys - 15.5) / 3.6) ** 1.4 <= 1
    R[eye] = RAMP("red")
    S[eye] = 3.0
    pupil = eye & (np.abs(xs - 15.5) < 1.1) & (np.abs(ys - 15.5) < 3.2)
    R[pupil] = RAMP("gray")
    S[pupil] = 13.0
    for (x0, y0) in [(15, 1), (15, 27), (1, 15), (27, 15)]:
        m = rect_mask(x0, y0, x0 + 1, y0 + 3) if y0 in (1, 27) else rect_mask(x0, y0, x0 + 3, y0 + 1)
        R[m] = RAMP("orange")
        S[m] = 2.0
    return finalize(R, S)


def _door_base(seed):
    R, S = blank("steel", 5.0)
    S += (fbm(seed) - 0.5) * 1.2
    bevel_rect(S, 0, 0, 31, 31)
    for y in (8, 23):
        S[y, 3:29] = 10.0
        S[y + 1, 3:29] = 3.0
    # Chevron stripes on the leading edge.
    ys, xs = np.mgrid[0:N, 0:N]
    edge = (xs >= 1) & (xs <= 4) & (ys >= 1) & (ys <= 30)
    chev = ((ys + np.abs(xs - 2) * 2) // 3) % 2 == 0
    paint(R, S, edge & chev, "yellow", 2.5)
    paint(R, S, edge & ~chev, "gray", 12.0)
    for (x, y) in [(7, 3), (27, 3), (7, 27), (27, 27)]:
        rivet(S, x, y)
    return R, S


def door_tech():
    R, S = _door_base(151)
    # Small viewing window.
    paint(R, S, rect_mask(13, 2, 22, 6), "cyan", 11.0)
    S[2, 13:23] = 7.0
    bevel_rect(S, 12, 1, 23, 7, hi=2.0, lo=-1.5)
    # Emblem: a raised diamond.
    ys, xs = np.mgrid[0:N, 0:N]
    d = np.abs(xs - 17.5) + np.abs(ys - 16)
    S[(d >= 4) & (d < 5)] = 2.0
    S[(d >= 5) & (d < 6)] = 9.0
    return finalize(R, S)


def _key_door(seed, color):
    R, S = _door_base(seed)
    band = rect_mask(6, 12, 30, 19)
    paint(R, S, band, color, 1.5 if color == "yellow" else 3.0)
    S[12, 6:31] = 1.0
    S[19, 6:31] = 7.0
    # Card slot with a glowing light.
    paint(R, S, rect_mask(15, 14, 20, 17), "gray", 12.0)
    paint(R, S, rect_mask(16, 15, 19, 16), color, 0.0)
    return finalize(R, S)


def door_red():
    return _key_door(161, "red")


def door_blue():
    return _key_door(171, "blue")


def door_yellow():
    return _key_door(181, "yellow")


def door_jamb():
    R, S = blank("steel", 6.0)
    S += (fbm(191) - 0.5) * 1.2
    S[:, 13] = 3.0
    S[:, 14:18] = 12.0
    S[:, 18] = 8.0
    for y in range(3, 32, 8):
        rivet(S, 5, y)
        rivet(S, 24, y)
    bevel_rect(S, 0, 0, 31, 31, hi=-1.0, lo=2.0)
    return finalize(R, S)


def _exit_panel(on):
    R, S = blank("steel", 5.5)
    S += (fbm(201) - 0.5) * 1.0
    bevel_rect(S, 0, 0, 31, 31)
    # Sign plate with lettering.
    paint(R, S, rect_mask(6, 3, 25, 11), "gray", 12.5)
    bevel_rect(S, 5, 2, 26, 12, hi=-1.0, lo=2.0)
    letters = text_mask("EXIT", 9, 5)
    paint(R, S, letters, "green" if on else "red", 0.0 if on else 1.5)
    # Lever housing.
    paint(R, S, rect_mask(11, 15, 20, 29), "steel", 9.0)
    bevel_rect(S, 11, 15, 20, 29, hi=2.0, lo=-1.0)
    if on:
        paint(R, S, rect_mask(14, 22, 17, 28), "steel", 2.0)
        paint(R, S, rect_mask(13, 26, 18, 28), "red", 2.0)
    else:
        paint(R, S, rect_mask(14, 16, 17, 22), "steel", 2.0)
        paint(R, S, rect_mask(13, 16, 18, 18), "red", 2.0)
    lamp = "green" if on else "red"
    paint(R, S, rect_mask(24, 21, 26, 23), lamp, 0.0 if on else 3.0)
    paint(R, S, rect_mask(5, 21, 7, 23), lamp, 0.0 if on else 3.0)
    return finalize(R, S)


def exit_off():
    return _exit_panel(False)


def exit_on():
    return _exit_panel(True)


def build_all():
    g = globals()
    return [g[name]() for name in TEXTURE_NAMES]
