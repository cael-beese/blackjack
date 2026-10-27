/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#include "games/blackjack/bj_bots.h"

#include "games/blackjack/bj_strategy.h"

static const char *const g_name[BOT_N] = { "STINGER", "HONEY", "BUZZ", "QUEENIE", "DRONE", "DEMO" };
static const char *const g_style[BOT_N] = { "THE PRO", "HUNCHES", "HOTHEAD", "HIGH ROLLER", "THE MIMIC", "" };
static const int64_t g_bank[BOT_N] = { 3000, 1500, 2000, 8000, 1500, 1000000 };

const char *bj_bot_name(int b) { return (b >= 0 && b < BOT_N) ? g_name[b] : "?"; }
const char *bj_bot_style(int b) { return (b >= 0 && b < BOT_N) ? g_style[b] : ""; }
int64_t bj_bot_bankroll(int b) { return (b >= 0 && b < BOT_N) ? g_bank[b] : 0; }

/* The bet ladder every seat uses (bj_game.h keeps the same one). */
static const int64_t g_ladder[] = { 10, 20, 50, 100, 200, 500, 1000 };
#define NLADDER ((int)(sizeof g_ladder / sizeof g_ladder[0]))

static int64_t clamp_bet(int64_t b, int64_t min, int64_t max, int64_t bank)
{
    /* The highest ladder step that is <= b and inside the limits. */
    int64_t best = 0;
    for (int i = 0; i < NLADDER; i++)
        if (g_ladder[i] <= b && g_ladder[i] >= min && g_ladder[i] <= max && g_ladder[i] <= bank)
            best = g_ladder[i];
    return best;
}

static int64_t pick(Rng *r, const int64_t *opts, int n) { return opts[rng_below(r, (uint32_t)n)]; }

int64_t bj_bot_bet(int bot, const BjBotMemory *m, int64_t min, int64_t max, Rng *r, int64_t side[BJ_SIDE_N])
{
    for (int i = 0; i < BJ_SIDE_N; i++) side[i] = 0;
    int64_t want = min;
    switch (bot) {
    case BOT_STINGER: want = 50; break;
    case BOT_HONEY: {
        static const int64_t o[] = { 10, 20, 20, 50 };
        want = pick(r, o, 4);
        if (rng_below(r, 3)) side[BJ_SIDE_PP] = 10;
        break;
    }
    case BOT_BUZZ:
        /* Lets a win ride once, then drops back. */
        want = (m->last_net > 0 && m->last_bet && m->last_bet <= 50) ? m->last_bet * 2 : 20;
        side[BJ_SIDE_HONEY] = BJ_HONEY_BET;
        if (rng_below(r, 2)) side[BJ_SIDE_213] = 5;
        break;
    case BOT_QUEENIE: {
        static const int64_t o[] = { 100, 200, 200, 500 };
        want = pick(r, o, 4);
        side[BJ_SIDE_PP] = 25;
        side[BJ_SIDE_213] = 25;
        side[BJ_SIDE_HONEY] = BJ_HONEY_BET;
        break;
    }
    case BOT_DRONE: want = 20; break;
    case BOT_DEMO: {
        static const int64_t o[] = { 20, 50, 50, 100 };
        want = pick(r, o, 4);
        if (!rng_below(r, 3)) side[BJ_SIDE_213] = 10;
        if (!rng_below(r, 2)) side[BJ_SIDE_HONEY] = BJ_HONEY_BET;
        break;
    }
    }
    int64_t bet = clamp_bet(want, min, max, m->bank);
    if (bet == 0) { for (int i = 0; i < BJ_SIDE_N; i++) side[i] = 0; return 0; }
    /* Side bets only from what is left after the main bet and a double. */
    int64_t spare = m->bank - 2 * bet;
    for (int i = 0; i < BJ_SIDE_N; i++) {
        if (side[i] > spare) side[i] = 0;
        spare -= side[i];
    }
    return bet;
}

int bj_bot_insure(int bot, const Card *hand, int n)
{
    (void)hand; (void)n;
    /* Insurance is a bad bet without a count; only the high roller takes it. */
    return bot == BOT_QUEENIE;
}

int bj_bot_play(int bot, const Card *hand, int n, Card up, unsigned can)
{
    BjTotal t = bj_total(hand, n);
    if (t.total >= 21) return BJ_ACT_STAND;
    switch (bot) {
    case BOT_HONEY:
        /* Never busts: stands on any 12 or more, doubles 11, splits aces and eights. */
        if (bj_is_pair(hand, n) && (can & BJ_CAN_SPLIT) &&
            (card_rank(hand[0]) == RANK_A || card_rank(hand[0]) == RANK_8))
            return BJ_ACT_SPLIT;
        if (n == 2 && t.total == 11 && !t.soft && (can & BJ_CAN_DOUBLE)) return BJ_ACT_DOUBLE;
        if (t.soft) return t.total >= 18 ? BJ_ACT_STAND : BJ_ACT_HIT;
        return t.total >= 12 ? BJ_ACT_STAND : BJ_ACT_HIT;
    case BOT_DRONE:
        return t.total >= 17 ? BJ_ACT_STAND : BJ_ACT_HIT;
    case BOT_BUZZ: {
        /* Basic strategy, plus a double on every soft hand it can. */
        BjAdvice a;
        bj_advise(hand, n, up, can, &a);
        if (t.soft && n == 2 && a.legal[BJ_ACT_DOUBLE] && t.total <= 19 && a.best == BJ_ACT_HIT)
            return BJ_ACT_DOUBLE;
        return a.best;
    }
    default: {
        BjAdvice a;
        bj_advise(hand, n, up, can, &a);
        return a.best == BJ_ACT_NONE ? BJ_ACT_STAND : a.best;
    }
    }
}

int bj_bot_think(int bot, int act, Rng *r)
{
    int base = 26 + (int)rng_below(r, 24);
    if (bot == BOT_STINGER) base = 18 + (int)rng_below(r, 10);     /* quick, sure */
    if (bot == BOT_HONEY) base += 12;                               /* a moment of doubt */
    if (act == BJ_ACT_DOUBLE || act == BJ_ACT_SPLIT) base += 14;
    return base;
}
