/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* selftest.c - see selftest.h. */
#include "platform/selftest.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "engine/card.h"
#include "engine/deck.h"
#include "engine/rng.h"
#include "games/blackjack/bj_game.h"

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

void selftest_add(SelfTestReport *r, const char *name, int pass, double ms, const char *fmt, ...)
{
    if (!pass) r->failures++;
    if (r->n >= SELFTEST_MAX) return;
    SelfTestItem *it = &r->item[r->n++];
    snprintf(it->name, sizeof it->name, "%s", name);
    it->pass = pass;
    it->ms = ms;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(it->detail, sizeof it->detail, fmt, ap);
    va_end(ap);
}


static Card C(const char *s)
{
    Card c = CARD_NONE;
    card_parse(s, &c);
    return c;
}

/* Hand totals and every side-bet outcome on known hands. */
static void check_rules(SelfTestReport *r)
{
    double t0 = now_ms();
    static const struct { const char *h; int total, soft; } tot[] = {
        { "As Kd", 21, 1 }, { "As Ad", 12, 1 }, { "As 6d 9c", 16, 0 }, { "Ks Qd 2c", 22, 0 }, { "As Ad Ac Ah 7s", 21, 1 },
    };
    int bad = 0;
    for (size_t i = 0; i < sizeof tot / sizeof tot[0]; i++) {
        Card c[8];
        int n = cards_parse(tot[i].h, c, 8);
        BjTotal t = bj_total(c, n);
        if (t.total != tot[i].total || t.soft != tot[i].soft) bad++;
    }
    int sides = bj_pp_outcome(C("8s"), C("8s")) == PP_PERFECT && bj_pp_outcome(C("8h"), C("8d")) == PP_COLOURED &&
                bj_pp_outcome(C("8h"), C("8c")) == PP_MIXED && bj_213_outcome(C("5h"), C("6h"), C("7h")) == T213_STRAIGHT_FLUSH &&
                bj_213_outcome(C("Qh"), C("Kc"), C("Ah")) == T213_STRAIGHT && bj_213_outcome(C("7h"), C("7h"), C("7h")) == T213_SUITED_TRIPS &&
                bj_hp_outcome(C("7h"), C("7h"), C("7h")) == HP_JACKPOT && bj_hp_outcome(C("7h"), C("7d"), C("7h")) == HP_THREE_COLOUR &&
                bj_hp_outcome(C("2h"), C("7c"), C("7s")) == HP_NONE;
    selftest_add(r, "hand totals, side bets", bad == 0 && sides, now_ms() - t0, "%d/5 totals, side bet outcomes %s",
                 5 - bad, sides ? "ok" : "BAD");
}

/* The exact return of the two fixed side bets over a six-deck shoe. */
static void check_returns(SelfTestReport *r)
{
    double t0 = now_ms();
    const double D = BJ_DECKS, N = BJ_SHOE_CARDS;
    double pp = 0, t3 = 0;
    for (int a = 0; a < 52; a++)
        for (int b = 0; b < 52; b++) {
            double wab = D * (b == a ? D - 1 : D) / (N * (N - 1));
            int o = bj_pp_outcome((Card)a, (Card)b);
            pp += wab * (o ? bj_pp_pays[o] + 1 : 0);
            for (int u = 0; u < 52; u++) {
                double w = wab * (D - (u == a) - (u == b)) / (N - 2);
                int o3 = bj_213_outcome((Card)a, (Card)b, (Card)u);
                t3 += w * (o3 ? bj_213_pays[o3] + 1 : 0);
            }
        }
    int ok = fabs(pp - 0.938907) < 1e-5 && fabs(t3 - 0.953790) < 1e-5;
    selftest_add(r, "side bet returns", ok, now_ms() - t0, "Perfect Pairs %.4f%%, 21+3 %.4f%% (want 93.8907, 95.3790)",
                 pp * 100, t3 * 100);
}

/* The hint against the six-deck chart on the decisions people get wrong. */
static void check_hint(SelfTestReport *r)
{
    double t0 = now_ms();
    static const struct { const char *h, *up; int act; } cases[] = {
        { "Ts 6d", "Kh", BJ_ACT_SURRENDER }, { "Ts 2d", "3h", BJ_ACT_HIT }, { "Ts 2d", "4h", BJ_ACT_STAND },
        { "7s 4d", "Ah", BJ_ACT_HIT },       { "6s 4d", "9h", BJ_ACT_DOUBLE }, { "As 7d", "9h", BJ_ACT_HIT },
        { "As 7d", "3h", BJ_ACT_DOUBLE },    { "8s 8d", "Ah", BJ_ACT_SPLIT },  { "9s 9d", "7h", BJ_ACT_STAND },
        { "As 4d", "4h", BJ_ACT_DOUBLE },    { "As 2d", "5h", BJ_ACT_DOUBLE }, { "5s 5d", "Th", BJ_ACT_HIT },
    };
    int bad = 0, n = (int)(sizeof cases / sizeof cases[0]);
    for (int i = 0; i < n; i++) {
        Card h[2];
        cards_parse(cases[i].h, h, 2);
        BjAdvice a;
        bj_advise(h, 2, C(cases[i].up), BJ_CAN_DOUBLE | BJ_CAN_SPLIT | BJ_CAN_SURRENDER, &a);
        if (a.best != cases[i].act) bad++;
    }
    double ms = now_ms() - t0;
    selftest_add(r, "strategy hint", bad == 0, ms, "%d/%d chart decisions, %.2f ms each", n - bad, n, ms / n);
}

