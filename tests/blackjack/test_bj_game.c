/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* The blackjack table: scripted rounds from a stacked shoe (blackjack,
   insurance, split and double, surrender, split aces, a refused double),
   credit conservation, shoe integrity, replay determinism, and the return
   of basic strategy over a long simulation (BJ_SIM_ROUNDS, default 300000). */
#include <string.h>

#include "test_util.h"
#include "games/blackjack/bj_game.h"

static void fast_config(BjConfig *c)
{
    bj_config_default(c);
    c->deal_gap = c->act_gap = c->dealer_gap = c->settle_ticks = 0;
    c->peek_ticks = c->clear_ticks = c->shuffle_ticks = c->sides_ticks = c->big_ticks = c->jackpot_ticks = 1;
    c->demo_bet_ticks = 1;
}

static void heads_up(BjConfig *c)
{
    for (int i = 0; i < BJ_SEATS; i++) c->bots[i] = -1;
    for (int i = 0; i < 4; i++) c->bench[i] = -1;
}

/* ---- the human's money, followed through the events ----------------------- */

typedef struct {
    int64_t staked, returned, main_staked, main_paid;
    long    rounds, hands, shuffles, denied, jackpots;
    uint64_t hash;
} Tally;

static void tally(Tally *t, const BjGame *g, const EventQueue *q)
{
    int hs = g->cfg.human_seat;
    for (int i = 0; i < q->n; i++) {
        const GameEvent *e = &q->e[i];
        t->hash = (t->hash ^ ((uint64_t)e->type << 48 ^ (uint64_t)(uint16_t)e->a << 32 ^
                              (uint64_t)(uint16_t)e->b << 16 ^ (uint32_t)e->v)) * 0x100000001B3ull;
        switch (e->type) {
        case EV_BJ_STAKE:
            if (e->a == hs) {
                t->staked += e->v;
                if (e->b == BJ_BET_MAIN || e->b == BJ_BET_DOUBLE || e->b == BJ_BET_SPLIT) t->main_staked += e->v;
            }
            break;
        case EV_BJ_RESULT:
            if (e->a == hs) { t->returned += e->v; t->main_paid += e->v; t->hands++; }
            break;
        case EV_BJ_SIDE: case EV_BJ_INS_RESULT:
            if (e->a == hs) t->returned += e->v;
            break;
        case EV_BJ_ROUND: t->rounds++; break;
        case EV_BJ_SHUFFLE: t->shuffles++; break;
        case EV_BJ_DENIED: t->denied++; break;
        case EV_BJ_JACKPOT: t->jackpots++; break;
        }
    }
}

/* The basic-strategy player: deal, never insure, play the hint. */
static uint32_t basic_player(const BjGame *g, const Wallet *w)
{
    if (g->phase == BJ_PH_BETTING) return BTN_DEAL;
    if (bj_human_insurance(g)) return BTN_HOLD2;
    if (bj_human_turn(g)) {
        const BjHand *h = bj_cur_hand(g, g->cfg.human_seat);
        BjAdvice a;
        bj_advise(h->c, h->n, bj_up_card(g), bj_human_can(g, w), &a);
        switch (a.best) {
        case BJ_ACT_HIT: return BTN_HOLD1;
        case BJ_ACT_DOUBLE: return BTN_HOLD3;
        case BJ_ACT_SPLIT: return BTN_HOLD4;
        case BJ_ACT_SURRENDER: return BTN_HOLD5;
        default: return BTN_HOLD2;
        }
    }
    return 0;
}

static void step(BjGame *g, Wallet *w, uint32_t pressed, Tally *t)
{
    InputFrame in = { 0 };
    in.pressed = in.down = pressed;
    EventQueue q = { .n = 0 };
    bj_tick(g, &in, w, &q);
    if (t) tally(t, g, &q);
}

/* ---- stacked-shoe scenarios ------------------------------------------------ */

