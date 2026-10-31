/* indoor.c -- INDOOR: a friendly played five a side in a hall (include/indoor.h,
 * docs/INDOOR.md). */
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
    install_tactics(team1, team2);
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
    release_tactics();
    _gIndoor = 0;
}

/* ---- Tactics -------------------------------------------------------------------- */

extern TeamTactics *_teamTacticsTable[];
extern void        *_tacticsStringTable[];
extern void        *_sidePitchSurnamePositionsTable[];
extern char _aIndoor22[], _aIndoor121[], _aIndoor31[], _aIndoor13[];

static int16_t      s_tacticsInstalled;
static TeamTactics *s_outdoorTactics[INDOOR_TACTICS];
static void        *s_outdoorNames[INDOOR_TACTICS];
static void        *s_outdoorSidePitch[INDOOR_TACTICS];
static TeamFileEx  *s_teams[2];
static uint8_t      s_teamTactics[2];

static void install_tactics(TeamFileEx *team1, TeamFileEx *team2)
{
    char *names[INDOOR_TACTICS] = { _aIndoor22, _aIndoor121, _aIndoor31, _aIndoor13 };

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
    /* A team on a tactic past the four plays 2-2 for this match. */
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

void Indoor_ParkPlayers(void)
{
    if (!_gIndoor)
        return;
    for (int16_t i = INDOOR_PLAYERS; i < 11; i++) {
        park(_team1SpritesTable[i]);
        park(_team2SpritesTable[i]);
    }
}

int16_t Indoor_RestartOrdinal(int16_t ordinal)
{
    static const int16_t kRole[INDOOR_PLAYERS + 1] = { 0, 1, 3, 4, 10, 11 };
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
