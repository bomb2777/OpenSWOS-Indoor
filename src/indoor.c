/* indoor.c -- INDOOR: a friendly played six a side in a hall (include/indoor.h,
 * docs/INDOOR.md). */
/* Size over speed: nothing here is hot (kick-off and two tiny per-frame loops), and at
 * -O2 the m68k compiler unrolled and inlined it to over 5 KB. */
#pragma GCC optimize ("Os")
#include "indoor.h"
#include "swos_types.h"
#include "game_types.h"
#include "menu_system.h"
#ifdef SWOS_SDL_BUILD
#include <stdio.h>
#include <stdlib.h>
#include "netplay_match.h"
#endif

int16_t _gIndoor;
int16_t _gIndoorFloor = INDOOR_LINOLEUM;
int16_t _gIndoorFriendlyFloor = INDOOR_OFF;

extern const char _aOff[];
extern char _aWood[], _aLinoleum[], _aStone[], _aBeachSand[];

extern uint16_t _gameType;
extern uint16_t _trainingGame;
extern uint16_t _gamePitchTypeOrSeason;
extern uint16_t _gamePitchType;

/* SWOS's pitch types, as _gamePitchType counts them. */
enum { PITCH_FROZEN = 0, PITCH_MUDDY = 1, PITCH_HARD = 6 };

int16_t Indoor_PitchTypeForFloor(int16_t floor)
{
    switch (floor) {
    case INDOOR_STONE: return PITCH_FROZEN;     /* polished stone plays like ice */
    case INDOOR_SAND:  return PITCH_MUDDY;      /* the ball dies in it */
    default:           return PITCH_HARD;       /* wood, linoleum */
    }
}

const char *Indoor_FloorName(int16_t floor)
{
    switch (floor) {
    case INDOOR_WOOD:     return _aWood;
    case INDOOR_LINOLEUM: return _aLinoleum;
    case INDOOR_STONE:    return _aStone;
    case INDOOR_SAND:     return _aBeachSand;
    default:              return _aOff;
    }
}

/* Only linoleum is drawn so far, so the button toggles OFF <-> LINOLEUM. Adding a
 * floor is adding it here once its pitch exists. */
int16_t Indoor_NextFriendlyFloor(int16_t floor)
{
    return floor == INDOOR_OFF ? INDOOR_LINOLEUM : INDOOR_OFF;
}

static void install_tactics(TeamFileEx *team1, TeamFileEx *team2);
static void release_tactics(void);
static void install_court(void);
static void release_court(void);
static void time_penalties_clear(void);

void Indoor_BeginMatch(TeamFileEx *team1, TeamFileEx *team2)
{
    int16_t floor = _gIndoorFriendlyFloor;

    _gIndoor = 0;
#ifdef SWOS_SDL_BUILD
    {
        /* SWOS_INDOOR=1 plays every friendly indoors (on linoleum, the one floor
         * there is), =0 none, without the key presses the FRIENDLY screen costs a
         * test. */
        const char *force = getenv("SWOS_INDOOR");
        if (force && force[0] == '1' && force[1] == '\0')
            floor = INDOOR_LINOLEUM;
        else if (force && force[0] == '0' && force[1] == '\0')
            floor = INDOOR_OFF;
    }
    extern int _gArcadeMode;
    /* An online match plays by the host's rules, which have no INDOOR yet. */
    if (_gArcadeMode || NetplayMatch_Enabled())
        floor = INDOOR_OFF;
#endif
    if (_gameType != 0 || _trainingGame != 0)
        floor = INDOOR_OFF;
    if (floor == INDOOR_OFF)
        return;

    _gIndoor = 1;
    _gIndoorFloor = floor;
    time_penalties_clear();
    install_tactics(team1, team2);
    install_court();
#ifdef SWOS_SDL_BUILD
    fprintf(stderr, "[indoor] this friendly is played indoors: %s, pitch type %d\n",
            Indoor_FloorName(floor), Indoor_PitchTypeForFloor(floor));
#endif
}

void Indoor_PinPitch(void)
{
    if (!_gIndoor)
        return;
    /* The floor decides the pitch, whatever the screen's PITCH TYPE said. */
    _gamePitchTypeOrSeason = 0;
    _gamePitchType = (uint16_t)Indoor_PitchTypeForFloor(_gIndoorFloor);
}

