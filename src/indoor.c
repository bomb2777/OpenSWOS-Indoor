/* indoor.c -- INDOOR: a friendly played five a side in a hall (include/indoor.h,
 * docs/INDOOR.md). */
#include "indoor.h"
#include "swos_types.h"
#include "game_types.h"
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

void Indoor_BeginMatch(void)
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
    /* The floor decides the pitch, whatever the screen's PITCH TYPE said. */
    _gamePitchTypeOrSeason = 0;
    _gamePitchType = (uint16_t)Indoor_PitchTypeForFloor(floor);
#ifdef SWOS_SDL_BUILD
    fprintf(stderr, "[indoor] this friendly is played indoors: %s, pitch type %u\n",
            Indoor_FloorName(floor), (unsigned)_gamePitchType);
#endif
}

void Indoor_EndMatch(void)
{
    _gIndoor = 0;
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