static void stack(BjGame *g, const char *cards)
{
    Card c[64];
    int n = cards_parse(cards, c, 64);
    if (n < 0) { fprintf(stderr, "bad stack %s\n", cards); exit(2); }
    g->need_shuffle = 0;
    g->table_dirty = 0;
    g->shoe_pos = 0;
    for (int i = 0; i < n; i++) g->shoe[i] = c[i];
    /* The rest of the shoe: anything, it is not reached. */
    for (int i = n; i < BJ_SHOE_CARDS; i++) g->shoe[i] = card_make(RANK_2, SUIT_C);
}

/* Runs until the player is asked for something (or the round settles);
   returns 1 if asked. */
static int run_to_prompt(BjGame *g, Wallet *w, Tally *t)
{
    for (int i = 0; i < 2000; i++) {
        if (bj_human_turn(g) || bj_human_insurance(g)) return 1;
        if (g->phase == BJ_PH_BETTING && g->table_dirty) return 0;
        step(g, w, 0, t);
    }
    CHECK(!"stuck");
    return 0;
}

typedef struct { const char *name, *shoe; uint32_t plays[8]; int64_t net; int res0, res1; } Scenario;

static void scenario(const Scenario *sc, int64_t credits)
{
    BjConfig cfg;
    fast_config(&cfg);
    heads_up(&cfg);
    BjGame g;
    bj_init(&g, &cfg, 1, 0);
    Wallet w = { credits, 1 };
    Tally t = { 0 };
    stack(&g, sc->shoe);
    step(&g, &w, BTN_DEAL, &t);
    int k = 0;
    while (run_to_prompt(&g, &w, &t)) {
        if (!sc->plays[k]) { fprintf(stderr, "%s: asked for play %d, script has none\n", sc->name, k); CHECK(0); return; }
        if (k == 0 && bj_pending(&g) <= 0) CHECK(!"nothing pending mid-round");
        step(&g, &w, sc->plays[k++], &t);
    }
    int64_t net = w.credits - credits;
    const BjSeat *st = &g.seat[cfg.human_seat];
    printf("  %-22s net %+lld  results %s %s\n", sc->name, (long long)net, bj_result_name(st->hand[0].result),
           st->nhands > 1 ? bj_result_name(st->hand[1].result) : "");
    CHECK_EQ_INT(net, sc->net);
    CHECK_EQ_INT(st->hand[0].result, sc->res0);
    if (sc->res1) CHECK_EQ_INT(st->hand[1].result, sc->res1);
    CHECK_EQ_INT(t.returned - t.staked, net);
    CHECK_EQ_INT(bj_pending(&g), 0);
}

static void scenarios(void)
{
    /* Deal order heads-up: player, up card, player, hole card. The bet is 20. */
    static const Scenario s[] = {
        { "blackjack 3:2", "As 9h Kd 7c", { 0 }, 30, BJ_RES_BLACKJACK, 0 },
        { "insured vs blackjack", "Ts Ah Qd Kc", { BTN_HOLD1, 0 }, 0, BJ_RES_LOSE, 0 },
        { "uninsured vs blackjack", "Ts Ah Qd Kc", { BTN_HOLD2, 0 }, -20, BJ_RES_LOSE, 0 },
        { "even money", "As Ah Kd 9c", { BTN_HOLD1, 0 }, 20, BJ_RES_BLACKJACK, 0 },
        { "split 8s, double both", "8s 6h 8d Tc 3c Td 3d 9s 7h",
          { BTN_HOLD4, BTN_HOLD3, BTN_HOLD3, 0 }, 80, BJ_RES_WIN, BJ_RES_WIN },
        { "surrender", "Ts 9h 6d 8c", { BTN_HOLD5, 0 }, -10, BJ_RES_SURRENDER, 0 },
        { "split aces, 21 not bj", "As 5h Ad Tc Kd 9s 5c", { BTN_HOLD4, 0 }, 20, BJ_RES_WIN, BJ_RES_PUSH },
        { "hit to bust", "Ts 9h 6d 8c Kd", { BTN_HOLD1, 0 }, -20, BJ_RES_BUST, 0 },
        { "dealer busts", "Ts 6h 3d Tc 9c", { BTN_HOLD2, 0 }, 20, BJ_RES_WIN, 0 },
        { "dealer stands soft 17", "Ts Ah 7d 6c", { BTN_HOLD2, BTN_HOLD2, 0 }, 0, BJ_RES_PUSH, 0 },
    };
    for (size_t i = 0; i < sizeof s / sizeof s[0]; i++) scenario(&s[i], 1000);

    /* A double the credits cannot cover is refused and changes nothing. */
    BjConfig cfg;
    fast_config(&cfg);
    heads_up(&cfg);
    BjGame g;
    bj_init(&g, &cfg, 1, 0);
    Wallet w = { 25, 1 };
    Tally t = { 0 };
    stack(&g, "5s 6h 6d Tc Ks 7c");
    step(&g, &w, BTN_DEAL, &t);
    CHECK(run_to_prompt(&g, &w, &t));
    CHECK(!(bj_human_can(&g, &w) & BJ_CAN_DOUBLE));
    long denied = t.denied;
    step(&g, &w, BTN_HOLD3, &t);
    CHECK_EQ_INT(t.denied, denied + 1);
    CHECK_EQ_INT(w.credits, 5);
    CHECK(bj_human_turn(&g));
}

