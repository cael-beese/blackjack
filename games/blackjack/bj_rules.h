/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#ifndef BPL_BJ_RULES_H
#define BPL_BJ_RULES_H

#include <stdint.h>

#include "engine/card.h"

/* games/blackjack/bj_rules.h - the table's house rules, hand values and the
   three side bets. Pure functions, no state.

   House rules ("Vegas Strip"): six decks, the dealer stands on soft 17,
   blackjack pays 3:2, the dealer peeks under an ace or a ten (an American
   hole card, so a dealer blackjack takes only the original bet), double on
   any first two cards and after a split, split to four hands, split aces
   take one card each and cannot be split again, a two-card 21 after a split
   is 21 and not a blackjack, late surrender on the first two cards (not
   after a split), insurance pays 2:1 and is offered when the dealer shows
   an ace; with a blackjack it is offered as EVEN MONEY. */

#define BJ_DECKS        6
#define BJ_SHOE_CARDS   (52 * BJ_DECKS)
#define BJ_CUT_CARD     234             /* 75% penetration: shuffle once this many are gone */
#define BJ_MAX_SPLIT_HANDS 4

/* The value of a card: 2..10, and 11 for an ace. */
static inline int bj_card_value(Card c)
{
    int r = card_rank(c);
    if (r == RANK_A) return 11;
    if (r >= RANK_T) return 10;
    return r + 2;
}

/* A hand's best total (<= 21 when it can be) and whether an ace still counts
   11 in it ("soft"). */
typedef struct { int total; int soft; } BjTotal;
BjTotal bj_total(const Card *c, int n);

static inline int bj_is_pair(const Card *c, int n)
{
    /* Any two ten-value cards split (K-Q is a pair here, as in most casinos). */
    return n == 2 && bj_card_value(c[0]) == bj_card_value(c[1]);
}

/* ---- side bets ---------------------------------------------------------- */

enum { BJ_SIDE_PP, BJ_SIDE_213, BJ_SIDE_HONEY, BJ_SIDE_N };

/* Perfect Pairs, on the player's first two cards. Payouts are "to 1". */
enum { PP_NONE, PP_MIXED, PP_COLOURED, PP_PERFECT, PP_N };
extern const int bj_pp_pays[PP_N];             /* 0, 6, 12, 25           */
int bj_pp_outcome(Card a, Card b);

/* 21+3: the player's two cards and the dealer's up card as a poker hand. */
enum { T213_NONE, T213_FLUSH, T213_STRAIGHT, T213_TRIPS, T213_STRAIGHT_FLUSH, T213_SUITED_TRIPS, T213_N };
extern const int bj_213_pays[T213_N];          /* 0, 5, 10, 30, 40, 100 */
int bj_213_outcome(Card a, Card b, Card up);

/* HONEY POT, the bee progressive: a fixed BJ_HONEY_BET a hand, on sevens -
   the player's first card, then the second, then the dealer's up card.
   Three suited sevens win the pot (at least BJ_HONEY_SEED); the rest pay
   fixed odds "to 1". BJ_HONEY_FEED of every bet goes into the pot. */
#define BJ_HONEY_BET    5
#define BJ_HONEY_FEED   1
#define BJ_HONEY_SEED   10000
enum { HP_NONE, HP_ONE_SEVEN, HP_TWO_SEVENS, HP_TWO_SUITED, HP_THREE, HP_THREE_COLOUR, HP_JACKPOT, HP_N };
extern const int bj_hp_pays[HP_N];             /* JACKPOT's entry is 0: it pays the pot */
int bj_hp_outcome(Card a, Card b, Card up);

const char *bj_side_name(int side);
const char *bj_side_outcome_name(int side, int outcome);

#endif
