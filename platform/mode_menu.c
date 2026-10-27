/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* mode_menu.c - APP_MENU: the lobby in front of the table, with the arcade
 * buttons alone:
 *   HOLD 1 (HIT)        take a seat at the table
 *   HOLD 2 (STAND)      strategy hint on / off
 *   HOLD 3 (DOUBLE)     the CONTROLS page
 *   stick, BET ONE      move the choice;  DEAL, START, OK: choose it
 *   BACK                attract;  60 s without input: attract
 * The CONTROLS page (panel.h) draws the whole panel with what every button
 * does and lights the one pressed; CASH OUT goes back; DEAL held MAP_HOLD
 * ticks opens BUTTON MAP (service.h), which comes back to the page. It opens
 * by itself the first time a person reaches the lobby. */
#include "platform/app.h"
#include "platform/panel.h"
#include "platform/service.h"
#include "platform/session.h"
#include "platform/table_view.h"

#define MAP_HOLD  120           /* 2 s */

static struct {
    int sel;
    int controls;
    int deal_ticks;             /* CONTROLS: DEAL held this long         */
    int from_map;               /* went to BUTTON MAP from CONTROLS      */
} M;

static void menu_enter(AppCtx *ctx, AppState from)
{
    (void)ctx;
    M.sel = MENU_PLAY;
    M.controls = 0;
    M.deal_ticks = 0;
    if (from == APP_SERVICE && M.from_map) M.controls = 1;     /* see the new map */
    M.from_map = 0;
    if (from == APP_ATTRACT && panel_controls_due()) {
        M.controls = 1;
        panel_controls_shown();
    }
}

static void choose(AppCtx *ctx, int sel)
{
    switch (sel) {
    case MENU_PLAY: app_request(ctx, APP_TABLE); break;
    case MENU_HINT: session_set_hint_on(!session_hint_on()); break;
    case MENU_CONTROLS: M.controls = 1; break;
    }
}

static void menu_tick(AppCtx *ctx, const InputFrame *in)
{
    uint32_t p = in->pressed;
    if (M.controls) {
        /* Every other button only lights up on the page. */
        M.deal_ticks = (in->down & BTN_DEAL) ? M.deal_ticks + 1 : 0;
        if (M.deal_ticks >= MAP_HOLD) {
            M.deal_ticks = 0;
            M.from_map = 1;
            service_request_map(ctx);
            return;
        }
        if (p & (BTN_CASH_OUT | BTN_BACK)) M.controls = 0;
        else if (session_idle_ticks() >= SESSION_IDLE_ATTRACT_TICKS) app_request(ctx, APP_ATTRACT);
        return;
    }
    for (int i = 0; i < MENU_NSEL; i++)
        if (p & (BTN_HOLD1 << i)) { M.sel = i; choose(ctx, i); return; }
    if (p & (BTN_DOWN | BTN_RIGHT | BTN_BET_ONE)) M.sel = (M.sel + 1) % MENU_NSEL;
    if (p & (BTN_UP | BTN_LEFT)) M.sel = (M.sel + MENU_NSEL - 1) % MENU_NSEL;
    if (p & (BTN_DEAL | BTN_BET_MAX | BTN_OK | BTN_START))
        choose(ctx, M.sel);
    else if (p & BTN_BACK)
        app_request(ctx, APP_ATTRACT);
    else if (session_idle_ticks() >= SESSION_IDLE_ATTRACT_TICKS)
        app_request(ctx, APP_ATTRACT);
}

static void fill(const AppCtx *ctx, MenuViewInfo *v)
{
    const Settings *s = session_settings();
    v->sel = M.sel;
    v->controls = M.controls;
    v->down = ctx->input.down;
    v->map_hold = (float)M.deal_ticks / MAP_HOLD;
    v->hint_on = session_hint_on();
    v->credits = ctx->wallet.credits;
    v->denom_cents = s->denom_cents;
    v->coin_credits = settings_coin_credits(s);
    v->pot = session_honey_pot();
    v->message = NULL;
}

static void menu_present_update(const AppCtx *ctx, const GameEvent *ev, int nev, float dt)
{
    MenuViewInfo v;
    fill(ctx, &v);
    app_common_update(ctx->state, ev, nev, dt);
    if (app_views->menu_update) app_views->menu_update(&v, ev, nev, dt);
}

static void menu_present_draw(const AppCtx *ctx)
{
    MenuViewInfo v;
    fill(ctx, &v);
    if (app_views->menu_view) app_views->menu_view(&v, ctx->time);
}

const AppMode mode_menu = {
    .name = "menu",
    .enter = menu_enter,
    .tick = menu_tick,
    .present_update = menu_present_update,
    .present_draw = menu_present_draw,
};
