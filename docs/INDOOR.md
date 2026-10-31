# INDOOR.md — five a side in a hall

> **Status (2026-10-31): in progress — phase 1 of 6 done (the switch), phase 2 (five a side).** This fork's own
> feature (`bomb2777/OpenSWOS-Indoor`, branch `indoor`); upstream OpenSWOS has no INDOOR.

## What it is

A **friendly** can be played indoors: five a side (a keeper and four), on a small pitch
with glass walls all round, so the ball never goes out of play — the only stoppages are a
goal, a foul (free kick, penalty) and half time. Squads, the database, the controls and
every other screen are unchanged.

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
   listed to come off, and the marks menu cannot swap a parked player in. For that
   `Indoor_BeginMatch` now decides at the START of `InitAndPlayGame`, before `SetupPlayers`
   shows the screen; `Indoor_PinPitch` sets the floor's pitch type where the old call was, and
   `Indoor_EndMatch` runs on every way out, EXIT included. (c) done: substitutions in the match
   never offer a parked player (`Indoor_IsParkedSlot`). `src/bench_subs.c` has the live static
   functions and an exported copy of each that nothing calls (from `BenchMenuHandleControls`,
   unreferenced); the bench changes are in the live ones.
3. **Indoor tactics** — 2-2, 1-2-1, 3-1, 1-3 on the same 5×7 grid (`TeamTactics`, only
   `positions[0..3]` used), compiled in (the Amiga reads no JSON), swapped into the tactics
   table for the match.
4. **The area and the walls** — a futsal-sized play area (start ≈ 150 × 244 px) centred in the
   unchanged 672 × 880 world; the ball rebounds off the walls instead of going out.
5. **The blue linoleum pitch** — one pitch file in the Amiga's own tile-map format, made by a
   script from our own drawing (so it can live in git), loaded by both targets the way the SWOS
   2020 pitches are.
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
