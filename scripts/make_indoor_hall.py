#!/usr/bin/env python3
"""make_indoor_hall.py -- the INDOOR hall's floor as a pitch map (docs/INDOOR.md, phase 5).

    python3 scripts/make_indoor_hall.py [--src playground_v2.png] [--preview out.png] [--amiga]

Writes bin/grafs_indoor/LINO.MAP: the blue linoleum court in a parquet hall, in the
format of the game's own pitch maps (an 8-bitplane .MAP like grafs_aga/P2020_n.MAP), which
both targets load in place of the pitch for an indoor match (pitch.c LoadPitchMap).

The look is the owner's SWIS hall (libgdx-swis-2022, core/assets/images/swis/hall/
playground_v2.png, its author's own art): the court's blues and white lines and the
parquet are taken from it, but the court is REDRAWN at the game's geometry
(include/indoor.h: 318 x 482, penalty area 148 x 75, ...) rather than scaled, so every line
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

The first 27 tiles are blanks: tiles 1-26 are the crowd's in every SWOS pitch bank, and
the game fills them at load from the player's own pitch (pitch.c, indoorTakeCrowd), so no
original art is in this file. The stands are three rows behind each goal (indoor.h
INDOOR_CROWD_*), laid out as a stadium's (map rows 1-3 / 51-53), and row 0 lists the crowd
tiles as every pitch's does -- the kit recolouring reads it (FixupAndColorPitchFile); the
camera never shows it. Side stands run between the end stands, and the bench's dugouts
stand beside the court on the left (phase 5.1; indoor.h INDOOR_SIDE_*, INDOOR_BENCH_X).
"""
import argparse
import os
import re

from PIL import Image, ImageDraw

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SWIS = os.path.expanduser("~/zz_max/projects/libgdx-swis-2022/core/assets/images/swis/hall/playground_v2.png")
OUT = os.path.join(REPO, "bin", "grafs_indoor", "LINO.MAP")
OUT_AMIGA = os.path.join(REPO, "bin", "grafs_indoor", "LINO_A.MAP")

# include/indoor.h -- keep in step.
HALF_W, HALF_L = 159, 241
CROWD_ROWS = 5                                  # INDOOR_CROWD_ROWS
CROWD_TOP_ROW = (449 - HALF_L) // 16 - 2 - CROWD_ROWS  # INDOOR_CROWD_TOP_ROW: 6
CROWD_BOTTOM_ROW = (449 + HALF_L + 16) // 16 + 1  # INDOOR_CROWD_BOTTOM_ROW: 45
BOX_HALF_W, BOX_DEPTH = 74, 75
# The sides of the hall (indoor.h, phase 5.1): the bench's right edge, the side stands.
GLASS_LEFT, GLASS_RIGHT = 10, 10                  # INDOOR_GLASS_LEFT / _RIGHT
HOLE_X0, HOLE_X1, HOLE_H = 300, 372, 24           # INDOOR_GLASS_HOLE_*: the goal's
BENCH_X = 336 - HALF_W - GLASS_LEFT - 4           # INDOOR_BENCH_X: 163
SIDE_COLS = 8
SIDE_LEFT_COL = (BENCH_X - 16 - 5) // 16 - SIDE_COLS    # 0
SIDE_RIGHT_COL = (336 + HALF_W + GLASS_RIGHT + 15) // 16  # 32
SIDE_FIRST_ROW = CROWD_TOP_ROW + CROWD_ROWS             # 11
SIDE_ROWS = CROWD_BOTTOM_ROW - SIDE_FIRST_ROW           # 34
DUGOUTS_Y = [(368, 430), (464, 526)]                    # the outdoor dugouts' (bench_subs.c 389/485)
PENALTY_SPOT = 71
CX, CY = 336, 449
LEFT, RIGHT, TOP, BOTTOM = CX - HALF_W, CX + HALF_W, CY - HALF_L, CY + HALF_L

# The SWIS hall, in its own units (GlobalValues.java): court x 172..1127, y 296..1694
# (libgdx: y up). Our court is 234 wide for its 955: the hall's scale here.
SWIS_COURT_W = 1127 - 172
SCALE = (2 * HALF_W) / SWIS_COURT_W

WORLD_W, WORLD_H = 672, 880
COLS, ROWS = 42, 55
RESERVED = {0, 7, 9, 78, 79, 80, 81, 106, 107, 10, 11} | set(range(112, 128)) | set(range(240, 256))
FAN_TILES = 27      # tile 0 and the crowd's 1-26, blank

