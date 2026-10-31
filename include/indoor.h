/* indoor.h -- INDOOR: a friendly played five a side in a hall (docs/INDOOR.md).
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

#include <stdint.h>

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

/* InitAndPlayGame, before kick-off: make this match indoor if it is a friendly and the
 * FRIENDLY screen (or SWOS_INDOOR=1, for a test) says so, and pin its pitch type to the
 * floor's. Anything else leaves _gIndoor 0. */
void Indoor_BeginMatch(void);

/* InitAndPlayGame, once the match is over. */
void Indoor_EndMatch(void);

#endif /* INDOOR_H */
