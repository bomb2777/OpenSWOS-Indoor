#!/usr/bin/env python3
"""make_indoor_hall.py -- the INDOOR hall's floor as a pitch map (docs/INDOOR.md, phase 5).

    python3 scripts/make_indoor_hall.py [--src playground_v2.png] [--preview out.png]

Writes bin/grafs_indoor/LINO.MAP: the blue linoleum court in a parquet hall, in the
format of the game's own pitch maps (an 8-bitplane .MAP like grafs_aga/P2020_n.MAP), which
both targets load in place of the pitch for an indoor match (pitch.c LoadPitchMap).

The look is the owner's SWIS hall (libgdx-swis-2022, core/assets/images/swis/hall/
playground_v2.png, its author's own art): the court's blues and white lines and the
parquet are taken from it, but the court is REDRAWN at the game's geometry
(include/indoor.h: 234 x 354, penalty area 108 x 55, ...) rather than scaled, so every line
is sharp and exactly where the match code has it. The parquet is a 32 x 32 patch of the
hall's, scaled like the court and repeated: a parquet that never repeats would be ~2300
unique tiles, more than the Amiga's pitch buffer holds; repeated it is four.

A pitch map is 42 x 55 cells of 16 x 16 (the 672 x 880 world): 2310 big-endian byte offsets
into a tile area, then the tiles, 16 rows of eight 16-bit plane words each (256 bytes).
Colours are indices into the game palette -- the AGA palette (src/swos_amiga_data_imports.c),
which is the DOS one index for index, so one map is right in both. Never used:

  0 7 9 78 79 80 81 106 107   the pitch condition's entries (_paletteIndices), rewritten
                              per pitch type
  10 11                       recoloured to the kits wherever row 0's tiles have them
                              (the crowd's, FixupAndColorPitchFile)
  112-127                     the replay "R" (docs/PALETTE_V2.md)
  240-255                     the kit block

and the first 23 tiles are blanks nobody uses: tiles 0-22 are the crowd's, which the fan
animation swaps in pairs wherever map rows 1-3 / 51-53 show them (animated_patterns.c).
"""
import argparse
import os
import re

from PIL import Image, ImageDraw

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SWIS = os.path.expanduser("~/zz_max/projects/libgdx-swis-2022/core/assets/images/swis/hall/playground_v2.png")
OUT = os.path.join(REPO, "bin", "grafs_indoor", "LINO.MAP")

# include/indoor.h -- keep in step.
HALF_W, HALF_L = 117, 177
BOX_HALF_W, BOX_DEPTH = 54, 55
PENALTY_SPOT = 53
CX, CY = 336, 449
LEFT, RIGHT, TOP, BOTTOM = CX - HALF_W, CX + HALF_W, CY - HALF_L, CY + HALF_L

# The SWIS hall, in its own units (GlobalValues.java): court x 172..1127, y 296..1694
# (libgdx: y up). Our court is 234 wide for its 955: the hall's scale here.
SWIS_COURT_W = 1127 - 172
SCALE = (2 * HALF_W) / SWIS_COURT_W

WORLD_W, WORLD_H = 672, 880
COLS, ROWS = 42, 55
RESERVED = {0, 7, 9, 78, 79, 80, 81, 106, 107, 10, 11} | set(range(112, 128)) | set(range(240, 256))
FAN_TILES = 23


def game_palette():
    src = open(os.path.join(REPO, "src", "swos_amiga_data_imports.c")).read()
    m = re.search(r"uint16_t _gamePalette\[256\] = \{(.*?)\};", src, re.S)
    words = [int(w, 16) for w in re.findall(r"0x([0-9A-Fa-f]{4})", m.group(1))]
    assert len(words) == 256
    return [(((w >> 8) & 15) * 17, ((w >> 4) & 15) * 17, (w & 15) * 17) for w in words]


