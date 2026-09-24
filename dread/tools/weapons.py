"""First-person weapon sprites, authored at the game's logical resolution
(each texel is drawn as a 2x2 block over the 160x100 view). 0 = transparent.

Frames are placed with their bottom edge on the bottom of the view and
centered horizontally (plus a per-frame x offset from WEAPON_FRAMES).
"""

import math

import numpy as np

import palette as P


def idx(r, s):
    return P.idx(r, max(0, min(15, int(round(s)))))


class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.a = np.zeros((h, w), dtype=int)

    def px(self, x, y, ramp, shade):
        x, y = int(round(x)), int(round(y))
        if 0 <= x < self.w and 0 <= y < self.h:
            self.a[y, x] = idx(ramp, shade)

    def rect(self, x0, y0, x1, y1, ramp, shade, lit=0.3):
        for y in range(int(y0), int(y1) + 1):
            for x in range(int(x0), int(x1) + 1):
                s = shade(x, y) if callable(shade) else shade + (x - x0) * lit
                self.px(x, y, ramp, s)

    def ellipse(self, cx, cy, rx, ry, ramp, base):
        for y in range(self.h):
            for x in range(self.w):
                dx, dy = (x + 0.5 - cx) / rx, (y + 0.5 - cy) / ry
                if dx * dx + dy * dy <= 1:
                    self.px(x, y, ramp, base + (dx + 0.5) * 1.5 + max(0, dy))

    def poly(self, pts, ramp, shade):
        """Filled polygon (even-odd), lit from the left."""
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        for y in range(int(min(ys)), int(max(ys)) + 1):
            yc = y + 0.5
            nodes = []
            j = len(pts) - 1
            for i in range(len(pts)):
                (xi, yi), (xj, yj) = pts[i], pts[j]
                if (yi < yc) != (yj < yc):
                    nodes.append(xi + (yc - yi) / (yj - yi) * (xj - xi))
                j = i
            nodes.sort()
            for k in range(0, len(nodes) - 1, 2):
                for x in range(int(round(nodes[k])), int(round(nodes[k + 1]))):
                    s = shade(x, y) if callable(shade) else shade + (x - min(xs)) * 0.15
                    self.px(x, y, ramp, s)

    def outline(self):
        solid = self.a != 0
        edge = np.zeros_like(solid)
        edge[1:, :] |= solid[:-1, :]
        edge[:-1, :] |= solid[1:, :]
        edge[:, 1:] |= solid[:, :-1]
        edge[:, :-1] |= solid[:, 1:]
        edge &= ~solid
        edge[-1, :] = False          # the bottom edge meets the screen edge
        self.a[edge] = idx("gray", 14)
        return self.a.copy()


def flash(c, cx, cy, size):
    for r, ramp, s in ((size, "orange", 1), (size * 0.65, "yellow", 0), (size * 0.3, "bone", 0)):
        for y in range(c.h):
            for x in range(c.w):
                dx, dy = abs(x + 0.5 - cx), abs(y + 0.5 - cy)
                # a round core with four rays
                star = dx * dy * 2.2 + (dx * dx + dy * dy) * 0.12 <= r * r * 0.12 + 1 and dx + dy <= r * 1.4
                core = dx * dx + dy * dy <= (r * 0.5) ** 2
                if star or core:
                    c.px(x, y, ramp, s)


# ------------------------------------------------------------------ fists

