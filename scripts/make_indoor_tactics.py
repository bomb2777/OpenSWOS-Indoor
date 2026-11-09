#!/usr/bin/env python3
"""make_indoor_tactics.py -- the five INDOOR tactics, made for the court by a model.

    python3 scripts/make_indoor_tactics.py          # writes src/indoor_tactics_data.c
    python3 scripts/indoor_tactics_sheet.py <dir>    # then look at them, one sheet each

docs/INDOOR.md, phases 3, 5.1 and 5.8. A SWOS tactic is, for each of the 35 ball sectors
(5 across x 7 along: sector = bx + 5 * by, by 0 the team's own end), where each outfield
player goes, on a 15 x 16 grid (x 0 the team's RIGHT, y 0 its own goal line; the game adds
the ball's place inside its sector on top). An indoor side has five outfield players
(ordinals 2-6), so a tactic fills positions[0..4] and leaves [5..9] empty.

Until 2026-11-09 each indoor player copied an outdoor preset's player (an 11-a-side role on
a big pitch) and the five were then spread apart -- which, with the ball in a corner, pulled
them apart again. Now, as in the owner's SWIS (libgdx-swis-2022, tactics/2-2_5x7.txt), the
five move as a team: each has a LINE (D, M, A) and a SIDE (R, C, L), and for every ball
sector

  1. along: its line's height follows the ball, from LINE_Y[line][0] with the ball at the
     own end to LINE_Y[line][1] at the opponents' (defenders up to the halfway line when the
     ball is at the opponents' goal: the owner), and a defender stays goal-side of the ball;
  2. across: its side's place, moved towards the ball's side by FOLLOW_X[line];
  3. closing up: pulled towards the ball by PULL x how wide the ball is x how near the
     player is (within REACH) -- with the ball in a corner the near players crowd in and
     the far ones tuck inside;
  4. spacing: never nearer a team-mate than MIN_GAP cells (pushed apart, a few rounds);
  5. clamped to the grid, inside the walls.

Tune by editing the numbers below, rerunning, looking at the sheets and playing. The lines
and sides of each tactic (SHAPES) are what src/indoor.c's line-up sort fills (kPlaces,
kSides): change one, change both. The restart tables give indoor 2-6 the places of outdoor
2, 5, 8, 10 and 11 (Indoor_RestartOrdinal).

The little pitch on the line-up screen (SIDE_PITCH) is drawn by hand: (x, y, flags) per
slot, keeper first, in the layout _sidePitchPositions* use (src/player_data.c).
"""
import math
import os

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "src", "indoor_tactics_data.c")

# Each tactic: (line, side) for ordinals 2..6, as kPlaces / kSides in src/indoor.c.
SHAPES = [
    ("2-2-1", [("D", "R"), ("D", "L"), ("M", "R"), ("M", "L"), ("A", "C")]),
    ("2-1-2", [("D", "R"), ("D", "L"), ("M", "C"), ("A", "R"), ("A", "L")]),
    ("1-2-2", [("D", "C"), ("M", "R"), ("M", "L"), ("A", "R"), ("A", "L")]),
    ("3-1-1", [("D", "R"), ("D", "C"), ("D", "L"), ("M", "C"), ("A", "C")]),
    ("1-3-1", [("D", "C"), ("M", "R"), ("M", "C"), ("M", "L"), ("A", "C")]),
]

# Height of each line (grid y, 0 own goal line .. 15 the opponents'; halfway 7.5) with
# the ball at the own end and at the opponents' end; linear in between.
LINE_Y = {"D": (2.0, 7.0), "M": (3.5, 10.5), "A": (6.5, 13.5)}
# How far a defender stays behind the ball (cells), when the ball is in the own half.
GOAL_SIDE = 1.5
# Across: a side's place (grid x; 0 the team's right) for a line of one, two or three.
SIDE_X = {1: {"C": 7.0}, 2: {"R": 4.0, "L": 10.0}, 3: {"R": 3.0, "C": 7.0, "L": 11.0}}
# How much of the ball's offset from the middle each line follows across.
FOLLOW_X = {"D": 0.35, "M": 0.5, "A": 0.45}
# Closing up: the pull towards the ball for a ball at a touchline (0 in the middle),
# fading to nothing at REACH cells from it -- full in the own end, none from PULL_UNTIL
# (ball rows) on: a SWOS tactic does not know who has the ball, so a team closing up in the
# opponents' half took its own width and passing options away (CPU v CPU: 9 goals a
# match became 0-1).
PULL, REACH, PULL_UNTIL = 0.30, 11.0, 4
# The nearest two team-mates may stand (cells).
MIN_GAP = 2.6
# The grid, kept off the glass and out of the own goalmouth.
X_MIN, X_MAX, Y_MIN, Y_MAX = 0, 14, 1, 14

KEEPER = (0x42, 0x06, 0x0000)
SIDE_PITCH = {
    "2-2-1": [KEEPER, (0x3F, 0x23, 0x4000), (0x45, 0x23, 0x8000), (0x03, 0x46, 0x8000), (0x81, 0x46, 0x4000), (0x42, 0x64, 0x0000)],
    "2-1-2": [KEEPER, (0x3F, 0x23, 0x4000), (0x45, 0x23, 0x8000), (0x42, 0x41, 0x0000), (0x3F, 0x5F, 0x4000), (0x45, 0x5F, 0x8000)],
    "1-2-2": [KEEPER, (0x42, 0x23, 0x0000), (0x03, 0x41, 0x8000), (0x81, 0x41, 0x4000), (0x3F, 0x5F, 0x4000), (0x45, 0x5F, 0x8000)],
    "3-1-1": [KEEPER, (0x03, 0x28, 0x8000), (0x42, 0x19, 0x0000), (0x81, 0x28, 0x4000), (0x42, 0x41, 0x0000), (0x42, 0x64, 0x0000)],
    "1-3-1": [KEEPER, (0x42, 0x23, 0x0000), (0x03, 0x46, 0x8000), (0x42, 0x37, 0x0000), (0x81, 0x46, 0x4000), (0x42, 0x64, 0x0000)],
}