def nearest(pal, rgb):
    best, bi = None, 0
    for i, c in enumerate(pal):
        if i in RESERVED:
            continue
        d = sum((a - b) ** 2 for a, b in zip(c, rgb))
        if best is None or d < best:
            best, bi = d, i
    return bi


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default=SWIS)
    ap.add_argument("--preview", help="also write the floor as a PNG (in the game's colours)")
    args = ap.parse_args()

    src = Image.open(args.src).convert("RGB")
    # The court's colours, sampled where the SWIS hall has them (image y is down:
    # 2000 - libgdx y).
    court = src.getpixel((300, 1000))       # the court, left of the centre circle
    box = src.getpixel((650, 1900 - 1550))  # the penalty area, top
    circle = src.getpixel((650, 1000))      # inside the centre circle
    line = (255, 255, 255)
    # The parquet: a patch from the hall's margin, scaled like the court, repeated.
    patch_src = src.crop((10, 10, 10 + round(32 / SCALE), 10 + round(32 / SCALE)))
    patch = patch_src.resize((32, 32), Image.LANCZOS)

    img = Image.new("RGB", (WORLD_W, WORLD_H))
    for y in range(0, WORLD_H, 32):
        for x in range(0, WORLD_W, 32):
            img.paste(patch, (x, y))
    d = ImageDraw.Draw(img)
    d.rectangle([LEFT, TOP, RIGHT, BOTTOM], fill=court)
    d.rectangle([CX - BOX_HALF_W, TOP, CX + BOX_HALF_W, TOP + BOX_DEPTH], fill=box)
    d.rectangle([CX - BOX_HALF_W, BOTTOM - BOX_DEPTH, CX + BOX_HALF_W, BOTTOM], fill=box)
    # The centre circle: the hall's ellipse (about 220 x 180 there), scaled.
    rx, ry = round(110 * SCALE), round(90 * SCALE)
    d.ellipse([CX - rx, CY - ry, CX + rx, CY + ry], fill=circle, outline=line)
    d.line([LEFT, CY, RIGHT, CY], fill=line)
    d.rectangle([LEFT, TOP, RIGHT, BOTTOM], outline=line)
    for y in (CY, TOP + PENALTY_SPOT, BOTTOM - PENALTY_SPOT):
        d.rectangle([CX - 1, y - 1, CX, y], fill=line)

    pal = game_palette()
    cache = {}
    idx = [[0] * WORLD_W for _ in range(WORLD_H)]
    for y in range(WORLD_H):
        for x in range(WORLD_W):
            rgb = img.getpixel((x, y))
            if rgb not in cache:
                cache[rgb] = nearest(pal, rgb)
            idx[y][x] = cache[rgb]

    # Tiles: FAN_TILES blanks, then each distinct tile in map order.
    tiles = [bytes(256)] * FAN_TILES
    seen = {}
    cells = []
    for r in range(ROWS):
        for c in range(COLS):
            t = bytes(idx[r * 16 + yy][c * 16 + xx] for yy in range(16) for xx in range(16))
            if t not in seen:
                seen[t] = len(tiles)
                tiles.append(t)
            cells.append(seen[t])

    def planar(t):
        out = bytearray()
        for row in range(16):
            px = t[row * 16:(row + 1) * 16]
            for p in range(8):
                w = 0
                for x in range(16):
                    if (px[x] >> p) & 1:
                        w |= 1 << (15 - x)
                out += w.to_bytes(2, "big")
        return bytes(out)

    data = b"".join((n * 256).to_bytes(4, "big") for n in cells)
    data += b"".join(planar(t) for t in tiles)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    open(OUT, "wb").write(data)
    used = sorted({v for t in tiles[FAN_TILES:] for v in t})
    print("wrote %s: %d bytes, %d tiles (%d unique + %d blanks), palette entries %s"
          % (os.path.relpath(OUT, REPO), len(data), len(tiles), len(tiles) - FAN_TILES,
             FAN_TILES, used))

    if args.preview:
        prev = Image.new("RGB", (WORLD_W, WORLD_H))
        prev.putdata([pal[v] for row in idx for v in row])
        prev.save(args.preview)


if __name__ == "__main__":
    main()