def _fist(c, cx, cy, s, side=1):
    """A gloved fist seen from behind, knuckles up, forearm down to the
    screen edge. side = 1 for the right hand (thumb on its left)."""
    w = 9 * s
    # forearm and sleeve
    c.poly([(cx - w * 0.45, c.h), (cx - w * 0.42, cy + 5 * s), (cx + w * 0.42, cy + 5 * s),
            (cx + w * 0.55, c.h)], "skin", 6)
    c.poly([(cx - w * 0.55, c.h), (cx - w * 0.5, cy + 9 * s), (cx + w * 0.5, cy + 9 * s),
            (cx + w * 0.65, c.h)], "olive", 6)
    # back of the hand
    c.rect(cx - w * 0.5, cy - 1 * s, cx + w * 0.5, cy + 6 * s, "brown",
           lambda x, y: 4 + (x - cx + w * 0.5) * 0.25 / s + (y - cy) * 0.15)
    # four finger rolls across the top
    for k in range(4):
        fx = cx - w * 0.5 + (k + 0.5) * w / 4
        c.ellipse(fx, cy - 1 * s, w / 8 + 0.4, 2.2 * s, "brown", 3)
        c.px(fx, cy - 2.4 * s, "brown", 1)
    # thumb across the front
    tx = cx - side * w * 0.15
    c.rect(tx - w * 0.3, cy + 2.5 * s, tx + w * 0.3, cy + 4.3 * s, "brown", 3)
    c.rect(tx - w * 0.3, cy + 4.3 * s, tx + w * 0.3, cy + 4.3 * s, "brown", 8)


def fists_idle():
    c = Canvas(96, 30)
    _fist(c, 16, 12, 2.0, side=-1)
    _fist(c, 80, 12, 2.0, side=1)
    return c.outline()


def fists_punch():
    c = Canvas(96, 46)
    _fist(c, 14, 30, 1.8, side=-1)
    _fist(c, 54, 9, 2.8, side=1)              # thrown forward toward the center
    return c.outline()


# ----------------------------------------------------------------- pistol

def _pistol(c, cx, top, recoil=0):
    """Sidearm seen from behind and above: a slide that widens toward the
    viewer, sights, frame, and a gloved hand wrapped around the grip."""
    t = top + recoil
    # slide: trapezoid, lit ridge down the middle
    def slide(x, y):
        return 3 + abs(x - cx) * 0.45 + (y - t) * 0.12
    c.poly([(cx - 6, t), (cx + 6, t), (cx + 8, t + 12), (cx - 8, t + 12)], "steel", slide)
    c.rect(cx - 1, t + 1, cx, t + 11, "steel", 1, lit=0)            # top ridge highlight
    c.rect(cx - 2, t - 3, cx + 1, t - 1, "steel", 2)                # front sight
    c.rect(cx - 7, t + 9, cx - 5, t + 11, "steel", 5)               # rear sight
    c.rect(cx + 4, t + 9, cx + 6, t + 11, "steel", 5)
    c.rect(cx - 2, t + 2, cx + 1, t + 4, "gray", 13)                # muzzle bore
    for y in range(t + 5, t + 9, 2):                                # grip serrations
        c.rect(cx - 7, y, cx - 6, y, "steel", 9)
        c.rect(cx + 5, y, cx + 6, y, "steel", 9)
    # frame and trigger guard
    c.rect(cx - 8, t + 13, cx + 8, t + 17, "gray", 9)
    # gloved hand: palm under the grip, fingers wrapped on each side
    c.poly([(cx - 9, t + 17), (cx + 9, t + 17), (cx + 12, c.h), (cx - 12, c.h)], "brown",
           lambda x, y: 5 + abs(x - cx) * 0.25)
    for k in range(3):
        c.rect(cx - 12, t + 19 + k * 3, cx - 9, t + 20 + k * 3, "brown", 3)   # fingers
        c.rect(cx + 9, t + 19 + k * 3, cx + 12, t + 20 + k * 3, "brown", 6)
    c.rect(cx - 5, t + 15, cx + 4, t + 17, "brown", 3)             # thumb over the top
    c.poly([(cx - 13, c.h - 6), (cx + 13, c.h - 6), (cx + 14, c.h), (cx - 14, c.h)],
           "olive", 6)                                              # sleeve


def pistol_idle():
    c = Canvas(40, 48)
    _pistol(c, 20, 12)
    return c.outline()


def pistol_fire():
    # taller canvas: frames are bottom-aligned, so the extra rows hold the flash
    c = Canvas(44, 64)
    _pistol(c, 22, 30)
    flash(c, 22, 15, 16)
    return c.outline()


def pistol_recoil():
    c = Canvas(40, 48)
    _pistol(c, 20, 9)
    return c.outline()