# The crowd's cells, as every SWOS pitch lays them out (tile numbers of the bank): row 0 the
# list the kit colours are applied through (cells 0-20 one team's, 21-41 the other's), the
# top stand (stadium rows 1-3, nearest the pitch last) and the bottom one (rows 51-53,
# nearest first; the front row's aisle is tile 25, which a stadium has from the end of
# its bank). Odd tiles are fans sitting, the next even one the same fans jumping.
ROW0 = [0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 25, 26, 26, 0, 0, 0]
TOP_STAND = [
    [1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 9, 3, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 9, 3, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3],
    [5, 7, 5, 7, 5, 7, 5, 7, 5, 7, 5, 7, 11, 5, 7, 5, 7, 5, 7, 5, 7, 5, 7, 5, 7, 5, 7, 5, 7, 11, 5, 7, 5, 7, 5, 7, 5, 7, 5, 7, 5, 7],
    [1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 9, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 9, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1],
]
BOTTOM_STAND = [
    [13, 15, 13, 15, 13, 15, 13, 15, 13, 15, 13, 15, 25, 13, 15, 13, 15, 13, 15, 13, 15, 13, 15, 13, 15, 13, 15, 13, 15, 25, 13, 15, 13, 15, 13, 15, 13, 15, 13, 15, 13, 15],
    [17, 19, 17, 19, 17, 19, 17, 19, 17, 19, 17, 19, 25, 17, 19, 17, 19, 17, 19, 17, 19, 17, 19, 17, 19, 17, 19, 17, 19, 25, 17, 19, 17, 19, 17, 19, 17, 19, 17, 19, 17, 19],
    [23, 21, 23, 21, 23, 21, 23, 21, 23, 21, 23, 21, 26, 23, 21, 23, 21, 23, 21, 23, 21, 23, 21, 23, 21, 23, 21, 23, 21, 26, 23, 21, 23, 21, 23, 21, 23, 21, 23, 21, 23, 21],
]


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
    ap.add_argument("--amiga", action="store_true",
                    help="the Amiga's map, LINO_A.MAP: the glass at the ends drawn into the floor")
    args = ap.parse_args()

    pal = game_palette()
    pal_rgb = pal
    grey = lambda i: pal_rgb[i]
    src = Image.open(args.src).convert("RGB")
    # The court's colours, sampled where the SWIS hall has them (image y is down:
    # 2000 - libgdx y).
    court = src.getpixel((300, 1000))       # the court, left of the centre circle
    box = src.getpixel((650, 1900 - 1550))  # the penalty area, top
    circle = src.getpixel((650, 1000))      # inside the centre circle
    line = (255, 255, 255)
    court_shadow = (17, 17, 136)             # the net's inside at the line: a dark floor blue
    floor_shadow = (119, 85, 51)             # a shadow on the parquet
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

    # The top goal's frame and net, behind the goal line. A stadium paints its goals into
    # the pitch; the goal SPRITES only add what must be drawn over the players -- the
    # bottom goal's front, the top goal's strip of net 18-23 px behind the line. So this
    # draws the rest of the top one: the crossbar, the posts, the net falling to the line,
    # its inside in shadow. Our own drawing, in the goal sprites' greys (palette 50-59).
    gx0, gx1 = CX - 36, CX + 36              # the frame, outside to outside (300..372)
    d.line([gx0, TOP - 18, gx1, TOP - 18], fill=grey(59))       # crossbar
    d.line([gx0 + 1, TOP - 17, gx1, TOP - 17], fill=grey(50))
    shades = [54, 53, 52, 52, 52, 51, 51, 50]
    for i in range(16):                      # the net, light at the top to dark at the line
        y = TOP - 16 + i
        c = grey(shades[i]) if i < len(shades) else court_shadow
        d.line([gx0 + 2, y, gx1 - 1, y], fill=c)
        if i % 2 == 0 and i < len(shades):   # its mesh
            for x in range(gx0 + 2 + (i // 2) % 2, gx1 - 1, 2):
                d.point((x, y), fill=grey(55))
    # The bench's two dugouts, beside the court on the left (our drawing, the goal
    # greys): a roof, its rim, and the seats under it, a shadow towards the court.
    for (y0, y1) in DUGOUTS_Y:
        x0, x1 = BENCH_X - 16, BENCH_X - 1
        d.rectangle([x0 + 2, y0 + 3, x1 + 3, y1 + 3], fill=floor_shadow)
        d.rectangle([x0, y0, x1, y1], fill=grey(54), outline=grey(58))
        d.line([x0 + 2, y0 + 1, x0 + 2, y1 - 12], fill=grey(59))
        d.rectangle([x0, y1 - 10, x1, y1], fill=grey(51), outline=grey(50))
        for x in range(x0 + 3, x1, 4):
            d.line([x, y1 - 9, x, y1 - 1], fill=grey(50))

    for i in range(18):                      # the posts, white in the light
        y = TOP - 17 + i
        c = grey([59, 58, 57, 56, 55][min(i // 4, 4)])
        d.point((gx0, y), fill=c)
        d.point((gx1 - 1, y), fill=c)
        d.point((gx0 + 1, y), fill=grey(50))
        d.point((gx1, y), fill=grey(50))

    # The glass (phase 5.3; the owner's SWIS hall: wall_transparent.png, sidewall_*.png --
    # grey, (98, 98, 98) at about 60 % over what is behind it), drawn last, over the goal.
    # Along each side, end stand to end stand, from the bench (left) or the stand (right)
    # to the court, with a light rim on its outside. The glass at the ends is seen in
    # perspective -- the top wall hides what is behind it, the top goal, the floor and the
    # nearest fans, the bottom one the court's last strip and the players there -- which
    # the PC draws over each frame (indoor.c, Indoor_DrawGlass); the Amiga, which cannot,
    # has its floor here (--amiga, LINO_A.MAP): the strip behind the top goal line, and as
    # high up the court from the bottom one (indoor.h INDOOR_GLASS_HEIGHT). Both with the
    # goal's hole, clear.
    gx0, gx1 = 336 - HALF_W - GLASS_LEFT, 336 + HALF_W + GLASS_RIGHT
    gtop = (CROWD_TOP_ROW + CROWD_ROWS) * 16
    gheight = TOP - (CROWD_TOP_ROW + CROWD_ROWS - 3) * 16      # 80
    def glass(box, hole=None):
        x0, y0, x1, y1 = box
        keep = img.crop(hole) if hole else None
        region = img.crop((x0, y0, x1 + 1, y1 + 1))
        tint = Image.new("RGB", region.size, (98, 98, 98))
        img.paste(Image.blend(region, tint, 0.6), (x0, y0))
        if hole:
            img.paste(keep, hole[:2])
    rim = grey(56)
    glass((gx0, TOP, LEFT - 1, BOTTOM))             # the left side
    glass((RIGHT + 1, TOP, gx1, BOTTOM))            # the right side
    d.line([gx0, TOP, gx0, BOTTOM], fill=rim)
    d.line([gx1, TOP, gx1, BOTTOM], fill=rim)
    if args.amiga:
        glass((gx0, gtop, gx1, TOP - 1), (HOLE_X0, TOP - HOLE_H, HOLE_X1 + 1, TOP))
        glass((gx0, BOTTOM - gheight, gx1, BOTTOM), (HOLE_X0, BOTTOM - HOLE_H, HOLE_X1 + 1, BOTTOM + 1))
        d.line([gx0, gtop, gx1, gtop], fill=rim)
        d.line([gx0, BOTTOM - gheight, gx1, BOTTOM - gheight], fill=rim)

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

    # The crowd's cells over the floor drawn there.
    def put(r, row):
        cells[r * COLS:(r + 1) * COLS] = row
    put(0, ROW0)
    # Five rows deep (phase 5.3): the stadium's rows repeated, the three nearest the court
    # last at the top and first at the bottom -- those are the ones the fans animate in.
    top = [TOP_STAND[1], TOP_STAND[2]] + TOP_STAND
    bottom = BOTTOM_STAND + [BOTTOM_STAND[1], BOTTOM_STAND[2]]
    for i, row in enumerate(top[-CROWD_ROWS:]):
        put(CROWD_TOP_ROW + i, row)
    for i, row in enumerate(bottom[:CROWD_ROWS]):
        put(CROWD_BOTTOM_ROW + i, row)
    # The side stands: the top team's fans on the left (1/3), the bottom team's on the
    # right (17/19) -- the end stands' plain rows; their other rows have a step or the
    # boards' edge drawn in, which in a column comes out as stripes. A stair every ten
    # rows (9, 25).
    for i in range(SIDE_ROWS):
        r = SIDE_FIRST_ROW + i
        aisle = i % 10 == 5
        for k in range(SIDE_COLS):
            cells[r * COLS + SIDE_LEFT_COL + k] = 9 if aisle else (1, 3)[(i + k) % 2]
            cells[r * COLS + SIDE_RIGHT_COL + k] = 25 if aisle else (17, 19)[(i + k) % 2]

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
    out = OUT_AMIGA if args.amiga else OUT
    os.makedirs(os.path.dirname(out), exist_ok=True)
    open(out, "wb").write(data)
    used = sorted({v for t in tiles[FAN_TILES:] for v in t})
    print("wrote %s: %d bytes, %d tiles (%d unique + %d blanks), palette entries %s"
          % (os.path.relpath(out, REPO), len(data), len(tiles), len(tiles) - FAN_TILES,
             FAN_TILES, used))

    if args.preview:
        prev = Image.new("RGB", (WORLD_W, WORLD_H))
        prev.putdata([pal[v] for row in idx for v in row])
        prev.save(args.preview)


if __name__ == "__main__":
    main()
