/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* lounge_view.c - the lobby in front of the table, and attract mode's title
 * card. See views.h.
 *
 * Lobby: the marquee; the table's card (its rules, the return with perfect
 * play, the limits and the bee regulars who sit there); the side bets with
 * their paytables and the live HONEY POT; three choices on HOLD 1-3 (take a
 * seat, the strategy hint, the CONTROLS page, controls_view.c). Key caps and
 * prompts carry the panel icon (ui_panel_glyph) showing where their button
 * is. Attract title: the marquee, the HONEY POT, a blackjack fanned on each
 * side and "PRESS DEAL". The demo itself is the real table, drawn by
 * table_present.c with its own PRESS DEAL bar. */
#include "render/views.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/app.h"
#include "platform/screen.h"
#include "platform/session.h"
#include "render/holdem_art.h"
#include "render/render.h"
#if defined(BPL_HAVE_AUDIO)
#include "audio.h"
#else
enum { SFX_MENU_MOVE, SFX_MENU_SELECT, SFX_HOLD_ON, SFX_HOLD_OFF, SFX_NEON_FLICKER, SFX_CASH_OUT, SFX_BUTTON };
#endif

static struct {
    int      inited;
    Marquee  mq;
    BulbRing bulbs;
    UiButton opt[MENU_NSEL];
    Meter    credits;
    int      sel, hint_on;
    double   t;
    float    hint_flash;
    /* attract */
    int      phase;
    float    phase_t;
    Marquee  amq;
    BulbRing abulbs;
} L;

static void lounge_init(void)
{
    if (L.inited) return;
    L.inited = 1;
    marquee_init(&L.mq, 1);
    marquee_init(&L.amq, 1);
    bulbs_init(&L.bulbs, (Rectangle){ 13, 13, PLAY_W - 26, PLAY_H - 26 }, 36);
    bulbs_init(&L.abulbs, (Rectangle){ 13, 13, PLAY_W - 26, PLAY_H - 26 }, 36);
    bulbs_set(&L.bulbs, BULBS_IDLE, 8);
    bulbs_set(&L.abulbs, BULBS_CHASE, 12);
    L.phase = -1;
}

static void msfx(int id, float vol, float pitch)
{
    dv_sfx(id, vol, pitch, 0);
}

static Card card_of(const char *s)
{
    Card c = CARD_NONE;
    if (s && *s) card_parse(s, &c);
    return c;
}

static TextStyle tstyle(Color c, TextAlign a)
{
    TextStyle t;
    memset(&t, 0, sizeof t);
    t.color = c;
    t.align = a;
    return t;
}

static void text_at(FontId f, const char *s, float x, float cy, float size, Color c, TextAlign a)
{
    TextStyle t = tstyle(c, a);
    text_draw_ex(f, s, x, cy - size * 0.56f, size, &t);
}

/* A fan of cards, each turning face up in turn as appear goes 0 -> 1. */
static void fan(const char *const *cards, int n, float cx, float cy, float r, float spread, float scale, float appear, double time,
                int seed)
{
    for (int k = 0; k < n; k++) {
        float kk = clampf(appear * 1.6f - 0.15f * (float)k, 0, 1);
        if (kk <= 0) continue;
        float e = ease(EASE_OUT_BACK, kk);
        float a = ((float)k - (float)(n - 1) * 0.5f) * spread * e + 0.02f * sinf((float)time * 1.3f + (float)k);
        CardPose p = { 0 };
        p.x = cx + sinf(a) * r;
        p.y = cy - cosf(a) * r + r + (1 - e) * 50;
        p.rot = a;
        p.scale = scale;
        p.flip = ease(EASE_INOUT_CUBIC, clampf(kk * 1.4f - 0.3f, 0, 1));
        card_shimmer(&p, time, k + seed);
        card_draw(card_of(cards[k]), CARD_M, &p);
    }
}

/* ---- the lobby ------------------------------------------------------------------ */