def _hands(c, cx, y, spread, sleeve=True):
    """Gloved hands gripping from both sides, forearms down to the edge."""
    for side in (-1, 1):
        hx = cx + side * spread
        c.poly([(hx - 5, y), (hx + 5, y), (hx + 7 + side * 3, c.h), (hx - 7 + side * 3, c.h)],
               "brown", lambda x, yy, hx=hx: 4 + abs(x - hx) * 0.3 + (yy - y) * 0.04)
        for k in range(3):
            c.rect(hx - 5, y + 2 + k * 3, hx + 5, y + 2 + k * 3, "brown", 7, lit=0)
        if sleeve:
            c.poly([(hx - 8 + side * 3, c.h - 7), (hx + 8 + side * 3, c.h - 7),
                    (hx + 10 + side * 4, c.h), (hx - 10 + side * 4, c.h)], "olive", 6)


# ------------------------------------------------------------- scattergun

def _scattergun(c, cx, top, pump=0):
    """Pump gun seen from behind: a long barrel narrowing into the screen,
    a wooden forend under it (slid back by pump px), steel receiver."""
    t = top

    def barrel(x, y):
        return 3 + abs(x - cx) * 0.35 + (y - t) * 0.05
    c.poly([(cx - 3, t), (cx + 3, t), (cx + 7, t + 30), (cx - 7, t + 30)], "steel", barrel)
    c.rect(cx - 1, t + 2, cx, t + 28, "steel", 1, lit=0)            # highlight
    c.rect(cx - 2, t - 1, cx + 1, t + 1, "gray", 13)                 # bore
    c.rect(cx - 1, t - 3, cx, t - 2, "steel", 2)                     # bead sight
    f = t + 12 + pump                                               # forend
    c.poly([(cx - 7, f), (cx + 7, f), (cx + 9, f + 12), (cx - 9, f + 12)], "brown",
           lambda x, y: 3 + abs(x - cx) * 0.3)
    for y in range(f + 2, f + 12, 3):
        c.rect(cx - 7, y, cx + 7, y, "brown", 8, lit=0)            # grooves
    c.rect(cx - 9, t + 30, cx + 9, t + 38, "steel",
           lambda x, y: 5 + abs(x - cx) * 0.25 + (y - t - 30) * 0.2)  # receiver
    c.rect(cx - 5, t + 32, cx + 4, t + 33, "gray", 12, lit=0)      # ejection port
    _hands(c, cx, t + 36, 9)


def shotgun_idle():
    c = Canvas(64, 58)
    _scattergun(c, 32, 10)
    return c.outline()


def shotgun_fire():
    c = Canvas(64, 72)
    _scattergun(c, 32, 26)
    flash(c, 32, 13, 20)
    return c.outline()


def shotgun_pump():
    c = Canvas(64, 58)
    _scattergun(c, 32, 13, pump=7)
    return c.outline()


# ---------------------------------------------------------- rotary cannon

def _cannon(c, cx, top, turn=0):
    """Six-barrel rotary gun seen from behind: separate barrels converging
    into the screen around a dark core, bores on a front ring; turn = 0/1
    rotates the cluster half a barrel."""
    t = top
    c.poly([(cx - 7, t), (cx + 7, t), (cx + 11, t + 24), (cx - 11, t + 24)], "gray", 13)
    # barrels at angles around the axis; the ones facing the viewer last
    barrels = []
    for k in range(6):
        a = (k + turn * 0.5) * math.pi / 3
        barrels.append((math.sin(a), math.cos(a)))
    for u, depth in sorted(barrels, key=lambda b: b[1]):
        if depth < -0.2:
            continue
        xt, xb = cx + u * 6, cx + u * 9.5
        wt, wb = 1.3 + depth * 0.6, 1.9 + depth * 0.9
        base = 6 - depth * 3 + u * 1.5
        c.poly([(xt - wt, t + 1), (xt + wt, t + 1), (xb + wb, t + 24), (xb - wb, t + 24)],
               "steel", lambda x, y, b=base, xt=xt: b + abs(x - xt) * 0.2)
    c.rect(cx - 10, t + 13, cx + 10, t + 15, "steel", 8, lit=0)     # clamp band
    for k in range(6):                                               # front ring bores
        a = (k + turn * 0.5) * math.pi / 3
        c.rect(cx + math.sin(a) * 5 - 0.5, t - 1 + math.cos(a) * 1.5,
               cx + math.sin(a) * 5 + 0.5, t + math.cos(a) * 1.5, "gray", 14, lit=0)
    c.rect(cx - 16, t + 24, cx + 16, t + 36, "gray",
           lambda x, y: 6 + abs(x - cx) * 0.2 + (y - t - 24) * 0.15)   # housing
    c.rect(cx - 12, t + 27, cx + 11, t + 28, "gray", 11, lit=0)
    c.rect(cx - 15, t + 26, cx - 11, t + 34, "yellow",
           lambda x, y: 3 + (y % 2) * 3)                              # ammo belt
    _hands(c, cx, t + 34, 14)


