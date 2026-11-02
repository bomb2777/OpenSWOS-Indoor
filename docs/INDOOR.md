# INDOOR.md — six a side in a hall

> **Status (2026-11-02): in progress — phases 1–5 done (the switch, five a side, tactics, court and walls, the hall), and phase 5.1 (six a side, the indoor rules, the hall's sides); phase 6 (online) open.** This fork's own
> feature (`bomb2777/OpenSWOS-Indoor`, branch `indoor`); upstream OpenSWOS has no INDOOR.

## What it is

A **friendly** can be played indoors: six a side (a keeper and five), on a small pitch
with glass walls all round, so the ball never goes out of play — the only stoppages are a
goal, a foul (free kick, penalty), the keeper's throw over the halfway line and half time.
The match is 2 × 20 minutes; a red card is a 5-minute time penalty; a free-kick wall is two
players. Squads, the database, the controls and every other screen are unchanged.

The **floor** of the hall decides how the ball plays, as a SWOS pitch type:

| floor | pitch type | drawn |
|---|---|---|
| WOOD | HARD | not yet |
| LINOLEUM (blue) | HARD | phase 5 |
| STONE | FROZEN (ice) | not yet |
| BEACH SAND | MUDDY | not yet |

Only LINOLEUM can be chosen until the others have a pitch.

**The rule everything follows:** with INDOOR off, a match plays exactly as upstream's. Every
change is behind `_gIndoor`, and nothing outside a friendly can set it.

## The switch

- **FRIENDLY screen:** an INDOOR row under SUBSTITUTES (entries 11–12 of
  `bin/data_json/menus/friendlyGameMenu.json`; `FriendlyIndoorProc`, `FriendlyIndoorRefresh`
  in `src/menus.c`). It toggles OFF ⇄ LINOLEUM. While it shows a floor, PITCH TYPE shows that
  floor's type and cannot be changed; OFF gives back what it said before. Like the pitch, it
  is fixed once the teams are chosen. **Both targets** have it: INDOOR is shared match code,
  and only online play (phase 6) is the PC's alone, as netplay is.
- **The match:** `Indoor_BeginMatch()` (`src/indoor.c`), called by `InitAndPlayGame`
  (`src/game_init.c`) just before kick-off, sets `_gIndoor`/`_gIndoorFloor` and pins
  `_gamePitchType` to the floor's type; `Indoor_EndMatch()` clears it when the match is over.
  It stays 0 for anything but a friendly: competitions, career, training, arcade and online
  matches (the lobby has no INDOOR yet — phase 6).
- **For a test:** `SWOS_INDOOR=1` plays every friendly indoors (=0 none), whatever the screen
  says. `SWOS.log` says `[indoor] this friendly is played indoors: ...`.
- Two flags, kept apart: `_gIndoorFriendlyFloor` is the screen's choice, `_gIndoor` the match
  being played. Match code tests `_gIndoor` only.

## Plan

1. **The switch** — done (above).
2. **Five a side** — (a) done: ordinals 6–11 parked beside the pitch every frame
   (`Indoor_ParkPlayers`, from the frame body before the players are updated) as if sent off
   (`sentAway`, which the AI, ball control and the kick-off wait already skip); the restart
   tables (kick-off, free kicks, penalties, ...) give indoor players 2–5 the places of 3, 4,
   10 and 11 (`Indoor_RestartOrdinal`: two centre backs and the two forwards who kick off),
   and the free-kick wall, which counts on ordinals 2–5, is skipped. During play they still
   follow the team's (outdoor) tactic slots 0–3 until phase 3. (b) done: a starting five and
   an ELEVEN-man bench (squad slots 5–15; owner's choice, after trying 6-sit-out). The PLAY
   MATCH screen lists the first 11 in plain slot order indoors (`Indoor_ListOrder`, where
   outdoors it is the tactic's role order -- which mixes slots 0–10, all starters outdoors), so
   rows 1–5 are the five who start, then the gap, then rows 6–16 in the bench colour; swapping
   a starter with any of them is a starter/bench swap (`Indoor_FirstBenchSlot`). In the match
   the bench cursor counts 1–11 (`Indoor_BenchSize`, `Indoor_BenchSlot`: cursor 1 is slot 5),
   the bench draws eleven figures in its column (7 px apart, past the dugout's end), a
   substitution from a parked sprite's slot swaps the players' data as one from 11–15 does,
   and the player who comes off is parked in his place. Only the five on the pitch are
   listed to come off, and the marks menu cannot swap a parked player in. Switching INDOOR on
   sets SUBSTITUTES to 5 FROM 5 (5 substitutions, and FROM 5 so no bench slot from 11 on is cut
   off); still changeable, and OFF gives back what it said. Injured players are kept off the
   bench from slot 5 on indoors (InitInGamePlayer), not only from 11. For that
   `Indoor_BeginMatch` now decides at the START of `InitAndPlayGame`, before `SetupPlayers`
   shows the screen; `Indoor_PinPitch` sets the floor's pitch type where the old call was, and
   `Indoor_EndMatch` runs on every way out, EXIT included. (c) done: substitutions in the match
   never offer a parked player (`Indoor_IsParkedSlot`). `src/bench_subs.c` has the live static
   functions and an exported copy of each that nothing calls (from `BenchMenuHandleControls`,
   unreferenced); the bench changes are in the live ones.
3. **Indoor tactics** — done: 2-2, 1-2-1, 3-1, 1-3 on the same 5×7 grid (`TeamTactics`,
   `positions[0..3]` used, [4..9] empty), compiled in (`src/indoor_tactics_data.c`, the Amiga
   reads no JSON), GENERATED by `scripts/make_indoor_tactics.py`: each indoor player takes over
   an outdoor preset's player sector by sector, or the middle of two (its ROLES table says
   which; tune there and rerun). For an indoor match they take the places of presets 0–3 in
   `_teamTacticsTable`, `_tacticsStringTable` and `_sidePitchSurnamePositionsTable`
   (`install_tactics` / `release_tactics` in `src/indoor.c`, from `Indoor_BeginMatch` /
   `Indoor_EndMatch`); a team on a tactic past the four plays 2-2, and gets its own back
   after. `TacticsData_SelectRelease` (the Amiga or DOS 4-4-2, put in slot 0 at EVERY
   `InitTeamsData`, kick-off and half time) stores into INDOOR's backup while they are
   installed (`Indoor_TakeOutdoorTactic`). The line-up screen shows four tactic buttons
   (`Indoor_TacticsButtons`) and no EDIT TACTICS; the in-match formation menu lists four
   (`Indoor_TacticsCount`).
4. **The court and the walls** — done (tuning open). The court is the owner's SWIS hall
   (libgdx-swis-2022, `hall/playground_v2.png`; its sprites are SWOS's at x3, so a third of its
   units) would be 318 × 466 px; the court is between that (too big, the owner) and a first
   futsal-sized 150 × 244 (no room to run, dribble or pass; ~15 goals a CPU match): 234 × 354
   (x 219–453, y 272–626), penalty area 108 × 55 (x 282–390), the hall's proportions kept,
   centred in the unchanged 672 × 880 world (`include/indoor.h`). Outdoor code keeps its
   numbers and wraps them: `INDOOR_X`/`INDOOR_Y` map a pitch-proportional position onto the
   court, piecewise linear through the lines and the penalty area's edges (81/193/478/590
   across, 129/216/682/769 along) so those land exactly on the court's; `INDOOR_GY` SHIFTS a
   position tied to a goal (net, posts, crossbar, the keeper's lines, the goal area's depth),
   `INDOOR_DX/DY` scale a distance; outdoors each is the number itself. The goals keep their
   size and their x (the court is centred like the pitch): only their y moves, goal sprites
   included. Own values: the penalty spot at futsal's 6 m (53 px, the area's edge,
   `INDOOR_SPOT_Y`), the free-kick distance 5 m (44 px). At kick-off `Indoor_BeginMatch` maps the
   tactics grid's coordinate tables and the ball's sector limits onto the court
   (`install_court`, given back at the end). The ball: no out of play -- `Indoor_BallAtWall`
   (from `UpdateBall`) mirrors its destination off the glass and keeps 3/4 of its speed; only a
   ball crossing a goal line in the mouth and under the bar goes to `CheckIfBallOutOfPlay`, i.e.
   a goal. The players: `Indoor_KeepPlayersInside` (frame body, after `MovePlayers`) while the
   ball is in play. The camera: centred across, along only as far as the stands behind the goals
   (`INDOOR_VIEW_TOP`/`_BOTTOM`, phase 5b) -- except while the bench is called (menu open, closing, a substitute
   walking on), when it goes to the bench as outdoors. `src/indoor.c` is compiled `-Os` (the m68k
   compiler made it 5 KB at -O2); the phase costs SWOS2 about 7 KB.
5. **The hall's floor** — (a) done: `bin/grafs_indoor/LINO.MAP`, the project's own file (tracked,
   in every drop: `FULL_DATA_DIRS`, the Amiga drawer, `make macos-app`, the web build, the
   installer), generated by `scripts/make_indoor_hall.py` from the owner's SWIS hall
   (`playground_v2.png`, his own art): its court blues, white lines and a 32 × 32 patch of its
   parquet, the court REDRAWN at the game's geometry (`include/indoor.h`; keep the script's
   constants in step) rather than scaled, so every line is where the match code has it. An
   8-bitplane pitch map like SWOS 2020's `P2020_n.MAP`: 62 tiles, 32 KB. Colours are game-palette
   entries (the AGA palette, the DOS one index for index, so one map serves both) chosen by
   nearest match, never the pitch condition's nine, the crowd's kit pair 10/11, the replay R
   (112–127) or the kit block (240–255). `LoadPitchMap` loads it for an indoor match on both
   targets whatever the pitch number or graphics style, and `FixupAndColorPitchFile` decodes it as
   an Amiga map even with DOS graphics on. The TOP GOAL's frame and net are drawn in the map too
   (our own drawing, in the goal greys 50–59): a stadium paints both goals into its pitch, and the
   goal sprites only add what goes over the players -- the bottom goal's front, the top goal's
   strip of net 18–23 px behind the line; without it the top goal was that strip alone.
   (b) done: **the crowd**, three rows behind each goal past a strip of floor (`indoor.h`
   `INDOOR_CROWD_*`: map rows 12–14 and 41–43), laid out as a stadium's rows 1–3 / 51–53, with
   map row 0 listing the crowd tiles as every pitch's does (the kit recolouring goes through it).
   Tiles 1–26 are the crowd in every SWOS pitch bank -- the same tiles at the same numbers in the
   Amiga maps and the PC banks -- so the map holds BLANKS there and `LoadPitchMap` fills them from
   the pitch the match would have been played on (`indoorTakeCrowd`: `SWCPICHn.MAP`, or
   `PITCHn.BLK` with DOS graphics), before it loads the hall: no original art is in the file. The
   fan animation (`animated_patterns.c`) and the camera follow the rows (`INDOOR_OR`: the outdoor
   number outdoors). The sides of the court stay floor: the animation only works in rows.
