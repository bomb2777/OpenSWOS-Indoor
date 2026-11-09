# INDOOR.md — six a side in a hall

> **Status (2026-11-09): in progress — phases 1–5 done (the switch, five a side, tactics, court and walls, the hall), phases 5.1 (six a side, the indoor rules, the hall's sides), 5.2-5.5 (the owner's lists after playing), 5.6 (the hall's menu music, PC), 5.7, 5.8 (the tactics made for the court), 6 (online, by direct IP, fork builds only).** This fork's own
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
  It stays 0 for anything but a friendly: competitions, career, training and arcade
  matches. An online match plays the HOST's floor from the lobby (phase 6).
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
      places, re-centred when one is off, at the indoor distance (offsets × 52/65). Replaced in
      5.4: those places, made for the big pitch, could stand it beside the line to goal.
   6. **The hall's sides.** A two-column stand along each side between the end stands (the top
      team's fans 1/3 on the left, the bottom team's 17/19 on the right, a stair every ten rows),
      animated (every other fan pick is a side cell on screen); the bench's two dugouts (our own
      drawing) beside the court on the left, in front of the left stand, right edge
      `INDOOR_BENCH_X` 174. The bench's world positions and the camera's bench views are the
      outdoor ones moved by `INDOOR_BENCH_SHIFT` (147). The camera follows the ball across as
      outdoors, clamped to the hall stand to stand (`INDOOR_HALL_LEFT/RIGHT` 112..528). **Laid out
      for the 4:3 view and the Amiga only — no widescreen (the owner)**: the court alone is 276 px,
      so the view pans and the bench is seen while play is on the left.
   Open: tuning by play.
5.2. **The owner's second list** (2026-11-02, after playing 5.1) — done:
   - **The ball:** ground friction × 2/3 indoors (`INDOOR_BALL_FRICTION_NUM/DEN`; 10 a frame on
     the HARD floor, not 15), the glass keeps 15/16 of its speed (`INDOOR_WALL_KEEP_SHIFT` 4), and
     after a whistle the ball comes off the glass too (UpdateBall's stopped-state fence calls
     `Indoor_BallAtWall` for a ball that was on the court; until then only the outdoor fence
     stopped it, ~30 px beyond the glass).
   - **Court 318 × 482** (x 177..495, y 208..690; box 148 × 75, spot 71, free kick 60 / 66) and
     **side stands six columns wide** (x 16..111 and 512..607; `INDOOR_SIDE_COLS` 6, the bench at
     `INDOOR_BENCH_X` 153): the panning camera never shows the hall's edge. End stands rows 8–10
     and 45–47.
   - **Tactics further apart:** per ball sector the five are moved from their centre by `SPREAD`
     1.5 (`scripts/make_indoor_tactics.py`).
   - **The pre-match screen** lists the six who start (`Indoor_StadiumRows`, from
     `ShowStadiumInit`).
   - **A player sitting out** stands by his own team's dugout with the small red card (sprite 139)
     over him (`Indoor_DrawPenaltyCards`, every frame from `DrawBenchAndSubsMenu`); the bench
     figures during play are drawn on the Amiga too indoors (on SDL they always were).
   The sizes in 5.1 above are superseded by these.