def cannon_idle():
    c = Canvas(72, 58)
    _cannon(c, 36, 10)
    return c.outline()


def cannon_fire0():
    c = Canvas(72, 72)
    _cannon(c, 36, 24)
    flash(c, 36, 12, 17)
    return c.outline()


def cannon_fire1():
    c = Canvas(72, 72)
    _cannon(c, 36, 25, turn=1)
    flash(c, 36, 14, 13)
    return c.outline()


# ----------------------------------------------------------------- launcher

def _launcher(c, cx, top, loaded=True):
    """Shoulder tube seen from behind: a wide olive tube narrowing into the
    screen with steel bands, the rocket's red tip in the bore."""
    t = top

    def tube(x, y):
        return 3 + abs(x - cx) * 0.3 + (y - t) * 0.03
    c.poly([(cx - 9, t), (cx + 9, t), (cx + 14, t + 32), (cx - 14, t + 32)], "olive", tube)
    for by in (t + 3, t + 20):
        w = 9 + (by - t) * 5 / 32
        c.rect(cx - w, by, cx + w, by + 2, "steel", 5, lit=0)
    c.ellipse(cx, t + 1, 8, 3.5, "gray", 11)                          # bore
    if loaded:
        c.ellipse(cx, t + 1.5, 4, 2, "red", 3)                        # warhead
    c.rect(cx + 12, t + 10, cx + 16, t + 16, "steel", 6)              # sight box
    c.rect(cx - 16, t + 32, cx + 16, t + 38, "olive",
           lambda x, y: 7 + abs(x - cx) * 0.2)                        # grip housing
    _hands(c, cx, t + 36, 13)


def launcher_idle():
    c = Canvas(68, 58)
    _launcher(c, 34, 10)
    return c.outline()


def launcher_fire():
    c = Canvas(68, 72)
    _launcher(c, 34, 24, loaded=False)
    flash(c, 34, 12, 18)
    return c.outline()


def launcher_recoil():
    c = Canvas(68, 58)
    _launcher(c, 34, 15, loaded=False)
    return c.outline()


# (name, function, x offset from center in logical px)
WEAPON_FRAMES = [
    ("fists_idle", fists_idle, 0),
    ("fists_punch", fists_punch, 0),
    ("pistol_idle", pistol_idle, 0),
    ("pistol_fire", pistol_fire, 0),
    ("pistol_recoil", pistol_recoil, 0),
    ("shotgun_idle", shotgun_idle, 0),
    ("shotgun_fire", shotgun_fire, 0),
    ("shotgun_pump", shotgun_pump, 0),
    ("cannon_idle", cannon_idle, 0),
    ("cannon_fire0", cannon_fire0, 0),
    ("cannon_fire1", cannon_fire1, 0),
    ("launcher_idle", launcher_idle, 0),
    ("launcher_fire", launcher_fire, 0),
    ("launcher_recoil", launcher_recoil, 0),
]


def build_all():
    return [(name, f(), dx) for name, f, dx in WEAPON_FRAMES]