5.1. **Six a side and the indoor rules** (the owner's list, 2026-11-02) — done, six commits:
   1. **Six a side on 276 × 418.** `INDOOR_PLAYERS 6` (parked slots 6–10, a bench of 10 in slots
      6–15), the court x 198..474, y 240..658, everything in it scaled (box 128 × 65, spot 62,
      free-kick distance 52 / step 57). Five tactics **2-2-1, 2-1-2, 1-2-2, 3-1-1, 1-3-1**
      (`scripts/make_indoor_tactics.py`: roles from 4-4-2, 4-2-4, 5-4-1, 5-3-2, 3-4-3); restart
      roles 3, 4, 8, 10, 11 (ordinals 2–3 back, 4 middle, 5–6 take the kick-off).
   2. **Clock 2 × 20** in the real time the GAME LENGTH option gives 2 × 45: `UpdateTime`'s step
      ×4 and reload ×9 indoors, half time at 20, full time at 40 (a friendly has no extra time).
   3. **The keeper's throw.** A ball released from the keeper's hands (`ST_KEEPER_HOLDS_BALL`,
      the throw/pass or kick block of `UpdatePlayers` → `Indoor_KeeperReleased`) that crosses
      the halfway line before another player of either side touched it (`_lastPlayerPlayed`;
      checked in `UpdateBall`, `Indoor_KeeperRuleCheck`) gives the other side a free kick on the
      halfway line where it crossed: the foul's stoppage without a foul, `ST_FOUL`, no wall. Any
      touch or stoppage disarms it. A ball off a body that is not a kick, header or control
      counts as untouched (the game does not record it). The computer's keeper passes out of
      his hands indoors (`INDOOR_KEEPER_PASSES`, `ai.c`): one rule free kick a CPU match, not 20.
   4. **Red card = time penalty.** `SendPlayerAway` → `Indoor_TimePenalty`: the player walks to the
      bench and sits out 5 minutes of the indoor clock (his side one short), then comes back on,
      booked (`cards` 1: a further yellow is another red); up to four at once. A player on his way
      off is no longer held inside the walls. **`SWOS_INDOOR_TEST_RED=1`** makes every indoor
      foul a red card, the computer's players included (the original sends off only a side with
      a human controller), so a CPU match shows it: `[indoor] time penalty` lines in `SWOS.log`.
   5. **A wall of two:** the back pair (ordinals 2–3) in the middle two of the outdoor wall's four
      places, re-centred when one is off, at the indoor distance (offsets × 52/65).
   6. **The hall's sides.** A two-column stand along each side between the end stands (the top
      team's fans 1/3 on the left, the bottom team's 17/19 on the right, a stair every ten rows),
      animated (every other fan pick is a side cell on screen); the bench's two dugouts (our own
      drawing) beside the court on the left, in front of the left stand, right edge
      `INDOOR_BENCH_X` 174. The bench's world positions and the camera's bench views are the
      outdoor ones moved by `INDOOR_BENCH_SHIFT` (147). The camera follows the ball across as
      outdoors, clamped to the hall stand to stand (`INDOOR_HALL_LEFT/RIGHT` 112..528). **Laid out
      for the 4:3 view and the Amiga only — no widescreen (the owner)**: the court alone is 276 px,
      so the view pans and the bench is seen while play is on the left.
   Open: the pre-match "team v team" screen still lists 11 + 5; tuning by play.