void menu_view_update(const MenuViewInfo *v, const GameEvent *ev, int nev, float dt)
{
    lounge_init();
    if (v->controls) {
        /* The CONTROLS page: a click for every button, so a press is heard as well as seen. */
        for (int i = 0; i < nev; i++)
            if (ev[i].type == APP_EV_BUTTON) msfx(SFX_BUTTON, 0.6f, 1.0f);
        L.t += dt;
        return;
    }
    L.sel = v->sel;
    if (v->hint_on != L.hint_on) { L.hint_on = v->hint_on; L.hint_flash = 1; }
    for (int i = 0; i < nev; i++) {
        const GameEvent *e = &ev[i];
        if (e->type == APP_EV_STATE && e->a == APP_TABLE && e->b == APP_MENU) msfx(SFX_CASH_OUT, 0.8f, 1);
        if (e->type != APP_EV_BUTTON) continue;
        uint32_t bit = 1u << e->a;
        for (int k = 0; k < MENU_NSEL; k++)
            if (bit & (BTN_HOLD1 << k)) ui_button_press(&L.opt[k]);
        if (bit & (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT | BTN_BET_ONE)) msfx(SFX_MENU_MOVE, 0.8f, 1.0f + 0.05f * (float)v->sel);
        else if (bit & BTN_HOLD2) msfx(v->hint_on ? SFX_HOLD_ON : SFX_HOLD_OFF, 0.8f, 1);
        else if (bit & (BTN_HOLD1 | BTN_HOLD3 | BTN_DEAL | BTN_OK | BTN_START | BTN_BET_MAX)) {
            ui_button_press(&L.opt[v->sel]);
            msfx(SFX_MENU_SELECT, 1, 1);
        }
    }
    L.t += dt;
    marquee_update(&L.mq, dt);
    if (marquee_take_cue(&L.mq)) msfx(SFX_NEON_FLICKER, 0.25f, 1);
    bulbs_update(&L.bulbs, dt);
    for (int i = 0; i < MENU_NSEL; i++) {
        UiButtonState st = i == v->sel ? BTN_STATE_LIT : BTN_STATE_ON;
        if (i == MENU_HINT && v->hint_on) st = BTN_STATE_LIT;
        ui_button_update(&L.opt[i], st, dt);
    }
    L.hint_flash = fmaxf(0, L.hint_flash - dt * 2);
    if (!L.credits.running && (long long)llround(L.credits.to) != v->credits) {
        if (L.credits.to == 0 && L.credits.shown == 0) meter_set(&L.credits, (double)v->credits);
        else meter_count(&L.credits, (double)v->credits, 0.6f);
    }
    meter_update(&L.credits, dt);
}

static void draw_table_card(const MenuViewInfo *v, double t)
{
    (void)v;
    Rectangle r = { 36, 140, 640, 340 };
    int on = L.sel == MENU_PLAY;
    ui_panel(r, UI_GOLD, on ? 0.7f + 0.2f * sinf((float)t * 3) : 0.3f, 1);
    static const char *const bj[2] = { "As", "Kh" };
    fan(bj, 2, r.x + 130, r.y + 130, 150, 0.32f, 0.82f, 1, t, 0);
    text_neon(FONT_NEON_M, "VEGAS STRIP", r.x + 420, r.y + 40, 30, UI_CYAN, 0.95f, 0.8f);
    text_neon(FONT_NEON_L, "BLACKJACK", r.x + 420, r.y + 88, 56, UI_GOLD, 1, 1);
    static const char *const rules[] = {
        "SIX DECKS, A NEW SHOE AT THE CUT CARD",
        "BLACKJACK PAYS 3 TO 2",
        "DEALER STANDS ON ALL 17S, PEEKS FOR BLACKJACK",
        "DOUBLE ANY TWO CARDS, AND AFTER A SPLIT",
        "SPLIT TO FOUR HANDS   -   LATE SURRENDER",
        "INSURANCE PAYS 2 TO 1",
    };
    for (int i = 0; i < 6; i++)
        text_at(FONT_UI_M, rules[i], r.x + 420, r.y + 136 + (float)i * 24, 19, (Color){ 236, 226, 210, 255 }, ALIGN_CENTER);
    text_at(FONT_DISP_S, "99.6% RETURN WITH PERFECT PLAY", r.x + 420, r.y + 290, 19, UI_HONEY, ALIGN_CENTER);
    text_at(FONT_UI_M, "BETS 10 TO 1,000", r.x + 420, r.y + 316, 18, (Color){ 170, 220, 240, 255 }, ALIGN_CENTER);
    /* The regulars, under the cards. */
    static const int cast[4] = { AV_STINGER, AV_HONEY, AV_BUZZ, AV_QUEENIE };
    static const char *const names[4] = { "STINGER", "HONEY", "BUZZ", "QUEENIE" };
    text_at(FONT_UI_S, "AT THE TABLE TONIGHT", r.x + 130, r.y + 250, 14, (Color){ 190, 170, 150, 255 }, ALIGN_CENTER);
    for (int i = 0; i < 4; i++) {
        float x = r.x + 40 + (float)i * 60, y = r.y + 290 + 2 * sinf((float)t * 1.6f + (float)i);
        dv_avatar(cast[i], x, y, 54, t);
        text_at(FONT_UI_S, names[i], x, y + 36, 12, hart_rig(cast[i])->theme, ALIGN_CENTER);
    }
}

