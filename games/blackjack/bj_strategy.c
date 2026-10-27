/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#include "games/blackjack/bj_strategy.h"

#include <string.h>

#include "games/blackjack/bj_rules.h"

/* Card values 1..10 here (an ace is 1; "soft" is tracked separately). The
   working state of one evaluation: the draw probabilities of the shoe it
   assumes, the dealer's final totals, and the player's hit/stand values. */
typedef struct {
    double p[11];               /* p[1..10]                                    */
    double dealer[6];           /* final 17..21, bust                         */
    double opt[32][2];          /* best of hit / stand, by hard sum and ace   */
    double hit[32][2];
} Eval;

static int best_total(int hard, int ace) { return (ace && hard + 10 <= 21) ? hard + 10 : hard; }

static int value1(Card c) { int v = bj_card_value(c); return v == 11 ? 1 : v; }

/* The dealer draws from (hard, ace) with probability w. excl is a card value
   the first draw cannot be (the dealer has peeked): 0 none. */
static void dealer_rec(const Eval *e, int hard, int ace, double w, int excl, double out[6])
{
    int t = best_total(hard, ace);
    if (t > 21) { out[5] += w; return; }
    if (t >= 17) { out[t - 17] += w; return; }      /* stands on all 17s, soft too */
    double norm = excl ? 1.0 - e->p[excl] : 1.0;
    for (int v = 1; v <= 10; v++) {
        if (v == excl || e->p[v] <= 0) continue;
        dealer_rec(e, hard + v, ace || v == 1, w * e->p[v] / norm, 0, out);
    }
}

static double stand_ev(const Eval *e, int total)
{
    if (total > 21) return -1.0;
    double ev = e->dealer[5];
    for (int t = 17; t <= 21; t++) {
        if (total > t) ev += e->dealer[t - 17];
        else if (total < t) ev -= e->dealer[t - 17];
    }
    return ev;
}

/* Fills e for an up card (value 1..10) and a shoe of six decks less the
   given cards. Later draws are taken from that same mix: the cards in sight
   carry most of what the composition changes, and the cost stays a few
   thousand steps. */
static void eval_build(Eval *e, int up, const Card *gone, int ngone)
{
    double cnt[11];
    for (int v = 1; v <= 10; v++) cnt[v] = (v == 10 ? 16.0 : 4.0) * BJ_DECKS;
    for (int i = 0; i < ngone; i++) cnt[value1(gone[i])] -= 1.0;
    double tot = 0;
    for (int v = 1; v <= 10; v++) tot += cnt[v];
    for (int v = 1; v <= 10; v++) e->p[v] = cnt[v] / tot;

    memset(e->dealer, 0, sizeof e->dealer);
    /* The hole card cannot make a blackjack: the dealer has peeked. */
    int excl = up == 1 ? 10 : up == 10 ? 1 : 0;
    dealer_rec(e, up, up == 1, 1.0, excl, e->dealer);

    /* Hit-or-stand values, from the top down: a hard sum over 21 is bust. */
    for (int hard = 31; hard >= 2; hard--)
        for (int ace = 1; ace >= 0; ace--) {
            if (hard > 21) { e->opt[hard][ace] = e->hit[hard][ace] = -1.0; continue; }
            double hit = 0;
            for (int v = 1; v <= 10; v++) {
                int nh = hard + v, na = ace || v == 1;
                hit += e->p[v] * (nh > 21 ? -1.0 : e->opt[nh][na]);
            }
            double st = stand_ev(e, best_total(hard, ace));
            e->hit[hard][ace] = hit;
            e->opt[hard][ace] = hit > st ? hit : st;
        }
}

void bj_strategy_init(void) {}

void bj_dealer_dist(int up_value, double out[6])
{
    Eval e;
    Card up = card_make(up_value == 11 ? RANK_A : up_value - 2 > RANK_T ? RANK_T : up_value - 2, SUIT_S);
    eval_build(&e, up_value == 11 ? 1 : up_value, &up, 1);
    memcpy(out, e.dealer, sizeof e.dealer);
}

static double double_ev(const Eval *e, int hard, int ace)
{
    double ev = 0;
    for (int v = 1; v <= 10; v++)
        ev += e->p[v] * stand_ev(e, best_total(hard + v, ace || v == 1));
    return 2.0 * ev;
}

/* One hand of a split, starting from a single card of value v (ace = 1). */
static double split_hand_ev(const Eval *e, int v)
{
    double ev = 0;
    for (int c = 1; c <= 10; c++) {
        int hard = v + c, ace = v == 1 || c == 1;
        if (v == 1) {
            /* Split aces get one card and stand; 21 is not a blackjack. */
            ev += e->p[c] * stand_ev(e, best_total(hard, ace));
            continue;
        }
        double best = e->opt[hard][ace];
        double dbl = double_ev(e, hard, ace);          /* double after split */
        ev += e->p[c] * (dbl > best ? dbl : best);
    }
    return ev;
}

void bj_advise(const Card *hand, int n, Card up, unsigned can, BjAdvice *out)
{
    memset(out, 0, sizeof *out);
    int hard = 0, ace = 0;
    for (int i = 0; i < n; i++) {
        int v = value1(hand[i]);
        if (v == 1) ace = 1;
        hard += v;
    }
    int total = best_total(hard, ace);
    if (total > 21) { out->best = BJ_ACT_NONE; return; }

    Eval e;
    Card gone[BJ_MAX_SPLIT_HANDS * 12 + 1];
    int ng = 0;
    for (int i = 0; i < n && ng < (int)(sizeof gone / sizeof gone[0]) - 1; i++) gone[ng++] = hand[i];
    gone[ng++] = up;
    eval_build(&e, value1(up), gone, ng);

    out->legal[BJ_ACT_STAND] = 1;
    out->ev[BJ_ACT_STAND] = stand_ev(&e, total);
    if (total < 21) {
        out->legal[BJ_ACT_HIT] = 1;
        out->ev[BJ_ACT_HIT] = e.hit[hard][ace];
        if ((can & BJ_CAN_DOUBLE) && n == 2) {
            out->legal[BJ_ACT_DOUBLE] = 1;
            out->ev[BJ_ACT_DOUBLE] = double_ev(&e, hard, ace);
        }
    }
    if ((can & BJ_CAN_SPLIT) && bj_is_pair(hand, n)) {
        out->legal[BJ_ACT_SPLIT] = 1;
        out->ev[BJ_ACT_SPLIT] = 2.0 * split_hand_ev(&e, value1(hand[0]));
    }
    if ((can & BJ_CAN_SURRENDER) && n == 2) {
        out->legal[BJ_ACT_SURRENDER] = 1;
        out->ev[BJ_ACT_SURRENDER] = -0.5;
    }
    /* Ties go to the earlier, simpler play in this order. */
    static const int order[] = { BJ_ACT_STAND, BJ_ACT_HIT, BJ_ACT_DOUBLE, BJ_ACT_SPLIT, BJ_ACT_SURRENDER };
    out->best = BJ_ACT_NONE;
    for (int i = 0; i < 5; i++) {
        int a = order[i];
        if (out->legal[a] && (out->best == BJ_ACT_NONE || out->ev[a] > out->ev[out->best] + 1e-12))
            out->best = a;
    }
}

const char *bj_action_name(int act)
{
    static const char *const n[BJ_ACT_N] = { "", "HIT", "STAND", "DOUBLE", "SPLIT", "SURRENDER" };
    return (act >= 0 && act < BJ_ACT_N) ? n[act] : "";
}
