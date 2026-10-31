# INDOOR.md — five a side in a hall

> **Status (2026-10-31): in progress — phase 1 of 6 done** (the switch). This fork's own
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
  is fixed once the teams are chosen. The Amiga has no row (hidden, its navigation restored):
  INDOOR is SDL only for now.
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
2. **Five a side** — ordinals 6–11 parked off the pitch as if sent off; 5 starters on the
   PLAY MATCH screen, the rest the bench; restart/kick-off positions for 5.
3. **Indoor tactics** — 2-2, 1-2-1, 3-1, 1-3 on the same 5×7 grid (`TeamTactics`, only
   `positions[0..3]` used), swapped into the tactics table for the match.
4. **The area and the walls** — a futsal-sized play area (start ≈ 150 × 244 px) centred in the
   unchanged 672 × 880 world; the ball rebounds off the walls instead of going out.
5. **The blue linoleum pitch** — drawn in code, no asset file.
6. **Online and replays** — the host's INDOOR in the lobby, the setup blob and the `.SRP`;
   `NETPLAY_COMPAT` raised and the fork's own online identity.

## Tests

Before each commit:

- `make X86_64=1` (and the Amiga build where its toolchain is installed).
- **Off = identical:** `scripts/career_golden_test.sh` must pass unchanged. The replay,
  rollback and netplay rigs (`replay_v2_test.sh`, `rollback_test.sh`, `netplay_test.sh`,
  `check_netplay_compat.sh`) need the maintainer's recordings in `tests/dos`, which are not in
  git: where they exist, they must pass unchanged too.
- **On:** a friendly with INDOOR (screen or `SWOS_INDOOR=1`); `SWOS.log`'s `[indoor]` line.
