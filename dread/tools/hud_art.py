"""Status bar graphics for DREAD, authored in palette space.

The panel has dark inset wells where numbers, the face and key icons go.
Every dynamic graphic is drawn opaque on that same well color, so the game
can overwrite a changed number in place without restoring the background.

Layout (x ranges of the 320x40 bar) is shared with src/hud.c through the
generated header src/gen/hud_layout.h.
"""

import numpy as np

import font as F
import palette as P

W, H = 320, 40

WELL = P.idx("gray", 14)          # background of every dynamic element
PANEL = "steel"

# name: (x, y, w, h) of each inset well
WELLS = {
    "ammo":   (4, 4, 40, 20),
    "health": (52, 4, 56, 20),
    "arms":   (116, 4, 36, 20),
    "face":   (158, 3, 28, 34),
    "armor":  (194, 4, 56, 20),
    "keys":   (258, 4, 10, 32),
    "table":  (274, 4, 42, 20),
}
LABELS = {"ammo": "AMMO", "health": "HEALTH", "arms": "ARMS", "armor": "ARMOR"}

BIG_W, BIG_H = 12, 16
SMALL_W, SMALL_H = 4, 6
FACE_W, FACE_H = 24, 30
KEY_W, KEY_H = 8, 10

# Ammo table rows: label, y inside the table well
TABLE_ROWS = [("BUL", 1), ("SHL", 8), ("RKT", 15)]


def rng(seed):
    return np.random.default_rng(seed)