void Indoor_EndMatch(void)
{
    time_penalties_clear();
    release_court();
    release_tactics();
    _gIndoor = 0;
}

/* ---- Tactics -------------------------------------------------------------------- */

extern TeamTactics *_teamTacticsTable[];
extern void        *_tacticsStringTable[];
extern void        *_sidePitchSurnamePositionsTable[];
extern char _aIndoor221[], _aIndoor212[], _aIndoor122[], _aIndoor311[], _aIndoor131[];

static int16_t      s_tacticsInstalled;
static TeamTactics *s_outdoorTactics[INDOOR_TACTICS];
static void        *s_outdoorNames[INDOOR_TACTICS];
static void        *s_outdoorSidePitch[INDOOR_TACTICS];
static TeamFileEx  *s_teams[2];
static uint8_t      s_teamTactics[2];

static void install_tactics(TeamFileEx *team1, TeamFileEx *team2)
{
    char *names[INDOOR_TACTICS] = { _aIndoor221, _aIndoor212, _aIndoor122, _aIndoor311, _aIndoor131 };

    if (s_tacticsInstalled)
        return;
    for (int16_t i = 0; i < INDOOR_TACTICS; i++) {
        s_outdoorTactics[i] = _teamTacticsTable[i];
        s_outdoorNames[i] = _tacticsStringTable[i];
        s_outdoorSidePitch[i] = _sidePitchSurnamePositionsTable[i];
        _teamTacticsTable[i] = &_indoorTactics[i];
        _tacticsStringTable[i] = names[i];
        _sidePitchSurnamePositionsTable[i] = _indoorSidePitch[i];
    }
    /* A team on a tactic past the five plays 2-2-1 for this match. */
    s_teams[0] = team1;
    s_teams[1] = team2;
    for (int16_t t = 0; t < 2; t++) {
        if (!s_teams[t])
            continue;
        s_teamTactics[t] = s_teams[t]->team.tactics;
        if (s_teams[t]->team.tactics >= INDOOR_TACTICS)
            s_teams[t]->team.tactics = 0;
    }
    s_tacticsInstalled = 1;
}

static void release_tactics(void)
{
    if (!s_tacticsInstalled)
        return;
    for (int16_t i = 0; i < INDOOR_TACTICS; i++) {
        _teamTacticsTable[i] = s_outdoorTactics[i];
        _tacticsStringTable[i] = s_outdoorNames[i];
        _sidePitchSurnamePositionsTable[i] = s_outdoorSidePitch[i];
    }
    for (int16_t t = 0; t < 2; t++)
        if (s_teams[t])
            s_teams[t]->team.tactics = s_teamTactics[t];
    s_tacticsInstalled = 0;
}

int16_t Indoor_TacticsCount(void)
{
    return _gIndoor ? INDOOR_TACTICS : 18;
}

int16_t Indoor_TakeOutdoorTactic(int16_t slot, TeamTactics *t)
{
    if (!s_tacticsInstalled || slot < 0 || slot >= INDOOR_TACTICS)
        return 0;
    s_outdoorTactics[slot] = t;
    return 1;
}

/* ---- Five a side ---------------------------------------------------------------- */

extern Sprite **_team1SpritesTable;
extern Sprite **_team2SpritesTable;
extern uint16_t _gameStatePl;

/* Where the parked players stand: beside the left touchline, as far out as a
 * sent-off player walks (SendPlayerAway: -20) and then some, so not even a sprite's
 * edge shows at the camera's leftmost position. */
enum { kParkX = -40, kParkY = 449 };

static void park(Sprite *s)
{
    s->sentAway = 1;
    s->x.s.i = kParkX;
    s->x.s.frac = 0;
    s->y.s.i = kParkY;
    s->y.s.frac = 0;
    s->z.raw = 0;
    s->destX = kParkX;
    s->destY = kParkY;
    s->speed = 0;
    s->deltaX.raw = 0;
    s->deltaY.raw = 0;
    s->deltaZ.raw = 0;
}

static void keeper_rule_disarm(void);
static void time_penalties_end(void);

void Indoor_ParkPlayers(void)
{
    if (!_gIndoor)
        return;
    if (_gameStatePl != ST_GAME_IN_PROGRESS)
        keeper_rule_disarm();       /* any stoppage ends it: a goal, a foul, half time */
    time_penalties_end();
    for (int16_t i = INDOOR_PLAYERS; i < 11; i++) {
        park(_team1SpritesTable[i]);
        park(_team2SpritesTable[i]);
    }
}

