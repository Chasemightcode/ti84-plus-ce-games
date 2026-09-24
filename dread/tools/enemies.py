"""Procedural enemy sprites for DREAD (front views, 32x32, 0 = transparent).

Four creatures and the boss, each with the same 10-frame layout:
    0-2  walk (cycled 0,1,2,1)      3  attack windup     4  attack
    5    pain                        6-8 dying           9  corpse
plus effect sprites (fireball, bullet puff, blood, explosion, rocket).

Figures are assembled from simple parts (legs, torso, arms, head, gear)
whose pose changes per frame, then outlined for readability.
"""

import math

import numpy as np

import palette as P

N = 32
FRAMES = ["walk0", "walk1", "walk2", "windup", "attack", "pain",
          "die0", "die1", "die2", "corpse"]


def idx(r, s):
    return P.idx(r, max(0, min(15, int(round(s)))))


class Canvas:
    def __init__(self):
        self.a = np.zeros((N, N), dtype=int)

    def px(self, x, y, ramp, shade):
        x, y = int(round(x)), int(round(y))
        if 0 <= x < N and 0 <= y < N:
            self.a[y, x] = idx(ramp, shade)

    def rect(self, x0, y0, x1, y1, ramp, shade):
        """Inclusive rectangle; shade(x, y) or a number. Lit from the left."""
        x0, x1 = int(round(min(x0, x1))), int(round(max(x0, x1)))
        y0, y1 = int(round(min(y0, y1))), int(round(max(y0, y1)))
        for y in range(max(0, y0), min(N, y1 + 1)):
            for x in range(max(0, x0), min(N, x1 + 1)):
                s = shade(x, y) if callable(shade) else shade + (x - x0) * 0.35
                self.a[y, x] = idx(ramp, s)

    def ellipse(self, cx, cy, rx, ry, ramp, base):
        for y in range(N):
            for x in range(N):
                dx, dy = (x + 0.5 - cx) / rx, (y + 0.5 - cy) / ry
                d = dx * dx + dy * dy
                if d <= 1.0:
                    self.a[y, x] = idx(ramp, base + (dx + 0.4) * 1.6 + max(0, dy) * 1.2)

    def limb(self, x0, y0, x1, y1, w, ramp, base):
        """Thick line from (x0,y0) to (x1,y1)."""
        steps = int(max(abs(x1 - x0), abs(y1 - y0)) * 2) + 1
        for i in range(steps + 1):
            t = i / steps
            x, y = x0 + (x1 - x0) * t, y0 + (y1 - y0) * t
            for dx in range(-int(w // 2), int(w - w // 2)):
                self.px(x + dx, y, ramp, base + dx * 0.6)

    def outline(self, ramp="gray", shade=14):
        solid = self.a != 0
        edge = np.zeros_like(solid)
        h, w = solid.shape
        edge[1:, :] |= solid[:-1, :]
        edge[:-1, :] |= solid[1:, :]
        edge[:, 1:] |= solid[:, :-1]
        edge[:, :-1] |= solid[:, 1:]
        edge &= ~solid
        self.a[edge] = idx(ramp, shade)
        return self

    def blood(self, cx, cy, n, seed):
        r = np.random.default_rng(seed)
        for _ in range(n):
            self.px(cx + r.normal(0, 2.2), cy + r.normal(0, 1.8), "red", r.integers(2, 6))


# ---------------------------------------------------------------- figures

class Style:
    def __init__(self, **kw):
        self.__dict__.update(kw)


GRUNT = Style(
    name="grunt", height=27, width=1.0,
    legs="olive", leg_shade=6, torso="olive", torso_shade=4, arms="olive",
    skin="olive", skin_shade=1, head_r=3.3, helmet="steel", eyes="red",
    gun="steel", boots="brown")

HEAVY = Style(
    name="heavy", height=28, width=1.3,
    legs="steel", leg_shade=7, torso="steel", torso_shade=5, arms="steel",
    skin="skin", skin_shade=5, head_r=3.5, helmet="steel", eyes="red",
    visor=True, gun="gray", boots="gray")

IMP = Style(
    name="imp", height=25, width=1.05,
    legs="rust", leg_shade=5, torso="rust", torso_shade=3, arms="rust",
    skin="rust", skin_shade=3, head_r=3.4, horns="bone", eyes="orange",
    spikes=True, claws=True, boots=None)

BRUTE = Style(
    name="brute", height=29, width=1.7,
    legs="flesh", leg_shade=4, torso="flesh", torso_shade=2, arms="flesh",
    skin="flesh", skin_shade=2, head_r=4.2, horns="bone", eyes="yellow",
    jaw=True, claws=True, boots=None)


def figure(st, pose):
    """Draw a standing creature. pose keys: step (-1, 0, 1), arms
    ('carry', 'aim', 'fire', 'raise', 'throw', 'swing', 'strike', 'flail'),
    lean (px), squash (0..1 height loss), hurt (bool)."""
    c = Canvas()
    w = st.width
    step = pose.get("step", 0)
    lean = pose.get("lean", 0)
    squash = pose.get("squash", 0.0)
    h = st.height * (1 - squash)
    foot_y = 31
    hip_y = foot_y - h * 0.45
    shoulder_y = foot_y - h * 0.8
    cx = 16 + lean * 0.3
    top = foot_y - h

    # legs: the stepping leg's foot rises and the knee comes forward
    for side in (-1, 1):
        lift = 2.5 if step == side else 0
        hx = cx + side * 2.2 * w
        fx = hx + side * (0.8 if step == -side else 0.2)
        fy = foot_y - lift
        c.limb(hx, hip_y, fx, fy, 3.0 * min(w, 1.4), st.legs, st.leg_shade)
        if st.boots:
            c.rect(fx - 1.5 * min(w, 1.3), fy - 1, fx + 1.5 * min(w, 1.3), fy, st.boots, 7)
        elif getattr(st, "claws", False):
            c.px(fx - 1, fy, "bone", 3)
            c.px(fx + 1, fy, "bone", 3)

    # torso
    tw = 4.2 * w
    c.rect(cx - tw + lean * 0.3, shoulder_y, cx + tw + lean * 0.3, hip_y + 1, st.torso, st.torso_shade)
    if getattr(st, "visor", False) or st.name == "heavy":
        c.rect(cx - tw + 1 + lean * 0.3, shoulder_y + 2, cx + tw - 1 + lean * 0.3, shoulder_y + 3,
               "steel", 3)                          # chest plate ridge
    if st.name == "grunt":
        c.rect(cx - 1 + lean * 0.3, shoulder_y + 1, cx + lean * 0.3, hip_y, "olive", 8)  # webbing
        c.rect(cx - tw + lean * 0.3, hip_y - 1, cx + tw + lean * 0.3, hip_y, "brown", 7)  # belt
    if getattr(st, "spikes", False):
        for sx in (-tw + 1, 0, tw - 1):
            c.px(cx + sx + lean * 0.3, shoulder_y - 1, "bone", 2)
    if st.name == "brute":
        c.rect(cx - 2, shoulder_y + 3, cx + 2, hip_y - 2, "flesh", 5)                      # belly
        c.rect(cx - tw + lean * 0.3, hip_y - 1, cx + tw + lean * 0.3, hip_y + 1, "brown", 8)

    # arms
    sx_l, sx_r = cx - tw - 0.5 + lean * 0.3, cx + tw + 0.5 + lean * 0.3
    arms = pose.get("arms", "carry")
    aw = 2.5 * min(w, 1.5)
    hand = []
    if arms in ("carry", "aim", "fire"):
        # both hands meet in front of the chest holding a weapon
        hy = shoulder_y + (5 if arms == "carry" else 3)
        c.limb(sx_l, shoulder_y + 1, cx - 1, hy, aw, st.arms, st.torso_shade + 1)
        c.limb(sx_r, shoulder_y + 1, cx + 1, hy, aw, st.arms, st.torso_shade + 2)
        hand = [(cx, hy)]
    elif arms == "raise":                          # windup: one arm up and back
        c.limb(sx_l, shoulder_y + 1, sx_l - 2, shoulder_y + 7, aw, st.arms, st.torso_shade + 1)
        c.limb(sx_r, shoulder_y + 1, sx_r + 3, shoulder_y - 6, aw, st.arms, st.torso_shade + 2)
        hand = [(sx_r + 3, shoulder_y - 7)]
    elif arms == "throw":                          # arm swung forward and down
        c.limb(sx_l, shoulder_y + 1, sx_l - 2, shoulder_y + 7, aw, st.arms, st.torso_shade + 1)
        c.limb(sx_r, shoulder_y + 1, cx + 2, shoulder_y + 4, aw, st.arms, st.torso_shade + 2)
        hand = [(cx + 2, shoulder_y + 4)]
    elif arms == "swing":                          # brute windup: both arms up
        c.limb(sx_l, shoulder_y + 1, sx_l - 3, shoulder_y - 6, aw, st.arms, st.torso_shade + 1)
        c.limb(sx_r, shoulder_y + 1, sx_r + 3, shoulder_y - 6, aw, st.arms, st.torso_shade + 2)
        hand = [(sx_l - 3, shoulder_y - 7), (sx_r + 3, shoulder_y - 7)]
    elif arms == "strike":                         # both fists smash down in front
        c.limb(sx_l, shoulder_y + 1, cx - 3, shoulder_y + 8, aw, st.arms, st.torso_shade + 1)
        c.limb(sx_r, shoulder_y + 1, cx + 3, shoulder_y + 8, aw, st.arms, st.torso_shade + 2)
        hand = [(cx - 3, shoulder_y + 9), (cx + 3, shoulder_y + 9)]
    elif arms == "flail":                          # hit: arms thrown out
        c.limb(sx_l, shoulder_y + 1, sx_l - 4, shoulder_y - 2, aw, st.arms, st.torso_shade + 1)
        c.limb(sx_r, shoulder_y + 1, sx_r + 4, shoulder_y - 3, aw, st.arms, st.torso_shade + 2)
        hand = [(sx_l - 4, shoulder_y - 3), (sx_r + 4, shoulder_y - 4)]
    else:                                          # hanging at the sides
        c.limb(sx_l, shoulder_y + 1, sx_l - 1, hip_y + 2, aw, st.arms, st.torso_shade + 1)
        c.limb(sx_r, shoulder_y + 1, sx_r + 1, hip_y + 2, aw, st.arms, st.torso_shade + 2)
        hand = [(sx_l - 1, hip_y + 3), (sx_r + 1, hip_y + 3)]

    if getattr(st, "claws", False) and arms not in ("carry", "aim", "fire"):
        for hx, hy in hand:
            c.px(hx - 1, hy + 1, "bone", 2)
            c.px(hx + 1, hy + 1, "bone", 2)

    # weapon held in front (seen end-on: a dark muzzle over a body)
    if arms in ("carry", "aim", "fire") and getattr(st, "gun", None):
        gx, gy = hand[0]
        big = st.name == "heavy"
        c.rect(gx - (3 if big else 2), gy - 1, gx + (3 if big else 2), gy + (2 if big else 1), st.gun, 5)
        c.rect(gx - 1, gy - 1, gx + (1 if big else 0), gy, "gray", 13)       # muzzle
        if arms == "fire":
            for r, ramp, s in ((3.5, "orange", 1), (2.2, "yellow", 0)):
                for yy in range(-4, 5):
                    for xx in range(-4, 5):
                        if xx * xx + yy * yy <= r * r:
                            c.px(gx + xx, gy - 1 + yy, ramp, s)

    # head
    hr = st.head_r
    hy = shoulder_y - hr + 0.5
    hx = cx + lean * 0.6
    c.ellipse(hx, hy, hr, hr * 1.05, st.skin, st.skin_shade)
    if getattr(st, "helmet", None):
        for yy in range(int(hy - hr), int(hy - 0.3)):
            for xx in range(int(hx - hr - 0.5), int(hx + hr + 1)):
                if (xx + 0.5 - hx) ** 2 + (yy + 0.5 - hy) ** 2 <= (hr + 0.5) ** 2:
                    c.px(xx, yy, st.helmet, 5 + (xx - hx) * 0.4)
    if getattr(st, "horns", None):
        for side in (-1, 1):
            c.limb(hx + side * (hr - 1), hy - hr + 1, hx + side * (hr + 1.5), hy - hr - 2.5,
                   1.2, st.horns, 2)
    eye_y = hy + 0.2
    if pose.get("hurt"):
        c.px(hx - 1.3, eye_y, "gray", 13)
        c.px(hx + 1.3, eye_y, "gray", 13)
    elif getattr(st, "visor", False):
        c.rect(hx - hr + 1, eye_y - 0.5, hx + hr - 1, eye_y, "red", 1)
    else:
        c.px(hx - 1.3, eye_y, st.eyes, 0)
        c.px(hx + 1.3, eye_y, st.eyes, 0)
    if getattr(st, "jaw", False):
        c.rect(hx - 2, hy + 1.5, hx + 2, hy + 2.5, "gray", 13)                  # mouth
        c.px(hx - 1, hy + 1.5, "bone", 1)
        c.px(hx + 1, hy + 1.5, "bone", 1)
    if pose.get("mouth"):
        c.rect(hx - 1, hy + 1.5, hx + 1, hy + 3, "red", 8)

    if pose.get("hurt"):
        c.blood(cx, shoulder_y + 4, 10, len(st.name))
    return c


def dying(st, stage):
    """Collapse sequence: 0 recoil, 1 knees buckle, 2 falling."""
    if stage == 0:
        c = figure(st, {"arms": "flail", "lean": -4, "hurt": True, "squash": 0.05})
    elif stage == 1:
        c = figure(st, {"arms": "hang", "lean": -2, "hurt": True, "squash": 0.35})
    else:
        c = corpse(st, spread=0.6)
    c.blood(16, 25, 14 + stage * 6, 10 + stage)
    return c


def corpse(st, spread=1.0):
    """Body lying on the floor, seen from its feet: a low heap and blood."""
    c = Canvas()
    w = st.width
    body_w = 7 * w * (0.7 + 0.3 * spread)
    base_y = 31
    c.ellipse(16, base_y - 0.5, body_w + 3, 1.6 * spread + 0.6, "red", 6)       # pool
    c.rect(16 - body_w, base_y - 3, 16 + body_w, base_y - 1, st.torso, st.torso_shade + 2)
    c.ellipse(16 + body_w - 1, base_y - 3, st.head_r * 0.9, st.head_r * 0.7, st.skin, st.skin_shade + 2)
    c.rect(16 - body_w - 2, base_y - 2, 16 - body_w, base_y - 1, st.legs, st.leg_shade + 2)
    if getattr(st, "horns", None):
        c.px(16 + body_w + 1, base_y - 5, st.horns, 3)
    c.blood(16, base_y - 2, 18, 99 + len(st.name))
    return c


def creature_frames(st):
    attack_pose = {
        "grunt": ({"arms": "aim"}, {"arms": "fire"}),
        "heavy": ({"arms": "aim"}, {"arms": "fire"}),
        "imp": ({"arms": "raise"}, {"arms": "throw", "mouth": True}),
        "brute": ({"arms": "swing", "mouth": True}, {"arms": "strike", "lean": 2, "mouth": True}),
    }[st.name]
    carry = "carry" if getattr(st, "gun", None) else "hang"
    frames = [
        figure(st, {"step": -1, "arms": carry}),
        figure(st, {"step": 0, "arms": carry}),
        figure(st, {"step": 1, "arms": carry}),
        figure(st, attack_pose[0]),
        figure(st, attack_pose[1]),
        figure(st, {"arms": "flail", "hurt": True, "lean": -2}),
        dying(st, 0),
        dying(st, 1),
        dying(st, 2),
        corpse(st),
    ]
    out = []
    for f in frames:
        f.outline()
        out.append(f.a.copy())
    return out


# ------------------------------------------------------------------ boss

def warden(pose):
    """The Warden, the final boss: an armored giant with one steel and one
    flesh leg, a rocket cannon for a left arm, a clawed right arm and a
    small horned head with a glowing visor. Drawn at twice the size of the
    other creatures in the game. pose keys: step, cannon ('low', 'aim',
    'fire'), claw ('hang', 'raise'), lean, squash, hurt, sparks."""
    c = Canvas()
    step = pose.get("step", 0)
    lean = pose.get("lean", 0)
    h = 30 * (1 - pose.get("squash", 0.0))
    foot_y = 31
    hip_y = foot_y - h * 0.40
    sh_y = foot_y - h * 0.80
    cx = 16 + lean * 0.3
    # legs: steel on the viewer's left, flesh on the right
    for side, ramp, shade in ((-1, "steel", 6), (1, "purple", 5)):
        lift = 2 if step == side else 0
        hx = cx + side * 4.0
        fx = hx + side * 0.8
        fy = foot_y - lift
        c.limb(hx, hip_y, fx, fy - 1, 4.4, ramp, shade)
        c.rect(fx - 2.6, fy - 1.4, fx + 2.6, fy, "gray" if side < 0 else "bone",
               9 if side < 0 else 4)
        if side < 0:
            c.rect(hx - 2, hip_y + 4, hx + 2, hip_y + 5, "steel", 10)       # knee joint
    c.rect(cx - 6.5, hip_y - 2, cx + 6.5, hip_y + 1, "bone", 6)             # pelvis plate
    # torso: a broad chest over a narrower waist, bone ribs
    c.rect(cx - 5, (sh_y + hip_y) / 2, cx + 5, hip_y - 1, "purple", 4)
    c.ellipse(cx, sh_y + 3.2, 8.2, 5.2, "purple", 3)
    for k in range(3):
        y = sh_y + 3 + k * 2
        c.rect(cx - 5 + k, y, cx - 1, y, "bone", 3, )
        c.rect(cx + 1, y, cx + 5 - k, y, "bone", 4)
    c.rect(cx - 8.5, sh_y - 0.5, cx - 5, sh_y + 2, "steel", 4)             # pauldron
    c.rect(cx + 5, sh_y - 0.5, cx + 8.5, sh_y + 2, "bone", 3)
    # cannon arm (viewer's left): upper arm, then the barrel pointing at you
    cannon = pose.get("cannon", "low")
    ex, ey = cx - 9.5, sh_y + 6
    c.limb(cx - 7.5, sh_y + 1, ex, ey, 3.2, "steel", 5)
    my = ey + (4 if cannon == "low" else 0)
    c.rect(ex - 3, my - 3, ex + 3, my + 3, "steel", lambda x, y: 3 + (x - ex + 3) * 0.5)
    c.rect(ex - 3, my - 1, ex + 3, my - 1, "steel", 9)                      # band
    c.ellipse(ex, my + 0.5, 1.6, 1.6, "gray", 14)                           # muzzle
    if cannon == "aim":
        c.ellipse(ex, my + 0.5, 1.0, 1.0, "orange", 1)                      # charging
    if cannon == "fire":
        for r, ramp, sh in ((4.2, "orange", 1), (2.8, "yellow", 0), (1.3, "bone", 0)):
            c.ellipse(ex, my + 0.5, r, r, ramp, sh)
    # claw arm (viewer's right)
    claw = pose.get("claw", "hang")
    if claw == "raise":
        hx, hy = cx + 11, sh_y - 4
    else:
        hx, hy = cx + 10.5, hip_y + 1
    c.limb(cx + 7.5, sh_y + 1, hx, hy, 3.0, "purple", 5)
    for k in (-1, 0, 1):
        c.limb(hx + k * 1.2, hy + (1 if claw != "raise" else -1), hx + k * 1.8,
               hy + (3.5 if claw != "raise" else -3.5), 1, "bone", 2)
    # head: small, horned, one glowing visor slit
    hx, hy = cx + lean * 0.5, sh_y - 2.8
    c.ellipse(hx, hy, 3.2, 3.0, "purple", 2)
    for side in (-1, 1):
        c.limb(hx + side * 2.2, hy - 1.8, hx + side * 4.6, hy - 5.5, 1.3, "bone", 1)
    if pose.get("hurt"):
        c.rect(hx - 2, hy, hx + 2, hy, "gray", 13)
    else:
        c.rect(hx - 2, hy - 0.2, hx + 2, hy + 0.3, "yellow", 0)
    if pose.get("hurt"):
        c.blood(cx, sh_y + 5, 12, 77)
    rng = np.random.default_rng(31 + int(pose.get("squash", 0) * 10))
    for _ in range(pose.get("sparks", 0)):                  # dying: bursts of fire
        x, y = rng.integers(6, 26), rng.integers(4, 26)
        c.px(x, y, "yellow", 0)
        c.px(x + 1, y, "orange", 1)
        c.px(x, y + 1, "orange", 2)
    return c


def warden_wreck(stage):
    """Collapsed armor heap: stage 0 still smoking, 1 the corpse."""
    c = Canvas()
    base_y = 31
    c.ellipse(16, base_y - 0.5, 14, 2.2, "red", 6)                        # pool
    c.ellipse(15, base_y - 3, 10, 3.2, "purple", 5)                       # body
    c.rect(6, base_y - 5, 12, base_y - 1, "steel", 5)                     # cannon arm
    c.ellipse(9, base_y - 3, 1.4, 1.4, "gray", 14)
    for x in range(11, 21, 3):                                            # ribs
        c.rect(x, base_y - 5, x + 1, base_y - 4, "bone", 3)
    c.ellipse(23, base_y - 4, 2.6, 2.2, "purple", 3)                      # head
    c.limb(24, base_y - 6, 26, base_y - 9, 1.2, "bone", 2)
    c.blood(16, base_y - 3, 20, 123)
    if stage == 0:
        rng = np.random.default_rng(8)
        for _ in range(14):
            c.px(rng.normal(15, 4), rng.normal(base_y - 10, 3), "gray", 5 + rng.integers(0, 5))
    return c


def warden_frames():
    frames = [
        warden({"step": -1}),
        warden({"step": 0}),
        warden({"step": 1}),
        warden({"cannon": "aim", "claw": "raise"}),
        warden({"cannon": "fire", "claw": "raise", "lean": 1}),
        warden({"lean": -3, "hurt": True, "claw": "raise"}),
        warden({"lean": -4, "hurt": True, "sparks": 10, "claw": "raise", "cannon": "aim"}),
        warden({"squash": 0.35, "hurt": True, "sparks": 16}),
        warden_wreck(0),
        warden_wreck(1),
    ]
    out = []
    for f in frames:
        f.outline()
        out.append(f.a.copy())
    return out


# ---------------------------------------------------------------- effects

def fireball(phase):
    c = Canvas()
    cy = 17
    for r, ramp, s in ((6.5, "red", 4), (5.0, "orange", 2), (3.2, "yellow", 1), (1.6, "bone", 0)):
        for y in range(N):
            for x in range(N):
                wob = 0.6 * math.sin((x + y) * 1.3 + phase * 2.1)
                if (x + 0.5 - 16) ** 2 + (y + 0.5 - cy) ** 2 <= (r + wob) ** 2:
                    c.px(x, y, ramp, s)
    # trailing flames
    for i in range(4):
        c.px(16 + (i - 1.5) * 3, cy + 7 + (i + phase) % 2, "orange", 3)
    return c.a.copy()


def puff(phase):
    c = Canvas()
    r = 2.0 + phase * 1.5
    for y in range(N):
        for x in range(N):
            d = (x + 0.5 - 16) ** 2 + (y + 0.5 - 16) ** 2
            if d <= r * r:
                c.px(x, y, "gray", 2 + phase * 3 + (d > (r - 1) ** 2) * 2)
    if phase == 0:
        c.px(16, 16, "yellow", 0)
    return c.a.copy()


def blood_hit(phase):
    c = Canvas()
    c.blood(16, 16, 10 + phase * 8, 50 + phase)
    return c.a.copy()


def explosion(phase):
    c = Canvas()
    r = 6 + phase * 3.5
    rng = np.random.default_rng(70 + phase)
    for y in range(N):
        for x in range(N):
            d = math.hypot(x + 0.5 - 16, y + 0.5 - 16) + rng.normal(0, 0.9)
            if d <= r:
                t = d / r
                if phase == 2:
                    c.px(x, y, "gray", 4 + t * 6)
                elif t < 0.45:
                    c.px(x, y, "bone" if phase == 0 else "yellow", 0)
                elif t < 0.75:
                    c.px(x, y, "orange", 1)
                else:
                    c.px(x, y, "red", 3)
    return c.a.copy()


def rocket_fly(phase):
    """A rocket seen from behind as it flies away: steel tail ring around a
    bright exhaust, with a flickering flame and a little smoke."""
    c = Canvas()
    cy = 17
    rng = np.random.default_rng(90 + phase)
    for _ in range(10):                                 # smoke trail
        c.px(16 + rng.normal(0, 2.2), cy + 3 + abs(rng.normal(0, 2.5)), "gray", 6 + rng.integers(0, 4))
    r_flame = 4.2 + phase * 1.0
    for y in range(N):
        for x in range(N):
            d = math.hypot(x + 0.5 - 16, y + 0.5 - cy)
            wob = 0.5 * math.sin(math.atan2(y - cy, x - 16) * 5 + phase * 2)
            if d <= r_flame + wob:
                c.px(x, y, "orange" if d > 2.6 else "yellow", 1 if d > 2.6 else 0)
    c.ellipse(16, cy, 2.8, 2.8, "steel", 4)             # tail ring
    c.ellipse(16, cy, 1.5, 1.5, "bone", 0)              # nozzle glow
    return c.a.copy()


CREATURES = [GRUNT, HEAVY, IMP, BRUTE]
EFFECTS = [("fireball0", lambda: fireball(0)), ("fireball1", lambda: fireball(1)),
           ("puff0", lambda: puff(0)), ("puff1", lambda: puff(1)),
           ("bloodhit0", lambda: blood_hit(0)), ("bloodhit1", lambda: blood_hit(1)),
           ("boom0", lambda: explosion(0)), ("boom1", lambda: explosion(1)),
           ("boom2", lambda: explosion(2)),
           ("rocketfly0", lambda: rocket_fly(0)), ("rocketfly1", lambda: rocket_fly(1))]


def build_all():
    """(name, array) list in appvar order."""
    out = []
    for st in CREATURES:
        for name, a in zip(FRAMES, creature_frames(st)):
            out.append(("%s_%s" % (st.name, name), a))
    for name, a in zip(FRAMES, warden_frames()):
        out.append(("boss_%s" % name, a))
    for name, f in EFFECTS:
        out.append((name, f()))
    return out
