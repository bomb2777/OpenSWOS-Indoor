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
static uint8_t      s_outdoorOrder[2][16];  /* each team's line-up before the indoor one */

/* A player's place on the pitch from his position in the team file: 0 keeper, 1 defence
 * (RB LB CD), 2 midfield (RW LW MF), 3 attack (ST). */
static int16_t position_group(const TeamFileEx *t, uint8_t fileIndex)
{
    int16_t pos = (int16_t)((t->players[fileIndex].positionAndFace & PF_POSITION_MASK) >> PF_POSITION_SHIFT);
    if (pos == 0) return 0;
    if (pos <= 3) return 1;
    if (pos <= 6) return 2;
    return 3;
}

/* The indoor line-up (phase 5.3; by value since the owner's correction): the tactic's
 * places filled in order -- the keeper, then its outfield places back to front -- each by
 * the BEST player left who plays that position (his SWIS's rule, getStartingFiveAccording-
 * ToTactic, picked the first in the team's order; that left Germany's Sammer and Moller
 * out), best meaning the highest market value (PlayerFile.price, which SWOS derives from
 * the skills), the team's own order deciding a tie; failing one of that position, the best
 * outfield player left. Then the bench, everyone else, by position (keepers, defence,
 * midfield, attack) and within one by value. Given back at the end of the match
 * (release_tactics). */
static int16_t best_left(const TeamFileEx *t, const uint8_t *used, int16_t group, int16_t anyOutfield)
{
    int16_t i, best = -1, bestPrice = -1;
    for (i = 0; i < 16; i++) {
        uint8_t f = t->team.playerNumbers[i];
        int16_t g = position_group(t, f);
        int16_t price = (int16_t)(uint8_t)t->players[f].price;
        if (used[i] || (anyOutfield ? g == 0 : g != group))
            continue;
        if (price > bestPrice) {
            bestPrice = price;
            best = i;
        }
    }
    return best;
}

static void sort_line_up(TeamFileEx *t)
{
    /* The outfield places of each indoor tactic, ordinals 2-6 (make_indoor_tactics.py's
     * ROLES): 1 defence, 2 midfield, 3 attack. */
    static const uint8_t kPlaces[INDOOR_TACTICS][INDOOR_PLAYERS - 1] = {
        { 1, 1, 2, 2, 3 },      /* 2-2-1 */
        { 1, 1, 2, 3, 3 },      /* 2-1-2 */
        { 1, 2, 2, 3, 3 },      /* 1-2-2 */
        { 1, 1, 1, 2, 3 },      /* 3-1-1 */
        { 1, 2, 2, 2, 3 },      /* 1-3-1 */
    };
    uint8_t order[16], used[16];
    int16_t n = 0, i, k, g;
    uint8_t tactic = t->team.tactics < INDOOR_TACTICS ? t->team.tactics : 0;

    for (i = 0; i < 16; i++)
        used[i] = 0;
    for (k = 0; k < INDOOR_PLAYERS; k++) {
        int16_t want = k == 0 ? 0 : kPlaces[tactic][k - 1];
        int16_t pick = best_left(t, used, want, 0);
        if (pick < 0)
            pick = best_left(t, used, 0, 1);    /* none of that position left */
        if (pick < 0)
            for (pick = 0; used[pick]; pick++)
                ;
        used[pick] = 1;
        order[n++] = t->team.playerNumbers[pick];
    }
    for (g = 0; g <= 3; g++) {
        int16_t pick;
        while ((pick = best_left(t, used, g, 0)) >= 0) {
            used[pick] = 1;
            order[n++] = t->team.playerNumbers[pick];
        }
    }
    for (i = 0; i < 16; i++)
        t->team.playerNumbers[i] = order[i];
    t->team.playerValuesDirty = TEAM_VALUES_DIRTY;  /* as the line-up screen's swap does */
}

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
        for (int16_t i = 0; i < 16; i++)
            s_outdoorOrder[t][i] = s_teams[t]->team.playerNumbers[i];
        sort_line_up(s_teams[t]);
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
        if (s_teams[t]) {
            s_teams[t]->team.tactics = s_teamTactics[t];
            for (int16_t i = 0; i < 16; i++)
                s_teams[t]->team.playerNumbers[i] = s_outdoorOrder[t][i];
        }
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

