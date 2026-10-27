/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#ifndef BPL_BJ_STRATEGY_H
#define BPL_BJ_STRATEGY_H

#include <stdint.h>

#include "engine/card.h"

/* games/blackjack/bj_strategy.h - the strategy hint: the expected value of
   every legal play of a hand against the dealer's up card, and the best one.

   The values are for a six-deck shoe less the cards in sight - the hand's
   own cards and the up card - with every later draw taken from that same
   mix, under this table's rules (bj_rules.h), given that the dealer has
   peeked and has no blackjack. That is basic strategy as the six-deck chart
   gives it: it uses only the player's own cards and the up card - never the
   rest of the shoe, never the hole card. (A pure infinite-deck model gets
   soft 15 v 4 and soft 13 v 5 wrong; the cards in sight are what decide
   them.) A split is valued as two hands that may each double but not split
   again, which understates a split by a hair and never changes the best
   play (tests/blackjack compares every two-card decision with the chart).

   EVs are per unit of the hand's bet before the play: a double that wins
   is +2, a surrender is -0.5, a split counts both hands. */

enum { BJ_ACT_NONE, BJ_ACT_HIT, BJ_ACT_STAND, BJ_ACT_DOUBLE, BJ_ACT_SPLIT, BJ_ACT_SURRENDER, BJ_ACT_N };

/* Which plays are allowed (the table decides; the advice only ranks them). */
enum {
    BJ_CAN_DOUBLE = 1u << 0,
    BJ_CAN_SPLIT = 1u << 1,
    BJ_CAN_SURRENDER = 1u << 2,
};

typedef struct {
    double  ev[BJ_ACT_N];       /* valid where legal[] is set              */
    uint8_t legal[BJ_ACT_N];
    int     best;               /* BJ_ACT_* with the highest EV            */
} BjAdvice;

/* Nothing to build: each bj_advise works its values out afresh (well under
   a millisecond). Kept so callers need not know that. */
void bj_strategy_init(void);

/* hand: the player's cards (n >= 2), up: the dealer's up card. A hand that
   is already 21 or more is given STAND (or nothing, if bust). */
void bj_advise(const Card *hand, int n, Card up, unsigned can, BjAdvice *out);

/* The dealer's final-total distribution for an up card (value 2..11), given
   no dealer blackjack: [0..4] = 17..21, [5] = bust. */
void bj_dealer_dist(int up_value, double out[6]);

const char *bj_action_name(int act);

#endif