5.3. **The owner's third list** (2026-11-02) — done (`dc78d73`):
   - **Glass** ((98,98,98) at ~60 %, his SWIS `wall_transparent` / `sidewall_*`), `0b5c1bf`:
     along the sides in the floor, 10 px (5 since 5.7). At the ends in perspective, both 80 px high
     (`INDOOR_GLASS_HEIGHT`): the top wall greys the floor behind it and the three nearest rows
     of fans, the bottom one the court's last 80 rows and the players in them; each with a clear
     hole where its goal stands (x 300..372; 24 px at the top line, the bottom goal frame's 18 px
     at the bottom one: the top goal, a keeper on the bottom line). The players are in front of
     the top wall (drawn before the sprites, `Indoor_DrawGlassBehind`) and behind the bottom one
     (after them, `Indoor_DrawGlassFront`), whose light rim runs along its foot, the goal line.
     The PC greys through a colour table from the palette on screen (rebuilt when it changes).
     The Amiga cannot lay a wall a frame (the blitter doing it was more than a frame's chip bus:
     the owner's A1200 crawled with a wall in view), so its map, `LINO_A.MAP`
     (`make_indoor_hall.py --amiga`), has the glass baked in as a checkerboard of grey (0x555,
     (x + y) even) -- the owner likes the retro look -- the three rows of fans behind the top
     wall are tiles 27-52, greyed copies of the crowd made once at load
     (`Indoor_GlassCrowdTiles`; those fans do not move), and a frame lays the checkerboard only
     over the boxes of the players and the ball inside the bottom wall (blitter, minterm 0xCA,
     both copies of a row of the 2x-tall frontbuffer).
   - **Court markings:** the penalty areas and the centre circle a lighter blue (palette 76,
     0x66E; the court 0x22C) inside clear white lines.
   - **Sorted line-up** (`install_tactics` → `sort_line_up`): the keeper, then each of the
     tactic's outfield places (2-2-1: D D M M A, 2-1-2: D D M A A, 1-2-2: D M M A A, 3-1-1:
     D D D M A, 1-3-1: D M M M A) by the BEST player left who plays it (RB/LB/CD, RW/LW/MF, ST)
     -- the highest `PlayerFile.price`, the team's order deciding a tie -- else the best outfield
     player left; the bench by position, within one by value. (His SWIS took the first in the
     team's order; that left Germany's Sammer and Moller out.) The outdoor order is put back at
     the end of the match.
   - **The PLAY MATCH screen's little pitch** (sprite 225) a blue court indoors: menu colours 14
     and 11 swapped (`Indoor_SidePitchColours`, from `SetPlayerSurnamesOnSidePitch`), swapped
     back outdoors, reset by `ProcessImageCommandStream` on the menu stream.
   - **Friction** 4/5 (12 a frame); the glass keeps 15/16.
   - **Walk-on** at the court's right edge (`InitPlayersBeforeEnteringPitch`), not through the
     stand; the match's first and last camera at the hall's ends (`game_loop.c`,
     `match_setup.c`), and a camera outside the hall jumps to its target (`MoveCamera`).
   - **Stands deeper:** `INDOOR_CROWD_ROWS` 5 (rows 6–10, 45–49; the fans animate in the
     three nearest the court), `INDOOR_SIDE_COLS` 8 (x 0..127 and 512..639); the bench at
     `INDOOR_BENCH_X` 163, outside the left glass. Parquet shows only in the bench zone.
   - **Players held by the glass at stoppages too** (`Indoor_KeepPlayersInside`): a slide or a
     dive the whistle stops used to carry on through it; not during the walk-on, half time, the
     end or a substitution.