extern void ConvertSpriteColors(uint16_t spriteIndex, uint16_t *colorTable);
#define SPR_SIDE_PITCH 225              /* the line-up screen's little pitch */

static int16_t s_sidePitchBlue;         /* sprite 225 has its colours swapped */

void Indoor_SidePitchColours(void)
{
    static uint16_t swap[16];
    if (s_sidePitchBlue == (_gIndoor != 0))
        return;
    for (int16_t i = 0; i < 16; i++)
        swap[i] = (uint16_t)i;
    swap[14] = 11;                      /* grass -> blue */
    swap[11] = 14;                      /* (and back) */
    ConvertSpriteColors(SPR_SIDE_PITCH, swap);
    s_sidePitchBlue = (_gIndoor != 0);
}

void Indoor_MenuSpritesReloaded(void)
{
    s_sidePitchBlue = 0;
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

extern int16_t _gameState;
extern int16_t _substituteInProgress;

void Indoor_KeepPlayersInside(void)
{
    if (!_gIndoor)
        return;
    /* In play and at every stoppage -- a slide or a dive the whistle stops goes on, and
     * used to go on through the glass (phase 5.3) -- but not while the teams walk on or
     * off (kick-off, half time, the end) nor while a substitute walks to or from the bench. */
    if (_gameStatePl != ST_GAME_IN_PROGRESS &&
        ((_gameState >= ST_STARTING_GAME && _gameState <= ST_GAME_ENDED) || _substituteInProgress))
        return;
    for (int16_t i = 0; i < INDOOR_PLAYERS; i++) {
        keep_inside(_team1SpritesTable[i]);
        keep_inside(_team2SpritesTable[i]);
    }
}

/* ---- The keeper's throw (phase 5.1) ------------------------------------------------ */

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

extern TeamGame _topTeamInGame;
extern void DrawSpriteHotspotInCameraView(uint16_t spriteIndex, int16_t x, int16_t y);
#define SPR_RED_CARD_SMALL 139          /* bench_subs.c's */

/* The y of a team's dugout: 389 or 485, by the rule the bench uses (bench_subs.c's
 * activation block and draw_bench_occupants_only). */
static int16_t team_bench_y(const TeamGeneralInfo *team)
{
    int16_t topY = 389, botY = 485;
    if ((uint8_t)_topTeamInGame.teamName[0] & 2) {
        int16_t t = topY; topY = botY; botY = t;
    }
    return team == &_topTeamData ? topY : botY;
}

void Indoor_TimePenalty(Sprite *player, TeamGeneralInfo *team)
{
    if (!_gIndoor)
        return;
    /* To his team's dugout, beside the seated bench: not off the pitch. */
    player->destX = INDOOR_BENCH_X + 10;
    player->destY = (int16_t)(team_bench_y(team) + 20);
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

void Indoor_DrawPenaltyCards(void)
{
    if (!_gIndoor)
        return;
    for (int16_t i = 0; i < INDOOR_PENALTY_SLOTS; i++) {
        const Sprite *p = s_penalties[i].player;
        if (p && p->sentAway)
            DrawSpriteHotspotInCameraView(SPR_RED_CARD_SMALL, p->x.s.i, (int16_t)(p->y.s.i - 22));
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

/* ---- The glass at the ends, drawn over each frame (PC; phase 5.3) -------------------- */

#ifdef SWOS_SDL_BUILD
#include "display.h"
extern void Platform_PaletteRGB(uint8_t rgb[768]);
extern volatile uintptr_t _frontbufferBasePtr;

static uint8_t  s_glassLut[256];     /* a colour -> that colour seen through the glass */
static uint8_t  s_glassRim;          /* the glass's top edge */
static uint32_t s_glassPalHash;      /* the palette the table was built for (0 = none) */

static int32_t nearest_colour(const uint8_t *rgb, int32_t r, int32_t g, int32_t b)
{
    int32_t best = 0, bestD = 0x7FFFFFFF;
    for (int32_t i = 0; i < 256; i++) {
        int32_t dr = rgb[i * 3] - r, dg = rgb[i * 3 + 1] - g, db = rgb[i * 3 + 2] - b;
        int32_t dd = dr * dr + dg * dg + db * db;
        if (dd < bestD) { bestD = dd; best = i; }
    }
    return best;
}

/* The table from the palette on screen, rebuilt when it changes (a fade, the pitch
 * condition's colours): 60 % of the glass's grey over each colour, as LINO.MAP's glass. */
static void glass_table(void)
{
    uint8_t rgb[768];
    uint32_t h = 2166136261u;
    Platform_PaletteRGB(rgb);
    for (int32_t i = 0; i < 768; i++)
        h = (h ^ rgb[i]) * 16777619u;
    h |= 1;
    if (h == s_glassPalHash)
        return;
    s_glassPalHash = h;
    for (int32_t i = 0; i < 256; i++)
        s_glassLut[i] = (uint8_t)nearest_colour(rgb, (rgb[i * 3] * 4 + 98 * 6) / 10,
                                                (rgb[i * 3 + 1] * 4 + 98 * 6) / 10,
                                                (rgb[i * 3 + 2] * 4 + 98 * 6) / 10);
    s_glassRim = (uint8_t)nearest_colour(rgb, 187, 187, 187);
}

/* World rectangle x0..x1, y0..y1 through the glass, row `rimY` its light rim -- but for
 * the goal's hole, rows h0..h1 of x INDOOR_GLASS_HOLE_X0..X1, seen through clear. */
static void glass_band(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t h0, int16_t h1,
                       int16_t rimY)
{
    int16_t camX = Render_CameraX(), camY = Render_CameraY();
    uint8_t *fb = (uint8_t *)_frontbufferBasePtr;
    for (int16_t y = y0; y <= y1; y++) {
        int16_t sy = (int16_t)(y - camY);
        int16_t inHole = (int16_t)(y >= h0 && y <= h1);
        uint8_t *row;
        if (sy < 0 || sy >= _screen_height)
            continue;
        row = fb + (size_t)sy * FRONTBUFFER_ROW_STRIDE_BYTES;
        for (int16_t x = x0; x <= x1; x++) {
            int16_t sx = (int16_t)(x - camX);
            if (sx < 0 || sx >= _screen_width)
                continue;
            if (inHole && x >= INDOOR_GLASS_HOLE_X0 && x <= INDOOR_GLASS_HOLE_X1)
                continue;
            row[sx] = y == rimY ? s_glassRim : s_glassLut[row[sx]];
        }
    }
}

/* The top wall, before the sprites: it stands behind the players (they are in front of
 * it, on the court), so it greys only what is behind it -- the floor and the three
 * nearest rows of fans, from them down to the goal line; its rim along its top. */
void Indoor_DrawGlassBehind(void)
{
    if (!_gIndoor)
        return;
    glass_table();
    glass_band(INDOOR_GLASS_X0, INDOOR_GLASS_TOP_Y0, INDOOR_GLASS_X1, INDOOR_TOP - 1,
               (int16_t)(INDOOR_TOP - INDOOR_GLASS_HOLE_H_TOP), INDOOR_TOP - 1, INDOOR_GLASS_TOP_Y0);
}

/* The bottom wall, after the sprites: the players on the court's last rows are behind it.
 * As high as the top one, up from the goal line; its rim along its foot, the goal line. */
void Indoor_DrawGlassFront(void)
{
    if (!_gIndoor)
        return;
    glass_table();
    glass_band(INDOOR_GLASS_X0, (int16_t)(INDOOR_BOTTOM - INDOOR_GLASS_HEIGHT), INDOOR_GLASS_X1, INDOOR_BOTTOM,
               (int16_t)(INDOOR_BOTTOM - INDOOR_GLASS_HOLE_H_BOTTOM), INDOOR_BOTTOM, INDOOR_BOTTOM);
}
#else
void Indoor_DrawGlassBehind(void)
{
}

void Indoor_DrawGlassFront(void)
{
}
#endif
