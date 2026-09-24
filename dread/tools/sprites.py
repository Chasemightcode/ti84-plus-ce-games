"""Procedural 32x32 world sprites for DREAD (pickups and decorations).

Sprites are authored in palette space like the textures: arrays of palette
indices, [y][x], with 0 meaning transparent. Objects rest on the bottom row,
which the renderer places on the floor.
"""

import numpy as np

import palette as P

N = 32


def idx(r, s):
    return P.idx(r, s)


class Canvas:
    def __init__(self):
        self.a = np.zeros((N, N), dtype=int)

    def rect(self, x0, y0, x1, y1, ramp, shade):
        """Filled rectangle, inclusive; shade may be a function (x, y) -> s."""
        for y in range(max(0, y0), min(N, y1 + 1)):
            for x in range(max(0, x0), min(N, x1 + 1)):
                s = shade(x, y) if callable(shade) else shade
                self.a[y, x] = idx(ramp, s)

    def ellipse(self, cx, cy, rx, ry, ramp, shade):
        for y in range(N):
            for x in range(N):
                dx, dy = (x + 0.5 - cx) / rx, (y + 0.5 - cy) / ry
                if dx * dx + dy * dy <= 1.0:
                    s = shade(x, y, dx, dy) if callable(shade) else shade
                    self.a[y, x] = idx(ramp, s)

    def px(self, x, y, ramp, shade):
        if 0 <= x < N and 0 <= y < N:
            self.a[y, x] = idx(ramp, shade)

    def hline(self, x0, x1, y, ramp, shade):
        for x in range(x0, x1 + 1):
            self.px(x, y, ramp, shade)

    def vline(self, x, y0, y1, ramp, shade):
        for y in range(y0, y1 + 1):
            self.px(x, y, ramp, shade)

    def outline(self, ramp="gray", shade=14):
        """Dark 1-pixel outline around the shape for readability."""
        solid = self.a != 0
        edge = np.zeros_like(solid)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            edge |= np.roll(np.roll(solid, dy, axis=0), dx, axis=1)
        edge &= ~solid
        # rolls wrap around; never outline across the canvas edges
        edge[0, :] &= solid[1, :]
        self.a[edge] = idx(ramp, shade)
        return self

    def done(self):
        return self.a.copy()


def box_shade(x0, x1, y0, y1, base, top=-2, side=2):
    """Simple lit box: brighter top rows, darker right edge."""
    def f(x, y):
        s = base
        if y <= y0 + 1:
            s += top
        if x >= x1 - 1:
            s += side
        if x == x0:
            s -= 1
        return s
    return f


def cross(c, cx, cy, arm, ramp="red", shade=2):
    c.rect(cx - arm, cy - 1, cx + arm, cy, ramp, shade)
    c.rect(cx - 1, cy - arm, cx, cy + arm, ramp, shade)


# ------------------------------------------------------------------ pickups

def stim():
    c = Canvas()
    c.rect(11, 24, 20, 31, "bone", box_shade(11, 20, 24, 31, 3))
    cross(c, 16, 28, 2)
    return c.outline().done()


def medkit():
    c = Canvas()
    c.rect(13, 17, 18, 18, "steel", 5)          # handle
    c.rect(14, 18, 17, 18, "gray", 0) if False else None
    c.rect(7, 19, 24, 31, "bone", box_shade(7, 24, 19, 31, 3))
    c.hline(7, 24, 25, "bone", 7)
    cross(c, 16, 25, 3)
    return c.outline().done()


def _vest(c, ramp):
    shade = box_shade(8, 23, 12, 31, 3)
    c.rect(8, 14, 23, 31, ramp, shade)
    c.rect(10, 12, 13, 14, ramp, 2)            # shoulder straps
    c.rect(18, 12, 21, 14, ramp, 2)
    c.rect(14, 12, 17, 17, "gray", 0) if False else None
    for y in range(12, 18):                     # neck opening
        for x in range(14, 18):
            c.a[y, x] = 0
    c.hline(9, 22, 22, ramp, 7)                 # plate seams
    c.hline(9, 22, 27, ramp, 7)
    c.vline(15, 18, 31, ramp, 6)
    c.px(11, 19, "gray", 1)
    c.px(20, 19, "gray", 1)


def armor():
    c = Canvas()
    _vest(c, "green")
    return c.outline().done()


