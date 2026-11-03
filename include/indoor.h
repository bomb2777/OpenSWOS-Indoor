/* indoor.h -- INDOOR: a friendly played six a side in a hall (docs/INDOOR.md).
 *
 * One switch on the FRIENDLY screen. A match that is not a friendly -- a competition,
 * a career, training, arcade, an online match -- is never indoor, and with the switch
 * off nothing here changes a byte of how a match plays.
 *
 * Two flags, kept apart on purpose:
 *   _gIndoorFriendlyFloor  what the FRIENDLY screen shows (OFF or a floor); a menu choice
 *   _gIndoor/_gIndoorFloor the match being played; set at kick-off by Indoor_BeginMatch,
 *                          cleared when the match is over by Indoor_EndMatch
 * Match code tests _gIndoor and nothing else.
 */
#ifndef INDOOR_H
#define INDOOR_H

#include "swos_types.h"   /* not <stdint.h>: the Amiga toolchain has none */
#include "game_types.h"

/* The floor of the hall. It decides the pitch type: the ball rolls and bounces as
 * on that kind of SWOS pitch. Only LINOLEUM is drawn so far. */
enum {
    INDOOR_OFF      = -1,       /* _gIndoorFriendlyFloor only: an outdoor friendly */
    INDOOR_WOOD     = 0,
    INDOOR_LINOLEUM = 1,
    INDOOR_STONE    = 2,
    INDOOR_SAND     = 3,
};

extern int16_t _gIndoor;                /* 1 while an indoor match is played */
extern int16_t _gIndoorFloor;           /* its floor, INDOOR_* */
extern int16_t _gIndoorFriendlyFloor;   /* the FRIENDLY screen's choice, INDOOR_OFF or a floor */

/* The SWOS pitch type (0 frozen .. 6 hard) a floor plays as. */
int16_t Indoor_PitchTypeForFloor(int16_t floor);

/* The menu text of a floor (or "OFF" for INDOOR_OFF). */
const char *Indoor_FloorName(int16_t floor);

/* The FRIENDLY screen's choice, as the next press of its INDOOR button makes it. */
int16_t Indoor_NextFriendlyFloor(int16_t floor);

/* InitAndPlayGame, first thing: make this match indoor if it is a friendly and the
 * FRIENDLY screen (or SWOS_INDOOR=1, for a test) says so; anything else leaves
 * _gIndoor 0. First, so the line-up screen (SetupPlayers) shows six a side. */
void Indoor_BeginMatch(TeamFileEx *team1, TeamFileEx *team2);

/* InitAndPlayGame, once it has set the match's pitch from the FRIENDLY screen: the
 * floor's pitch type instead, indoors. */
void Indoor_PinPitch(void);

/* InitAndPlayGame, once the match is over -- or the line-up screen was left. Gives
 * the tactics table and the two teams' tactics back. */
void Indoor_EndMatch(void);

/* ---- Six a side (phase 5.1; five a side until then) ----------------------------
 * A side keeps its 11 sprites; ordinals 1..INDOOR_PLAYERS play (1 the keeper), the
 * rest are PARKED beside the pitch for the whole match, as a sent-off player is
 * (sentAway), which the AI, ball control and the kick-off wait already skip. */
#define INDOOR_PLAYERS 6

/* Every frame, before the players are updated: puts the parked ones back where they
 * belong, whatever reset them (the walk-on, half time, a penalty, a substitution).
 * Does nothing outdoors. */
void Indoor_ParkPlayers(void);

/* The ordinal whose place an indoor player takes in the restart tables (kick-off,
 * free kick, penalty, ...): 2 and 3 the centre backs', 4 a central midfielder's, 5 and
 * 6 the forwards' (so the kick-off has its two at the centre circle). Outdoors, the
 * ordinal itself. */
int16_t Indoor_RestartOrdinal(int16_t ordinal);
/* The two-man wall (phase 5.4): where wall player `slot` (0 or 1, of `inWall` standing) goes
 * for a free kick from (ballX, ballY) at the goal on row goalY -- on the line from the ball
 * to the goal's middle, just past the free-kick distance, the two side by side across it. */
void Indoor_WallSpot(int16_t ballX, int16_t ballY, int16_t goalY, int16_t slot, int16_t inWall,
                     int16_t *x, int16_t *y);

/* Whether the player in this slot of a side (0 = the keeper .. 10) is parked: never
 * offered for substitution. Always 0 outdoors. */
int16_t Indoor_IsParkedSlot(uint16_t slot);