/* ---- long runs ---------------------------------------------------------------- */

static void check_shoe(const BjGame *g)
{
    int count[52] = { 0 };
    for (int i = 0; i < BJ_SHOE_CARDS; i++) if (g->shoe[i] < 52) count[g->shoe[i]]++;
    for (int i = 0; i < 52; i++) CHECK_EQ_INT(count[i], BJ_DECKS);
}

static Tally run(uint64_t seed, long rounds, int bots, int sides, int64_t *credits_out)
{
    BjConfig cfg;
    fast_config(&cfg);
    if (!bots) heads_up(&cfg);
    BjGame g;
    bj_init(&g, &cfg, seed, 0);
    Wallet w = { 1000000000, 1 };
    int64_t start = w.credits;
    Tally t = { 0 };
    if (sides) { g.side[BJ_SIDE_PP] = 5; g.side[BJ_SIDE_213] = 5; g.side[BJ_SIDE_HONEY] = BJ_HONEY_BET; }
    int shoes = 0;
    while (t.rounds < rounds) {
        step(&g, &w, basic_player(&g, &w), &t);
        if (g.shoes != shoes) { shoes = g.shoes; check_shoe(&g); }
        if (g.phase == BJ_PH_BETTING) CHECK(bj_pending(&g) == 0);
        for (int s = 0; s < BJ_SEATS; s++) CHECK(g.seat[s].nhands <= BJ_MAX_SPLIT_HANDS);
    }
    CHECK_EQ_INT(w.credits - start, t.returned - t.staked);
    if (credits_out) *credits_out = w.credits;
    return t;
}

int main(void)
{
    scenarios();

    /* Determinism: the same seed and input give the same event stream. */
    int64_t c1, c2, c3;
    Tally a = run(42, 3000, 1, 1, &c1), b = run(42, 3000, 1, 1, &c2), c = run(43, 3000, 1, 1, &c3);
    CHECK(a.hash == b.hash && c1 == c2);
    CHECK(a.hash != c.hash);
    printf("with bots and side bets: %ld rounds, %ld shuffles, credits conserved\n", a.rounds, a.shuffles);

    long n = 300000;
    const char *e = getenv("BJ_SIM_ROUNDS");
    if (e) n = atol(e);
    double t0 = test_now();
    Tally r = run(7, n, 0, 0, NULL);
    double ret = (double)r.main_paid / (double)r.main_staked;
    /* Six decks, S17, DAS, late surrender, no resplit of aces: basic
       strategy gives back about 99.7%. One hand has an SD of about 1.15
       bets, so the bound below is several SDs wide at the default length. */
    double sd = 1.15 / sqrt((double)r.hands);
    printf("basic strategy: %ld rounds, %ld hands, return %.3f%% (+/- %.3f%%), %.1f s\n", r.rounds, r.hands,
           ret * 100, sd * 100, test_now() - t0);
    CHECK(fabs(ret - 0.997) < 5 * sd + 0.001);
    return test_finish("bj_game");
}
