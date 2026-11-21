/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
/* tactics_menu.c — Tactics selection menu callback functions.
 *
 * Chunk 80 (partial): 7 exported functions ported from SWOS2.s lines 86791-86831.
 */

#include "tactics_menu.h"
#include "menu_system.h"
#include "indoor.h"          /* INDOOR: its own USER A-F */

/* ---- ASM externs ---- */
extern uint16_t _chosenTactics;

/* Tactic ID constants */
#define TACTIC_USER_A  12
#define TACTIC_USER_B  13
#define TACTIC_USER_C  14
#define TACTIC_USER_D  15
#define TACTIC_USER_E  16
#define TACTIC_USER_F  17

/* ================================================================
 * User_A_Tactics  (SWOS2.s line 86793)
 * Sets chosenTactics to USER_A and exits menu.
 * ================================================================ */
__attribute__((used, externally_visible))
void User_A_Tactics(void)
{
    _chosenTactics = Indoor_UserTacticSlot(TACTIC_USER_A);    /* (INDOOR: 5-10) */
    SetExitMenuFlag();
}

/* ================================================================
 * User_B_Tactics  (SWOS2.s line 86799)
 * ================================================================ */
__attribute__((used, externally_visible))
void User_B_Tactics(void)
{
    _chosenTactics = Indoor_UserTacticSlot(TACTIC_USER_B);    /* (INDOOR: 5-10) */
    SetExitMenuFlag();
}

/* ================================================================
 * User_C_Tactics  (SWOS2.s line 86805)
 * ================================================================ */
__attribute__((used, externally_visible))
void User_C_Tactics(void)
{
    _chosenTactics = Indoor_UserTacticSlot(TACTIC_USER_C);    /* (INDOOR: 5-10) */
    SetExitMenuFlag();
}

/* ================================================================
 * User_D_Tactics  (SWOS2.s line 86811)
 * ================================================================ */
__attribute__((used, externally_visible))
void User_D_Tactics(void)
{
    _chosenTactics = Indoor_UserTacticSlot(TACTIC_USER_D);    /* (INDOOR: 5-10) */
    SetExitMenuFlag();
}

/* ================================================================
 * User_E_Tactics  (SWOS2.s line 86817)
 * ================================================================ */
__attribute__((used, externally_visible))
void User_E_Tactics(void)
{
    _chosenTactics = Indoor_UserTacticSlot(TACTIC_USER_E);    /* (INDOOR: 5-10) */
    SetExitMenuFlag();
}

/* ================================================================
 * User_F_Tactics  (SWOS2.s line 86823)
 * ================================================================ */
__attribute__((used, externally_visible))
void User_F_Tactics(void)
{
    _chosenTactics = Indoor_UserTacticSlot(TACTIC_USER_F);    /* (INDOOR: 5-10) */
    SetExitMenuFlag();
}

/* ================================================================
 * ExitChooseTacticsMenu  (SWOS2.s line 86829)
 * Sets chosenTactics to -1 (abort) and exits menu.
 * ================================================================ */
__attribute__((used, externally_visible))
void ExitChooseTacticsMenu(void)
{
    _chosenTactics = 0xFFFF;
    SetExitMenuFlag();
}