int16_t Indoor_RestartOrdinal(int16_t ordinal)
{
    static const int16_t kRole[INDOOR_PLAYERS + 1] = { 0, 1, 3, 4, 8, 10, 11 };
    if (!_gIndoor || ordinal < 1 || ordinal > INDOOR_PLAYERS)
        return ordinal;
    return kRole[ordinal];
}

int16_t Indoor_IsParkedSlot(uint16_t slot)
{
    return (_gIndoor && slot >= INDOOR_PLAYERS && slot < 11) ? 1 : 0;
}

int16_t Indoor_BenchSize(void)
{
    return _gIndoor ? (int16_t)(16 - INDOOR_PLAYERS) : 5;
}

uint16_t Indoor_FirstBenchSlot(void)
{
    return _gIndoor ? INDOOR_PLAYERS : 11;
}

uint16_t Indoor_BenchSlot(uint16_t index)
{
    return (uint16_t)(index - 1 + Indoor_FirstBenchSlot());
}

const uint8_t *Indoor_ListOrder(const uint8_t *rolesOrder)
{
    static const uint8_t kSlotOrder[11] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
    return _gIndoor ? kSlotOrder : rolesOrder;
}

void Indoor_TacticsButtons(struct MenuEntry *first)
{
    MenuEntry *e = first;
    for (int16_t i = 0; i < 18; i++, e++) {
        if (i < INDOOR_TACTICS)
            e->fg = (uintptr_t)_tacticsStringTable[i];
        else
            e->isInvisible = 1;
    }
}

void Indoor_StadiumRows(struct MenuEntry *first)
{
    MenuEntry *e = first;
    if (!_gIndoor)
        return;
    /* Left: shirts 5-15, names 16-26, substitutes 27-31; right: 32-42, 43-53, 54-58. */
    for (int16_t i = INDOOR_PLAYERS; i < 11; i++) {
        e[5 + i].isInvisible = 1;
        e[16 + i].isInvisible = 1;
        e[32 + i].isInvisible = 1;
        e[43 + i].isInvisible = 1;
    }
    for (int16_t k = 0; k < 5; k++) {
        e[27 + k].isInvisible = 1;
        e[54 + k].isInvisible = 1;
    }
}

/* ---- The court (phase 4) ------------------------------------------------------------ */

/* Linear between two fixed points (a0 -> b0, a1 -> b1). */
static int16_t lerp_seg(int16_t v, int16_t a0, int16_t a1, int16_t b0, int16_t b1)
{
    return (int16_t)(b0 + (int32_t)(v - a0) * (b1 - b0) / (a1 - a0));
}

__attribute__((noinline)) int16_t Indoor_MapX(int16_t x)
{
    const int16_t boxL = 336 - INDOOR_BOX_HALF_WIDTH, boxR = 336 + INDOOR_BOX_HALF_WIDTH;
    if (x < 81)   return (int16_t)(x + (INDOOR_LEFT - 81));
    if (x > 590)  return (int16_t)(x - (590 - INDOOR_RIGHT));
    if (x < 193)  return lerp_seg(x, 81, 193, INDOOR_LEFT, boxL);
    if (x <= 478) return lerp_seg(x, 193, 478, boxL, boxR);
    return lerp_seg(x, 478, 590, boxR, INDOOR_RIGHT);
}

__attribute__((noinline)) int16_t Indoor_MapY(int16_t y)
{
    const int16_t boxT = INDOOR_TOP + INDOOR_BOX_DEPTH, boxB = INDOOR_BOTTOM - INDOOR_BOX_DEPTH;
    if (y < 129)  return (int16_t)(y + (INDOOR_TOP - 129));
    if (y > 769)  return (int16_t)(y - (769 - INDOOR_BOTTOM));
    if (y < 216)  return lerp_seg(y, 129, 216, INDOOR_TOP, boxT);
    if (y <= 682) return lerp_seg(y, 216, 682, boxT, boxB);
    return lerp_seg(y, 682, 769, boxB, INDOOR_BOTTOM);
}

