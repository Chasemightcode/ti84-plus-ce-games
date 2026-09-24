"""Build every DREAD asset.

    python tools/build_assets.py

Generates (all derived from code in tools/, nothing hand-edited):
    src/gfx/palette.png, src/gfx/walls.png, src/gfx/convimg.yaml
    src/gfx/dreadtx.c/.h + build/appvars/DREADTX.8xv (via convimg)
    src/gfx/dreadsp.c/.h + build/appvars/DREADSP.8xv (via convimg)
    src/gfx/dreaden.c/.h + build/appvars/DREADEN.8xv (via convimg)
    src/gfx/dreadwp.c/.h + build/appvars/DREADWP.8xv (via convimg)
    build/appvars/DREADL1.8xv ...            (via convbin; levels/e1m*.txt)
    build/testvars/DREADL1.8xv               (levels/test_map.txt, for tests)
    src/gen/trig.c, src/gen/assets.h, src/gen/things.h, src/gen/thingdefs.c,
    src/gen/hud_layout.h, src/gen/weapons.h
    icon.png, art/*.png                      (previews)
and verifies that every texel survived palette conversion exactly.
"""

import glob
import math
import os
import shutil
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import enemies as E  # noqa: E402
import hud_art as HA  # noqa: E402
import levels as LV  # noqa: E402
import palette as P  # noqa: E402
import sprites as S  # noqa: E402
import textures as T  # noqa: E402
import things as TH  # noqa: E402
import weapons as WP  # noqa: E402

GFX = os.path.join(ROOT, "src", "gfx")
GEN = os.path.join(ROOT, "src", "gen")
OUT = os.path.join(ROOT, "build", "appvars")
TESTVARS = os.path.join(ROOT, "build", "testvars")
ART = os.path.join(ROOT, "art")
LEVELS = os.path.join(ROOT, "levels")

ANG_COUNT = 1024


def cedev_bin(tool):
    cands = []
    if os.environ.get("CEDEV"):
        cands.append(os.path.join(os.environ["CEDEV"], "bin"))
    cands.append(os.path.join(os.path.expanduser("~"), "CEdev", "bin"))
    for d in cands:
        for name in (tool + ".exe", tool):
            p = os.path.join(d, name)
            if os.path.exists(p):
                return p
    p = shutil.which(tool)
    if p:
        return p
    sys.exit("error: cannot find %s; set CEDEV to your CEdev folder" % tool)


def write_if_changed(path, data):
    mode = "wb" if isinstance(data, bytes) else "w"
    old = None
    if os.path.exists(path):
        with open(path, "rb" if mode == "wb" else "r") as f:
            old = f.read()
    if old != data:
        with open(path, mode) as f:
            f.write(data)


# ------------------------------------------------------------------ palette

def build_palette():
    pal = P.build_palette()
    img = Image.new("RGB", (256, 1))
    img.putdata(pal)
    img.save(os.path.join(GFX, "palette.png"))
    return np.array(pal, dtype=np.uint8)


# ----------------------------------------------------------------- textures

