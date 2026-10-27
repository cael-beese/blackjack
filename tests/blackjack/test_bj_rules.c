/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* Hand totals, and the exact return of each side bet over a six-deck shoe. */
#include <string.h>

#include "test_util.h"
#include "games/blackjack/bj_rules.h"

static Card C(const char *s)
{
    Card c;
    if (card_parse(s, &c)) { fprintf(stderr, "bad card %s\n", s); exit(2); }
    return c;
}

static void check_total(const char *cards, int total, int soft)
{
    Card c[12];
    int n = cards_parse(cards, c, 12);
    BjTotal t = bj_total(c, n);
    if (t.total != total || t.soft != soft)
        fprintf(stderr, "  %s: got %d%s, want %d%s\n", cards, t.total, t.soft ? " soft" : "", total, soft ? " soft" : "");
    CHECK(t.total == total && t.soft == soft);
}

int main(void)
{
    check_total("As Kd", 21, 1);
    check_total("As Ad", 12, 1);
    check_total("As 6d 9c", 16, 0);
    check_total("Ks Qd 2c", 22, 0);
    check_total("As Ad Ac Ah 7s", 21, 1);
    check_total("5s 6d", 11, 0);
    check_total("As 6d", 17, 1);
    check_total("As 6d Th", 17, 0);
    CHECK(bj_is_pair((Card[]){ C("Ks"), C("Qd") }, 2));
    CHECK(!bj_is_pair((Card[]){ C("As"), C("Kd") }, 2));

    CHECK_EQ_INT(bj_pp_outcome(C("8s"), C("8s")), PP_PERFECT);
    CHECK_EQ_INT(bj_pp_outcome(C("8h"), C("8d")), PP_COLOURED);
    CHECK_EQ_INT(bj_pp_outcome(C("8h"), C("8c")), PP_MIXED);
    CHECK_EQ_INT(bj_pp_outcome(C("Kh"), C("Qh")), PP_NONE);
    CHECK_EQ_INT(bj_213_outcome(C("7h"), C("7h"), C("7h")), T213_SUITED_TRIPS);
    CHECK_EQ_INT(bj_213_outcome(C("7h"), C("7c"), C("7h")), T213_TRIPS);
    CHECK_EQ_INT(bj_213_outcome(C("5h"), C("6h"), C("7h")), T213_STRAIGHT_FLUSH);
    CHECK_EQ_INT(bj_213_outcome(C("Ah"), C("2c"), C("3h")), T213_STRAIGHT);
    CHECK_EQ_INT(bj_213_outcome(C("Qh"), C("Kc"), C("Ah")), T213_STRAIGHT);
    CHECK_EQ_INT(bj_213_outcome(C("Kh"), C("Ac"), C("2h")), T213_NONE);
    CHECK_EQ_INT(bj_213_outcome(C("2h"), C("9h"), C("Kh")), T213_FLUSH);
    CHECK_EQ_INT(bj_hp_outcome(C("7h"), C("7h"), C("7h")), HP_JACKPOT);
    CHECK_EQ_INT(bj_hp_outcome(C("7h"), C("7d"), C("7h")), HP_THREE_COLOUR);
    CHECK_EQ_INT(bj_hp_outcome(C("7h"), C("7c"), C("7h")), HP_THREE);
    CHECK_EQ_INT(bj_hp_outcome(C("7h"), C("7h"), C("8h")), HP_TWO_SUITED);
    CHECK_EQ_INT(bj_hp_outcome(C("7h"), C("7c"), C("8h")), HP_TWO_SEVENS);
    CHECK_EQ_INT(bj_hp_outcome(C("7h"), C("2c"), C("7s")), HP_ONE_SEVEN);
    CHECK_EQ_INT(bj_hp_outcome(C("2h"), C("7c"), C("7s")), HP_NONE);

    /* Exact returns: every ordered draw from a six-deck shoe, in which each
       card id appears BJ_DECKS times. */
    const double D = BJ_DECKS, N = BJ_SHOE_CARDS;
    double pp = 0, t3 = 0, hp = 0, hp_jack = 0;
    for (int a = 0; a < 52; a++)
        for (int b = 0; b < 52; b++) {
            double wab = D * (b == a ? D - 1 : D) / (N * (N - 1));
            int o = bj_pp_outcome((Card)a, (Card)b);
            pp += wab * (o ? bj_pp_pays[o] + 1 : 0);
            for (int u = 0; u < 52; u++) {
                double same = (u == a) + (u == b);
                double w = wab * (D - same) / (N - 2);
                int o3 = bj_213_outcome((Card)a, (Card)b, (Card)u);
                t3 += w * (o3 ? bj_213_pays[o3] + 1 : 0);
                int oh = bj_hp_outcome((Card)a, (Card)b, (Card)u);
                if (oh == HP_JACKPOT) hp_jack += w;
                else if (oh) hp += w * (bj_hp_pays[oh] + 1);
            }
        }
    double hp_seed = hp + hp_jack * (BJ_HONEY_SEED + BJ_HONEY_BET) / BJ_HONEY_BET;
    printf("Perfect Pairs return %.4f%%\n", pp * 100);
    printf("21+3 return          %.4f%%\n", t3 * 100);
    printf("HONEY POT fixed pays %.4f%%, jackpot 1 in %.0f, at the seed %.4f%%, plus %.1f%% fed to the pot\n",
           hp * 100, 1 / hp_jack, hp_seed * 100, 100.0 * BJ_HONEY_FEED / BJ_HONEY_BET);
    /* Perfect Pairs in closed form: after the first card, 5 of the 311 left
       are the same card, 6 the other suit of its colour, 12 the other colour. */
    double pp_exact = (26.0 * 5 + 13.0 * 6 + 7.0 * 12) / 311.0;
    CHECK(fabs(pp - pp_exact) < 1e-12);
    CHECK(t3 > 0.90 && t3 < 1.0);
    CHECK(hp_seed > 0.5 && hp_seed < 1.0);
    return test_finish("bj_rules");
}
