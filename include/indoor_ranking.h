/*
 * indoor_ranking.h -- (OpenSWOS-Indoor) the fork's online provider: log-in to SWOS United's site
 * and the OOO Indoor ranking (src/indoor_ranking.c, docs/INDOOR.md "Ranked").
 */
#ifndef INDOOR_RANKING_H
#define INDOOR_RANKING_H

/* Make it the online provider (ModApi_SetOnlineProvider), unless a mod has set one; from
 * Account_Init. Nothing in a build without netplay, or without the server's key. */
void IndoorRanking_Register(void);

#endif
