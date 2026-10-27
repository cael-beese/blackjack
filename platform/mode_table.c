/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* mode_table.c - APP_TABLE: the blackjack table (games/blackjack) at 60 Hz
 * with the app's Wallet, the strategy hint, stats, the hand log, the HONEY
 * POT kept in the save file, and cash-out.
 *
 * Controls are games/blackjack/bj_game.h's, plus:
 *   STICK      up / down = bet more / less, right = the largest bet (the
 *              game reads them in BETTING). On the player's turn, up turns
 *              the strategy hint on or off.
 *   CASH OUT   between rounds: back to the lobby. During a round it is
 *              refused ("FINISH THE ROUND") - the bets are in play.
 *   60 s idle between rounds: attract mode.
 * The table carries on across visits to the lobby: the same shoe, the same
 * regulars, the same bets.
 *
 * Presentation: app_views->table_* (render/table_present.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "games/blackjack/bj_game.h"
#include "platform/app.h"
#include "platform/session.h"
#include "platform/table_view.h"

static struct {
    BjGame   g;
    int      active;
    BjAdvice advice;
    int      advice_key;        /* which hand state the advice is for (-1: none) */
    int64_t  round_paid;        /* the player's returns this round               */
    const char *msg;
    int      msg_ticks;
} T;

static void new_table(void)
{
    BjConfig cfg;
    bj_config_default(&cfg);
    if (!session_settings()->regulars) {
        for (int i = 0; i < BJ_SEATS; i++) cfg.bots[i] = -1;
        for (int i = 0; i < 4; i++) cfg.bench[i] = -1;
    }
    bj_init(&T.g, &cfg, session_next_seed(), session_honey_pot());
    session_set_honey_pot(T.g.pot);
    T.active = 1;
    T.advice_key = -1;
}

static void table_enter(AppCtx *ctx, AppState from)
{
    (void)ctx;
    (void)from;
    if (!T.active) new_table();
    /* The regulars setting changed in the service menu: a new table, but
       only between rounds. */
    int have_bots = 0;
    for (int s = 0; s < BJ_SEATS; s++) have_bots |= T.g.seat[s].kind == BJ_SEAT_BOT;
    if (T.g.phase == BJ_PH_BETTING && have_bots != (session_settings()->regulars != 0)) {
        int64_t bet = T.g.bet, side[BJ_SIDE_N];
        memcpy(side, T.g.side, sizeof side);
        new_table();
        T.g.bet = bet;
        memcpy(T.g.side, side, sizeof side);
    }
}

static void say(const char *m)
{
    T.msg = m;
    T.msg_ticks = 150;
}

static void log_round(const BjGame *g)
{
    FILE *f = session_hand_log();
    if (!f) return;
    const BjSeat *st = &g->seat[g->cfg.human_seat];
    char buf[512];
    int n = snprintf(buf, sizeof buf, "%lld TABLE %016llx #%d dealer", (long long)time(NULL),
                     (unsigned long long)g->shoe_seed, g->rounds);
    char cs[3];
    for (int i = 0; i < g->ndealer && n < (int)sizeof buf - 8; i++) n += snprintf(buf + n, sizeof buf - (size_t)n, " %s", card_str(g->dealer[i], cs));
    for (int h = 0; h < st->nhands && n < (int)sizeof buf - 40; h++) {
        n += snprintf(buf + n, sizeof buf - (size_t)n, " | hand%d bet %lld", h + 1, (long long)st->hand[h].bet);
        for (int i = 0; i < st->hand[h].n && n < (int)sizeof buf - 8; i++)
            n += snprintf(buf + n, sizeof buf - (size_t)n, " %s", card_str(st->hand[h].c[i], cs));
        n += snprintf(buf + n, sizeof buf - (size_t)n, " %s", bj_result_name(st->hand[h].result));
    }
    if (n < (int)sizeof buf - 40)
        snprintf(buf + n, sizeof buf - (size_t)n, " | staked %lld returned %lld\n", (long long)st->staked,
                 (long long)st->returned);
    fputs(buf, f);
    fflush(f);
}

static void count(const BjGame *g, const GameEvent *e)
{
    Stats *st = session_stats();
    int hs = g->cfg.human_seat;
    switch (e->type) {
    case EV_BJ_POT:
        session_set_honey_pot(e->v);
        return;
    case EV_BJ_STAKE:
        if (e->a != hs) return;
        if (e->b == BJ_BET_MAIN) { st->rounds++; st->wagered += (uint64_t)e->v; T.round_paid = 0; }
        else if (e->b == BJ_BET_DOUBLE) { st->doubles++; st->wagered += (uint64_t)e->v; }
        else if (e->b == BJ_BET_SPLIT) { st->splits++; st->wagered += (uint64_t)e->v; }
        else if (e->b == BJ_BET_INSURANCE) st->insured++;
        else if (e->b >= 0 && e->b < SAVE_SIDES) st->side_bet[e->b] += (uint64_t)e->v;
        break;
    case EV_BJ_RESULT: {
        if (e->a != hs) return;
        int res = e->b >> 4;
        st->hands++;
        st->paid += (uint64_t)e->v;
        T.round_paid += e->v;
        if (res == BJ_RES_WIN || res == BJ_RES_BLACKJACK) st->wins++;
        else if (res == BJ_RES_PUSH) st->pushes++;
        else st->losses++;
        if (res == BJ_RES_BLACKJACK) st->blackjacks++;
        if (res == BJ_RES_SURRENDER) st->surrenders++;
        break;
    }
    case EV_BJ_SIDE: {
        int k = e->b & 15;
        if (e->a != hs || e->v <= 0 || k >= SAVE_SIDES) return;
        st->side_paid[k] += (uint64_t)e->v;
        st->side_hits[k]++;
        T.round_paid += e->v;
        break;
    }
    case EV_BJ_INS_RESULT:
        if (e->a == hs) T.round_paid += e->v;
        break;
    case EV_BJ_JACKPOT:
        if (e->a == hs) st->jackpots++;
        break;
    case EV_BJ_ROUND:
        if (T.round_paid > st->biggest_win) st->biggest_win = T.round_paid;
        log_round(g);
        break;
    default:
        return;
    }
    session_stats_changed();
}

/* Development only (screenshots, celebrations, the README): BJ_TEST_SHOE="7h
 * 9c ..." puts these cards on top of every new shoe, in dealing order. It
 * exists in the desktop build alone; the cabinet's DRM build has no such
 * code, so nothing outside the game can ever choose its cards there. */
static void test_stack(BjGame *g)
{
#if defined(BPL_PLATFORM_DESKTOP)
    const char *e = getenv("BJ_TEST_SHOE");
    Card c[64];
    int n = e ? cards_parse(e, c, 64) : -1;
    for (int i = 0; i < n && g->shoe_pos + i < BJ_SHOE_CARDS; i++) g->shoe[g->shoe_pos + i] = c[i];
#else
    (void)g;
#endif
}

/* The advice for the player's current hand, worked out once per hand state. */
static void update_advice(const AppCtx *ctx)
{
    BjGame *g = &T.g;
    if (!bj_human_turn(g)) { T.advice_key = -1; return; }
    const BjSeat *st = &g->seat[g->cfg.human_seat];
    const BjHand *h = &st->hand[st->cur];
    unsigned can = bj_human_can(g, &ctx->wallet);
    int key = (int)can << 12 | st->cur << 8 | st->nhands << 4 | h->n;
    if (key == T.advice_key) return;
    bj_advise(h->c, h->n, bj_up_card(g), can, &T.advice);
    T.advice_key = key;
}

static void table_tick(AppCtx *ctx, const InputFrame *in)
{
    BjGame *g = &T.g;
    InputFrame f = *in;
    uint32_t p = in->pressed;
    if (T.msg_ticks > 0 && --T.msg_ticks == 0) T.msg = NULL;

    /* The stick bets (panel.h): right = the largest bet. Up and down are the
       game's own bet more / less; on the player's turn up is the hint. */
    if (p & BTN_RIGHT) f.pressed |= BTN_BET_MAX;
    if (bj_human_turn(g) && (p & (BTN_UP | BTN_BET_ONE))) {
        session_set_hint_on(!session_hint_on());
        f.pressed &= ~(uint32_t)(BTN_UP | BTN_BET_ONE);
    }
    f.pressed &= ~(uint32_t)(BTN_LEFT | BTN_RIGHT);

    if (p & BTN_CASH_OUT) {
        f.pressed &= ~BTN_CASH_OUT;
        if (g->phase == BJ_PH_BETTING) {
            app_request(ctx, APP_MENU);
            return;
        }
        say("FINISH THE ROUND FIRST - THE BETS ARE IN PLAY");
    }

    int n0 = ctx->events.n;
    bj_tick(g, &f, &ctx->wallet, &ctx->events);
    for (int i = n0; i < ctx->events.n; i++) {
        count(g, &ctx->events.e[i]);
        if (ctx->events.e[i].type == EV_BJ_SHUFFLE) test_stack(g);
    }
    update_advice(ctx);

    /* What the file must say if the power goes now (session.h). */
    session_set_pending(PENDING_TABLE, bj_pending(g));

    if (g->phase == BJ_PH_BETTING && session_idle_ticks() >= SESSION_IDLE_ATTRACT_TICKS)
        app_request(ctx, APP_ATTRACT);
}

static void fill(const AppCtx *ctx, TableViewInfo *v)
{
    memset(v, 0, sizeof *v);
    v->game = &T.g;
    v->credits = ctx->wallet.credits;
    v->denom_cents = session_settings()->denom_cents;
    v->hint_on = session_hint_on();
    v->your_turn = bj_human_turn(&T.g);
    v->can = bj_human_can(&T.g, &ctx->wallet);
    v->advice = (v->your_turn && T.advice_key >= 0) ? &T.advice : NULL;
    v->insurance = bj_human_insurance(&T.g);
    v->message = T.msg;
    v->pressed = ctx->input.pressed;
}

static void table_present_update(const AppCtx *ctx, const GameEvent *ev, int nev, float dt)
{
    TableViewInfo v;
    fill(ctx, &v);
    app_common_update(ctx->state, ev, nev, dt);
    if (app_views->table_update) app_views->table_update(&v, ev, nev, dt);
}

static void table_present_draw(const AppCtx *ctx)
{
    TableViewInfo v;
    fill(ctx, &v);
    if (app_views->table_view) app_views->table_view(&v, ctx->time);
}

const AppMode mode_table = {
    .name = "table",
    .enter = table_enter,
    .tick = table_tick,
    .present_update = table_present_update,
    .present_draw = table_present_draw,
};