6. **Online and replays** — the host's INDOOR in the lobby, the setup blob and the `.SRP`;
   `NETPLAY_COMPAT` raised and the fork's own online identity.

## Tests

Before each commit:

- `make X86_64=1` and `make -f Makefile.amiga` (the m68k toolchain on PATH, BUILDING.md).
- On the Amiga, a look in FS-UAE: the change on screen, and nothing else changed.
- **Off = identical, careers:** `scripts/career_golden_test.sh` must pass unchanged. It does
  NOT exercise the match engine: the autopilot simulates the career club's matches.
- **Off = identical, matches:** a live match is deterministic where an offline one is not (an
  offline match runs on a real-time clock, so even pinned seeds part within a minute). Record
  one with the build before the change -- `SWOS_ASSETS=~/.swos/assets MATCHES=1 FRAMES=3000
  scripts/netplay_lobby_test.sh` (two instances through the lobby; the host writes
  `save/LATEST.SRP`, give it a HOME of its own) -- and replay it with the build after:
  `bin/SWOS.x86_64 --srp-resim=<file>` must say `every one as recorded`. A live match is
  never indoor, so this is INDOOR off.
- The replay, rollback and netplay rigs (`replay_v2_test.sh`, `rollback_test.sh`,
  `netplay_test.sh`, `check_netplay_compat.sh`) need the maintainer's recordings in
  `tests/dos`, which are not in git: where they exist, they must pass unchanged too.
- **On:** a friendly with INDOOR (screen or `SWOS_INDOOR=1`); `SWOS.log`'s `[indoor]` line.
  Headless test runs must ignore the desk's gamepads (`SDL_JOYSTICK_IGNORE_DEVICES` /
  `SDL_GAMECONTROLLER_IGNORE_DEVICES` = their VID/PID): one rests at up-left and moves every
  menu cursor.