/* ---- The bench -------------------------------------------------------------------
 * Outdoors the bench is squad slots 11-15 (5 players); indoors everyone who does not
 * start is on it: slots 6-15, 10 players. A parked sprite's slot (6-10) holds a bench
 * player, so a substitution from it swaps with a player on the pitch exactly as one from
 * 11-15 does, and the player who comes off is parked in his place.
 * The in-match bench cursor counts 1..Indoor_BenchSize() (0 is the coach). */
int16_t  Indoor_BenchSize(void);                /* 5 outdoors, 10 indoors */
uint16_t Indoor_FirstBenchSlot(void);           /* 11 outdoors, 6 indoors */
uint16_t Indoor_BenchSlot(uint16_t index);      /* bench cursor 1.. -> squad slot */

/* The order a team's first 11 are LISTED in (line-up screen, substitutions menu): the
 * tactic's role order (_positionsTable, 1-based slots) outdoors. Indoors the plain
 * slot order instead, so rows 1-6 are exactly the six who start (slots 0-5) -- the
 * role order mixes slots 0-10, which outdoors are all starters anyway. */
const uint8_t *Indoor_ListOrder(const uint8_t *rolesOrder);

/* ---- Tactics (phase 3; five since phase 5.1) ---------------------------------------
 * Five: 2-2-1, 2-1-2, 1-2-2, 3-1-1, 1-3-1 (src/indoor_tactics_data.c, generated by
 * scripts/make_indoor_tactics.py). For an indoor match they take the places of the
 * first five presets in _teamTacticsTable (and of their names and line-up pictures), so
 * a team's tactic 0-4 means them; a team on another tactic plays 2-2-1. All given back
 * at the end of the match. */
#define INDOOR_TACTICS 5
extern TeamTactics _indoorTactics[INDOOR_TACTICS];
extern uint16_t    _indoorSidePitch[INDOOR_TACTICS][33];

/* The line-up screen's 18 tactic buttons (entries 38-55), indoors: the first five
 * named after the indoor tactics, the rest hidden. The menu is unpacked afresh every
 * time it opens, so outdoors nothing is left behind. Its own function, not a branch in
 * SetUpTacticsButtons' loop: there the m68k compiler unrolled and inlined it for 17 KB. */
struct MenuEntry;
void Indoor_TacticsButtons(struct MenuEntry *first);

/* The pre-match screen (ShowStadiumInit, entries from `first`): indoors only the starting
 * INDOOR_PLAYERS of each side -- the rows of slots 6-10 and the substitutes' hidden.
 * Nothing outdoors. */
void Indoor_StadiumRows(struct MenuEntry *first);

/* The line-up screen's little pitch (sprite 225, used by nothing else) is a blue court
 * indoors: its grass (menu colour 14) swapped with blue (11) -- a swap, so swapping again
 * gives the green back exactly. SetPlayerSurnamesOnSidePitch calls it before every draw;
 * ProcessImageCommandStream, extracting the menu sprites afresh, calls
 * Indoor_MenuSpritesReloaded. */
void Indoor_SidePitchColours(void);
void Indoor_MenuSpritesReloaded(void);

/* How many tactics a team can choose from: 18 outdoors, INDOOR_TACTICS indoors. */
int16_t Indoor_TacticsCount(void);

/* TacticsData_SelectRelease (the Amiga or DOS 4-4-2, at every InitTeamsData) puts a
 * release's preset in a slot of the table. While the indoor tactics are installed it
 * goes into their backup instead, for slots 0-4: returns 1 then, 0 if the caller should
 * write the table itself. */
int16_t Indoor_TakeOutdoorTactic(int16_t slot, TeamTactics *t);

/* ---- The court (phase 4) -----------------------------------------------------------
 * The 672x880 world, the camera and the centre spot (336, 449) stay; the indoor court is
 * a smaller rectangle centred in it. Its size is the owner's SWIS hall
 * (libgdx-swis-2022, hall/playground_v2.png): court 955 x 1398, penalty area 438 x 220
 * there, in units of a third of a SWOS pixel (its player sprites are SWOS's at x3), would
 * be 318 x 466 and 146 x 73 here. A futsal-sized 150 x 244 left no room to run, the full
 * 318 x 466 was too big (the owner, playing both); 234 x 354 was the first compromise,
 * 276 x 418 the one for six a side (phase 5.1), and after playing that the court is 15 %
 * bigger again (phase 5.2): 318 x 482, everything in it scaled with it.
 *
 * Code written for the outdoor pitch keeps its numbers and wraps them, so outdoors it is
 * unchanged and indoors it lands on the court:
 *   INDOOR_X(x), INDOOR_Y(y)   a position proportional to the pitch -- piecewise linear
 *                              through the lines and the penalty area's edges (81, 193,
 *                              478, 590 across; 129, 216, 682, 769 along), so those land on
 *                              the court's exactly; shifted beyond the lines. Touchlines,
 *                              the penalty area, the tactics grid, the AI's bands.
 *   INDOOR_GY(y)               a position tied to a GOAL: shifted, not scaled, as the goal
 *                              sprite is not (net, posts, crossbar, the keeper's lines).
 *   INDOOR_DX(d), INDOOR_DY(d) a distance, scaled.
 * The goal's x (the mouth 302..366, the posts) needs none: the court is centred on 336
 * like the pitch, so the goals stay where they are across. */
