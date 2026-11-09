#!/usr/bin/env python3
"""indoor_tactics_sheet.py -- draw the INDOOR tactics, one sheet per tactic.

    python3 scripts/indoor_tactics_sheet.py OUTDIR [src/indoor_tactics_data.c]
    python3 scripts/indoor_tactics_sheet.py OUTDIR --swis <SWIS tactic .txt>

docs/INDOOR.md 5.8. A sheet is the 35 ball sectors as small courts (5 across, 7 along;
the team attacks UPWARDS, its own goal at the bottom), each with the ball's sector shaded,
the ball's middle as a black dot, and the five outfield players where the tactic sends
them for it: defenders blue, midfielders green, attackers red, numbered 2-6 (ordinals).

Grid: a SWOS tactic places a player on 15 x 16 cells (x 0 the team's RIGHT, y 0 its own
goal line). Drawn with the team's right on the right, as seen from behind its goal.

--swis draws a tactic of the owner's SWIS (libgdx-swis-2022, assets/tactics/*_5x7.txt):
per ball sector a target CELL of the same 5 x 7 grid for the keeper and four players; the
cells' middles are drawn on the same 15 x 16 scale, keeper grey.
"""
import os
import re
import sys

from PIL import Image, ImageDraw

CELL = 9                    # pixels per grid cell
W, H = 15 * CELL, 16 * CELL
PAD = 6
LINE_COLOURS = {"D": (40, 90, 220), "M": (20, 150, 60), "A": (210, 40, 40), "K": (120, 120, 120)}

# Each tactic's lines, ordinal 2 first (as kPlaces in src/indoor.c).
LINES = {
    "2-2-1": "DDMMA", "2-1-2": "DDMAA", "1-2-2": "DMMAA", "3-1-1": "DDDMA", "1-3-1": "DMMMA",
}


def ball_cell(bx, by):
    """The middle of a ball sector on the 15 x 16 grid (cell units)."""
    return (bx + 0.5) * 3.0, (by + 0.5) * 16.0 / 7.0


def to_px(x, y):
    """Grid (x 0 = the team's right, y 0 = own goal) -> pixels, right on the right."""
    return (14.5 - x) * CELL, (15.5 - y) * CELL


def read_c(path):
    """{name: [player][35] of (x, y)} from a generated indoor_tactics_data.c."""
    text = open(path).read()
    out = {}
    for m in re.finditer(r"/\* (\d-\d-\d) \*/\s*\{[^}]*\},\s*\{(.*?)\n        \},", text, re.S):
        name, body = m.group(1), m.group(2)
        players = []
        for pl in body.strip().split("\n")[:5]:
            vals = [int(v, 16) for v in re.findall(r"0x([0-9A-F]{2})", pl)]
            players.append([(v >> 4, v & 15) for v in vals[:35]])
        out[name] = players
    return out


def read_swis(path):
    """SWIS: 35 rows of keeper;p1;p2;p3;p4 target cells -> (x, y) cell middles."""
    rows = []
    for line in open(path):
        line = line.split("#")[0].strip()
        if line:
            rows.append([int(v) for v in line.strip(";").split(";")])
    players = [[None] * 35 for _ in range(5)]
    for sec, row in enumerate(rows[:35]):
        for p, cell in enumerate(row[:5]):
            # SWIS's upward grid: row from its own goal, column from the team's right.
            players[p][sec] = ball_cell(cell % 5, cell // 5)
    return players


def draw(name, players, lines, path):
    cols, rows = 5, 7
    img = Image.new("RGB", (cols * (W + PAD) + PAD, rows * (H + PAD) + PAD + 16), (250, 250, 250))
    d = ImageDraw.Draw(img)
    d.text((PAD, 2), "%s   (attacking upwards; the team's right on the right)" % name, fill=(0, 0, 0))
    for by in range(7):
        for bx in range(5):
            ox = PAD + (4 - bx) * (W + PAD)          # bx 0 is the team's right: drawn right
            oy = 16 + PAD + (6 - by) * (H + PAD)     # by 6 the opponents' end: drawn top
            d.rectangle([ox, oy, ox + W - 1, oy + H - 1], fill=(64, 150, 150), outline=(0, 0, 0))
            d.line([ox, oy + H // 2, ox + W - 1, oy + H // 2], fill=(220, 240, 240))
            gw = 4 * CELL                             # the goals
            d.rectangle([ox + (W - gw) // 2, oy + H - 3, ox + (W + gw) // 2, oy + H - 1], fill=(255, 255, 255))
            d.rectangle([ox + (W - gw) // 2, oy, ox + (W + gw) // 2, oy + 2], fill=(255, 255, 255))
            # the ball's sector, shaded, and its middle
            sx0, sy0 = (4 - bx) * 3 * CELL, (6 - by) * H / 7
            d.rectangle([ox + sx0, oy + sy0, ox + sx0 + 3 * CELL - 1, oy + sy0 + H / 7 - 1], outline=(255, 255, 120))
            bxp, byp = to_px(*ball_cell(bx, by))
            d.ellipse([ox + bxp - 3, oy + byp - 3, ox + bxp + 3, oy + byp + 3], fill=(0, 0, 0))
            sec = bx + 5 * by
            for i, pl in enumerate(players):
                pos = pl[sec]
                if pos is None:
                    continue
                px, py = to_px(*pos)
                col = LINE_COLOURS[lines[i]]
                d.ellipse([ox + px - 5, oy + py - 5, ox + px + 5, oy + py + 5], fill=col, outline=(255, 255, 255))
                d.text((ox + px - 3, oy + py - 6), str(i + 2) if lines[i] != "K" else "K", fill=(255, 255, 255))
            d.text((ox + 2, oy + 2), str(sec), fill=(230, 230, 230))
    img.save(path)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    if len(sys.argv) > 3 and sys.argv[2] == "--swis":
        players = read_swis(sys.argv[3])
        name = os.path.basename(sys.argv[3]).split("_")[0]
        path = os.path.join(out, "swis-%s.png" % name)
        draw("SWIS " + name, players, "KDDAA", path)
        print("wrote", path)
        return
    src = sys.argv[2] if len(sys.argv) > 2 else "src/indoor_tactics_data.c"
    for name, players in read_c(src).items():
        path = os.path.join(out, "%s.png" % name)
        draw(name, players, LINES[name], path)
        print("wrote", path)


if __name__ == "__main__":
    main()