def heavy_armor():
    c = Canvas()
    _vest(c, "blue")
    c.rect(13, 23, 18, 26, "cyan", 1)           # power cell
    return c.outline().done()


def orb():
    c = Canvas()
    def sh(x, y, dx, dy):
        d = (dx + 0.45) ** 2 + (dy + 0.45) ** 2
        return 0.5 + d * 3.2
    c.ellipse(16, 20, 7.5, 7.5, "cyan", sh)
    c.ellipse(16, 20, 3.0, 3.0, "purple", lambda *a: 2)
    c.px(13, 16, "gray", 1)
    c.px(14, 16, "gray", 1)
    c.px(13, 17, "gray", 1)
    c.a[30:32, :] = 0
    c.ellipse(16, 31, 6, 1.2, "cyan", 11)       # glow on the floor
    return c.outline("purple", 13).done()


def clip():
    c = Canvas()
    c.rect(14, 22, 18, 31, "steel", box_shade(14, 18, 22, 31, 6))
    c.rect(15, 20, 17, 21, "yellow", 3)         # top round
    c.px(16, 19, "yellow", 1)
    return c.outline().done()


def bullet_box():
    c = Canvas()
    c.rect(8, 22, 24, 31, "olive", box_shade(8, 24, 22, 31, 4))
    for x in range(9, 24, 2):
        c.rect(x, 19, x, 21, "yellow", 2)
        c.px(x, 18, "yellow", 0)
    c.rect(11, 25, 21, 27, "tan", 3)            # label
    return c.outline().done()


def shells():
    c = Canvas()
    for i, x in enumerate((10, 13, 16, 19)):
        c.rect(x, 25, x + 1, 30, "red", 3 + (i & 1))
        c.rect(x, 30, x + 1, 31, "yellow", 2)
    return c.outline().done()


def shell_box():
    c = Canvas()
    c.rect(7, 21, 25, 31, "rust", box_shade(7, 25, 21, 31, 4))
    for x in range(9, 24, 3):
        c.rect(x, 18, x + 1, 20, "red", 2)
    c.rect(10, 24, 22, 27, "bone", 5)
    c.hline(11, 21, 25, "red", 3)
    return c.outline().done()


def rocket():
    c = Canvas()
    c.rect(14, 14, 17, 29, "olive", lambda x, y: 3 + (x - 14))
    c.rect(15, 11, 16, 13, "red", 2)            # warhead
    c.px(15, 10, "red", 1)
    c.rect(12, 27, 13, 31, "steel", 6)          # fins
    c.rect(18, 27, 19, 31, "steel", 6)
    c.rect(14, 30, 17, 31, "steel", 8)
    return c.outline().done()


def rocket_box():
    c = Canvas()
    c.rect(6, 20, 25, 31, "brown", box_shade(6, 25, 20, 31, 4))
    c.hline(6, 25, 25, "brown", 8)
    for x in (9, 14, 19, 23):
        c.rect(x, 16, x + 1, 19, "red", 2)
    c.rect(12, 27, 19, 29, "yellow", 3)         # stencil
    return c.outline().done()


def _keycard(color):
    c = Canvas()
    c.rect(11, 22, 21, 30, color, box_shade(11, 21, 22, 30, 3))
    c.rect(11, 24, 21, 25, "gray", 12)          # magnetic stripe
    c.rect(18, 27, 20, 29, "bone", 1)           # chip
    c.px(12, 22, "bone", 0)
    return c.outline().done()


def key_red():
    return _keycard("red")


def key_blue():
    return _keycard("blue")


def key_yellow():
    return _keycard("yellow")


def shotgun_pickup():
    c = Canvas()
    c.rect(4, 26, 27, 27, "steel", lambda x, y: 3 + (y - 26) * 3)   # barrel
    c.rect(4, 28, 12, 29, "steel", 6)                               # pump
    c.rect(19, 26, 27, 30, "brown", lambda x, y: 3 + (y - 26))      # stock
    c.rect(16, 28, 18, 30, "gray", 9)
    return c.outline().done()


def chaingun_pickup():
    c = Canvas()
    for i, y in enumerate((23, 25, 27)):
        c.rect(3, y, 16, y, "steel", 2 + i * 2)
    c.rect(15, 22, 26, 29, "steel", box_shade(15, 26, 22, 29, 5))
    c.rect(18, 29, 21, 31, "gray", 9)           # grip
    c.rect(24, 24, 28, 26, "olive", 4)
    return c.outline().done()


