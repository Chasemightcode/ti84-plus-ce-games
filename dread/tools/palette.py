"""DREAD palette: 16 ramps x 16 shades.

Index = ramp * 16 + shade. Shade 0 is the brightest color of a ramp and
shade 15 is nearly black, so darkening a color by L light levels is just
min(shade + L, 15) inside the same ramp. The engine builds its distance
shading tables from that rule at startup.

Index 0 (gray ramp, shade 0) is reserved as the transparent color for
sprites and is never used by wall textures.

The calculator stores palette entries as 5 bits per channel, and convimg
matches source pixels against the palette after that rounding. Every entry
here is therefore built directly in 5-bit space and kept unique there, so
images written with these exact colors convert back to the exact index.
"""

RAMP_NAMES = [
    "gray", "steel", "brown", "tan", "red", "flesh", "orange", "yellow",
    "green", "olive", "blue", "cyan", "purple", "skin", "rust", "bone",
]

# Brightest color of each ramp, plus how much the top shades lean toward
# white (a small highlight keeps bright ends from looking flat).
RAMP_BASE = {
    "gray":   ((232, 232, 228), 0.00),
    "steel":  ((168, 182, 204), 0.10),
    "brown":  ((178, 124, 76), 0.00),
    "tan":    ((230, 200, 146), 0.05),
    "red":    ((255, 60, 44), 0.10),
    "flesh":  ((196, 70, 76), 0.00),
    "orange": ((255, 152, 40), 0.15),
    "yellow": ((255, 238, 110), 0.25),
    "green":  ((96, 232, 72), 0.10),
    "olive":  ((150, 156, 88), 0.00),
    "blue":   ((80, 128, 255), 0.15),
    "cyan":   ((96, 236, 224), 0.20),
    "purple": ((188, 100, 228), 0.05),
    "skin":   ((244, 180, 138), 0.05),
    "rust":   ((196, 104, 56), 0.00),
    "bone":   ((240, 230, 204), 0.10),
}

TRANSPARENT = (255, 0, 255)


def ramp(name):
    return RAMP_NAMES.index(name)


def idx(name, shade):
    """Palette index for a ramp name and shade (clamped 0..15)."""
    s = max(0, min(15, int(shade)))
    return RAMP_NAMES.index(name) * 16 + s


def _shade_factor(s):
    # Roughly exponential falloff: 1.0 at s=0 down to ~0.08 at s=15.
    return 0.845 ** s


def _natural(name, s):
    (br, bg, bb), hi = RAMP_BASE[name]
    f = _shade_factor(s)
    r, g, b = br * f, bg * f, bb * f
    h = hi * max(0.0, 1.0 - s / 3.0)
    r += (255 - r) * h
    g += (255 - g) * h
    b += (255 - b) * h
    return (r, g, b)


def _expand5(v):
    """5-bit channel to the 8-bit value the calculator/convimg reconstruct."""
    return (v << 3) | (v >> 2)


def _nearest_free(target, used):
    """Nearest unused 5-bit color to an 8-bit target, by squared distance."""
    t5 = [max(0, min(31, int(round(c / 255.0 * 31)))) for c in target]
    best = None
    for radius in range(0, 6):
        for dr in range(-radius, radius + 1):
            for dg in range(-radius, radius + 1):
                for db in range(-radius, radius + 1):
                    if max(abs(dr), abs(dg), abs(db)) != radius:
                        continue
                    c = (t5[0] + dr, t5[1] + dg, t5[2] + db)
                    if min(c) < 0 or max(c) > 31 or c in used:
                        continue
                    e = [_expand5(v) for v in c]
                    d = sum((e[i] - target[i]) ** 2 for i in range(3))
                    if best is None or d < best[0]:
                        best = (d, c)
        if best is not None:
            return best[1]
    raise RuntimeError("palette space exhausted near %r" % (target,))


def build_palette():
    """Return a list of 256 (r, g, b) tuples, each exactly representable
    and unique in 5-bit-per-channel color."""
    pal5 = [None] * 256
    used = {(31, 0, 31)}             # transparent magenta
    pal5[0] = (31, 0, 31)
    # Assign bright shades first so collisions get resolved in the dark end,
    # where a one-step nudge is invisible.
    for s in range(16):
        for r, name in enumerate(RAMP_NAMES):
            i = r * 16 + s
            if i == 0:
                continue
            c = _nearest_free(_natural(name, s), used)
            used.add(c)
            pal5[i] = c
    return [tuple(_expand5(v) for v in c) for c in pal5]


if __name__ == "__main__":
    p = build_palette()
    for i in range(0, 256, 16):
        print(RAMP_NAMES[i // 16].ljust(7), " ".join("%02x%02x%02x" % c for c in p[i:i + 16]))