#define INDOOR_HALF_WIDTH   159     /* 336 +- 159: 177..495 */
#define INDOOR_HALF_LENGTH  241     /* 449 +- 241: 208..690 */
#define INDOOR_BOX_HALF_WIDTH 74    /* the penalty area: 262..410 */
#define INDOOR_BOX_DEPTH      75    /* ... 75 deep: to 283 / from 615 */
#define INDOOR_FREE_KICK_DISTANCE 60 /* futsal's 5 m at the court's scale; outdoors 65 px (9.15 m) */
#define INDOOR_FREE_KICK_STEP     66 /* sideways out of it; outdoors 70 */
#define INDOOR_LEFT    (336 - INDOOR_HALF_WIDTH)
#define INDOOR_RIGHT   (336 + INDOOR_HALF_WIDTH)
#define INDOOR_TOP     (449 - INDOOR_HALF_LENGTH)
#define INDOOR_BOTTOM  (449 + INDOOR_HALF_LENGTH)

/* Out of line on purpose: inlined at the ~60 places that wrap a number, they cost the
 * m68k binary 11 KB. */
int16_t Indoor_MapX(int16_t x);
int16_t Indoor_MapY(int16_t y);
int16_t Indoor_GoalY(int16_t y);

/* The penalty spot (outdoors 187 / 711, 11 m): futsal's 6 m, at the edge of the area. */
#define INDOOR_PENALTY_SPOT 71
#define INDOOR_SPOT_Y(y) (_gIndoor ? (int16_t)((y) <= 449 ? INDOOR_TOP + INDOOR_PENALTY_SPOT \
                                                         : INDOOR_BOTTOM - INDOOR_PENALTY_SPOT) \
                                   : (int16_t)(y))

#define INDOOR_X(x)   (_gIndoor ? Indoor_MapX((int16_t)(x))  : (int16_t)(x))
#define INDOOR_Y(y)   (_gIndoor ? Indoor_MapY((int16_t)(y))  : (int16_t)(y))
#define INDOOR_GY(y)  (_gIndoor ? Indoor_GoalY((int16_t)(y)) : (int16_t)(y))
#define INDOOR_DX(d)  (_gIndoor ? (int16_t)((int32_t)(d) * (INDOOR_RIGHT - INDOOR_LEFT) / (590 - 81)) : (int16_t)(d))
#define INDOOR_DY(d)  (_gIndoor ? (int16_t)((int32_t)(d) * (INDOOR_BOTTOM - INDOOR_TOP) / (769 - 129)) : (int16_t)(d))

/* The hall's floor, loaded by LoadPitchMap for an indoor match in place of the pitch
 * (generated by scripts/make_indoor_hall.py; a file of the project's own, tracked). */
#ifdef SWOS_SDL_BUILD
#define INDOOR_HALL_MAP "grafs_indoor/LINO.MAP"
#else
#define INDOOR_HALL_MAP "grafs_indoor/LINO_A.MAP"   /* the glass at the ends baked in */
#endif

/* ---- The crowd (phase 5b) ----------------------------------------------------------
 * The stands are behind the goals, three rows of map cells each, past the goal and a
 * strip of floor beyond the glass -- not at the outer edge of the map (rows 1-3 / 51-53),
 * which the indoor camera never reaches. They are the stadium's own fans: LINO.MAP holds
 * blanks in tiles 1-26 (the crowd's, in every SWOS pitch bank, Amiga or PC), filled at
 * load from the pitch the match would have been played on (pitch.c), so no original art
 * is in the file. Map row 0 lists them as a pitch does, for the kit colours.
 * The fan animation (animated_patterns.c) and the camera (move_camera.c) follow the rows.
 * Kept in step with scripts/make_indoor_hall.py. */
