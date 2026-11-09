# INDOOR changelog

What this fork (`bomb2777/OpenSWOS-Indoor`, branch `indoor`) has added to OpenSWOS, newest
first. All of it applies only to a friendly played with INDOOR switched on; with it off, the
game is upstream's. How it works in detail: [docs/INDOOR.md](docs/INDOOR.md).

## 2026-11-09

- **New indoor tactics, made for the court:**
  - Each tactic starts from a balanced shape (defence, midfield, attack; left, centre, right).
  - The team moves with the ball as a block.
  - In the own half the players near a ball on the wing close up around it; going forward
    the team stays wide.
  - With the ball at the opponents' goal the defenders push up to the halfway line.
  - Older builds can't play online against this one (compat 1005).

## 2026-11-04

- **Online play (PC):**
  - MULTIPLAYER on the main menu connects two players directly by IP address.
  - In the lobby the host picks LINOLEUM on PITCH TYPE (after RANDOM) to play the match
    indoors. This sets SUBSTITUTES to 5 FROM 5.
  - Only builds of this fork play each other. Official OpenSWOS builds are refused.
- **The court is teal** (`#118888`, the penalty areas and centre circle `#33AAAA`). With the
  blue court, blue kits were hard to see; teal stands out from every kit colour. The little
  pitch on the PLAY MATCH screen is a green and blue checkerboard to match.
- **No red blink on a post or the bar** any more; hitting the woodwork only makes its sound.
- **The side glass is half as wide** (5 px instead of 10).
- **Fixed:** after a goal's replay the bottom glass could turn solid white (PC).
- **The glass marker** is now the SWIS ring and cross (replacing the white flash):
  - It appears on every touch of the glass. Only hard hits make a thud.
  - On the end walls it sits at the ball's height.
  - On the side walls it sits on the white side line.
- **Indoor menu music (PC):** while INDOOR shows LINOLEUM on the FRIENDLY screen, the menus
  play the hall's own drum & bass track. OFF brings back the normal menu music.
- README: a block of its own for the fork, with a screenshot of an indoor match.

## 2026-11-03

- **Futsal-style substitutions:**
  - The bench can substitute while a keeper holds the ball, with no limit.
  - A substituted player may come back on.
  - Never the keeper holding the ball.
- **A hard hit on the glass** lights it up with a thud (later replaced by the marker).
- **Replays show the glass.** A saved `.SRP` of an indoor match plays back in the hall.
- **The wall stands on the line from the ball to the goal** for every free kick.
- **Defenders stand wider and higher** in 2-2-1, 2-1-2, 1-2-2 and 1-3-1. At restarts the two
  defenders take the full backs' places.
- **The line-up by side and strength:**
  - A clearly better forward on the bench takes a midfielder's place (e.g. Croatia: Suker for
    Prosinecki).
  - Each line's players stand on their own side: right backs and right wingers on the right,
    and so on.
- **The bench during play** shows the coach and four players, on the PC as on the Amiga.
- **The mini court on PLAY MATCH** has no corner arcs and no penalty-area arcs.
- **Amiga:**
  - The glass is a checkerboard baked into the hall's map, so it costs no time per frame.
  - The players, the ball and the referee behind the bottom wall are greyed.
  - The bench figures show as on the PC.

## 2026-11-02

- **Six a side** (a keeper and five) on a bigger court, 318 x 482.
- **Five indoor tactics: 2-2-1, 2-1-2, 1-2-2, 3-1-1, 1-3-1.** The players stand further apart
  than at first.
- **The best six start:** for the team's tactic, each place is filled with the most valuable
  player of that position. The pre-match screen lists the six.
- **Clock 2 x 20 minutes**, in the real time the GAME LENGTH option gives 2 x 45.
- **The keeper's throw** must not cross the halfway line untouched: if it does, the other side
  gets a free kick on the halfway line.
- **A red card is a 5-minute time penalty:**
  - The player sits out by his dugout with a small red card over him, then comes back on.
  - His side plays one short meanwhile.
- **A wall of two** at free kicks.
- **The ball:**
  - It rolls further.
  - It keeps most of its speed off the glass.
  - It bounces off the glass after a whistle too.
- **The players are held inside the glass**, during stoppages as well.
- **The hall:**
  - Stands along both sides and behind the goals, with animated fans.
  - Two dugouts beside the court on the left.
  - The camera stays inside the hall.
- **The glass:**
  - Grey and see-through, along the sides and at both ends in perspective (80 px high).
  - Clear holes where the goals stand.
  - The top wall is behind the players, the bottom wall in front of them.
- **Court markings:** lighter penalty areas and centre circle inside white lines.
- **The mini court on PLAY MATCH** is drawn as the court.
- **Players walk on** from the court's edge, not through the stand.

## 2026-11-01

- **Indoor tactics** (first four, later five) and their own buttons on the line-up screen;
  no EDIT TACTICS indoors.
- **The court and its glass walls:**
  - The ball never goes out of play. It comes back off the glass.
  - Only a goal crosses a goal line.
- **The hall's floor**, LINOLEUM, drawn from the SWIS hall, with the crowd behind the goals
  and the top goal drawn in.
- **Switching INDOOR on sets SUBSTITUTES to 5 FROM 5.** Injured players are kept off the
  indoor bench.

## 2026-10-31

- **The INDOOR row on the FRIENDLY screen** (OFF / LINOLEUM), on the PC and the Amiga:
  - While it shows a floor, PITCH TYPE follows the floor.
  - It never applies to competitions, career, training, arcade or online matches.
- **Five a side in the match** (first version), with a big bench. The line-up screen and the
  substitutions are changed to match.

## Not yet

- **More floors:**
  - Wood (HARD).
  - Stone (FROZEN).
  - Beach sand (MUDDY).
- **The menu music on the Amiga.**