__attribute__((noinline)) int16_t Indoor_GoalY(int16_t y)
{
    return (int16_t)(y <= 449 ? y + (INDOOR_TOP - 129) : y - (769 - INDOOR_BOTTOM));
}


extern void ResetBothTeamSpinTimers(void);

/* How much of its speed the ball keeps off the glass: 15/16 (phase 5.2; a post keeps 3/4). */
#define INDOOR_WALL_KEEP_SHIFT 4

static void mirror_dest(int16_t *dest, int32_t saved)
{
    int16_t at = (int16_t)(saved >> 16);
    *dest = (int16_t)(at - (*dest - at));
}

int16_t Indoor_BallAtWall(Sprite *ball, int32_t savedX, int32_t savedY)
{
    int16_t bx = ball->x.s.i;
    int16_t by = ball->y.s.i;
    int16_t hit = 0;

    if (by < INDOOR_TOP || by > INDOOR_BOTTOM) {
        /* Into the goal: in the mouth (UpdateBall's posts test x - 1 against 302..366,
         * as CheckIfBallOutOfPlay does) and under the bar. The goal code decides. */
        if (bx - 1 >= 302 && bx - 1 <= 366 && ball->z.s.i <= 15 &&
            bx >= INDOOR_LEFT && bx <= INDOOR_RIGHT)
            return 1;
        mirror_dest(&ball->destY, savedY);
        hit = 1;
    }
    if (bx < INDOOR_LEFT || bx > INDOOR_RIGHT) {
        mirror_dest(&ball->destX, savedX);
        hit = 1;
    }
    if (hit) {
        ball->x.raw = savedX;
        ball->y.raw = savedY;
        ball->speed = (int16_t)(ball->speed - (ball->speed >> INDOOR_WALL_KEEP_SHIFT));
        ResetBothTeamSpinTimers();
    }
    return 0;
}

/* The tactics grid's target coordinates (a tactic's position byte indexes them) and the
 * ball's sector limits (which of the 5x7 sectors the ball is in), mapped onto the court
 * for the match: then every tactic, outdoor or indoor, means the same on it. [4] of the
 * X limits is the Y sectors' leading bound (129), read as such by UpdateBall. */
extern uint16_t _playerXQuadrantsCoordinates[15];
extern uint16_t _playerYQuadrantCoordinates[16];
extern uint16_t _ballXQuadrantLimits[5];
extern uint16_t _ballYQuadrantLimits[6];
extern Sprite   _goal1TopSprite, _goal2BottomSprite;

static int16_t  s_courtInstalled;
static uint16_t s_playerX[15], s_playerY[16], s_ballX[5], s_ballY[6];
static int32_t  s_goal1Y, s_goal2Y;

static void install_court(void)
{
    int16_t i;
    if (s_courtInstalled)
        return;
    for (i = 0; i < 15; i++) {
        s_playerX[i] = _playerXQuadrantsCoordinates[i];
        _playerXQuadrantsCoordinates[i] = (uint16_t)Indoor_MapX((int16_t)s_playerX[i]);
    }
    for (i = 0; i < 16; i++) {
        s_playerY[i] = _playerYQuadrantCoordinates[i];
        _playerYQuadrantCoordinates[i] = (uint16_t)Indoor_MapY((int16_t)s_playerY[i]);
    }
    for (i = 0; i < 4; i++) {
        s_ballX[i] = _ballXQuadrantLimits[i];
        _ballXQuadrantLimits[i] = (uint16_t)Indoor_MapX((int16_t)s_ballX[i]);
    }
    s_ballX[4] = _ballXQuadrantLimits[4];
    _ballXQuadrantLimits[4] = (uint16_t)Indoor_MapY((int16_t)s_ballX[4]);
    for (i = 0; i < 6; i++) {
        s_ballY[i] = _ballYQuadrantLimits[i];
        _ballYQuadrantLimits[i] = (uint16_t)Indoor_MapY((int16_t)s_ballY[i]);
    }
    /* The goals: shifted, as the goal frame in UpdateBall is (INDOOR_GY). */
    s_goal1Y = _goal1TopSprite.y.raw;
    s_goal2Y = _goal2BottomSprite.y.raw;
    _goal1TopSprite.y.s.i = Indoor_GoalY(_goal1TopSprite.y.s.i);
    _goal2BottomSprite.y.s.i = Indoor_GoalY(_goal2BottomSprite.y.s.i);
    s_courtInstalled = 1;
}