#define INDOOR_CROWD_ROWS        5      /* 3 until phase 5.3; the fans animate in the 3 nearest */
#define INDOOR_CROWD_TOP_ROW     (INDOOR_TOP / 16 - 2 - INDOOR_CROWD_ROWS) /* 6: y 96..175 */
#define INDOOR_CROWD_BOTTOM_ROW  ((INDOOR_BOTTOM + 16) / 16 + 1) /* 45: y 720..799 */
#define INDOOR_CROWD_TILES       26     /* tiles 1..26 of a pitch bank */

/* The camera's reach along the hall: the top stand's first row to the bottom one's last. */
#define INDOOR_VIEW_TOP     (INDOOR_CROWD_TOP_ROW * 16)
#define INDOOR_VIEW_BOTTOM  ((INDOOR_CROWD_BOTTOM_ROW + INDOOR_CROWD_ROWS) * 16)

/* ---- The sides of the hall (phase 5.1; the stands wider in 5.2 and 5.3) --------------
 * Across, left to right: the left stand (9 columns, the top team's crowd tiles), floor,
 * the bench's dugouts (drawn in LINO.MAP at the outdoor ones' y, their right edge at
 * INDOOR_BENCH_X), floor, the court, floor, the right stand (the bottom team's tiles).
 * The side stands run between the end stands; the fan animation picks cells in them too.
 * The camera follows the ball across as outdoors, clamped to INDOOR_HALL_LEFT..RIGHT:
 * laid out for the 4:3 view (336) and the Amiga's (320) -- no widescreen (the owner) --
 * so the view pans, and the bench is seen while play is on the left. */
#define INDOOR_GLASS_LEFT        10     /* the side glass's width, left and right */
#define INDOOR_GLASS_RIGHT       10
#define INDOOR_BENCH_X           (INDOOR_LEFT - INDOOR_GLASS_LEFT - 4)  /* the bench (its sprites), 163 */
#define INDOOR_SIDE_COLS         8      /* 9 for a moment in 5.3, 6 in 5.2, 2 in 5.1 */
#define INDOOR_SIDE_LEFT_COL     ((INDOOR_BENCH_X - 16 - 5) / 16 - INDOOR_SIDE_COLS)   /* 0: x 0..127 */
#define INDOOR_SIDE_RIGHT_COL    ((INDOOR_RIGHT + INDOOR_GLASS_RIGHT + 15) / 16)         /* 32: x 512..639 */
#define INDOOR_SIDE_FIRST_ROW    (INDOOR_CROWD_TOP_ROW + INDOOR_CROWD_ROWS)             /* 11 */
#define INDOOR_SIDE_ROWS         (INDOOR_CROWD_BOTTOM_ROW - INDOOR_SIDE_FIRST_ROW)      /* 34: rows 11..44 */
#define INDOOR_HALL_LEFT         (INDOOR_SIDE_LEFT_COL * 16)
#define INDOOR_HALL_RIGHT        ((INDOOR_SIDE_RIGHT_COL + INDOOR_SIDE_COLS) * 16)
/* The bench's world positions are the outdoor ones (_benchXLimit 27, the subs menu at
 * 100, ...) moved right by this, and so is the camera while the bench is called. */
#define INDOOR_BENCH_SHIFT       (INDOOR_BENCH_X - 27)

/* ---- The glass (phase 5.3) ----------------------------------------------------------
 * Grey and see-through (the owner's SWIS hall: (98,98,98) at ~60 %). Along the sides it is
 * in the floor (LINO.MAP), INDOOR_GLASS_LEFT/RIGHT wide. At the ends it is seen in
 * perspective, as high as from the three nearest rows of fans to the goal line: the top
 * wall hides what is behind it -- the floor there and those fans -- and the bottom one as
 * many of the court's last rows and the players in them. Each has a HOLE where its goal
 * stands, the goal's width and height, seen through clear: the top goal, a keeper on the
 * bottom goal line. The players are IN FRONT of the top wall and BEHIND the bottom one. Both
 * targets draw the walls over every frame (Indoor_DrawGlassBehind before the sprites, the top
 * wall: fans greyed live, players not; Indoor_DrawGlassFront after them, the bottom wall: the
 * players there greyed) -- the PC through a colour table. The Amiga cannot afford that a
 * frame: its map (LINO_A.MAP) has the glass baked in as a checkerboard, the fans behind the
 * top wall are greyed tiles that no longer move, and only the boxes of the players and the
 * ball behind the bottom wall are greyed a frame (indoor.c). */