static void paytable(float x, float y, float w, const char *title, Color col, const char *const *names, const int *pays, int n)
{
    char b[16];
    text_neon(FONT_NEON_M, title, x + w * 0.5f, y, 24, col, 1, 0.8f);
    for (int i = 0; i < n; i++) {
        float ry = y + 34 + (float)i * 23;
        text_at(FONT_UI_M, names[i], x + 8, ry, 17, (Color){ 230, 220, 210, 255 }, ALIGN_LEFT);
        snprintf(b, sizeof b, "%d:1", pays[i]);
        text_at(FONT_DISP_S, b, x + w - 8, ry, 16, col, ALIGN_RIGHT);
    }
}

static void draw_side_bets(const MenuViewInfo *v, double t)
{
    Rectangle r = { 692, 140, 552, 340 };
    ui_panel(r, UI_MAGENTA, 0.3f, 1);
    text_at(FONT_DISP_S, "SIDE BETS", r.x + r.width * 0.5f, r.y + 22, 18, (Color){ 220, 200, 230, 255 }, ALIGN_CENTER);
    static const char *const ppn[3] = { "PERFECT PAIR", "COLOURED PAIR", "MIXED PAIR" };
    static const int ppp[3] = { 25, 12, 6 };
    paytable(r.x + 16, r.y + 58, 250, "PERFECT PAIRS", UI_MAGENTA, ppn, ppp, 3);
    static const char *const tn[5] = { "SUITED TRIPS", "STRAIGHT FLUSH", "THREE OF A KIND", "STRAIGHT", "FLUSH" };
    static const int tp[5] = { 100, 40, 30, 10, 5 };
    paytable(r.x + 286, r.y + 58, 250, "21+3", UI_CYAN, tn, tp, 5);
    text_at(FONT_UI_S, "YOUR FIRST TWO CARDS", r.x + 141, r.y + 158, 13, (Color){ 160, 150, 160, 255 }, ALIGN_CENTER);
    text_at(FONT_UI_S, "YOUR TWO CARDS + THE DEALER'S UP CARD", r.x + 411, r.y + 204, 13, (Color){ 160, 150, 160, 255 }, ALIGN_CENTER);
    /* The HONEY POT: live. */
    Rectangle h = { r.x + 16, r.y + 222, r.width - 32, 104 };
    float k = 0.5f + 0.5f * sinf((float)t * 2.2f);
    gfx_spr_rot(sprite(SPR_GLOW), h.x + h.width * 0.5f, h.y + 30, h.width, 90, 0, gfx_add(UI_AMBER, 0.1f + 0.08f * k));
    text_neon(FONT_NEON_M, "HONEY POT", h.x + 110, h.y + 20, 28, UI_AMBER, 0.9f + 0.1f * k, 1);
    char b[48], n1[24];
    TextStyle ts = tstyle((Color){ 255, 246, 196, 255 }, ALIGN_RIGHT);
    ts.color2 = (Color){ 240, 150, 30, 255 };
    ts.glow = UI_AMBER;
    ts.glow_k = 0.6f;
    text_draw_ex(FONT_DISP_M, dv_chips(v->pot, n1, sizeof n1), h.x + h.width - 8, h.y + 20 - 36 * 0.56f, 36, &ts);
    snprintf(b, sizeof b, "%d CREDITS A HAND, ON SEVENS:", BJ_HONEY_BET);
    text_at(FONT_UI_M, b, h.x + 8, h.y + 52, 16, (Color){ 230, 210, 180, 255 }, ALIGN_LEFT);
    text_at(FONT_UI_M, "THREE SUITED 7S WIN THE POT   -   THREE 7S SAME COLOUR 500:1   -   THREE 7S 100:1", h.x + 8, h.y + 74, 15,
            (Color){ 255, 200, 120, 255 }, ALIGN_LEFT);
    text_at(FONT_UI_M, "TWO SUITED 7S 50:1   -   TWO 7S 25:1   -   A 7 FIRST 3:1", h.x + 8, h.y + 94, 15,
            (Color){ 255, 200, 120, 255 }, ALIGN_LEFT);
}

