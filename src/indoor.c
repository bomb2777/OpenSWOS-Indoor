/* indoor.c -- INDOOR: a friendly played six a side in a hall (include/indoor.h,
 * docs/INDOOR.md). */
/* Size over speed: nothing here is hot (kick-off and two tiny per-frame loops), and at
 * -O2 the m68k compiler unrolled and inlined it to over 5 KB. */
#pragma GCC optimize ("Os")
#include "indoor.h"
#include "swos_types.h"
#include "game_types.h"
#include "menu_system.h"
#include "display.h"            /* AGA, the frontbuffer's geometry (the glass) */
#include "endian_helpers.h"
#include "swos_sections.h"     /* the glass sources in chip RAM */
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

/* The menus play the hall's music while the FRIENDLY screen's INDOOR row shows a floor
 * (SDL: TryPlayMusic, sound_indoor/menu.mp3; the Amiga keeps its menu song). */
int32_t Indoor_MenuMusic(void)
{
    return _gIndoorFriendlyFloor != INDOOR_OFF;
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
    Indoor_BuildMarkSprites();
    Indoor_ClearMarker();
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
    Indoor_ClearMarker();
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

/* A player's side from his position: 0 right (RB RW), 2 left (LB LW), 1 the middle. */
static int16_t position_side(const TeamFileEx *t, uint8_t fileIndex)
{
    int16_t pos = (int16_t)((t->players[fileIndex].positionAndFace & PF_POSITION_MASK) >> PF_POSITION_SHIFT);
    if (pos == 1 || pos == 4) return 0;
    if (pos == 2 || pos == 5) return 2;
    return 1;
}

static int16_t slot_price(const TeamFileEx *t, int16_t slot)
{
    return (int16_t)(uint8_t)t->players[t->team.playerNumbers[slot]].price;
}

/* How much more a forward must be worth to take a midfield place (phase 5.4: the owner,
 * "Davor Suker must start for Croatia -- he could replace an offensive midfielder"; 49 of
 * the 1,711 teams). */
#define FORWARD_FOR_MIDFIELDER 5

static void sort_line_up(TeamFileEx *t)
{
    /* The outfield places of each indoor tactic, ordinals 2-6 (make_indoor_tactics.py's
     * ROLES): 1 defence, 2 midfield, 3 attack; and the side each stands on, from its
     * average x on the tactics grid (0 right, x < 6; 2 left, x > 8; 1 the middle). */
    static const uint8_t kPlaces[INDOOR_TACTICS][INDOOR_PLAYERS - 1] = {
        { 1, 1, 2, 2, 3 },      /* 2-2-1 */
        { 1, 1, 2, 3, 3 },      /* 2-1-2 */
        { 1, 2, 2, 3, 3 },      /* 1-2-2 */
        { 1, 1, 1, 2, 3 },      /* 3-1-1 */
        { 1, 2, 2, 2, 3 },      /* 1-3-1 */
    };
    static const uint8_t kSides[INDOOR_TACTICS][INDOOR_PLAYERS - 1] = {
        { 0, 2, 0, 2, 1 },
        { 0, 2, 1, 0, 2 },
        { 1, 0, 2, 0, 2 },
        { 0, 1, 2, 1, 1 },
        { 1, 0, 1, 2, 1 },
    };
    uint8_t order[16], used[16];
    int16_t place[INDOOR_PLAYERS];      /* the slot (in the team's order) on each place */
    int16_t n = 0, i, j, k, g;
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
        place[k] = pick;
    }
    /* A forward on the bench clearly better than a midfielder takes his place. */
    for (k = 1; k < INDOOR_PLAYERS; k++) {
        int16_t fw;
        if (kPlaces[tactic][k - 1] != 2 || (fw = best_left(t, used, 3, 0)) < 0)
            continue;
        if (slot_price(t, fw) >= slot_price(t, place[k]) + FORWARD_FOR_MIDFIELDER) {
            used[place[k]] = 0;
            used[fw] = 1;
            place[k] = fw;
        }
    }
    /* Each line's players onto its places by side (the owner: a left winger played on the
     * right): its places and its players both put in side order, right to left, and paired
     * off; players of one side keep their order by value. */
    for (g = 1; g <= 3; g++) {
        int16_t pl[INDOOR_PLAYERS], ps[INDOOR_PLAYERS], np = 0, tmp;
        for (k = 1; k < INDOOR_PLAYERS; k++)
            if (kPlaces[tactic][k - 1] == g)
                pl[np++] = k;
        for (j = 0; j < np; j++)
            ps[j] = place[pl[j]];
        for (i = 1; i < np; i++)            /* (insertion sorts of two or three) */
            for (j = i; j > 0 && kSides[tactic][pl[j - 1] - 1] > kSides[tactic][pl[j] - 1]; j--) {
                tmp = pl[j]; pl[j] = pl[j - 1]; pl[j - 1] = tmp;
            }
        for (i = 1; i < np; i++)
            for (j = i; j > 0 && position_side(t, t->team.playerNumbers[ps[j - 1]]) >
                                 position_side(t, t->team.playerNumbers[ps[j]]); j--) {
                tmp = ps[j]; ps[j] = ps[j - 1]; ps[j - 1] = tmp;
            }
        for (j = 0; j < np; j++)
            place[pl[j]] = ps[j];
    }
    for (k = 0; k < INDOOR_PLAYERS; k++)
        order[n++] = t->team.playerNumbers[place[k]];
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
#ifdef SWOS_SDL_BUILD
    fprintf(stderr, "[indoor] %.17s line-up (tactic %d):", (const char *)t->team.teamName, tactic);
    for (k = 0; k < INDOOR_PLAYERS; k++)
        fprintf(stderr, "%s %.23s", k ? "," : "", (const char *)t->players[order[k]].playerName);
    fprintf(stderr, "\n");
#endif
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

/* The outdoor player whose place an indoor one takes at a restart (kick-off, free kick,
 * penalty): 2 and 3 the full backs 2 and 5 (phase 5.4; the centre backs 3 and 4 until then
 * stood beside the keeper), 4 the middle, 5 and 6 the forwards who take the kick-off. */
int16_t Indoor_RestartOrdinal(int16_t ordinal)
{
    static const int16_t kRole[INDOOR_PLAYERS + 1] = { 0, 1, 2, 5, 8, 10, 11 };
    if (!_gIndoor || ordinal < 1 || ordinal > INDOOR_PLAYERS)
        return ordinal;
    return kRole[ordinal];
}

static int32_t isqrt32(int32_t v)
{
    int32_t r = 0, bit = 1L << 30;
    while (bit > v)
        bit >>= 2;
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

#define WALL_GAP 5                      /* half the space between the two, as outdoors */

__attribute__((noinline)) void Indoor_WallSpot(int16_t ballX, int16_t ballY, int16_t goalY, int16_t slot,
                                               int16_t inWall, int16_t *x, int16_t *y)
{
    const int32_t vx = 336 - ballX, vy = goalY - ballY;
    const int32_t d = INDOOR_FREE_KICK_DISTANCE + 2;    /* (outside the distance kept) */
    int32_t len = isqrt32(vx * vx + vy * vy), side;
    if (len < 1)
        len = 1;
    side = inWall < 2 ? 0 : (slot ? WALL_GAP : -WALL_GAP);
    *x = (int16_t)(ballX + (vx * d - vy * side) / len);
    *y = (int16_t)(ballY + (vy * d + vx * side) / len);
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

#define SPR_SIDE_PITCH 225              /* the line-up screen's little pitch */
extern uint8_t *_spriteGraphicsPtr;

static int16_t s_sidePitchBlue;         /* sprite 225 is the hall's (its court mixed, no arcs) */

/* The markings a hall does not have (phase 5.4): the four corner arcs and the two arcs on
 * the penalty areas. Spans (y, x0, x1) of the top-left quarter of the sprite; the others
 * are its mirrors (x -> 131 - x, y -> 114 - y). A pixel is cleared only when it has the
 * lines' colour, so other art (the DOS sprites) loses nothing else. */
static const uint8_t kArcSpans[][3] = {
    { 2, 5, 5 }, { 3, 5, 5 }, { 4, 4, 4 }, { 5, 2, 3 },
    { 20, 53, 53 }, { 21, 54, 54 }, { 22, 55, 55 }, { 23, 56, 56 },
    { 24, 57, 58 }, { 25, 59, 61 }, { 26, 62, 65 },
};
#define ARC_PIXELS (18 * 4)
static uint8_t s_arcSaved[ARC_PIXELS];

static uint8_t *side_pixel_at(SpriteGraphics *spr, int16_t x, int16_t y, uint16_t *bit, int16_t *planes)
{
    uint16_t wq = (uint16_t)spr->wquads;
#ifdef SWOS_SDL_BUILD
    *bit = 0; *planes = 0;
    return (uint8_t *)spr->sprData + (int32_t)y * wq * 16 + x;
#else
    *planes = (int16_t)spriteNumPlanes(SPR_SIDE_PITCH);
    *bit = (uint16_t)(0x8000u >> (x & 15));
    return (uint8_t *)spr->sprData + (int32_t)y * wq * 2 * *planes + (x >> 4) * 2;
#endif
}

static uint8_t side_pixel(SpriteGraphics *spr, int16_t x, int16_t y)
{
    uint16_t bit; int16_t planes;
    uint8_t *p = side_pixel_at(spr, x, y, &bit, &planes);
#ifdef SWOS_SDL_BUILD
    return *p;
#else
    uint8_t c = 0;
    for (int16_t pl = 0; pl < planes; pl++)
        if (*(uint16_t *)(p + pl * spr->wquads * 2) & bit)
            c |= (uint8_t)(1 << pl);
    return c;
#endif
}

static void set_side_pixel(SpriteGraphics *spr, int16_t x, int16_t y, uint8_t c)
{
    uint16_t bit; int16_t planes;
    uint8_t *p = side_pixel_at(spr, x, y, &bit, &planes);
#ifdef SWOS_SDL_BUILD
    *p = c;
#else
    for (int16_t pl = 0; pl < planes; pl++) {
        uint16_t *w = (uint16_t *)(p + pl * spr->wquads * 2);
        *w = (c & (1 << pl)) ? (uint16_t)(*w | bit) : (uint16_t)(*w & ~bit);
    }
#endif
}

/* hide: clear the arcs (saving what was there); else put it back. */
static void side_pitch_arcs(int16_t hide)
{
    SpriteGraphics *spr = ((SpriteGraphics **)_spriteGraphicsPtr)[SPR_SIDE_PITCH];
    if (!spr || !spr->sprData || spr->wquads * 16 < 132 || spr->nlines < 115)
        return;
    const uint8_t line = side_pixel(spr, 1, 57), court = side_pixel(spr, 20, 40);
    int16_t n = 0;
    for (uint16_t s = 0; s < sizeof(kArcSpans) / sizeof(kArcSpans[0]); s++)
        for (int16_t x0 = kArcSpans[s][1]; x0 <= kArcSpans[s][2]; x0++)
            for (int16_t m = 0; m < 4; m++) {
                const int16_t x = (m & 1) ? (int16_t)(131 - x0) : x0;
                const int16_t y = (m & 2) ? (int16_t)(114 - kArcSpans[s][0]) : kArcSpans[s][0];
                if (hide) {
                    s_arcSaved[n] = side_pixel(spr, x, y);
                    if (s_arcSaved[n] == line)
                        set_side_pixel(spr, x, y, court);
                } else {
                    set_side_pixel(spr, x, y, s_arcSaved[n]);
                }
                n++;
            }
}

/* The court's teal on the little pitch: the menus have 16 colours and no teal, so every
 * other pixel of its grass (14, green) becomes the blue (11) -- a checkerboard of the two,
 * which reads as teal at that size (the owner's idea). Which pixels changed is kept, so
 * outdoors gets exactly its grass back. */
#define SIDE_W 132
#define SIDE_H 115
static uint8_t s_sideMixed[(SIDE_W * SIDE_H + 7) / 8];

static void side_pitch_mix(int16_t on)
{
    SpriteGraphics *spr = ((SpriteGraphics **)_spriteGraphicsPtr)[SPR_SIDE_PITCH];
    if (!spr || !spr->sprData || spr->wquads * 16 < SIDE_W || spr->nlines < SIDE_H)
        return;
    for (int16_t y = 0; y < SIDE_H; y++)
        for (int16_t x = 0; x < SIDE_W; x++) {
            const int32_t i = (int32_t)y * SIDE_W + x;
            if (on) {
                const uint8_t mixed = (uint8_t)(((x + y) & 1) && side_pixel(spr, x, y) == 14);
                s_sideMixed[i >> 3] = (uint8_t)((s_sideMixed[i >> 3] & ~(1u << (i & 7))) | (mixed << (i & 7)));
                if (mixed)
                    set_side_pixel(spr, x, y, 11);
            } else if (s_sideMixed[i >> 3] & (1u << (i & 7))) {
                set_side_pixel(spr, x, y, 14);
            }
        }
}

void Indoor_SidePitchColours(void)
{
    if (s_sidePitchBlue == (_gIndoor != 0))
        return;
    if (_gIndoor) {
        side_pitch_arcs(1);             /* (the arcs off while the grass is still one colour) */
        side_pitch_mix(1);
    } else {
        side_pitch_mix(0);
        side_pitch_arcs(0);
    }
    s_sidePitchBlue = (_gIndoor != 0);
}

/* The court's colours (2026-11-04: teal, the owner -- the blue hid blue kits). LINO.MAP draws
 * the court in palette entry 75 and its penalty areas and centre circle in 76 (blue there,
 * 0x22C and 0x66E); in an indoor match they are teal instead. Nothing else in an indoor
 * match draws in them: the players, the ball, the referee and the HUD are 16-colour sprites
 * (0-15), and the crowd tiles taken from the player's pitch use 0-15 too (measured in the
 * Amiga, PC and 2020 styles). Set with the pitch type's colours (ApplyPitchPaletteAndFactors,
 * live and in the replay viewer), given back outdoors; the PC styles that build their own
 * palette each frame get them too (Indoor_CourtPaletteRGB). */
/* The PC's glass sees the floor through a colour table (nearest entry to 60 % grey over each
 * colour, glass_table): the palette has no grey teal, so the court behind the bottom wall
 * came out plain grey. Two entries no indoor match draws in (77, 82: not in the hall map,
 * the crowd or a sprite; not 16-31, the Amiga's sprite colours) hold the court's and the
 * areas' teal through the glass, and the table finds them. The PC only: the Amiga's glass
 * is a checkerboard, the floor's own colour between. */
#ifdef SWOS_SDL_BUILD
#define COURT_ENTRIES 4
#else
#define COURT_ENTRIES 2
#endif
static const uint8_t  kCourtEntry[4] = { 75, 76, 77, 82 };
static const uint16_t kCourtTeal[4]  = { 0x188,     /* the court, #118888 */
                                         0x3AA,     /* its areas, #33AAAA */
                                         0x477,     /* the court through the glass */
                                         0x588 };   /* the areas through it */
static uint16_t s_courtSaved[4];
static int16_t  s_courtTeal;

void Indoor_CourtPalette(uint16_t *pal)
{
    if (_gIndoor) {
        for (int16_t k = 0; k < COURT_ENTRIES; k++) {
            if (!s_courtTeal)
                s_courtSaved[k] = pal[kCourtEntry[k]];
            pal[kCourtEntry[k]] = kCourtTeal[k];
        }
        s_courtTeal = 1;
    } else if (s_courtTeal) {
        for (int16_t k = 0; k < COURT_ENTRIES; k++)
            pal[kCourtEntry[k]] = s_courtSaved[k];
        s_courtTeal = 0;
    }
}

#ifdef SWOS_SDL_BUILD
void Indoor_CourtPaletteRGB(uint8_t *rgb)
{
    const uint16_t *c = kCourtTeal;
    const uint8_t *e = kCourtEntry;
    if (!_gIndoor)
        return;
    for (int16_t k = 0; k < COURT_ENTRIES; k++) {
        rgb[e[k] * 3 + 0] = (uint8_t)(((c[k] >> 8) & 15) * 17);
        rgb[e[k] * 3 + 1] = (uint8_t)(((c[k] >> 4) & 15) * 17);
        rgb[e[k] * 3 + 2] = (uint8_t)((c[k] & 15) * 17);
    }
}
#endif

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

static void mark_glass(Sprite *ball, int16_t side);

int16_t Indoor_BallAtWall(Sprite *ball, int32_t savedX, int32_t savedY)
{
    int16_t bx = ball->x.s.i;
    int16_t by = ball->y.s.i;
    int16_t hit = 0, side = 0;

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
        side = 1;
    }
    if (hit) {
        mark_glass(ball, side);         /* (before its speed is cut) */
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

/* ---- The glass at the ends, drawn over each frame (phase 5.3) ------------------------ */

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

/* The table from the palette on screen, rebuilt when it changes (the pitch condition's
 * colours): 60 % of the glass's grey over each colour, as LINO.MAP's glass. Never during a
 * fade (fade.c g_fadeLevel below FADE_MAX): a fade scales every colour alike, so the table
 * of the bright palette stays right, while one built from a dark palette sends every colour
 * to the wrong one -- after a goal's replay the bottom wall came out white for a frame.
 * (The PC keeps this clear glass; the Amiga has a checkerboard -- the owner, 5.4.) */
static void glass_table(void)
{
    extern int g_fadeLevel;
    uint8_t rgb[768];
    uint32_t h = 2166136261u;
    if (g_fadeLevel < 0x80 && s_glassPalHash != 0)
        return;
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

static int16_t marker_opaque(int16_t x, int16_t y);

/* World rectangle x0..x1, y0..y1 through the glass, row `rimY` its light rim -- but for
 * the goal's hole, rows h0..h1 of x INDOOR_GLASS_HOLE_X0..X1, seen through clear, and the
 * marker's own pixels (it is drawn before the bottom wall and stays bright). */
static void glass_band(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t h0, int16_t h1,
                       int16_t rimY)
{
    int16_t camX = Render_CameraX(), camY = Render_CameraY();
    uint8_t *fb = (uint8_t *)_frontbufferBasePtr;
    int16_t mx0 = 1, my0 = 1, mx1 = 0, my1 = 0;
    (void)Indoor_MarkerBox(&mx0, &my0, &mx1, &my1);
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
            if (x >= mx0 && x <= mx1 && y >= my0 && y <= my1 &&
                marker_opaque((int16_t)(x - mx0), (int16_t)(y - my0)))
                continue;
            row[sx] = y == rimY ? s_glassRim : s_glassLut[row[sx]];
        }
    }
}

#elif defined(AGA)
/* The Amiga: no colour table a frame could afford -- the glass is a CHECKERBOARD of grey
 * over what is behind it, the classic see-through: every other pixel, fixed to the world
 * (it does not crawl when the camera scrolls), laid by the blitter one bitplane at a time:
 * D = A ? B : C, A the checkerboard (BLTADAT, the edges by its first/last-word masks), B
 * the grey's bit in that plane (BLTBDAT), C/D the frame (minterm 0xCA). Into both copies
 * of a row in the 2x-tall frontbuffer: a bob is undrawn from the other copy, so the glass
 * is in what it restores, and laying the same grey on the same pixels again changes
 * nothing -- nothing builds up. The addressing is the crowd animation's
 * (animated_patterns.c drawPatternFromIndex). */
#include "display.h"
extern volatile uintptr_t _frontbufferBasePtr;
extern uint16_t _cameraXMod320, _cameraYMod320;
extern FixedPoint _cameraX, _cameraY;   /* Render_CameraX/Y read them on the Amiga */

#define GLASS_GREY 52                   /* 0x555 */

#define GLASS_BLT_W(reg, val)                                               \
    do {                                                                    \
        uint16_t _v = (uint16_t)(val);                                      \
        volatile uint16_t *_p = &(reg);                                     \
        __asm__ volatile ("move.w %0,(%1)" : : "d"(_v), "a"(_p) : "memory");\
    } while (0)
#define GLASS_BLT_L(reg, val)                                               \
    do {                                                                    \
        uint32_t _v = (uint32_t)(val);                                      \
        volatile uint32_t *_p = &(reg);                                     \
        __asm__ volatile ("move.l %0,(%1)" : : "d"(_v), "a"(_p) : "memory");\
    } while (0)

static void glass_table(void)
{
}

static void glass_wait_blit(void)
{
    _custom->dmacon = (uint16_t)DMACON_BLT_ON;
    (void)(_custom->dmaconr & (1u << 14));
    while (BLIT_BUSY())
        ;
    _custom->dmacon = (uint16_t)DMACON_BLT_OFF;
}

/* ONE blit for a box, all eight planes at once: the frontbuffer's planes are interleaved
 * (a row is eight 48-byte plane lines), so the box is `rows * 8` lines of PLANE_STRIDE. A
 * reads the checkerboard per line from s_glassMask (a row's eight lines share its parity's
 * pattern), B the grey's bit per line from s_glassColour (line p of a row: plane p), C/D the
 * frame: D = A ? B : C (minterm 0xCA). Only the copy of the rows the display shows: a bob is
 * undrawn from the other one, which holds the floor -- with its glass, from the map. Both
 * sources are in chip RAM (the blitter reads nothing else). Each line of them is
 * GLASS_MAX_WORDS wide; the blit takes `words` of it. */
#ifndef INDOOR_GLASS_SPRITES
#define INDOOR_GLASS_SPRITES 1              /* 0: the per-frame glass off (a test) */
#endif
#define GLASS_MAX_WORDS 4                   /* a box up to 49 px wide */
#define GLASS_MAX_ROWS  40
static uint16_t s_glassMask[(GLASS_MAX_ROWS + 1) * 8 * GLASS_MAX_WORDS] SWOS_CHIP_BSS;
static uint16_t s_glassColour[GLASS_MAX_ROWS * 8 * GLASS_MAX_WORDS] SWOS_CHIP_BSS;
static int16_t  s_glassSources;

static void glass_sources(void)
{
    if (s_glassSources)
        return;
    for (int16_t line = 0; line < (GLASS_MAX_ROWS + 1) * 8; line++)
        for (int16_t w = 0; w < GLASS_MAX_WORDS; w++) {
            int16_t row = (int16_t)(line >> 3), pl = (int16_t)(line & 7);
            s_glassMask[line * GLASS_MAX_WORDS + w] = (uint16_t)((row & 1) ? 0x5555 : 0xAAAA);
            if (row < GLASS_MAX_ROWS)
                s_glassColour[line * GLASS_MAX_WORDS + w] =
                    (uint16_t)(((GLASS_GREY >> pl) & 1) ? 0xFFFF : 0);
        }
    s_glassSources = 1;
}

/* World x0..x1 of rows y0..y1 (no hole), clipped to the screen: one blit. */
static void glass_rect(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    int16_t camX = Render_CameraX(), camY = Render_CameraY();
    int16_t sx0 = (int16_t)(x0 - camX), sx1 = (int16_t)(x1 - camX);
    int16_t sy0 = (int16_t)(y0 - camY), sy1 = (int16_t)(y1 - camY);
    int16_t bx0, bx1, w0, words, rows;
    uint16_t fwm, lwm;
    uint32_t dst, a, b;
    if (sx0 < 0) sx0 = 0;
    if (sx1 > SCREEN_WIDTH - 1) sx1 = SCREEN_WIDTH - 1;
    if (sy0 < 0) sy0 = 0;
    if (sy1 > SCREEN_HEIGHT - 1) sy1 = SCREEN_HEIGHT - 1;
    if (sx0 > sx1 || sy0 > sy1)
        return;
    rows = (int16_t)(sy1 - sy0 + 1);
    if (rows > GLASS_MAX_ROWS)
        rows = GLASS_MAX_ROWS;
    bx0 = (int16_t)(sx0 + (int16_t)_cameraXMod320);
    bx1 = (int16_t)(sx1 + (int16_t)_cameraXMod320);
    w0 = (int16_t)(bx0 >> 4);
    words = (int16_t)((bx1 >> 4) - w0 + 1);
    if (words > GLASS_MAX_WORDS)
        words = GLASS_MAX_WORDS;
    fwm = (uint16_t)(0xFFFFu >> (bx0 & 15));
    lwm = (uint16_t)(0xFFFFu << (15 - (bx1 & 15)));
    glass_sources();
    dst = (uint32_t)(_frontbufferBasePtr + (uint32_t)(sy0 + (int16_t)_cameraYMod320) * ROW_STRIDE_BYTES +
                     SCROLL_LEFT_MARGIN_BYTES + w0 * 2);
    /* the world row's parity picks where in the mask the first line starts */
    a = (uint32_t)(uintptr_t)&s_glassMask[(((sy0 + camY) & 1) * 8) * GLASS_MAX_WORDS];
    b = (uint32_t)(uintptr_t)&s_glassColour[0];
    glass_wait_blit();
    GLASS_BLT_W(_custom->bltcon0, 0x0FCA);      /* use A, B, C, D; D = A ? B : C */
    GLASS_BLT_W(_custom->bltcon1, 0);
    GLASS_BLT_W(_custom->bltafwm, fwm);
    GLASS_BLT_W(_custom->bltalwm, lwm);
    GLASS_BLT_W(_custom->bltamod, (GLASS_MAX_WORDS - words) * 2);
    GLASS_BLT_W(_custom->bltbmod, (GLASS_MAX_WORDS - words) * 2);
    GLASS_BLT_W(_custom->bltcmod, PLANE_STRIDE - words * 2);
    GLASS_BLT_W(_custom->bltdmod, PLANE_STRIDE - words * 2);
    GLASS_BLT_L(_custom->bltapt, a);
    GLASS_BLT_L(_custom->bltbpt, b);
    GLASS_BLT_L(_custom->bltcpt, dst);
    GLASS_BLT_L(_custom->bltdpt, dst);
    GLASS_BLT_W(_custom->bltsize, (uint16_t)(((rows * 8) << 6) | words));
}

/* World x0..x1 of rows y0..y1 through the glass, but for the goal's hole (rows h0..h1 of
 * x INDOOR_GLASS_HOLE_X0..X1): the box split around it, each part clipped. */
static void glass_box(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t h0, int16_t h1)
{
    int16_t a = h0 > y0 ? h0 : y0, b = h1 < y1 ? h1 : y1;
    if (x0 > x1 || y0 > y1)
        return;
    if (a > b) {                        /* clear of the hole's rows */
        glass_rect(x0, y0, x1, y1);
        return;
    }
    if (y0 < a)
        glass_rect(x0, y0, x1, (int16_t)(a - 1));
    if (b < y1)
        glass_rect(x0, (int16_t)(b + 1), x1, y1);
    if (x0 < INDOOR_GLASS_HOLE_X0)
        glass_rect(x0, a, (int16_t)(x1 < INDOOR_GLASS_HOLE_X0 - 1 ? x1 : INDOOR_GLASS_HOLE_X0 - 1), b);
    if (x1 > INDOOR_GLASS_HOLE_X1)
        glass_rect((int16_t)(x0 > INDOOR_GLASS_HOLE_X1 + 1 ? x0 : INDOOR_GLASS_HOLE_X1 + 1), a, x1, b);
}

extern Sprite _ballSprite;
extern Sprite _refereeSprite;

/* A sprite's box -- left/right of its x, up/down of its foot -- through the bottom wall,
 * where it is inside it. */
static void glass_sprite(int16_t x, int16_t y, int16_t left, int16_t right, int16_t up, int16_t down)
{
    int16_t x0 = (int16_t)(x - left), x1 = (int16_t)(x + right);
    int16_t y0 = (int16_t)(y - up), y1 = (int16_t)(y + down);
    if (y0 < INDOOR_BOTTOM - INDOOR_GLASS_HEIGHT) y0 = INDOOR_BOTTOM - INDOOR_GLASS_HEIGHT;
    if (y1 > INDOOR_BOTTOM) y1 = INDOOR_BOTTOM;
    if (x0 < INDOOR_GLASS_X0) x0 = INDOOR_GLASS_X0;
    if (x1 > INDOOR_GLASS_X1) x1 = INDOOR_GLASS_X1;
    glass_box(x0, y0, x1, y1, (int16_t)(INDOOR_BOTTOM - INDOOR_GLASS_HOLE_H_BOTTOM), INDOOR_BOTTOM);
}

/* The Amiga's glass is in its map (LINO_A.MAP: the floor and court behind it a checkerboard,
 * make_indoor_hall.py --amiga) and in the greyed fans below (Indoor_GlassCrowdTiles): nothing
 * to lay a frame -- the whole wall by the blitter was more than a frame's chip bus. What a
 * map cannot hold are the bobs behind the bottom wall: those boxes, a few small blits. */
void Indoor_DrawGlassBehind(void)
{
}

void Indoor_DrawGlassFront(void)
{
    if (!_gIndoor || !INDOOR_GLASS_SPRITES)
        return;
    for (int16_t t = 0; t < 2; t++) {
        Sprite **tbl = t ? _team2SpritesTable : _team1SpritesTable;
        for (int16_t i = 0; i < INDOOR_PLAYERS; i++)
            glass_sprite(tbl[i]->x.s.i, tbl[i]->y.s.i, 10, 12, 26, 5);
    }
    glass_sprite(_refereeSprite.x.s.i, _refereeSprite.y.s.i, 10, 12, 26, 5);   /* as the PC greys him */
    glass_sprite(_ballSprite.x.s.i, (int16_t)(_ballSprite.y.s.i - _ballSprite.z.s.i), 5, 7, 8, 7);
}

void Indoor_GlassReplaySprite(uint16_t sprIdx, int16_t worldX, int16_t worldY)
{
    const SpriteGraphics *g;
    if (!_gIndoor || !INDOOR_GLASS_SPRITES)
        return;
    g = ((SpriteGraphics **)_spriteGraphicsPtr)[sprIdx];
    if (!g || worldY + g->nlines <= INDOOR_BOTTOM - INDOOR_GLASS_HEIGHT || worldY > INDOOR_BOTTOM)
        return;
    glass_sprite(worldX, worldY, 0, (int16_t)(g->pixWidth - 1), 0, (int16_t)(g->nlines - 1));
}

/* The fans behind the top wall, greyed: tiles 27-52 of LINO_A.MAP are 1-26 (the crowd, kit
 * coloured by now) under the checkerboard -- (x + y) even, as in the map: in a tile's row r
 * the pixels of the parity of r. 8-plane tiles, a row of eight plane words. Once, after
 * FixupAndColorPitchFile has coloured the crowd. */
void Indoor_GlassCrowdTiles(uint8_t *tiles)
{
    if (!_gIndoor)
        return;
    for (int16_t t = 1; t <= INDOOR_CROWD_TILES; t++) {
        const uint8_t *src = tiles + (uint32_t)t * 256;
        uint8_t *dst = tiles + (uint32_t)(t + INDOOR_CROWD_TILES) * 256;
        for (int16_t r = 0; r < 16; r++) {
            uint16_t mask = (uint16_t)((r & 1) ? 0x5555 : 0xAAAA);
            for (int16_t pl = 0; pl < 8; pl++) {
                uint16_t w = BE_READ_U16(src + r * 16 + pl * 2);
                w = (uint16_t)((w & (uint16_t)~mask) | (((GLASS_GREY >> pl) & 1) ? mask : 0));
                BE_WRITE_U16(dst + r * 16 + pl * 2, w);
            }
        }
    }
}
#endif

#ifdef SWOS_SDL_BUILD
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
void Indoor_GlassReplaySprite(uint16_t sprIdx, int16_t worldX, int16_t worldY)
{
    (void)sprIdx; (void)worldX; (void)worldY;
}

void Indoor_DrawGlassFront(void)
{
    if (!_gIndoor)
        return;
    glass_table();
    glass_band(INDOOR_GLASS_X0, (int16_t)(INDOOR_BOTTOM - INDOOR_GLASS_HEIGHT), INDOOR_GLASS_X1, INDOOR_BOTTOM,
               (int16_t)(INDOOR_BOTTOM - INDOOR_GLASS_HOLE_H_BOTTOM), INDOOR_BOTTOM, INDOOR_BOTTOM);
}
#elif !defined(AGA)
void Indoor_DrawGlassBehind(void)
{
}

void Indoor_GlassReplaySprite(uint16_t sprIdx, int16_t worldX, int16_t worldY)
{
    (void)sprIdx; (void)worldX; (void)worldY;
}

void Indoor_DrawGlassFront(void)
{
}
#endif


/* ---- The marker on the glass and the woodwork (phase 5.5) ----------------------------- *
 * The owner's SWIS marks where the ball hits the glass and blinks a post or the bar red;
 * here as there (its WallImpact, at a third of its size: its ball is 16 px, ours 5): every
 * touch draws a red ring that grows from 2 to 10 px and fades, over a white cross with a
 * red centre that stays for two thirds of the second and then fades -- at an end where the
 * ball is SEEN, its height above the floor too (the ends' glass stands 80 px high in the
 * picture), at a side on the white side line -- and a hard hit (INDOOR_MARK_SPEED) thuds
 * with the ball's bounce sample; a post or the bar blinks red three times. (Until 2026-11-04 the struck piece of glass lit up as a checkerboard: "looks odd".) One marker, the latest hit: a world sprite of its own pictures (sprite table
 * slots INDOOR_MARK_SPRITE_FIRST..), shown by the corner flag's sprite, which a hall has
 * no use for (indoors it is the marker or hidden). So it is drawn, undrawn on the Amiga
 * and recorded in a replay like any sprite; it is not match state but the corner flag's
 * place and picture are (sim_state.c), indoors only. */
#include "sfx_crowd.h"

extern Sprite _cornerFlagSprite;
#ifdef SWOS_SDL_BUILD
extern Sprite _cornerFlagSprite2;
#endif
extern void PlaySfxSample16(void);

enum { MARK_GLASS = 0, MARK_SPRITES = 6 };
#define MARK_GLASS_STEPS    6           /* the ring's six sizes, a picture each */
#define MARK_GLASS_FRAMES   48          /* six steps of 8: about a second, as SWIS's */
#define MARK_RING_MAX       10          /* the ring's last radius */
#define MARK_MAX_Z          (INDOOR_GLASS_HEIGHT - MARK_RING_MAX)   /* on the ends' glass */

static const struct { uint8_t w, h, cx, cy; } kMarkSize[MARK_SPRITES] = {
    { 21, 21, 10, 10 }, { 21, 21, 10, 10 }, { 21, 21, 10, 10 },  /* the ring and the cross */
    { 21, 21, 10, 10 }, { 21, 21, 10, 10 }, { 21, 21, 10, 10 },
};
static const uint8_t kRingRadius[MARK_GLASS_STEPS] = { 2, 3, 5, 6, 8, 10 };
#define MARK_MAX_LINES  21
#define MARK_MAX_WQ     2

static SpriteGraphics s_markHdr[MARK_SPRITES];
#ifdef SWOS_SDL_BUILD
static uint8_t s_markPix[MARK_SPRITES][MARK_MAX_LINES * MARK_MAX_WQ * 16];
#else
/* 4 colour planes and a mask of as many rows again (a linked sprite's), in chip RAM. */
SWOS_CHIP_BSS static uint16_t s_markPix[MARK_SPRITES][MARK_MAX_LINES * MARK_MAX_WQ * 4 * 2];
#endif
static int16_t s_markBuilt;
static int16_t s_markKind = -1, s_markTimer, s_markX, s_markY, s_markZ;

/* A marker picture's colour at (x, y): 0 clear, 2 white, 1 grey, 10 red. SWIS fades with
 * alpha; here a fading line keeps every other pixel, then every third. */
static uint8_t mark_colour(int16_t k, int16_t x, int16_t y)
{
    int16_t dx, dy, d2, r, n;
    dx = (int16_t)(x - kMarkSize[k].cx);
    dy = (int16_t)(y - kMarkSize[k].cy);
    n = (int16_t)(dx < 0 ? -dx : dx) + (int16_t)(dy < 0 ? -dy : dy);   /* (for the fading) */
    if (dx == 0 && dy == 0)
        return 10;                                      /* the red centre */
    if ((dx == 0 || dy == 0) && n <= 3) {               /* the cross, 3 px each way */
        if (k <= MARK_GLASS + 3)
            return 2;                                   /* white, two thirds of the time */
        if (k == MARK_GLASS + 4)
            return 1;                                   /* then grey */
        return (n & 1) ? 1 : 0;                         /* and half of it */
    }
    r = kRingRadius[k - MARK_GLASS];
    d2 = (int16_t)(dx * dx + dy * dy);
    if (d2 < r * r - r || d2 > r * r + r)               /* the ring, one pixel wide */
        return 0;
    if (k <= MARK_GLASS + 1)
        return 10;                                      /* red */
    if (k <= MARK_GLASS + 3)
        return ((x + y) & 1) ? 0 : 10;                  /* fading */
    return ((x + y) % 3) ? 0 : 10;                      /* nearly gone */
}

/* The marker's pictures into their slots: once, and again if the table lost them. */
void Indoor_BuildMarkSprites(void)
{
    SpriteGraphics **table = (SpriteGraphics **)_spriteGraphicsPtr;
    if (!table)
        return;
    if (s_markBuilt && table[INDOOR_MARK_SPRITE_FIRST] == &s_markHdr[0])
        return;
    for (int16_t k = 0; k < MARK_SPRITES; k++) {
        SpriteGraphics *h = &s_markHdr[k];
        const int16_t w = kMarkSize[k].w, ht = kMarkSize[k].h, wq = (int16_t)((w + 15) / 16);
#ifdef SWOS_SDL_BUILD
        uint8_t *px = s_markPix[k];
        for (int16_t y = 0; y < ht; y++)
            for (int16_t x = 0; x < wq * 16; x++)
                px[y * wq * 16 + x] = x < w ? mark_colour(k, x, y) : 0;
        h->sprData = px;
        h->maskPtr = 0;
        h->type = 0;
#else
        /* As the game's own: per line its 4 planes' words, then the mask, a linked
         * sprite's (the corner flag's type), its row for each plane. */
        const SpriteGraphics *like = table[1070];          /* SPR_CORNER_FLAG_FRAME_0 */
        const uint16_t sp = 4, reps = (like && like->type) ? sp : 1;
        uint16_t *data = s_markPix[k], *mask = data + (uint32_t)ht * sp * wq;
        for (int16_t y = 0; y < ht; y++) {
            for (int16_t q = 0; q < wq; q++) {
                uint16_t pl[4] = { 0, 0, 0, 0 }, m = 0;
                for (int16_t b = 0; b < 16; b++) {
                    const int16_t x = (int16_t)(q * 16 + b);
                    const uint8_t c = x < w ? mark_colour(k, x, y) : 0;
                    if (!c)
                        continue;
                    m |= (uint16_t)(0x8000u >> b);
                    for (int16_t p = 0; p < 4; p++)
                        if (c & (1 << p))
                            pl[p] |= (uint16_t)(0x8000u >> b);
                }
                for (int16_t p = 0; p < 4; p++)
                    data[(uint32_t)y * sp * wq + p * wq + q] = pl[p];
                for (uint16_t r = 0; r < reps; r++)
                    mask[(uint32_t)y * reps * wq + r * wq + q] = m;
            }
        }
        if (like)
            *h = *like;
        h->sprData = (uint8_t *)data;
        h->maskPtr = (uint8_t *)mask;
#endif
        h->pixWidth = w;
        h->nlines = ht;
        h->wquads = wq;
        h->centerX = kMarkSize[k].cx;
        h->centerY = kMarkSize[k].cy;
        h->bltsize = (uint16_t)((ht << 6) + wq + wq);
        h->ordinal = (int16_t)(INDOOR_MARK_SPRITE_FIRST + k);
        table[INDOOR_MARK_SPRITE_FIRST + k] = h;
    }
    s_markBuilt = 1;
}

static void mark(int16_t kind, int16_t x, int16_t y, int16_t z, int16_t frames)
{
    Indoor_BuildMarkSprites();
    s_markKind = kind;
    s_markX = x;
    s_markY = y;
    s_markZ = z;
    s_markTimer = frames;
}

/* The ball off the glass (Indoor_BallAtWall), every touch: at a side (x past the court) or
 * an end. At an end the mark is where the ball is seen: on the line, as high as the ball
 * (a sprite's z lifts it, as the ball's lifts the ball) up the glass that stands there, no
 * higher than its top. At a side, whose glass lies in the floor, it is on the floor, on the
 * white side line where the glass begins (the owner). Only a hard hit (INDOOR_MARK_SPEED)
 * thuds. */
static void mark_glass(Sprite *ball, int16_t side)
{
    int16_t z = ball->z.s.i;
    if (z < 0) z = 0;
    if (z > MARK_MAX_Z) z = MARK_MAX_Z;
    if (side) {
        int16_t y = ball->y.s.i;
        if (y < INDOOR_TOP) y = INDOOR_TOP;
        if (y > INDOOR_BOTTOM) y = INDOOR_BOTTOM;
        mark(MARK_GLASS, ball->x.s.i < 336 ? (int16_t)INDOOR_LEFT : (int16_t)INDOOR_RIGHT, y, 0,
             MARK_GLASS_FRAMES);
    } else {
        int16_t x = ball->x.s.i;
        if (x < INDOOR_GLASS_X0 + MARK_RING_MAX) x = INDOOR_GLASS_X0 + MARK_RING_MAX;
        if (x > INDOOR_GLASS_X1 - MARK_RING_MAX) x = INDOOR_GLASS_X1 - MARK_RING_MAX;
        mark(MARK_GLASS, x, ball->y.s.i <= 449 ? INDOOR_TOP : INDOOR_BOTTOM, z, MARK_GLASS_FRAMES);
    }
    if (ball->speed >= INDOOR_MARK_SPEED)
        PlaySfxSample16();              /* the thud: the ball's bounce */
}

/* At kick-off and the final whistle: no marker, and the corner flag back on the floor (its
 * z is 0 outdoors, which never sets it). */
void Indoor_ClearMarker(void)
{
    s_markTimer = 0;
    s_markKind = -1;
    _cornerFlagSprite.z.raw = 0;
}

/* Each frame, for the corner flag (UpdateCornerFlagSprite): the marker, or nothing. */
void Indoor_UpdateMarker(void)
{
    Sprite *spr = &_cornerFlagSprite;
    int16_t image = -1;
#ifdef SWOS_SDL_BUILD
    _cornerFlagSprite2.imageIndex = -1;
#endif
    if (s_markTimer > 0) {
        const int16_t t = --s_markTimer;
        image = (int16_t)(INDOOR_MARK_SPRITE_FIRST + s_markKind + (MARK_GLASS_FRAMES - 1 - t) / 8);
        spr->x.s.i = s_markX;
        spr->y.s.i = s_markY;
        spr->z.raw = (int32_t)s_markZ << 16;
    }
    spr->imageIndex = image;
}

#ifdef SWOS_SDL_BUILD
/* Whether the marker's picture shown has a colour at (x, y) of its box (Indoor_MarkerBox). */
static int16_t marker_opaque(int16_t x, int16_t y)
{
    const int16_t k = (int16_t)(_cornerFlagSprite.imageIndex - INDOOR_MARK_SPRITE_FIRST);
    if (k < 0 || k >= MARK_SPRITES)
        return 0;
    return s_markPix[k][y * ((kMarkSize[k].w + 15) / 16) * 16 + x] != 0;    /* (chunky, as built) */
}
#endif

/* The marker's box in the world while it shows (the PC's bottom wall leaves its pixels bright). */
int16_t Indoor_MarkerBox(int16_t *x0, int16_t *y0, int16_t *x1, int16_t *y1)
{
    const Sprite *spr = &_cornerFlagSprite;
    int16_t k;
    if (!_gIndoor || s_markTimer <= 0 || spr->imageIndex < INDOOR_MARK_SPRITE_FIRST)
        return 0;
    k = (int16_t)(spr->imageIndex - INDOOR_MARK_SPRITE_FIRST);
    *x0 = (int16_t)(spr->x.s.i - kMarkSize[k].cx);
    *y0 = (int16_t)(spr->y.s.i - spr->z.s.i - kMarkSize[k].cy);
    *x1 = (int16_t)(*x0 + kMarkSize[k].w - 1);
    *y1 = (int16_t)(*y0 + kMarkSize[k].h - 1);
    return 1;
}