def build_textures(pal):
    texs = T.build_all()
    # Column-major storage: tile i is written transposed so convimg's
    # row-major output gives each texture column as 32 contiguous bytes.
    sheet = np.zeros((32 * len(texs), 32), dtype=np.uint8)
    for i, t in enumerate(texs):
        assert t.shape == (32, 32) and t.min() >= 1 and t.max() <= 255
        sheet[i * 32:(i + 1) * 32, :] = t.T
    Image.fromarray(pal[sheet]).save(os.path.join(GFX, "walls.png"))

    cols, scale, pad = 7, 4, 14
    rows = (len(texs) + cols - 1) // cols
    prev = Image.new("RGB", (cols * (32 * scale + 8), rows * (32 * scale + pad + 8)), (32, 32, 32))
    dr = ImageDraw.Draw(prev)
    for i, t in enumerate(texs):
        x = (i % cols) * (32 * scale + 8) + 4
        y = (i // cols) * (32 * scale + pad + 8) + 4
        prev.paste(Image.fromarray(pal[t]).resize((32 * scale, 32 * scale), Image.NEAREST), (x, y + pad))
        dr.text((x, y), "%d %s" % (i, T.TEXTURE_NAMES[i]), fill=(255, 255, 255))
    prev.save(os.path.join(ART, "walls_preview.png"))
    return texs


def build_sprites(pal):
    sprs = S.build_all()
    sheet = np.zeros((32 * len(sprs), 32), dtype=np.uint8)
    for i, a in enumerate(sprs):
        assert a.shape == (32, 32) and a.min() >= 0 and a.max() <= 255
        sheet[i * 32:(i + 1) * 32, :] = a.T          # column-major, like walls
    Image.fromarray(pal[sheet]).save(os.path.join(GFX, "wsprites.png"))

    hud_dir = os.path.join(GFX, "hud")
    os.makedirs(hud_dir, exist_ok=True)
    for f in glob.glob(os.path.join(hud_dir, "*.png")):
        os.remove(f)
    hud = []
    for name, a in HA.build_all():
        if name == "panel":                           # sprite width is 8-bit
            hud.append(("panel_l", a[:, :160]))
            hud.append(("panel_r", a[:, 160:]))
        else:
            hud.append((name, a))
    for name, a in hud:
        assert a.min() >= 1, name                     # HUD art is opaque
        Image.fromarray(pal[a.astype(np.uint8)]).save(os.path.join(hud_dir, "hud_%s.png" % name))

    cols, scale, pad = 8, 4, 14
    rows = (len(sprs) + cols - 1) // cols
    bg = (70, 64, 58)
    prev = Image.new("RGB", (cols * (32 * scale + 8), rows * (32 * scale + pad + 8)), bg)
    dr = ImageDraw.Draw(prev)
    for i, a in enumerate(sprs):
        rgb = pal[a].copy()
        rgb[a == 0] = bg
        x = (i % cols) * (32 * scale + 8) + 4
        y = (i // cols) * (32 * scale + pad + 8) + 4
        prev.paste(Image.fromarray(rgb).resize((32 * scale, 32 * scale), Image.NEAREST), (x, y + pad))
        dr.text((x, y), "%d %s" % (i, S.SPRITE_NAMES[i]), fill=(255, 255, 255))
    prev.save(os.path.join(ART, "sprites_preview.png"))
    return sprs, hud


def build_enemies(pal):
    frames = E.build_all()
    sheet = np.zeros((32 * len(frames), 32), dtype=np.uint8)
    for i, (name, a) in enumerate(frames):
        assert a.shape == (32, 32) and a.min() >= 0 and a.max() <= 255, name
        sheet[i * 32:(i + 1) * 32, :] = a.T
    Image.fromarray(pal[sheet]).save(os.path.join(GFX, "esprites.png"))
    cols, scale, pad = 10, 3, 12
    rows = (len(frames) + cols - 1) // cols
    bg = (70, 64, 58)
    prev = Image.new("RGB", (cols * (32 * scale + 6), rows * (32 * scale + pad + 6)), bg)
    dr = ImageDraw.Draw(prev)
    for i, (name, a) in enumerate(frames):
        rgb = pal[a].copy()
        rgb[a == 0] = bg
        x = (i % cols) * (32 * scale + 6) + 3
        y = (i // cols) * (32 * scale + pad + 6) + 3
        prev.paste(Image.fromarray(rgb).resize((32 * scale, 32 * scale), Image.NEAREST), (x, y + pad))
        dr.text((x, y), name, fill=(255, 255, 255))
    prev.save(os.path.join(ART, "enemies_preview.png"))
    return [a for _, a in frames]


def build_weapons(pal):
    wdir = os.path.join(GFX, "weapons")
    os.makedirs(wdir, exist_ok=True)
    for f in glob.glob(os.path.join(wdir, "*.png")):
        os.remove(f)
    frames = WP.build_all()
    for name, a, dx in frames:
        assert a.shape[1] <= 160 and a.shape[0] <= 100, name
        Image.fromarray(pal[a.astype(np.uint8)]).save(os.path.join(wdir, "wp_%s.png" % name))
    lines = ["/* Generated by tools/build_assets.py from tools/weapons.py -- do not edit. */",
             "#ifndef GEN_WEAPONS_H", "#define GEN_WEAPONS_H", ""]
    for i, (name, a, dx) in enumerate(frames):
        lines.append("#define WF_%-16s %d" % (name.upper(), i))
    lines.append("#define NUM_WEAPON_FRAMES %d" % len(frames))
    lines.append("/* horizontal offset of each frame from the view center, logical px */")
    lines.append("#define WEAPON_FRAME_DX { %s }" % ", ".join(str(dx) for _, _, dx in frames))
    # convimg sorts the images by file name, so bind its wp_<name> macros
    # (gfx/dreadwp.h) to WF_* ids by name rather than by position
    lines.append("/* fill tab[NUM_WEAPON_FRAMES] from gfx/dreadwp.h after DREADWP_init() */")
    lines.append("#define WEAPON_FRAME_BIND(tab) do { \\")
    for name, a, dx in frames:
        lines.append("    (tab)[WF_%s] = wp_%s; \\" % (name.upper(), name))
    lines.append("} while (0)")
    lines += ["", "#endif", ""]
    write_if_changed(os.path.join(GEN, "weapons.h"), "\n".join(lines))
    # preview on a mock view
    bg = (40, 38, 36)
    prev = Image.new("RGB", (160 * len(frames), 100), bg)
    for i, (name, a, dx) in enumerate(frames):
        rgb = pal[a].copy()
        rgb[a == 0] = bg
        prev.paste(Image.fromarray(rgb), (i * 160 + 80 - a.shape[1] // 2 + dx, 100 - a.shape[0]))
    prev.resize((prev.width * 2, prev.height * 2), Image.NEAREST).save(os.path.join(ART, "weapons_preview.png"))
    return frames


def verify_enemies(frames):
    name, content = read_8xv(os.path.join(GFX, "DREADEN.8xv"))
    assert name == "DREADEN", name
    blob = b"".join(a.T.astype(np.uint8).tobytes() for a in frames)
    if content.find(blob) < 0:
        sys.exit("verify: enemy sprite data in DREADEN does not match the authored indices")
    print("  DREADEN verified: %d bytes, %d frames" % (len(content), len(frames)))
    if len(content) > 65000:
        sys.exit("DREADEN is too large for one AppVar")


def verify_weapons(frames):
    name, content = read_8xv(os.path.join(GFX, "DREADWP.8xv"))
    assert name == "DREADWP", name
    for fname, a, dx in frames:
        h, w = a.shape
        rle = bytearray([w, h])
        for row in a:
            x = 0
            while x < w:
                t = 0
                while x < w and row[x] == 0:
                    t += 1
                    x += 1
                rle.append(t)
                if x == w:
                    break
                run = []
                while x < w and row[x] != 0:
                    run.append(int(row[x]))
                    x += 1
                rle.append(len(run))
                rle += bytes(run)
        if content.find(bytes(rle)) < 0:
            sys.exit("verify: weapon frame %s is not the expected RLET data" % fname)
    print("  DREADWP verified: %d bytes, %d frames" % (len(content), len(frames)))


def verify_sprites(sprs, hud):
    name, content = read_8xv(os.path.join(GFX, "DREADSP.8xv"))
    assert name == "DREADSP", name
    blob = b"".join(a.T.astype(np.uint8).tobytes() for a in sprs)
    if content.find(blob) < 0:
        sys.exit("verify: sprite data in DREADSP does not match the authored indices")
    for hname, a in hud:
        h, w = a.shape
        img = bytes([w, h]) + a.astype(np.uint8).tobytes()
        if content.find(img) < 0:
            sys.exit("verify: HUD image %s not found verbatim in DREADSP" % hname)
    print("  DREADSP verified: %d bytes, %d sprites, %d HUD images"
          % (len(content), len(sprs), len(hud)))
    if len(content) > 65000:
        sys.exit("DREADSP is too large for one AppVar")


def write_hud_layout():
    lines = ["/* Generated by tools/build_assets.py from tools/hud_art.py -- do not edit. */",
             "#ifndef GEN_HUD_LAYOUT_H", "#define GEN_HUD_LAYOUT_H", ""]
    for name, (x, y, w, h) in HA.WELLS.items():
        n = name.upper()
        lines += ["#define WELL_%s_X %d" % (n, x), "#define WELL_%s_Y %d" % (n, y),
                  "#define WELL_%s_W %d" % (n, w), "#define WELL_%s_H %d" % (n, h)]
    lines += ["#define BIG_W %d" % HA.BIG_W, "#define BIG_H %d" % HA.BIG_H,
              "#define SMALL_W %d" % HA.SMALL_W, "#define SMALL_H %d" % HA.SMALL_H,
              "#define FACE_W %d" % HA.FACE_W, "#define FACE_H %d" % HA.FACE_H,
              "#define KEY_W %d" % HA.KEY_W, "#define KEY_H %d" % HA.KEY_H,
              "#define HUD_WELL_COLOR %d" % HA.WELL]
    for i, (label, ry) in enumerate(HA.TABLE_ROWS):
        lines.append("#define TABLE_ROW%d_Y %d" % (i, ry))
    lines += ["#define TABLE_NUM_X 13", "#define TABLE_MAX_X 29",
              "#define FACE_LOOKS %d" % len(HA.LOOKS), "", "#endif", ""]
    write_if_changed(os.path.join(GEN, "hud_layout.h"), "\n".join(lines))


CONVIMG_YAML = """# Generated by tools/build_assets.py -- do not edit.
palettes:
  - name: dread_pal
    fixed-entries:
      - image: palette.png

converts:
  - name: walls
    palette: dread_pal
    width-and-height: false
    tilesets:
      tile-width: 32
      tile-height: 32
      pointer-table: true
      images:
        - walls.png

  - name: wsprites
    palette: dread_pal
    width-and-height: false
    tilesets:
      tile-width: 32
      tile-height: 32
      pointer-table: true
      images:
        - wsprites.png

  - name: hud
    palette: dread_pal
    images:
      - hud/*.png

  - name: esprites
    palette: dread_pal
    width-and-height: false
    tilesets:
      tile-width: 32
      tile-height: 32
      pointer-table: true
      images:
        - esprites.png

  - name: wframes
    palette: dread_pal
    style: rlet
    transparent-index: 0
    images:
      - weapons/*.png

outputs:
  - type: appvar
    name: DREADEN
    source-format: c
    include-file: dreaden.h
    archived: true
    comment: "DREAD enemies"
    converts:
      - esprites

  - type: appvar
    name: DREADWP
    source-format: c
    include-file: dreadwp.h
    archived: true
    comment: "DREAD weapons"
    converts:
      - wframes

  - type: appvar
    name: DREADSP
    source-format: c
    include-file: dreadsp.h
    archived: true
    comment: "DREAD sprites"
    converts:
      - wsprites
      - hud

  - type: appvar
    name: DREADTX
    source-format: c
    include-file: dreadtx.h
    archived: true
    comment: "DREAD textures"
    palettes:
      - dread_pal
    converts:
      - walls
"""


def run_convimg():
    write_if_changed(os.path.join(GFX, "convimg.yaml"), CONVIMG_YAML)
    r = subprocess.run([cedev_bin("convimg")], cwd=GFX, capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout, r.stderr)
        sys.exit("convimg failed")


def read_8xv(path):
    """Return (name, content bytes) of a single-variable .8xv file."""
    d = open(path, "rb").read()
    assert d[:8] == b"**TI83F*", path
    pos = 55
    hdr_len = d[pos] | d[pos + 1] << 8   # entry header, after this word
    name = d[pos + 5:pos + 13].rstrip(b"\0").decode()
    # Entry header is followed by the variable length word, then the
    # variable data; an AppVar's data starts with its own 2-byte size.
    var = d[pos + 2 + hdr_len + 2:]
    size = var[0] | var[1] << 8
    return name, var[2:2 + size]


def verify_textures(pal, texs):
    name, content = read_8xv(os.path.join(GFX, "DREADTX.8xv"))
    assert name == "DREADTX", name
    # Expected layout: 512-byte palette, then every tile's raw pixels.
    # Palette entries are 1555 with bit 15 holding green's 6th (low) bit.
    expect_pal = b"".join(
        (((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3) | (((g >> 2) & 1) << 15)).to_bytes(2, "little")
        for r, g, b in pal.tolist())
    blob = b"".join(bytes(t.T.astype(np.uint8).tobytes()) for t in texs)
    pal_at = content.find(expect_pal)
    tex_at = content.find(blob)
    if pal_at < 0:
        sys.exit("verify: palette not found verbatim in DREADTX")
    if tex_at < 0:
        # Find the first mismatching texel for a useful message.
        sys.exit("verify: texture data in DREADTX does not match the authored indices")
    print("  DREADTX verified: %d bytes, palette @%d, %d textures @%d"
          % (len(content), pal_at, len(texs), tex_at))


# -------------------------------------------------------------- generated C

def write_trig():
    vals = [int(round(math.sin(2 * math.pi * i / ANG_COUNT) * 16384))
            for i in range(ANG_COUNT + ANG_COUNT // 4)]
    lines = ["/* Generated by tools/build_assets.py -- do not edit. */",
             "#include <stdint.h>", "",
             "/* sin(i * 2pi / 1024) in Q14; cos(a) = sin_table[a + 256]. */",
             "const int16_t sin_table[%d] = {" % len(vals)]
    for i in range(0, len(vals), 12):
        lines.append("    " + ", ".join("%d" % v for v in vals[i:i + 12]) + ",")
    lines.append("};")
    write_if_changed(os.path.join(GEN, "trig.c"), "\n".join(lines) + "\n")


def write_assets_h():
    lines = ["/* Generated by tools/build_assets.py -- do not edit. */",
             "#ifndef GEN_ASSETS_H", "#define GEN_ASSETS_H", ""]
    lines.append("/* Palette ramps: index = ramp * 16 + shade (0 bright .. 15 dark). */")
    for i, n in enumerate(P.RAMP_NAMES):
        lines.append("#define RAMP_%-8s 0x%02X" % (n.upper(), i * 16))
    lines.append("#define PAL(ramp, shade) ((uint8_t)(RAMP_##ramp + (shade)))")
    lines.append("")
    lines.append("/* Wall texture ids (map tile value = id + 1). */")
    for i, n in enumerate(T.TEXTURE_NAMES):
        lines.append("#define TEX_%-14s %d" % (n.upper(), i))
    lines.append("#define NUM_TEXTURES %d" % len(T.TEXTURE_NAMES))
    lines += ["", "#endif", ""]
    write_if_changed(os.path.join(GEN, "assets.h"), "\n".join(lines))


# ------------------------------------------------------------------- levels

def level_appvar(path, var, outdir, pal):
    try:
        info, data = LV.compile_level(path)
    except ValueError as e:
        sys.exit("level error: %s" % e)
    raw = os.path.join(outdir, var + ".bin")
    with open(raw, "wb") as f:
        f.write(data)
    out = os.path.join(outdir, var + ".8xv")
    r = subprocess.run([cedev_bin("convbin"), "-r", "-j", "bin", "-k", "8xv", "-i", raw,
                        "-o", out, "-n", var, "-b", "DREAD " + info["name"]],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout, r.stderr)
        sys.exit("convbin failed for " + path)
    os.remove(raw)
    write_map_preview(path, data, pal)
    st = info["stats"]
    print("  %s <- %s (%s, %dx%d, %d bytes, monsters %d/%d/%d)"
          % (var, os.path.basename(path), info["name"], info["size"][0], info["size"][1],
             len(data), st["easy"], st["normal"], st["hard"]))


def build_levels(pal):
    """levels/e1m1..e1m5 are the campaign (DREADL1..5, released); the
    engine test map is compiled as a DREADL1 of its own in build/testvars,
    which the emulator tests load instead."""
    files = sorted(glob.glob(os.path.join(LEVELS, "e1m*.txt")))
    for n, path in enumerate(files, 1):
        level_appvar(path, "DREADL%d" % n, OUT, pal)
    os.makedirs(TESTVARS, exist_ok=True)
    level_appvar(os.path.join(LEVELS, "test_map.txt"), "DREADL1", TESTVARS, pal)


def write_map_preview(path, data, pal):
    w, h = data[4], data[5]
    tiles = data[40:40 + w * h]
    texs = T.build_all()
    avg = [pal[t].reshape(-1, 3).mean(axis=0) for t in texs]
    s = 8
    img = Image.new("RGB", (w * s, h * s), (20, 20, 20))
    dr = ImageDraw.Draw(img)
    for y in range(h):
        for x in range(w):
            t = tiles[y * w + x]
            if t == 0:
                col = (70, 64, 58)
            elif t >= 0x80:
                col = {0x80: (200, 200, 200), 0x81: (255, 60, 40),
                       0x82: (80, 128, 255), 0x83: (255, 230, 90)}[t]
            else:
                col = tuple(int(c) for c in avg[(t & 0x3F) - 1])
            dr.rectangle([x * s, y * s, x * s + s - 1, y * s + s - 1], fill=col)
    sx, sy = data[6], data[7]
    dr.ellipse([sx * s + 1, sy * s + 1, sx * s + s - 2, sy * s + s - 2], fill=(0, 255, 0))
    name = os.path.splitext(os.path.basename(path))[0]
    img.save(os.path.join(ART, "map_%s.png" % name))


# --------------------------------------------------------------------- icon

def build_icon():
    """16x16 program icon for shells like Cesium: a red D on black."""
    D = ["................",
         ".##########.....",
         ".###########....",
         ".###.....####...",
         ".###......###...",
         ".###.......###..",
         ".###.......###..",
         ".###.......###..",
         ".###.......###..",
         ".###.......###..",
         ".###......####..",
         ".###.....####...",
         ".###########....",
         ".##########.....",
         "................",
         "................"]
    img = Image.new("RGB", (16, 16), (16, 0, 0))
    for y, row in enumerate(D):
        for x, c in enumerate(row):
            if c == "#":
                shade = 255 - y * 8
                img.putpixel((x, y), (shade, 24, 16))
    img.save(os.path.join(ROOT, "icon.png"))


def main():
    for d in (GFX, GEN, OUT, ART):
        os.makedirs(d, exist_ok=True)
    print("DREAD asset build")
    pal = build_palette()
    texs = build_textures(pal)
    sprs, hud = build_sprites(pal)
    efr = build_enemies(pal)
    wfr = build_weapons(pal)
    run_convimg()
    verify_textures(pal, texs)
    verify_sprites(sprs, hud)
    verify_enemies(efr)
    verify_weapons(wfr)
    write_trig()
    write_assets_h()
    write_if_changed(os.path.join(GEN, "things.h"), TH.c_header())
    write_if_changed(os.path.join(GEN, "thingdefs.c"), TH.c_source(sprs + efr))
    write_hud_layout()
    build_levels(pal)
    build_icon()
    for var in ("DREADTX", "DREADSP", "DREADEN", "DREADWP"):
        shutil.move(os.path.join(GFX, var + ".8xv"), os.path.join(OUT, var + ".8xv"))
    print("done")


if __name__ == "__main__":
    main()