static void release_court(void)
{
    int16_t i;
    if (!s_courtInstalled)
        return;
    for (i = 0; i < 15; i++) _playerXQuadrantsCoordinates[i] = s_playerX[i];
    for (i = 0; i < 16; i++) _playerYQuadrantCoordinates[i] = s_playerY[i];
    for (i = 0; i < 5; i++)  _ballXQuadrantLimits[i] = s_ballX[i];
    for (i = 0; i < 6; i++)  _ballYQuadrantLimits[i] = s_ballY[i];
    _goal1TopSprite.y.raw = s_goal1Y;
    _goal2BottomSprite.y.raw = s_goal2Y;
    s_courtInstalled = 0;
}

static void keep_inside(Sprite *s)
{
    if (s->sentAway)
        return;                     /* on his way to the bench, or sitting out his time */
    if (s->x.s.i < INDOOR_LEFT)   { s->x.s.i = INDOOR_LEFT;   s->x.s.frac = 0; }
    if (s->x.s.i > INDOOR_RIGHT)  { s->x.s.i = INDOOR_RIGHT;  s->x.s.frac = 0; }
    if (s->y.s.i < INDOOR_TOP)    { s->y.s.i = INDOOR_TOP;    s->y.s.frac = 0; }
    if (s->y.s.i > INDOOR_BOTTOM) { s->y.s.i = INDOOR_BOTTOM; s->y.s.frac = 0; }
}

void Indoor_KeepPlayersInside(void)
{
    if (!_gIndoor || _gameStatePl != ST_GAME_IN_PROGRESS)
        return;
    for (int16_t i = 0; i < INDOOR_PLAYERS; i++) {
        keep_inside(_team1SpritesTable[i]);
        keep_inside(_team2SpritesTable[i]);
    }
}

/* ---- The keeper's throw (phase 5.1) ------------------------------------------------ */

extern int16_t  _gameState;
extern Sprite  *_lastPlayerPlayed;
extern int16_t  _breakCameraMode;
extern uint8_t  _playerTurnFlags;
extern int16_t  _foulXCoordinate, _foulYCoordinate;
extern int16_t  _cameraDirection;
extern int16_t  _cameraXVelocity, _cameraYVelocity;
extern int16_t  _gameNotInProgressCounterWriteOnly;
extern int16_t  _stoppageTimerTotal, _stoppageTimerActive;
extern TeamGeneralInfo *_lastTeamPlayedBeforeBreak;
extern TeamGeneralInfo  _topTeamData;
extern void StopAllPlayers(void);
extern void PlayFoulWhistleSample(void);

static Sprite          *s_keeperReleaser;   /* the keeper whose throw is in the air; 0 = none */
static TeamGeneralInfo *s_keeperTeam;
static int16_t          s_keeperBallY;      /* the ball's y when last looked at */

static void keeper_rule_disarm(void)
{
    s_keeperReleaser = 0;
}

void Indoor_KeeperReleased(Sprite *player, TeamGeneralInfo *team)
{
    if (!_gIndoor || _gameState != ST_KEEPER_HOLDS_BALL || player->playerOrdinal != 1)
        return;
    s_keeperReleaser = player;
    s_keeperTeam = team;
    s_keeperBallY = player->y.s.i;      /* the ball is in his hands */
}

/* A free kick to the side that did not break the rule, at (x, y): a foul's stoppage
 * without a foul -- the tail of TestFoulForPenaltyAndFreeKick (tackle_foul.c), ST_FOUL
 * (no wall), the camera turned as for a foul by `offender`. */
static void award_free_kick(TeamGeneralInfo *offender, int16_t x, int16_t y)
{
    PlayFoulWhistleSample();
    _cameraDirection = (offender == &_topTeamData) ? 0 : 4;
    _gameState = ST_FOUL;
    _breakCameraMode = -1;
    _playerTurnFlags = 0xFF;
    _foulXCoordinate = x;
    _foulYCoordinate = y;
    _gameStatePl = ST_STOPPED;
    _gameNotInProgressCounterWriteOnly = 0;
    _lastTeamPlayedBeforeBreak = offender->opponentsTeam;
    _stoppageTimerTotal = 0;
    _stoppageTimerActive = 0;
    StopAllPlayers();
    _cameraXVelocity = 0;
    _cameraYVelocity = 0;
}