#define INDOOR_GLASS_X0          (INDOOR_LEFT - INDOOR_GLASS_LEFT)          /* 167 */
#define INDOOR_GLASS_X1          (INDOOR_RIGHT + INDOOR_GLASS_RIGHT)        /* 505 */
#define INDOOR_GLASS_TOP_Y0      ((INDOOR_CROWD_TOP_ROW + INDOOR_CROWD_ROWS - 3) * 16)  /* 128 */
#define INDOOR_GLASS_HEIGHT      (INDOOR_TOP - INDOOR_GLASS_TOP_Y0)          /* 80, both ends */
#define INDOOR_GLASS_HOLE_X0     300    /* the goal frame, outside to outside */
#define INDOOR_GLASS_HOLE_X1     372
#define INDOOR_GLASS_HOLE_H_TOP    24   /* the goals' heights at the line: the top one's */
#define INDOOR_GLASS_HOLE_H_BOTTOM 18   /* (net to the line) and the bottom one's frame */
void Indoor_DrawGlassBehind(void);
void Indoor_DrawGlassFront(void);
/* The Amiga: the greyed fans behind the top wall, tiles 27-52 of LINO_A.MAP from 1-26 (the
 * crowd), made once the crowd has its kit colours (FixupAndColorPitchFile). */
void Indoor_GlassCrowdTiles(uint8_t *tiles);

/* A number of the crowd code outdoors, its indoor counterpart indoors. */
#define INDOOR_OR(outdoor, indoor)  (_gIndoor ? (indoor) : (outdoor))

/* Every frame, after the players have moved, in play and at stoppages (not while the
 * teams walk on or off, nor during a substitution): the players on the court kept inside
 * its walls (the parked and the sent-off left alone). Indoor_BeginMatch has
 * already mapped the tactics grid's coordinate tables and the ball's sector limits onto
 * the court and moved the goal sprites to its ends (Indoor_EndMatch gives them back). */
void Indoor_KeepPlayersInside(void);

/* The ball on the hall's floor (phase 5.2, 5.3): its ground friction times NUM/DEN -- 4/5,
 * so 15 a frame on the HARD floor becomes 12 and the ball rolls further than outdoors (2/3
 * was too far, the owner). In the air as outdoors. UpdateBall. */
#define INDOOR_BALL_FRICTION_NUM 4
#define INDOOR_BALL_FRICTION_DEN 5

/* UpdateBall, instead of the outdoor "out of play": the ball off a wall, unless it is
 * going into a goal (in the mouth, under the bar), which is left to the goal code.
 * Returns 1 if it was a goal-mouth crossing CheckIfBallOutOfPlay must decide. */
int16_t Indoor_BallAtWall(Sprite *ball, int32_t savedX, int32_t savedY);

/* ---- The keeper's throw (phase 5.1) ------------------------------------------------
 * A ball the keeper releases from his HANDS (a throw, a pass or a kick out of them) must
 * not cross the halfway line before another player -- of either side -- has touched it;
 * if it does, the other side has a free kick on the halfway line where it crossed.
 * UpdatePlayers' two restart blocks call Indoor_KeeperReleased as the ball leaves a
 * player (it acts only for a keeper holding the ball, indoors); UpdateBall calls
 * Indoor_KeeperRuleCheck every frame the ball is in play. A ball off a body that is not
 * a kick, a header or a control does not count as touched: the game does not record it. */
void Indoor_KeeperReleased(Sprite *player, TeamGeneralInfo *team);
/* The computer's keeper knows the rule: holding the ball indoors, it passes (quick
 * fire) where outdoors it would kick (ai.c). */
#define INDOOR_KEEPER_PASSES()  (_gIndoor && _gameState == ST_KEEPER_HOLDS_BALL)
void Indoor_KeeperRuleCheck(const Sprite *ball);

/* ---- The time penalty (phase 5.1) --------------------------------------------------
 * Indoors a red card -- straight or a second yellow -- sends the player to his team's
 * dugout for INDOOR_PENALTY_MINUTES of the indoor clock, a small red card over him; his side plays one short meanwhile,
 * then he comes back on, booked. SendPlayerAway calls Indoor_TimePenalty; the return is
 * Indoor_ParkPlayers' (every frame). SWOS_INDOOR_TEST_RED=1 makes every card a red, to
 * see it in a computer match (Indoor_TestRed, tackle_foul.c). */
#define INDOOR_PENALTY_MINUTES 5
void    Indoor_TimePenalty(Sprite *player, TeamGeneralInfo *team);
/* Every frame (DrawBenchAndSubsMenu, after the sprites): the small red card over each
 * player sitting out, by his team's dugout. Nothing outdoors. */
void    Indoor_DrawPenaltyCards(void);
int16_t Indoor_TestRed(void);

#endif /* INDOOR_H */