void menu_view_draw(const MenuViewInfo *v, double time)
{
    lounge_init();
    double t = L.t;
    (void)time;
    if (v->controls) {
        controls_view_draw(v, t);
        return;
    }
    render_begin();
    ui_background(t, 1);
    draw_table_card(v, t);
    draw_side_bets(v, t);

    /* The three choices, each on its own button. */
    static const char *const names[MENU_NSEL] = { "TAKE A SEAT", "STRATEGY HINT", "CONTROLS" };
    static const Color cols[MENU_NSEL] = { { 255, 210, 90, 255 }, { 40, 230, 255, 255 }, { 200, 170, 255, 255 } };
    for (int i = 0; i < MENU_NSEL; i++) {
        Rectangle r = { 36 + (float)i * 410, 498, 390, 50 };
        char s[48];
        if (i == MENU_HINT) snprintf(s, sizeof s, "STRATEGY HINT %s", v->hint_on ? "ON" : "OFF");
        else snprintf(s, sizeof s, "%s", names[i]);
        UiButtonState st = i == v->sel ? BTN_STATE_LIT : BTN_STATE_ON;
        if (i == MENU_HINT && v->hint_on) st = BTN_STATE_LIT;
        ui_button_draw_key(&L.opt[i], r, s, i == v->sel ? cols[i] : (Color){ 150, 140, 170, 255 }, st, t, BTN_HOLD1 << i);
    }

    float pulse = 0.6f + 0.4f * sinf((float)t * 4);
    char s[96];
    if (v->sel == MENU_CONTROLS) snprintf(s, sizeof s, "PRESS DEAL TO SEE THE CONTROLS");
    else if (v->sel == MENU_HINT) snprintf(s, sizeof s, "PRESS DEAL TO TURN THE HINT %s", v->hint_on ? "OFF" : "ON");
    else snprintf(s, sizeof s, "PRESS DEAL TO TAKE A SEAT");
    const float dgh = 26, dgw = ui_panel_glyph_w(dgh), stw = text_width(FONT_NEON_M, s, 34, 0);
    float sx0 = 640 - (dgw + 16 + stw) * 0.5f;
    ui_panel_glyph(sx0, 584 - dgh * 0.5f, dgh, BTN_DEAL, UI_GOLD, pulse);
    text_neon(FONT_NEON_M, s, sx0 + dgw + 16 + stw * 0.5f, 584, 34, UI_MAGENTA, pulse, 1.0f);

    meter_draw(&L.credits, (Rectangle){ 30, 618, 232, 76 }, "CREDITS", UI_CYAN);
    TextStyle ts = tstyle((Color){ 170, 160, 175, 255 }, ALIGN_CENTER);
    text_draw_ex(FONT_UI_M, "HIT: SEAT    STAND: HINT    DOUBLE: CONTROLS    DEAL: CHOOSE    COIN: CREDITS", 760, 636, 19, &ts);
    if (v->credits <= 0) {
        snprintf(s, sizeof s, "INSERT COIN  -  %lld CREDITS PER COIN", (long long)v->coin_credits);
        ts.color = UI_CYAN;
        text_draw_ex(FONT_UI_M, s, 760, 662, 20, &ts);
    } else if (v->message) {
        ts.color = UI_AMBER;
        text_draw_ex(FONT_UI_M, v->message, 760, 662, 20, &ts);
    } else {
        ts.color = (Color){ 120, 112, 128, 255 };
        text_draw_ex(FONT_UI_S, "FREE PLAY  -  FOR AMUSEMENT ONLY  -  REAL SHUFFLES, REAL ODDS", 760, 664, 17, &ts);
    }
    bulbs_draw(&L.bulbs);
    marquee_draw(&L.mq, 640, 72, 0.86f);
    render_end();
    dv_fill_account(DV_FILL_MENU);
}

/* ---- attract ------------------------------------------------------------------ */