5.4. **The owner's fourth list** (2026-11-03) — done:
   - **The little pitch** without the four corner arcs and the two arcs on the penalty areas:
     their pixels in sprite 225 painted the court's colour indoors (only where they have the
     lines' colour), what was there saved and put back for an outdoor match
     (`side_pitch_arcs`, both targets: chunky on the PC, planar on the Amiga).
   - **The glass** was a lighter checkerboard on both targets for a day; the owner wanted it
     back as it was (the PC clear, the Amiga's checkerboard 0x555).
   - **The bench during play on the PC** shows the coach and four a side, as the Amiga
     (`s_benchSeats` in `DrawBenchAndSubsMenu`'s play-time path; the substitutes menu lists all).
   - **Defenders further from the keeper** (`make_indoor_tactics.py`, `DEFENDERS`): in 2-2-1,
     2-1-2, 1-2-2 and 1-3-1 the defenders are moved out from the middle ×1.8 and a grid row up
     (never below row 2), in every ball sector -- the keeper holding the ball included (that
     is the tactic too). 3-1-1, which the owner liked, is as it was. At restarts (kick-off, free
     kicks) indoor 2 and 3 take the outdoor full backs' places, 2 and 5, not the centre backs'
     (`Indoor_RestartOrdinal`).
   - **The wall on the line to goal** (`Indoor_WallSpot`, from `UpdatePlayers`): for every free
     kick against them (all seven `ST_FREE_KICK_*`, the two widest included), the back pair
     stand on the line from the ball to their goal's middle, just past the free-kick distance
     (62 px), 10 px apart across it; one alone when the other is off.
   - **The line-up by side and by strength** (`sort_line_up`): after the places are filled, a
     forward on the bench worth at least 5 more (`FORWARD_FOR_MIDFIELDER`) than a midfielder
     takes his place (Croatia: Suker for Prosinecki; 49 of the 1,711 teams change). Then
     each line's players go onto its places by side: places and players (RB/RW right, LB/LW
     left, the rest the middle) both in side order, paired off. A `[indoor] <team> line-up`
     line in `SWOS.log` names the six.
   - **Replays show the glass** (the owner, 2026-11-03): a replay during the match
     (`DrawReplayFrame`, both its `.HIL` ring and a `.SRP`) lays the top wall after the crowd
     and, on the PC, the bottom one after the sprites; the Amiga's walls are in its map, and it
     greys each recorded sprite's box in the bottom wall (`Indoor_GlassReplaySprite`). A saved
     `.SRP` says it was played indoors -- META flag 64, its floor in the word after
     `gameMaxSubstitutes` (docs/REPLAY_FORMAT_V2.md) -- and the viewer (LOAD/VIEW, the
     recordings player) shows it in the hall (`ReplayV2_UseRecordedIndoor`, before the pitch
     is loaded; undone in `ReplayV2_RestoreView`). A `.HIL` saved from an indoor match does
     not say so: it plays on the outdoor pitch. The bench figures are not in a recording.
5.5. **The owner's fifth list** (2026-11-03):
   - **Substitutions as in futsal, within SWOS's menu** (`bench_subs.c`): indoors the bench
     can substitute while a keeper holds the ball (outdoors that stoppage allows only
     tactics), without a limit (`_maxSubsPerTeam` not checked), and a substituted player may
     come back on (his position is not set to 0xFF). Never the keeper holding the ball
     (`holding_keeper`: he does not walk off with it, and the substitution waited for
     ever); bench slot 11 is an outfield substitute, not the reserve keeper. The substitute
     walks on from the indoor dugout. True flying substitutions in play were turned down
     (the owner: "a mess and a fun killer"). `[indoor] substitution` lines in `SWOS.log`;
     `SWOS_INDOOR_TEST_SUB=1` (PC) makes each keeper's side substitute whenever he holds the
     ball, through the menu's own controls -- the 1st, 2nd, 3rd substitute in turn.
   - **PC and Amiga alike:** the Amiga greys the referee behind the bottom wall too, and
     draws the bench figures from the moment a dugout's edge is in view, as the PC.
   - **The marker** (his SWIS marks a hit on the glass with a ring and a cross and blinks a post
     or the bar red; here in the game's pixels): EVERY touch of the glass (a CPU match some 30;
     until 2026-11-04 only the hard ones, which alone still thud: `ball->speed` >=
     `INDOOR_MARK_SPEED`, 1300, shots and clearances, about a third) draws SWIS's mark (its
     `WallImpact`, at a third of its size: its ball is 16 px, ours 5) -- a red ring growing from 2 to 10 px and
     fading (dotted, then every third pixel), over a white cross 3 px each way with a red
     centre, white for two thirds of the second, then grey (six pictures of 21 x 21, 48
     frames) -- at an end WHERE THE BALL IS SEEN: on the line, lifted by the ball's height (the
     sprite's z, as the ball's own; on the glass, which stands 80 px high in the picture, no
     higher than its top less the ring); at a side on the floor, on the white side line where
     the glass begins (the owner: the sides need no height) -- a hard hit with the ball's
     bounce sample (`PlaySfxSample16`, the PC sound too). Until 2026-11-04 the struck piece of glass lit up
     as a fading white checkerboard instead (the owner: "looks odd"). The corner flag's z goes
     back to 0 at the final whistle (`Indoor_ClearMarker` from `Indoor_EndMatch`; outdoors
     nothing sets it). A post or the bar blinked red three times as in SWIS
     (`Indoor_Woodwork`, from `UpdateBall`'s woodwork) until 2026-11-04, when the owner had it
     removed; the woodwork keeps only its sound. One marker, the latest hit. Its six pictures
     are made at kick-off in sprite table slots 1684..1689 (`INDOOR_MARK_SPRITE_FIRST`; the
     table grew from 1684 to 1690) -- chunky on the PC, planar with a mask in chip RAM on the Amiga
     -- and shown by the corner flag's sprite, which a hall has no use for: indoors
     `UpdateCornerFlagSprite` hands it to `Indoor_UpdateMarker` (the marker or nothing; until
     then nothing hid the outdoor flag indoors). So the Amiga undraws it as any bob and
     a replay records it (the viewer makes the pictures for an indoor recording). The PC's
     bottom wall leaves its pixels bright (`Indoor_MarkerBox`, `marker_opaque`: not its
     whole box, which showed as a bright square around the ring).
5.6. **The hall's menu music** (2026-11-04, PC only): while the FRIENDLY screen's INDOOR
   row shows a floor, the menus play the owner's own track, `bin/sound_indoor/menu.mp3`
   (his "liquid" drum & bass, 51 s, looped; streamed by `src/music_stream.c` with dr_mp3),
   whatever `[audio] music` says; OFF gives the normal menu music back. Switching the row
   cuts the song playing and starts the other (`SwitchMenuMusic`, src/audio.c), when menu
   music is on at all; `TryPlayMusic` picks it by `Indoor_MenuMusic()`, so it also plays in
   the other menus and after the match while the row stays on. `[indoor] menu music` in
   `SWOS.log`. `sound_indoor` ships with every PC drop (build_dist.sh, the installer, the
   .app, WASM), not the Amiga drawer: the Amiga keeps its menu song (the owner: "not so
   important" -- its chip RAM has no room for the track, it would have to be streamed from
   fast RAM).
5.7. **The owner's sixth list** (2026-11-04):
   - **The side glass half as wide**, 5 px (`INDOOR_GLASS_LEFT/RIGHT`; "too fat"): LINO.MAP and
     LINO_A.MAP made again (`scripts/make_indoor_hall.py`, which reproduced the old maps byte
     for byte before the change). The bench stays where it stood (`INDOOR_BENCH_X` 163, no
     longer worked out from the glass), so the hall's floor between bench and glass is 5 px
     wider; the side stands' columns came out the same.
   - **The PC's glass colour table is no longer built during a fade** (`glass_table`): it was
     rebuilt from whatever palette was on screen, a fade's dark one too (18 times in a CPU
     match), and from that sent the colours to the wrong ones -- after a goal's replay the
     bottom wall showed solid white in a screenshot. A fade scales every colour alike, so the
     bright palette's table stays right through it.
   - **No red blink on a post or the bar any more** (the owner): `Indoor_Woodwork` and its
     two pictures removed, the sprite table 1690 again; `ball_physics.c` as upstream's there.
   - **The court teal** (`#118888`, its areas `#33AAAA`; the owner: the blue hid blue kits, and
     of the candidates drawn with every kit colour on them teal kept them all apart). The
     palette has no teal and is full, so the hall's map still draws the court in entries 75
     and 76 (blue, 0x22C / 0x66E) and an indoor match makes them teal: `Indoor_CourtPalette`
     from `ApplyPitchPaletteAndFactors` (live and the replay viewer; given back outdoors) and
     `Indoor_CourtPaletteRGB` for the PC styles that build their palette each frame. Safe
     because nothing else in an indoor match draws in them: sprites are 0-15, and the crowd
     tiles taken from the player's pitch use 0-15 too (measured in the Amiga, PC and 2020
     styles). The PC's glass needs a grey teal to see the floor through (its colour table
     found only plain grey): entries 77 and 82, unused in an indoor match, hold it (PC only;
     the Amiga's glass is a checkerboard). The shadow inside the top goal (203) stays dark
     blue. The little pitch on PLAY MATCH has the menus' 16 colours and no teal: its grass is
     a checkerboard of their green and blue (`side_pitch_mix`; the owner's idea), which also
     dots the centre circle's line where it was drawn in the grass colour.
   - README: the fork's own block at its top, a screenshot of an indoor match
     (`docs/images/screenshots/indoor_01.png`).
5.8. **The tactics made for the court** (2026-11-09; the owner: "a balanced start, the shape
   moving with the ball, denser around it in the corners", after his SWIS's
   `tactics/2-2_5x7.txt`). `scripts/make_indoor_tactics.py` no longer copies outdoor players:
   each of the five has a line (D/M/A) and a side (R/C/L) (`SHAPES`, the same as `kPlaces`/
   `kSides` in `indoor.c`), and for every one of the 35 ball sectors its place is computed --
   its line's height following the ball (`LINE_Y`: defenders 2.0 .. 7.0, up to the halfway
   line when the ball is at the opponents' goal -- the owner; midfield 3.5 .. 10.5, attack
   6.5 .. 13.5; a defender goal-side of a ball in the own half by `GOAL_SIDE`), its side's
   place shifted towards the ball (`FOLLOW_X` per line), pulled in towards a wide ball
   (`PULL` 0.30 within `REACH` 11 cells, in the own half only: a SWOS tactic does not know
   who has the ball, and closing up while attacking took the team's width -- CPU v CPU 9
   goals a match became 0-1; `PULL_UNTIL` 4), then kept `MIN_GAP` 2.6 cells apart and on the
   grid. `scripts/indoor_tactics_sheet.py <dir> [file.c | --swis <SWIS .txt>]` draws each
   tactic as its 35 ball sectors (and a SWIS tactic the same way) -- look before playing.
   CPU v CPU (cpu_friendly.sh, 3 matches): 0:3, 3:2, 0:1, where the old tactics made 5:4,
   4:4. NETPLAY_COMPAT 1005.
   **Round 2, attacking wide** (the owner, after the sheets: "still too close to each other --
   wider as soon as the ball is on and over the middleline; in defence it can stay"): from
   ball row `WIDE_FROM` 3 (the halfway line's) on, the sides take `SIDE_X_WIDE`'s places
   (2.0/12.0 for a pair, 1.5/7.0/12.5 for three, against 4/10 and 3/7/11), the wide players
   follow the ball across only `WIDE_FOLLOW` 0.5 of `FOLLOW_X`, and `MIN_GAP_WIDE` is 4.0.
   The attacking shape now spans 10-11 of the 15 cells across (6-8 before); rows 0-2 are
   unchanged. CPU v CPU: 0:2, 1:2, 0:2. Tuning by play is open. NETPLAY_COMPAT 1006.
6. **Online** (2026-11-04) — done:
   - **The fork's own identity** (`include/project_identity.h`): `PROJECT_NETPLAY_TAG`
     `OPENSWOS-INDOOR`, `PROJECT_GGPO_GAME` `openswos-indoor`, and `NETPLAY_COMPAT` from
     1000 (1006; `include/netplay_compat.h`), where upstream's numbers never reach -- a
     fork build never meets one that has no INDOOR. A rebase that brings a raise of
     upstream's raises ours by one. The handshake also compares the rollback state's layout,
     which the indoor state below changed.
   - **Direct IP, no server of our own:** the main menu's MULTIPLAYER opens `p2p/p2pMenu`
     (`MultiplayerMenuSelected`, `src/menus.c`) instead of the login to upstream's server.
     A connected pair goes to the usual lobby.
   - **The host chooses it in the lobby**, on PITCH TYPE: ... SPRING, RANDOM, **LINOLEUM**
     (`next_pitch`, `src/online_menus.c`), which sets SUBSTITUTES to 5 FROM 5 as the
     FRIENDLY screen's row does (leaving it: 2 FROM 5). The value is
     `LOBBY_PITCH_INDOOR` + the floor in `LobbyRules.pitchType` (`include/netplay_lobby.h`),
     so the rules' wire format is unchanged; `clamp_rules` accepts it. At kick-off the lobby
     sets `_gIndoorOnlineFloor` and the floor's pitch type (`NetplayLobby_ApplyMatch`).
   - **The match setup carries the floor** (`src/netplay_setup.c`, version 5: one word after
     the crowd, the floor + 1, 0 outdoors), so the joiner and `--srp-resim` play the hall
     from the host's data; a version-4 setup is outdoors. `Indoor_BeginMatch` takes
     `_gIndoorOnlineFloor` while `NetplayMatch_Enabled()` (never this machine's FRIENDLY
     row or `SWOS_INDOOR`); `Indoor_EndMatch` clears it.
   - **Rollback state:** what an indoor match changes as it plays -- the time penalties, the
     keeper's throw, the glass marker -- is one struct, `g_indoorSim` (`IndoorSimState`,
     `include/indoor.h`), a SIM region of `src/sim_state.c` (all zero outdoors). So are the
     hall's crowd rows of the pitch tile map (`_pitchDatBuffer:indoor_fans`, rows 6..49:
     the end stands and the side stands, which the fan animation writes; outdoors only rows
     1-3 and 51-53 change) -- without them the first rollback of the first online indoor
     match parted the games at the next fan pick (found by `SWOS_LIVE_DESYNC_DUMP` and
     `--netplay-state-stream-diff`: `_animatedPatterns`). The rest
     (tactics, line-up, court tables, glass, palette) is set at kick-off from data both
     games have, and does not change in play.
   - `SWOS_LOBBY_INDOOR=1` has a test host choose LINOLEUM. Two players through the lobby
     with random input on an emulated wifi link (both predict wrong and roll back), no server
     needed: `HOST_WIDTH=362 JOIN_WIDTH=362 PROFILE=wifi HOST_ENV="SWOS_LOBBY_INDOOR=1
     SWOS_INDOOR_TEST_RED=1" JOIN_ENV="SWOS_INDOOR_TEST_RED=1" scripts/netplay_lobby_test.sh`
     -- both peers must end on the same hash (`SWOS_INDOOR_TEST_RED` on BOTH or neither:
     it changes the match). `SWOS_ASSETS` must point at the assets when HOME is the test's.
   - The rollback layout changed, so a recording made before it is refused by
     `--srp-resim` ("laid out differently"). The proof that outdoors is unchanged: the
     build without the `g_indoorSim` region replays the old live match, every frame as
     recorded.

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