def ball_cell(bx, by):
    """The middle of a ball sector on the 15 x 16 grid."""
    return (bx + 0.5) * 3.0 - 0.5, (by + 0.5) * 16.0 / 7.0 - 0.5


def sector_positions(shape, bx, by):
    """Where the five go with the ball in sector (bx, by): [(x, y)] floats."""
    ballx, bally = ball_cell(bx, by)
    progress = by / 6.0
    wide = min(1.0, abs(ballx - 7.0) / 6.0)
    counts = {}
    for line, _ in shape:
        counts[line] = counts.get(line, 0) + 1
    pos = []
    for line, side in shape:
        lo, hi = LINE_Y[line]
        y = lo + (hi - lo) * progress
        if line == "D" and bally < 7.5:
            y = min(y, max(Y_MIN, bally - GOAL_SIDE))
        x = SIDE_X[counts[line]][side] + FOLLOW_X[line] * (ballx - 7.0)
        # closing up on the ball, the more the wider it is and the nearer the player
        d = math.hypot(ballx - x, bally - y)
        k = PULL * wide * max(0.0, 1.0 - d / REACH) * max(0.0, 1.0 - by / float(PULL_UNTIL))
        x += k * (ballx - x)
        y += k * (bally - y)
        if line == "D" and bally < 7.5:
            y = min(y, max(Y_MIN, bally - GOAL_SIDE))
        pos.append([x, y])
    # spacing: push apart any two nearer than MIN_GAP
    for _ in range(30):
        moved = False
        for i in range(len(pos)):
            for j in range(i + 1, len(pos)):
                dx, dy = pos[j][0] - pos[i][0], pos[j][1] - pos[i][1]
                d = math.hypot(dx, dy)
                if d >= MIN_GAP:
                    continue
                if d < 1e-6:            # on top of each other: apart across
                    dx, dy, d = 1.0, 0.0, 1.0
                push = (MIN_GAP - d) / 2.0
                ux, uy = dx / d, dy / d
                pos[i][0] -= ux * push
                pos[i][1] -= uy * push
                pos[j][0] += ux * push
                pos[j][1] += uy * push
                moved = True
        for p in pos:
            p[0] = min(X_MAX, max(X_MIN, p[0]))
            p[1] = min(Y_MAX, max(Y_MIN, p[1]))
        if not moved:
            break
    return [(int(round(x)), int(round(y))) for x, y in pos]


def tactic(shape):
    """[player][35] position bytes (x << 4 | y), sector order."""
    players = [[0] * 35 for _ in shape]
    for by in range(7):
        for bx in range(5):
            for i, (x, y) in enumerate(sector_positions(shape, bx, by)):
                players[i][bx + 5 * by] = (x << 4) | y
    return players


def c_positions(flat):
    rows = [flat[r * 7:(r + 1) * 7] for r in range(5)]
    return "{ { " + ", ".join("{ " + ", ".join("0x%02X" % b for b in row) + " }" for row in rows) + " } }"


def main():
    lines = [
        "/* GENERATED by scripts/make_indoor_tactics.py -- do not edit by hand; edit its",
        " * numbers and rerun (look with scripts/indoor_tactics_sheet.py). The five INDOOR",
        " * tactics (docs/INDOOR.md, 5.1 and 5.8): positions[0..4] for ordinals 2-6, made by",
        " * the script's model; [5..9] empty. Installed in _teamTacticsTable[0..4] for an",
        " * indoor match only (indoor.c). */",
        '#include "indoor.h"',
        '#include "game_types.h"',
        "",
        "TeamTactics _indoorTactics[INDOOR_TACTICS] = {",
    ]
    empty = c_positions([0] * 35)
    for idx, (name, shape) in enumerate(SHAPES):
        nb = list(name.encode("ascii")) + [0] * (9 - len(name))
        lines.append("    {   /* %s */" % name)
        lines.append("        { " + ", ".join("0x%02X" % b for b in nb) + " },")
        lines.append("        {")
        players = [c_positions(p) for p in tactic(shape)]
        players += [empty] * (10 - len(shape))
        lines.append(",\n".join("            " + p for p in players))
        lines.append("        },")
        lines.append("        { " + ", ".join(["0xFF"] * 10) + " },   /* no pairs */")
        lines.append("        %d                                       /* ball out of play: itself */" % idx)
        lines.append("    },")
    lines.append("};")
    lines.append("")
    lines.append("/* The line-up screen's little pitch: (x, y, flags) per slot, as _sidePitchPositions*. */")
    lines.append("uint16_t _indoorSidePitch[INDOOR_TACTICS][33] = {")
    for name, _ in SHAPES:
        trip = SIDE_PITCH[name] + [KEEPER] * (11 - len(SIDE_PITCH[name]))
        vals = ", ".join("0x%X, 0x%X, 0x%X" % t for t in trip)
        lines.append("    { %s },   /* %s */" % (vals, name))
    lines.append("};")
    open(OUT, "w").write("\n".join(lines) + "\n")
    print("wrote", os.path.relpath(OUT, REPO))


if __name__ == "__main__":
    main()
