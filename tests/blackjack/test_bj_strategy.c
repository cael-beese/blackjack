/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* The hint's best play for every two-card hand, against the published
   six-deck chart for these rules (S17, DAS, late surrender, peek). */
#include <string.h>

#include "test_util.h"
#include "games/blackjack/bj_rules.h"
#include "games/blackjack/bj_strategy.h"

/* Columns: dealer 2 3 4 5 6 7 8 9 T A. H hit, S stand, D double, P split,
   R surrender. */
typedef struct { const char *hand; const char *row; } Row;
static const Row chart[] = {
    { "Ts 7d", "SSSSSSSSSS" }, { "Ts 6d", "SSSSSHHRRR" }, { "Ts 5d", "SSSSSHHHRH" },
    { "Ts 4d", "SSSSSHHHHH" }, { "Ts 3d", "SSSSSHHHHH" }, { "Ts 2d", "HHSSSHHHHH" },
    { "7s 4d", "DDDDDDDDDH" }, { "6s 4d", "DDDDDDDDHH" }, { "5s 4d", "HDDDDHHHHH" },
    { "5s 3d", "HHHHHHHHHH" }, { "4s 3d", "HHHHHHHHHH" }, { "3s 2d", "HHHHHHHHHH" },
    { "As 9d", "SSSSSSSSSS" }, { "As 8d", "SSSSSSSSSS" }, { "As 7d", "SDDDDSSHHH" },
    { "As 6d", "HDDDDHHHHH" }, { "As 5d", "HHDDDHHHHH" }, { "As 4d", "HHDDDHHHHH" },
    { "As 3d", "HHHDDHHHHH" }, { "As 2d", "HHHDDHHHHH" },
    { "As Ad", "PPPPPPPPPP" }, { "Ts Kd", "SSSSSSSSSS" }, { "9s 9d", "PPPPPSPPSS" },
    { "8s 8d", "PPPPPPPPPP" }, { "7s 7d", "PPPPPPHHHH" }, { "6s 6d", "PPPPPHHHHH" },
    { "5s 5d", "DDDDDDDDHH" }, { "4s 4d", "HHHPPHHHHH" }, { "3s 3d", "PPPPPPHHHH" },
    { "2s 2d", "PPPPPPHHHH" },
};

static char code(int act)
{
    switch (act) {
    case BJ_ACT_HIT: return 'H';
    case BJ_ACT_STAND: return 'S';
    case BJ_ACT_DOUBLE: return 'D';
    case BJ_ACT_SPLIT: return 'P';
    case BJ_ACT_SURRENDER: return 'R';
    }
    return '?';
}

int main(void)
{
    static const char ups[] = "23456789TA";
    int mism = 0, n = (int)(sizeof chart / sizeof chart[0]);
    double t0 = test_now();
    bj_strategy_init();
    printf("strategy tables built in %.2f ms\n", (test_now() - t0) * 1e3);
    for (int r = 0; r < n; r++) {
        Card h[2];
        CHECK(cards_parse(chart[r].hand, h, 2) == 2);
        for (int u = 0; u < 10; u++) {
            Card up = card_make(u == 9 ? RANK_A : u == 8 ? RANK_T : u, SUIT_H);
            BjAdvice a;
            bj_advise(h, 2, up, BJ_CAN_DOUBLE | BJ_CAN_SPLIT | BJ_CAN_SURRENDER, &a);
            if (code(a.best) != chart[r].row[u]) {
                mism++;
                printf("  %s vs %c: hint %c (H %+.4f S %+.4f D %+.4f P %+.4f), chart %c\n", chart[r].hand, ups[u],
                       code(a.best), a.ev[BJ_ACT_HIT], a.ev[BJ_ACT_STAND], a.ev[BJ_ACT_DOUBLE], a.ev[BJ_ACT_SPLIT],
                       chart[r].row[u]);
            }
        }
    }
    printf("%d of %d decisions differ from the six-deck chart\n", mism, n * 10);
    CHECK(mism == 0);

    /* Spot values (infinite deck, dealer peeked): 11 v 6 doubles for about
       +0.667, 16 v T hits for about -0.540. */
    BjAdvice a;
    Card h11[2] = { card_make(RANK_7, 0), card_make(RANK_4, 1) };
    bj_advise(h11, 2, card_make(RANK_6, 2), BJ_CAN_DOUBLE, &a);
    printf("11 v 6: double %+.4f\n", a.ev[BJ_ACT_DOUBLE]);
    CHECK(fabs(a.ev[BJ_ACT_DOUBLE] - 0.667) < 0.01);
    Card h16[2] = { card_make(RANK_T, 0), card_make(RANK_6, 1) };
    bj_advise(h16, 2, card_make(RANK_K, 2), 0, &a);
    printf("16 v T: hit %+.4f stand %+.4f\n", a.ev[BJ_ACT_HIT], a.ev[BJ_ACT_STAND]);
    CHECK(fabs(a.ev[BJ_ACT_HIT] + 0.540) < 0.01);
    CHECK(a.best == BJ_ACT_HIT);
    /* No surrender or double after a hit: a three-card 16 v T. */
    Card h3[3] = { card_make(RANK_T, 0), card_make(RANK_4, 1), card_make(RANK_2, 1) };
    bj_advise(h3, 3, card_make(RANK_K, 2), BJ_CAN_SURRENDER | BJ_CAN_DOUBLE, &a);
    CHECK(!a.legal[BJ_ACT_SURRENDER] && !a.legal[BJ_ACT_DOUBLE]);
    return test_finish("bj_strategy");
}