/* A thousand rounds at a bare table: every credit staked comes back or is
   lost exactly once, and every shoe holds six decks. */
static void check_table(SelfTestReport *r)
{
    double t0 = now_ms();
    BjConfig cfg;
    bj_config_default(&cfg);
    cfg.deal_gap = cfg.act_gap = cfg.dealer_gap = cfg.settle_ticks = 0;
    cfg.peek_ticks = cfg.clear_ticks = cfg.shuffle_ticks = cfg.sides_ticks = cfg.big_ticks = cfg.jackpot_ticks = 1;
    static BjGame g;
    bj_init(&g, &cfg, 99, 0);
    Wallet w = { 1000000, 1 };
    int64_t staked = 0, back = 0;
    int rounds = 0, shoe_ok = 1, shoes = 0;
    g.side[BJ_SIDE_213] = 5;
    while (rounds < 1000) {
        InputFrame in;
        memset(&in, 0, sizeof in);
        if (g.phase == BJ_PH_BETTING) in.pressed = BTN_DEAL;
        else if (bj_human_insurance(&g)) in.pressed = BTN_HOLD2;
        else if (bj_human_turn(&g)) {
            const BjHand *h = bj_cur_hand(&g, g.cfg.human_seat);
            BjAdvice a;
            bj_advise(h->c, h->n, bj_up_card(&g), bj_human_can(&g, &w), &a);
            in.pressed = a.best == BJ_ACT_HIT ? BTN_HOLD1 : a.best == BJ_ACT_DOUBLE ? BTN_HOLD3 :
                         a.best == BJ_ACT_SPLIT ? BTN_HOLD4 : a.best == BJ_ACT_SURRENDER ? BTN_HOLD5 : BTN_HOLD2;
        }
        EventQueue q = { .n = 0 };
        bj_tick(&g, &in, &w, &q);
        for (int i = 0; i < q.n; i++) {
            const GameEvent *e = &q.e[i];
            if (e->a != g.cfg.human_seat && e->type != EV_BJ_ROUND) continue;
            if (e->type == EV_BJ_STAKE) staked += e->v;
            else if (e->type == EV_BJ_RESULT || e->type == EV_BJ_SIDE || e->type == EV_BJ_INS_RESULT) back += e->v;
            else if (e->type == EV_BJ_ROUND) rounds++;
        }
        if (g.shoes != shoes) {
            shoes = g.shoes;
            int cnt[52] = { 0 };
            for (int i = 0; i < BJ_SHOE_CARDS; i++) if (g.shoe[i] < 52) cnt[g.shoe[i]]++;
            for (int i = 0; i < 52; i++) if (cnt[i] != BJ_DECKS) shoe_ok = 0;
        }
    }
    int64_t net = w.credits - 1000000;
    int ok = net == back - staked && shoe_ok;
    selftest_add(r, "table: 1000 rounds", ok, now_ms() - t0, "credits %s (net %+lld), %d shoes %s", net == back - staked ? "conserved" : "LEAK",
                 (long long)net, shoes, shoe_ok ? "all six-deck" : "BAD");
}
static void check_shuffle(SelfTestReport *r)
{
    double t0 = now_ms();
    Rng a, b;
    rng_seed(&a, 12345);
    rng_seed(&b, 12345);
    int same = 1;
    for (int i = 0; i < 1000; i++) if (rng_next(&a) != rng_next(&b)) same = 0;
    Deck d;
    deck_init(&d);
    deck_shuffle(&d, &a);
    uint64_t seen = 0;
    for (int i = 0; i < 52; i++) seen |= UINT64_C(1) << d.c[i];
    int perm = seen == (UINT64_C(1) << 52) - 1;
    /* Position counts over many shuffles: every card should land in every
       position about equally (a coarse chi-square, 52x52 cells). */
    static uint32_t cnt[52][52];
    memset(cnt, 0, sizeof cnt);
    const int N = 20000;
    for (int k = 0; k < N; k++) {
        deck_init(&d);
        deck_shuffle(&d, &a);
        for (int i = 0; i < 52; i++) cnt[d.c[i]][i]++;
    }
    double e = N / 52.0, chi = 0;
    for (int i = 0; i < 52; i++)
        for (int j = 0; j < 52; j++) chi += (cnt[i][j] - e) * (cnt[i][j] - e) / e;
    /* 2601 degrees of freedom: mean 2601, sd ~72; 3000 is > 5 sd. */
    uint64_t s1 = rng_os_seed(), s2 = rng_os_seed();
    int ok = same && perm && chi < 3000 && s1 != s2;
    selftest_add(r, "shuffle and RNG", ok, now_ms() - t0, "replay %s, permutation %s, chi2 %.0f (df 2601), OS seed %s",
                 same ? "ok" : "BAD", perm ? "ok" : "BAD", chi, s1 != s2 ? "ok" : "REPEATS");
}

void selftest_run_logic(SelfTestReport *r)
{
    check_rules(r);
    check_returns(r);
    check_hint(r);
    check_shuffle(r);
    check_table(r);
}
