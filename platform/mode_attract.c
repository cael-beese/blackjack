/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* mode_attract.c - APP_ATTRACT: the machine shows itself off between players.
 *
 *   TITLE (8 s): the neon title, the rules and side bets, "PRESS DEAL"
 *   DEMO (about 50 s): the real table with every seat played by the bees -
 *        the player's seat by the DEMO bot, which plays the strategy hint
 * then round again. Any button (or a touch) goes to the lobby; a COIN is
 * credited as well (session.c).
 *
 * Honesty (CONTRACT section 5): the demo is the real game, run by bj_tick
 * exactly as in play, on its own seed from the session's seed stream. Every
 * shoe is a real Fisher-Yates shuffle; nothing is chosen, filtered or
 * replayed to look good. The demo never touches the player's credits or the
 * HONEY POT on the save file: its pot is a copy. */
#include <string.h>

#include "games/blackjack/bj_game.h"
#include "platform/app.h"
#include "platform/session.h"
#include "platform/table_view.h"

#define TITLE_TICKS (8 * 60)
#define DEMO_TICKS  (50 * 60)

static struct {
    int    phase, t;
    Rng    rng;             /* attract's own stream: seeds for the demos */
    Wallet w;               /* the demo's wallet (no seat uses it)       */
    BjGame g;
} A;

static void start_title(void)
{
    A.phase = ATTRACT_TITLE;
    A.t = 0;
}

static void start_demo(void)
{
    BjConfig cfg;
    bj_config_default(&cfg);
    cfg.demo = 1;
    if (!session_settings()->regulars) {
        for (int i = 0; i < BJ_SEATS; i++) cfg.bots[i] = -1;
        for (int i = 0; i < 4; i++) cfg.bench[i] = -1;
    }
    bj_init(&A.g, &cfg, rng_next(&A.rng), session_honey_pot());
    A.w.credits = 0;
    A.w.denom = 1;
    A.phase = ATTRACT_DEMO;
    A.t = 0;
}

static void attract_enter(AppCtx *ctx, AppState from)
{
    (void)ctx;
    if (from == APP_SERVICE && A.phase != ATTRACT_TITLE) return;    /* resume the demo */
    rng_seed(&A.rng, session_next_seed());
    start_title();
}

static void attract_tick(AppCtx *ctx, const InputFrame *in)
{
    if ((in->pressed & ~(uint32_t)BTN_DEBUG) || in->touch) {
        app_request(ctx, APP_MENU);
        return;
    }
    A.t++;
    switch (A.phase) {
    case ATTRACT_TITLE:
        if (A.t >= TITLE_TICKS) start_demo();
        break;
    case ATTRACT_DEMO:
        bj_tick(&A.g, NULL, &A.w, &ctx->events);
        /* Leave between rounds, never in the middle of one. */
        if (A.t >= DEMO_TICKS && A.g.phase == BJ_PH_BETTING && A.g.timer > 60) start_title();
        break;
    }
}

static void fill_table(const AppCtx *ctx, TableViewInfo *v)
{
    memset(v, 0, sizeof *v);
    v->game = &A.g;
    v->credits = ctx->wallet.credits;
    v->hint_on = 1;
    v->demo = 1;
}

static void attract_present_update(const AppCtx *ctx, const GameEvent *ev, int nev, float dt)
{
    AttractViewInfo av = { A.phase, ctx->wallet.credits, A.phase == ATTRACT_DEMO ? A.g.pot : session_honey_pot() };
    app_common_update(ctx->state, ev, nev, dt);
    if (app_views->attract_update) app_views->attract_update(&av, ev, nev, dt);
    if (A.phase == ATTRACT_DEMO && app_views->table_update) {
        TableViewInfo v;
        fill_table(ctx, &v);
        app_views->table_update(&v, ev, nev, dt);
    }
}

static void attract_present_draw(const AppCtx *ctx)
{
    AttractViewInfo av = { A.phase, ctx->wallet.credits, A.phase == ATTRACT_DEMO ? A.g.pot : session_honey_pot() };
    if (A.phase == ATTRACT_DEMO && app_views->table_view) {
        TableViewInfo v;
        fill_table(ctx, &v);
        app_views->table_view(&v, ctx->time);
    }
    if (app_views->attract_view) app_views->attract_view(&av, ctx->time);
}

const AppMode mode_attract = {
    .name = "attract",
    .enter = attract_enter,
    .tick = attract_tick,
    .present_update = attract_present_update,
    .present_draw = attract_present_draw,
};