void Indoor_KeeperRuleCheck(const Sprite *ball)
{
    int16_t y;

    if (!_gIndoor || !s_keeperReleaser || _gameStatePl != ST_GAME_IN_PROGRESS)
        return;
    if (_lastPlayerPlayed != s_keeperReleaser) {    /* someone else has played it */
        keeper_rule_disarm();
        return;
    }
    y = ball->y.s.i;
    if ((s_keeperBallY <= 449) != (y <= 449)) {
        TeamGeneralInfo *offender = s_keeperTeam;
        keeper_rule_disarm();
#ifdef SWOS_SDL_BUILD
        fprintf(stderr, "[indoor] the keeper's throw crossed the halfway line untouched: "
                        "free kick at x %d\n", ball->x.s.i);
#endif
        award_free_kick(offender, ball->x.s.i, 449);
        return;
    }
    s_keeperBallY = y;
}

/* ---- The time penalty (phase 5.1) ------------------------------------------------ */

extern uint32_t _gameTime;
extern PlayerInfo *GetPlayerPointerFromShirtNumber(Sprite *player, TeamGeneralInfo *team);

/* The minute the clock shows: _gameTime is BCD in the m68k byte order on both targets
 * (byte 1 hundreds, 2 tens, 3 ones -- game_state.c's GAMETIME_* ). */
static int16_t clock_minute(void)
{
    const uint8_t *t = (const uint8_t *)&_gameTime;
    return (int16_t)(t[1] * 100 + t[2] * 10 + t[3]);
}

/* Two a side can sit out at once: a third red before the first is back is still a
 * send-off, only for the rest of the match. */
#define INDOOR_PENALTY_SLOTS 4
static struct {
    Sprite          *player;    /* 0 = free */
    TeamGeneralInfo *team;
    int16_t          backAt;    /* the clock minute he comes back on */
} s_penalties[INDOOR_PENALTY_SLOTS];

static void time_penalties_clear(void)
{
    for (int16_t i = 0; i < INDOOR_PENALTY_SLOTS; i++)
        s_penalties[i].player = 0;
}

void Indoor_TimePenalty(Sprite *player, TeamGeneralInfo *team)
{
    if (!_gIndoor)
        return;
    player->destX = INDOOR_BENCH_X;     /* to the bench, not off the pitch */
    for (int16_t i = 0; i < INDOOR_PENALTY_SLOTS; i++) {
        if (!s_penalties[i].player) {
            s_penalties[i].player = player;
            s_penalties[i].team = team;
            s_penalties[i].backAt = (int16_t)(clock_minute() + INDOOR_PENALTY_MINUTES);
#ifdef SWOS_SDL_BUILD
            fprintf(stderr, "[indoor] time penalty: player %d sits out from minute %d to %d\n",
                    player->playerOrdinal, clock_minute(), s_penalties[i].backAt);
#endif
            return;
        }
    }
}

static void time_penalties_end(void)
{
    int16_t now = clock_minute();
    for (int16_t i = 0; i < INDOOR_PENALTY_SLOTS; i++) {
        Sprite *p = s_penalties[i].player;
        PlayerInfo *pi;
        if (!p || now < s_penalties[i].backAt)
            continue;
        s_penalties[i].player = 0;
        if (!p->sentAway)
            continue;               /* already back some other way */
        /* Back on, booked: one more yellow is another red. Positioning walks him from
         * the bench to his place in the formation. */
        p->sentAway = 0;
        p->cards = 1;
        pi = GetPlayerPointerFromShirtNumber(p, s_penalties[i].team);
        if (pi)
            pi->numYellowCards = 1;
#ifdef SWOS_SDL_BUILD
        fprintf(stderr, "[indoor] time penalty over: player %d back on at minute %d\n",
                p->playerOrdinal, now);
#endif
    }
}

int16_t Indoor_TestRed(void)
{
#ifdef SWOS_SDL_BUILD
    static int16_t s_testRed = -1;
    if (s_testRed < 0) {
        const char *v = getenv("SWOS_INDOOR_TEST_RED");
        s_testRed = (v && v[0] == '1' && v[1] == '\0') ? 1 : 0;
    }
    return (int16_t)(_gIndoor && s_testRed);
#else
    return 0;
#endif
}
