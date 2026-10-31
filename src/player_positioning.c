/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#include "player_positioning.h"
#include "indoor.h"          /* INDOOR: the court */

/* ==================================================================
 * Chunk 53 — Player Tactical Positioning
 *
 * Ported functions:
 *   SetPlayerWithNoBallDestination (A1=player, A6=team, D6=ballX, D7=ballY)
 * ================================================================== */

/* ---- External globals ---- */
extern TeamGeneralInfo _topTeamData;
extern TeamGeneralInfo _bottomTeamData;

extern int16_t  _gameState;

extern int16_t  _ballQuadrantIndex;
extern int16_t  _playerXQuadrantOffset;
extern int16_t  _playerYQuadrantOffset;

/* Tactics lookup table: array of raw 68k pointers to TeamTactics structures */
extern void *_teamTacticsTable[];

/* Position lookup tables (in SWOS2.s code section) */
extern int16_t  _playerXQuadrantsCoordinates[];
extern int16_t  _playerYQuadrantCoordinates[];


/* ==================================================================
 * SetPlayerWithNoBallDestination
 *   from: SetPlayerWithNoBallDestination
 *   in:  A1=player, A6=team, D6=ballX, D7=ballY
 *
 * Looks up team tactics table, gets position from quadrant-based
 * positioning table (35 entries per player x ballQuadrantIndex),
 * maps to world coordinates via playerXQuadrantsCoordinates /
 * playerYQuadrantCoordinates tables.
 * Goalkeeper uses separate proportional positioning (mulu/divu).
 * ================================================================== */
void __attribute__((used, externally_visible))
SetPlayerWithNoBallDestination(Sprite *player, TeamGeneralInfo *team,
                                int16_t ballX, int16_t ballY)
{
    /* Look up tactics structure */
    TeamTactics *tactics = (TeamTactics *)(_teamTacticsTable[team->tactics]);

    /* If ball out of play: use redirect tactics */
    if (_gameState == ST_KEEPER_HOLDS_BALL ||
        _gameState == ST_GOAL_OUT_LEFT ||
        _gameState == ST_GOAL_OUT_RIGHT) {
        int16_t idx = (int16_t)(int8_t)tactics->ballOutOfPlayTactics;
        tactics = (TeamTactics *)(_teamTacticsTable[idx]);
    }

    /* Advance to positions array */
    uint8_t *positions = (uint8_t *)tactics->positions;

    int16_t ordinal = player->playerOrdinal;
    int16_t d0 = ordinal - 2;

    if (d0 < 0) {
        /* Goalkeeper: proportional positioning */
        /* INDOOR: the keeper's band and the pitch it is proportional to, on the
         * court (indoor.h) */
        int16_t leftX = INDOOR_X(285);
        int16_t rightX = INDOOR_X(387);
        int16_t xRange = rightX - leftX + 1; /* 103 */
        uint16_t xRel = (uint16_t)(ballX - INDOOR_X(81));
        uint16_t destXCalc = (uint16_t)(((uint32_t)xRel * (uint32_t)xRange) / (uint16_t)INDOOR_DX(510));
        player->destX = (int16_t)(leftX + destXCalc);

        int16_t topY = INDOOR_GY(135);
        int16_t botY = INDOOR_GY(161);
        if (team != &_topTeamData) {
            topY = INDOOR_GY(737);
            botY = INDOOR_GY(763);
        }
        int16_t yRange = botY - topY + 1;
        uint16_t yRel = (uint16_t)(ballY - INDOOR_Y(129));
        uint16_t destYCalc = (uint16_t)(((uint32_t)yRel * (uint32_t)(uint16_t)yRange) / (uint16_t)INDOOR_DY(641));
        player->destY = (int16_t)(topY + destYCalc);
        return;
    }

    /* Non-goalkeeper: look up position from tactics table */
    uint16_t tableOffset = (uint16_t)d0 * 35;
    int16_t d3 = _playerXQuadrantOffset;
    int16_t d4 = _playerYQuadrantOffset;
    uint8_t positionByte;

    if (team == &_topTeamData) {
        tableOffset += (uint16_t)_ballQuadrantIndex;
        positionByte = positions[tableOffset];
    } else {
        /* Right/bottom team: mirror index and position */
        uint16_t mirrorIdx = 34 - (uint16_t)_ballQuadrantIndex;
        tableOffset += mirrorIdx;
        positionByte = (uint8_t)(0xEF - positions[tableOffset]);
    }

    /* Extract X and Y indices from packed position byte */
    int16_t xIdx = (positionByte >> 4) & 0xF;
    int16_t yIdx = positionByte & 0xF;

    d3 += _playerXQuadrantsCoordinates[xIdx];
    d4 += _playerYQuadrantCoordinates[yIdx];

    /* Team-specific X adjustment */
    d3 -= 4;
    if (team != &_topTeamData)
        d3 += 8; /* net +4 for bottom team */

    /* Clamp to pitch bounds */
    if (d3 < INDOOR_X(81)) d3 = INDOOR_X(81);
    if (d3 > INDOOR_X(590)) d3 = INDOOR_X(590);
    if (d4 < INDOOR_Y(129)) d4 = INDOOR_Y(129);
    if (d4 > INDOOR_Y(769)) d4 = INDOOR_Y(769);

    player->destX = d3;
    player->destY = d4;
}
