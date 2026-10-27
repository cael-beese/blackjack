/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#include "games/blackjack/bj_rules.h"

const int bj_pp_pays[PP_N] = { 0, 6, 12, 25 };
const int bj_213_pays[T213_N] = { 0, 5, 10, 30, 40, 100 };
const int bj_hp_pays[HP_N] = { 0, 4, 40, 75, 150, 750, 0 };

BjTotal bj_total(const Card *c, int n)
{
    BjTotal t = { 0, 0 };
    int aces = 0;
    for (int i = 0; i < n; i++) {
        int v = bj_card_value(c[i]);
        if (v == 11) { aces++; v = 1; }
        t.total += v;
    }
    /* At most one ace can count 11 without busting. */
    if (aces && t.total + 10 <= 21) { t.total += 10; t.soft = 1; }
    return t;
}

static int is_red(Card c) { return card_suit(c) == SUIT_D || card_suit(c) == SUIT_H; }

int bj_pp_outcome(Card a, Card b)
{
    /* A pair of ranks, not of values: K-Q is not a Perfect Pair. */
    if (card_rank(a) != card_rank(b)) return PP_NONE;
    if (card_suit(a) == card_suit(b)) return PP_PERFECT;
    if (is_red(a) == is_red(b)) return PP_COLOURED;
    return PP_MIXED;
}

int bj_213_outcome(Card a, Card b, Card up)
{
    int r[3] = { card_rank(a), card_rank(b), card_rank(up) };
    int flush = card_suit(a) == card_suit(b) && card_suit(b) == card_suit(up);
    if (r[0] == r[1] && r[1] == r[2]) return flush ? T213_SUITED_TRIPS : T213_TRIPS;
    /* Sort three ranks. */
    for (int i = 0; i < 2; i++)
        for (int j = i + 1; j < 3; j++)
            if (r[j] < r[i]) { int t = r[i]; r[i] = r[j]; r[j] = t; }
    int straight = 0;
    if (r[0] != r[1] && r[1] != r[2]) {
        if (r[2] - r[0] == 2) straight = 1;
        /* A-2-3: the ace plays low. */
        if (r[0] == RANK_2 && r[1] == RANK_3 && r[2] == RANK_A) straight = 1;
    }
    if (straight && flush) return T213_STRAIGHT_FLUSH;
    if (straight) return T213_STRAIGHT;
    if (flush) return T213_FLUSH;
    return T213_NONE;
}

int bj_hp_outcome(Card a, Card b, Card up)
{
    /* The sevens must come in order: the first card, then the second, then
       the up card. A seven on the second card alone pays nothing. */
    if (card_rank(a) != RANK_7) return HP_NONE;
    if (card_rank(b) != RANK_7) return HP_ONE_SEVEN;
    if (card_rank(up) != RANK_7)
        return card_suit(a) == card_suit(b) ? HP_TWO_SUITED : HP_TWO_SEVENS;
    if (card_suit(a) == card_suit(b) && card_suit(b) == card_suit(up)) return HP_JACKPOT;
    if (is_red(a) == is_red(b) && is_red(b) == is_red(up)) return HP_THREE_COLOUR;
    return HP_THREE;
}

const char *bj_side_name(int side)
{
    switch (side) {
    case BJ_SIDE_PP:    return "PERFECT PAIRS";
    case BJ_SIDE_213:   return "21+3";
    case BJ_SIDE_HONEY: return "HONEY POT";
    }
    return "?";
}

const char *bj_side_outcome_name(int side, int o)
{
    static const char *const pp[PP_N] = { "", "MIXED PAIR", "COLOURED PAIR", "PERFECT PAIR" };
    static const char *const t3[T213_N] = { "", "FLUSH", "STRAIGHT", "THREE OF A KIND", "STRAIGHT FLUSH", "SUITED TRIPS" };
    static const char *const hp[HP_N] = { "", "LUCKY 7", "TWO 7S", "TWO SUITED 7S", "THREE 7S", "THREE 7S SAME COLOUR", "HONEY POT" };
    if (o < 0) return "";
    if (side == BJ_SIDE_PP && o < PP_N) return pp[o];
    if (side == BJ_SIDE_213 && o < T213_N) return t3[o];
    if (side == BJ_SIDE_HONEY && o < HP_N) return hp[o];
    return "";
}