def launcher_pickup():
    c = Canvas()
    c.rect(3, 23, 28, 28, "olive", lambda x, y: 2 + abs(y - 25) * 2)
    c.rect(3, 23, 5, 28, "steel", 4)            # muzzle ring
    c.rect(14, 28, 17, 31, "gray", 9)           # grip
    c.rect(20, 20, 24, 22, "steel", 5)          # sight
    return c.outline().done()


# ------------------------------------------------------------- decorations

def lamp():
    c = Canvas()
    c.rect(15, 6, 16, 29, "steel", lambda x, y: 4 + (x - 15) * 3)
    c.rect(12, 29, 19, 31, "steel", 6)
    c.ellipse(16, 5, 4.5, 3.5, "yellow", lambda x, y, dx, dy: 0 + (dx * dx + dy * dy) * 2)
    c.rect(13, 7, 18, 8, "steel", 3)
    return c.outline().done()


def pillar():
    c = Canvas()
    c.rect(10, 4, 21, 31, "steel", lambda x, y: 3 + abs(x - 13) // 2)
    c.rect(9, 4, 22, 6, "steel", 2)
    c.rect(9, 29, 22, 31, "steel", 6)
    for y in (10, 16, 22):
        c.rect(12, y, 19, y + 1, "cyan", 1)
    return c.outline().done()


def barrel():
    c = Canvas()
    c.rect(9, 12, 22, 31, "green", lambda x, y: 3 + abs(x - 13) // 2)
    for y in (12, 20, 30):
        c.hline(9, 22, y, "steel", 5)
    c.ellipse(16, 12, 6.5, 1.8, "green", 7)
    c.rect(12, 14, 19, 18, "yellow", 2)         # hazard label
    c.px(15, 16, "gray", 13)
    c.px(16, 16, "gray", 13)
    return c.outline().done()


def blood():
    c = Canvas()
    c.ellipse(16, 30, 9, 2.2, "red", lambda x, y, dx, dy: 6 + (dx * dx) * 3)
    c.ellipse(9, 29.5, 2.5, 1.2, "red", 7)
    c.ellipse(24, 30.5, 2.0, 1.0, "red", 8)
    return c.done()


def bones():
    c = Canvas()
    c.ellipse(12, 26.5, 4, 3.5, "bone", lambda x, y, dx, dy: 2 + (dx + dy + 1))
    c.px(10, 26, "gray", 13)
    c.px(13, 26, "gray", 13)
    c.rect(11, 29, 13, 30, "bone", 4)
    c.rect(16, 29, 25, 30, "bone", 3)           # femur
    c.rect(15, 28, 16, 31, "bone", 2)
    c.rect(25, 28, 26, 31, "bone", 2)
    return c.outline().done()


def brazier():
    c = Canvas()
    c.rect(15, 18, 16, 29, "steel", 6)
    c.rect(12, 29, 19, 31, "steel", 7)
    c.rect(9, 14, 22, 17, "rust", lambda x, y: 3 + (y - 14))
    rng = np.random.default_rng(7)
    for x in range(10, 22):
        h = int(4 + 6 * np.exp(-((x - 15.5) / 3.5) ** 2) + rng.integers(0, 3))
        for k in range(h):
            y = 13 - k
            ramp = "yellow" if k < h * 0.4 else "orange" if k < h * 0.8 else "red"
            c.px(x, y, ramp, 0 if ramp == "yellow" else 1)
    return c.outline("red", 12).done()


# Order defines sprite ids (see things.py and src/gen/things.h).
SPRITE_NAMES = [
    "stim", "medkit", "armor", "heavy_armor", "orb",
    "clip", "bullet_box", "shells", "shell_box", "rocket", "rocket_box",
    "key_red", "key_blue", "key_yellow",
    "shotgun_pickup", "chaingun_pickup", "launcher_pickup",
    "lamp", "pillar", "barrel", "blood", "bones", "brazier",
]


def build_all():
    g = globals()
    out = []
    for name in SPRITE_NAMES:
        a = g[name]()
        assert a.shape == (N, N)
        out.append(a)
    return out
