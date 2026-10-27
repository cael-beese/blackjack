/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#ifndef BPL_BJ_BOTS_H
#define BPL_BJ_BOTS_H

#include <stdint.h>

#include "engine/card.h"
#include "engine/rng.h"
#include "games/blackjack/bj_rules.h"

/* games/blackjack/bj_bots.h - the bee regulars who share the table.

   Each is a style of player you meet at a real table, so the table looks
   alive and their choices read as character, not as noise:
     STINGER  the pro: perfect basic strategy, flat bets, no side bets -
              except the HONEY POT once the pot reaches 30,000
     HONEY    the hunch player: never risks a bust (stands on 12 and up),
              plays the HONEY POT two rounds in three,
              loves Perfect Pairs, bets on a whim
     BUZZ     the hothead: basic strategy, but lets wins ride (parlays),
              always plays the HONEY POT
     QUEENIE  the high roller: big bets, every side bet, always insures
     DRONE    the mimic: plays like the dealer (hits to 17), never doubles
              or splits, plays the HONEY POT every other round or so
     DEMO     plays the player's seat in attract mode: basic strategy, and
              always the HONEY POT

   Honesty (CONTRACT section 5): a bot decides from ITS OWN cards and the
   dealer's up card only - these functions are not given the shoe, the hole
   card or anyone else's hand. Their randomness (bet sizes, think time) comes
   from the table's bot stream, never the shoe's. */

enum { BOT_STINGER, BOT_HONEY, BOT_BUZZ, BOT_QUEENIE, BOT_DRONE, BOT_DEMO, BOT_N };

typedef struct {
    int64_t last_bet;          /* the bot's previous main bet (0: none yet)    */
    int64_t last_net;          /* what that round won (+) or lost (-)          */
    int64_t bank;              /* its chips                                    */
} BjBotMemory;

/* The pot the pro starts chasing the HONEY POT at (it is on the sign for all
   to see, so this is public information). */
#define BJ_PRO_CHASE 30000

/* The main bet for the next round (0: sits out) and the side bets, each
   within [min, max] and what the bank can pay. pot: the HONEY POT now. */
int64_t bj_bot_bet(int bot, const BjBotMemory *m, int64_t min, int64_t max, int64_t pot, Rng *r,
                   int64_t side[BJ_SIDE_N]);
/* Insurance (or even money with a blackjack) when the dealer shows an ace. */
int     bj_bot_insure(int bot, const Card *hand, int n);
/* The play: BJ_ACT_* among those in `can` (bj_strategy.h); HIT and STAND are
   always allowed below 21. */
int     bj_bot_play(int bot, const Card *hand, int n, Card up, unsigned can);
/* How long it takes to decide, in ticks: the hand reads as a person's. */
int     bj_bot_think(int bot, int act, Rng *r);

const char *bj_bot_name(int bot);
const char *bj_bot_style(int bot);    /* "THE PRO", ... */
int64_t     bj_bot_bankroll(int bot); /* the chips it sits down with */

#endif