def panel():
    a = np.full((H, W), P.idx(PANEL, 9), dtype=int)
    n = rng(3).random((H, W))
    a = a + (n > 0.82).astype(int) - (n < 0.12).astype(int)
    a[0, :] = P.idx(PANEL, 4)
    a[1, :] = P.idx(PANEL, 6)
    a[H - 1, :] = P.idx(PANEL, 13)
    # section dividers
    for x in (48, 112, 154, 190, 254, 270):
        a[2:H - 1, x] = P.idx(PANEL, 13)
        a[2:H - 1, x + 1] = P.idx(PANEL, 6)
    # rivets
    for x in (3, 316):
        for y in (4, 34):
            a[y, x] = P.idx(PANEL, 2)
            a[y + 1, x] = P.idx(PANEL, 11)
    # wells with a bevel (dark top/left, light bottom/right)
    for name, (x, y, w, h) in WELLS.items():
        a[y:y + h, x:x + w] = WELL
        a[y - 1, x - 1:x + w + 1] = P.idx(PANEL, 13)
        a[y - 1:y + h + 1, x - 1] = P.idx(PANEL, 13)
        a[y + h, x - 1:x + w + 1] = P.idx(PANEL, 5)
        a[y:y + h + 1, x + w] = P.idx(PANEL, 5)
    # labels under the number wells
    for name, text in LABELS.items():
        x, y, w, h = WELLS[name]
        tw = F.text_width(text)
        m = F.text_mask(text, (H, W), x + (w - tw) // 2, y + h + 4)
        a[m] = P.idx("steel", 2)
    # ammo table labels
    tx, ty, tw, th = WELLS["table"]
    for label, ry in TABLE_ROWS:
        m = F.text_mask(label, (H, W), tx + 1, ty + ry)
        a[m] = P.idx("steel", 4)
        m = F.text_mask("/", (H, W), tx + 1 + 12 + 3 * SMALL_W, ty + ry)
        a[m] = P.idx("steel", 6)
    return a


def big_digit(ch):
    """12x16 red digit on the well color: 5x7 glyph scaled 2x, shaded, with
    a dark drop shadow."""
    a = np.full((BIG_H, BIG_W), WELL, dtype=int)
    if ch == " ":
        return a
    g = F.glyph_mask(F.FONT5x7, ch)
    big = np.kron(g, np.ones((2, 2), dtype=bool))       # 14x10
    sh = np.zeros((BIG_H, BIG_W), dtype=bool)
    sh[2:16, 2:12] = big
    a[sh] = P.idx("red", 12)
    for yy in range(14):
        for xx in range(10):
            if big[yy, xx]:
                s = 1 + yy // 4
                if xx % 2 == 1 and yy % 2 == 1:
                    s += 1
                a[yy + 1, xx + 1] = P.idx("red", s)
    return a


def small_digit(ch, ramp, shade):
    a = np.full((SMALL_H, SMALL_W), WELL, dtype=int)
    if ch != " ":
        g = F.glyph_mask(F.FONT3x5, ch)
        a[0:5, 0:3][g] = P.idx(ramp, shade)
    return a


def key_icon(color):
    a = np.full((KEY_H, KEY_W), WELL, dtype=int)
    if color is None:
        a[1:9, 1:7] = P.idx("gray", 12)          # empty slot outline
        a[2:8, 2:6] = WELL
        return a
    a[1:9, 1:7] = P.idx(color, 2)
    a[1, 1:7] = P.idx(color, 0)
    a[1:9, 6] = P.idx(color, 5)
    a[3:5, 1:7] = P.idx("gray", 12)
    a[6:8, 4:6] = P.idx("bone", 1)
    return a


# ------------------------------------------------------------------ faces
#
# One face per health band (0 = healthy .. 4 = near death) and look
# (center, left, right, pain), plus a dead face. Damage accumulates as
# bruises, cuts, blood and a swollen eye.

LOOKS = ["center", "left", "right", "pain"]


def face(band, look):
    a = np.full((FACE_H, FACE_W), WELL, dtype=int)
    yy, xx = np.mgrid[0:FACE_H, 0:FACE_W].astype(float)
    dead = look == "dead"
    skin = "olive" if dead else "skin"

    # head and jaw
    head = ((xx - 11.5) / 9.2) ** 2 + ((yy - 16) / 12.5) ** 2 <= 1
    jaw = (np.abs(xx - 11.5) < 7 - (yy - 22) * 0.6) & (yy >= 22) & (yy < 29)
    face_m = head | jaw
    shade = 3 + np.clip((xx - 6) * 0.25, 0, 3) + np.clip((yy - 20) * 0.2, 0, 2) + band * 0.5
    a[face_m] = [P.idx(skin, s) for s in shade[face_m]]

    # helmet: dome over the top with a ridge and a red visor stripe
    helm = (((xx - 11.5) / 11) ** 2 + ((yy - 10) / 9.5) ** 2 <= 1) & (yy <= 11)
    hs = 3 + np.clip((xx - 4) * 0.3, 0, 4)
    a[helm] = [P.idx("steel", s) for s in hs[helm]]
    a[11, 2:22] = P.idx("steel", 9)
    a[1:11, 11] = P.idx("steel", 2)
    a[9, 5:19] = P.idx("red", 3 + band // 2)
    # chin strap
    for y in range(12, 25):
        a[y, 2] = P.idx("steel", 7)
        a[y, 21] = P.idx("steel", 8)

    # brows
    by = 13 if look != "pain" else 14
    a[by, 6:10] = P.idx("brown", 10)
    a[by, 14:18] = P.idx("brown", 10)

    # eyes
    ey = 15
    if dead:
        for cx in (7.5, 15.5):
            for d in (-1, 0, 1):
                a[ey + d, int(cx + d)] = P.idx("gray", 13)
                a[ey + d, int(cx - d)] = P.idx("gray", 13)
    elif look == "pain":
        a[ey, 6:10] = P.idx("gray", 13)
        a[ey, 14:18] = P.idx("gray", 13)
    else:
        shift = {"center": 1, "left": 0, "right": 2}[look]
        for ex in (6, 14):
            a[ey - 1:ey + 1, ex:ex + 4] = P.idx("bone", 1)
            a[ey - 1:ey + 1, ex + shift:ex + shift + 2] = P.idx("gray", 13)
        if band >= 3:                              # swollen right eye
            a[ey - 1:ey + 2, 14:18] = P.idx("purple", 6)
            a[ey, 15:17] = P.idx("gray", 13)

    # nose
    a[16:20, 11] = P.idx(skin, 6 + band // 2)
    a[19, 10:13] = P.idx(skin, 7 + band // 2)

    # mouth
    if dead or look == "pain":
        a[22:25, 9:15] = P.idx("gray", 13)
        a[22, 9:15] = P.idx("red", 8)
    else:
        a[23, 8:16] = P.idx("flesh", 9)
        if band >= 2:
            a[23, 8] = a[23, 15] = P.idx(skin, 8)  # grimace

    # damage marks
    r = rng(100 + band)
    if band >= 1:
        a[20:22, 4:6] = P.idx("purple", 8)          # bruise on the cheek
    if band >= 2 or dead:
        for x in (16, 17):                         # cut on the forehead
            for y in range(12, 12 + 3 + band):
                if y < FACE_H and face_m[y, x] and y not in (ey, ey - 1):
                    a[y, x] = P.idx("red", 4)
    if band >= 3 or dead:
        for x0, y0, n in ((5, 12, 5), (18, 18, 6)):   # streaks from the cuts
            for k in range(n):
                y, x = y0 + k, x0 + (k // 3)
                if face_m[y, x]:
                    a[y, x] = P.idx("red", 4 + k // 3)
    if band >= 4 or dead:
        for x, y in ((9, 25), (10, 26), (10, 27), (14, 25), (14, 26)):  # drips
            a[y, x] = P.idx("red", 5)
    return a


def faces():
    out = []
    for band in range(5):
        for look in LOOKS:
            out.append(("face_%d_%s" % (band, look), face(band, look)))
    out.append(("face_dead", face(4, "dead")))
    return out


def build_all():
    """List of (name, array) in appvar order."""
    items = [("panel", panel())]
    for ch in "0123456789%":
        items.append(("big_%s" % ("pct" if ch == "%" else ch), big_digit(ch)))
    items.append(("big_blank", big_digit(" ")))
    for ch in "0123456789":
        items.append(("small_%s" % ch, small_digit(ch, "yellow", 1)))
    for ch in "12345":
        items.append(("arm_off_%s" % ch, small_digit(ch, "gray", 9)))
    for ch in "12345":
        items.append(("arm_sel_%s" % ch, small_digit(ch, "bone", 0)))
    items.append(("small_blank", small_digit(" ", "gray", 0)))
    for c in ("red", "blue", "yellow"):
        items.append(("key_%s" % c, key_icon(c)))
    items.append(("key_none", key_icon(None)))
    items += faces()
    return items