void attract_view_update(const AttractViewInfo *v, const GameEvent *ev, int nev, float dt)
{
    (void)ev; (void)nev;
    lounge_init();
    if (v->phase != L.phase) {
        L.phase = v->phase;
        L.phase_t = 0;
        if (v->phase == ATTRACT_TITLE) marquee_init(&L.amq, 1);
    }
    L.phase_t += dt;
    L.t += dt;
    marquee_update(&L.amq, dt);
    bulbs_update(&L.abulbs, dt);
}

void attract_view_draw(const AttractViewInfo *v, double time)
{
    lounge_init();
    double t = L.t;
    (void)time;
    /* The demo table draws its own PRESS DEAL bar. */
    if (v->phase != ATTRACT_TITLE) return;
    float pulse = 0.7f + 0.3f * sinf((float)t * 4.2f);
    float ap = clampf(L.phase_t / 0.9f, 0, 1);
    render_begin();
    ui_background(t, 1);
    /* The HONEY POT, big. */
    float pe = ease(EASE_OUT_BACK, clampf((L.phase_t - 0.15f) / 0.6f, 0, 1));
    if (pe > 0.01f) {
        Rectangle r = { 640 - 300 * pe, 196, 600 * pe, 150 };
        gfx_spr_rot(sprite(SPR_GLOW), 640, 270, 760 * pe, 240, 0, gfx_add(UI_AMBER, 0.16f + 0.08f * sinf((float)t * 2)));
        ui_panel(r, UI_AMBER, 0.6f, 1);
        text_neon(FONT_NEON_M, "THE HONEY POT", 640, 230, 34, UI_AMBER, ap, 1);
        char n1[24];
        TextStyle ts = tstyle((Color){ 255, 246, 196, 255 }, ALIGN_CENTER);
        ts.color2 = (Color){ 240, 150, 30, 255 };
        ts.glow = UI_AMBER;
        ts.glow_k = 0.8f;
        ts.shadow = BLACK;
        ts.shadow_k = 0.8f;
        text_draw_ex(FONT_DISP_L, dv_chips(v->pot, n1, sizeof n1), 640, 296 - 70 * 0.56f, 70, &ts);
        text_at(FONT_UI_M, "THREE SUITED SEVENS WIN IT", 640, 330, 18, (Color){ 255, 220, 160, 255 }, ALIGN_CENTER);
    }
    static const char *const bj1[2] = { "As", "Jc" }, *const bj2[2] = { "Ah", "Kh" };
    fan(bj1, 2, 170, 330, 150, 0.34f, 0.74f, clampf((L.phase_t - 0.3f) / 1.0f, 0, 1), t, 0);
    fan(bj2, 2, 1110, 330, 150, 0.34f, 0.74f, clampf((L.phase_t - 0.5f) / 1.0f, 0, 1), t, 5);
    {
        const float gh = 40, gw = ui_panel_glyph_w(gh), tw = text_width(FONT_NEON_L, "PRESS DEAL", 66, 0);
        float x0 = 640 - (gw + 22 + tw) * 0.5f;
        ui_panel_glyph(x0, 470 - gh * 0.5f, gh, BTN_DEAL, UI_GOLD, pulse);
        text_neon(FONT_NEON_L, "PRESS DEAL", x0 + gw + 22 + tw * 0.5f, 470, 66, UI_CYAN, pulse, 1.2f);
    }
    TextStyle ts = tstyle(UI_HONEY, ALIGN_CENTER);
    ts.spacing = 2;
    text_draw_ex(FONT_UI_M, "BLACKJACK PAYS 3 TO 2   *   PERFECT PAIRS   *   21+3   *   THE HONEY POT", 640, 528, 23, &ts);
    ts.color = (Color){ 150, 140, 160, 255 };
    ts.spacing = 1;
    text_draw_ex(FONT_UI_S, "FREE PLAY  -  FOR AMUSEMENT ONLY  -  REAL SHUFFLES, REAL ODDS", 640, 562, 18, &ts);
    char s[48];
    snprintf(s, sizeof s, "CREDITS  %lld", (long long)v->credits);
    text_neon(FONT_NEON_M, s, 640, 636, 30, UI_GOLD, 0.9f, 0.6f);
    bulbs_draw(&L.abulbs);
    marquee_draw(&L.amq, 640, 92, 1.0f);
    render_end();
    dv_fill_account(DV_FILL_TITLE);
}
