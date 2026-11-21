/*
 * netplay_compat.h -- which builds can play each other online.
 *
 * Two games play a match only when NETPLAY_COMPAT is the same (src/p2p_link.c), and
 * the matchmaking server lists a game only to players with the same number
 * (VERSION:OPENSWOS/<n>, mm_client.c; Connections.cpp). Until 2026-10-25 the two had
 * to be the same COMMIT, which turned away most players who had not updated that
 * day, for changes that could never have parted their matches.
 *
 * RAISE IT whenever a change could make two games play a match differently:
 *   - anything the match simulation does (the SIM tier, src/sim_state.c), its
 *     input, clock or random numbers -- the replay and netplay rigs show it;
 *   - the rules a match is played by (team data, tactics, a host switch);
 *   - what the two games say to each other: the link (p2p_link.c), the lobby
 *     (netplay_lobby.c), GGPO-X, or the recorder's stream.
 *
 * scripts/check_netplay_compat.sh catches the first kind: it plays the test
 * recordings and FAILS when the SIM hashes change and this number did not
 * (record: tests/netplay_compat.txt, --update after raising it). It cannot see the
 * other two, nor a match the recordings never reach (a booking, a penalty
 * shoot-out) -- those are on whoever makes the change. A build with uncommitted
 * changes plays only its own commit (the handshake's dirty flag): its number
 * vouches for nothing.
 *
 * A desync between two different commits with the same number means a raise was
 * missed: the recorder names both builds (docs/NETPLAY.md §7).
 */
#ifndef NETPLAY_COMPAT_H
#define NETPLAY_COMPAT_H

/* OpenSWOS-Indoor counts from 1000 (docs/INDOOR.md phase 6), where upstream's numbers never
 * reach: a fork build must never meet one without INDOOR. When a rebase brings a raise of
 * upstream's, raise this by one too. 1001: INDOOR online (upstream's 9); 1002: upstream's 10;
 * 1003: upstream's 11; 1004: upstream's 12;
 * 1005: the indoor tactics made by a model (5.8); 1006: they attack wide;
 * 1007: online, the indoor line-up is the lobby's (arranged on its team screen);
 * 1008: upstream's 13; 1009-1011: upstream's 14-16;
 * 1012: LINOLEUM NO HALF (the setup's floor word 0x100, the lobby's PITCH TYPE 0x80);
 * 1013: a ball in the goal stays in the net (no glass bounce out of its corners);
 * 1014: forwards at the box from halfway on, the 1-3-1 wingers a line of their own;
 * 1015: a booking by the right glass takes place on the foul's left (Indoor_BookingSide);
 *   a time penalty follows its player when the bench menu swaps two (Indoor_SpritesSwapped);
 * 1016: the indoor USER A-F (tactics 5-10): the lobby sends them, the setup carries them in USER A/B. */
#define NETPLAY_COMPAT 1016   /* upstream's history -- 16: a long bench is the front row (places 6.. standing before the game's dugouts); 15: a World 2026-27 friendly's long bench (long_bench.c); 14: main's 13 and mod41-match's 13 merged (a world's stoppage time); 13: SWOS 2020's AMIGA60 and MOON gameplay styles (the handshake's style byte 3 and 4: dos_mode.h); 12: a two-legged tie's first leg leaves its injuries and sendings-off on the second (netplay_lobby.c leg_carry); 11: the replay ring is 38000 bytes on SDL, the resync state with it (10: the handshake carries the mods that change the match; 9: the XBOX gameplay style, the handshake's style byte; 8: the host's crowd in the setup; 7: the lobby's match mode and service ids; 6: the lobby chat) */

#endif
