/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* table_present.c - the blackjack table: the felt, the five seats and the bee
 * regulars, the dealer's cards and shoe, chips in flight, the HONEY POT sign,
 * the strategy hint and the button bar; and the same table in attract mode.
 *
 * How it follows the game. Everything that HAPPENS arrives as an event and
 * starts an animation: a card flies from the shoe, chips slide to a betting
 * spot or back to their owner, a hand is split apart. What the table SHOWS at
 * rest is the view's own picture built from those events (cards per hand,
 * chips per spot), and when nothing is moving it is checked against the const
 * game state; a mismatch (the view started mid-round, or an event was missed)
 * snaps the picture to the game, so a lost event can only cost an animation,
 * never a wrong card or number.
 *
 * Honesty: the dealer's hole card is drawn face down until EV_BJ_HOLE says
 * what it is; the dealer's "peek" is a lift of the card corner, shown the same
 * whether there is a blackjack or not, and its result comes from EV_BJ_PEEK.
 * Nothing here reads the shoe. Cosmetic randomness is the renderer's stream. */
#include "render/views.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"
#include "rlgl.h"
#include "platform/fx_settings.h"
#include "platform/panel.h"
#include "platform/screen.h"
#include "render/holdem_art.h"
#include "render/render.h"
#if defined(BPL_HAVE_AUDIO)
#include "audio.h"
#endif

#define PI_F 3.14159265f
#define NS BJ_SEATS
#define NH BJ_MAX_SPLIT_HANDS
#define NC BJ_HAND_CARDS

/* Chip spots of a seat: its hands (0..3), its three side bets, insurance. */
enum { SL_HAND0 = 0, SL_SIDE0 = NH, SL_INS = NH + BJ_SIDE_N, NSLOT };

/* ---- layout (1280 x 720) ---------------------------------------------------- */

/* Seats round the arc, first base (seat 0) at the dealer's left. */
static const float k_theta[NS] = { 30, 60, 90, 120, 150 };

#define F_BET     0.76f         /* betting circle, fraction of the rail's radius */
#define F_CARDS   0.56f         /* a regular's cards                              */
#define F_YOURS   0.600f        /* the player's cards                             */
#define F_AVATAR  1.04f         /* the regular's portrait, over the rail         */
#define BOT_SC    0.48f         /* a regular's cards (CARD_M scale)              */
#define YOU_SC    0.64f         /* the player's cards                            */
#define DEALER_SC 0.64f
#define DEALER_Y  166.0f
#define AV_D      86.0f
#define BAR_Y     652.0f
#define SHOE_X    1094.0f
#define SHOE_Y    130.0f
#define TRAY_X    186.0f
#define TRAY_Y    130.0f
#define HOUSE_X   640.0f        /* where the house's chips come from and go to   */
#define HOUSE_Y   100.0f

static float rad(int s) { return k_theta[s] * PI_F / 180.0f; }
static Vector2 polar(int s, float f) { return (Vector2){ BT_CX + f * BT_A * cosf(rad(s)), BT_CY + f * BT_B * sinf(rad(s)) }; }
static Vector2 out_dir(int s) { return (Vector2){ cosf(rad(s)), sinf(rad(s)) }; }
/* Along the arc, pointing from first base towards third base on screen
   (right to left), the order hands are played in. */
static Vector2 along(int s) { return (Vector2){ -sinf(rad(s)), cosf(rad(s)) }; }
static Vector2 vadd(Vector2 a, Vector2 b, float k) { return (Vector2){ a.x + b.x * k, a.y + b.y * k }; }

static int is_you(const BjGame *g, int s) { return g->seat[s].kind == BJ_SEAT_HUMAN; }
/* The player's place at the table (in attract a bee plays it): bigger cards,
   the chips come from the bottom of the screen, no portrait. */
static int is_pseat(const BjGame *g, int s) { return s == g->cfg.human_seat; }
static float card_sc(const BjGame *g, int s)
{
    /* The player's cards shrink a little when three or four split hands share the spot. */
    if (is_pseat(g, s)) return g->seat[s].nhands > 2 ? YOU_SC * 0.82f : YOU_SC;
    return BOT_SC;
}

/* Where hand h of nh sits: split hands spread along the arc, first hand on
   the right (it is played first, like the seats). */
static Vector2 hand_pos(const BjGame *g, int s, int h, int nh)
{
    Vector2 c = polar(s, is_pseat(g, s) ? F_YOURS : F_CARDS);
    float sp = is_pseat(g, s) ? (nh <= 2 ? 170.0f : nh == 3 ? 140.0f : 118.0f) : 72.0f;
    if (nh > 2 && !is_pseat(g, s)) sp = 60.0f;
    return vadd(c, along(s), -((float)h - (float)(nh - 1) * 0.5f) * sp);
}

/* Card k of a hand: a fan rising away from the middle of the table (to the
   right on the right half and in the middle, to the left on the left half),
   so a long hand never climbs over the felt's printing. A doubled card lies
   across. */
static void card_pos(const BjGame *g, int s, int h, int nh, int k, int doubled, float *x, float *y, float *rot)
{
    Vector2 c = hand_pos(g, s, h, nh);
    float sc = card_sc(g, s) / 0.5f;
    float dx = (nh > 2 ? 13.0f : 17.0f) * sc, dy = -11.0f * sc;
    if (cosf(rad(s)) < -0.1f) dx = -dx;
    *x = c.x - dx + (float)k * dx;
    *y = c.y - dy * 0.5f + (float)k * dy;
    *rot = 0;
    if (doubled && k == 2) { *rot = PI_F * 0.5f; *x += (dx > 0 ? 6 : -6) * sc; *y -= 4 * sc; }
}

/* The dealer's cards stay centred as they come. */
static Vector2 dealer_card(int k, int n)
{
    return (Vector2){ 640.0f + ((float)k - (float)(n - 1) * 0.5f) * 60.0f, DEALER_Y };
}

static Vector2 slot_pos(const BjGame *g, int s, int slot, int nh)
{
    Vector2 b = polar(s, is_pseat(g, s) ? 0.79f : F_BET);
    Vector2 t = along(s), u = out_dir(s);
    if (slot < NH) return nh <= 1 ? b : vadd(b, t, -((float)slot - (float)(nh - 1) * 0.5f) * 46.0f);
    switch (slot) {
    case SL_SIDE0 + BJ_SIDE_PP: return vadd(b, t, -62);
    case SL_SIDE0 + BJ_SIDE_213: return vadd(b, t, 62);
    case SL_SIDE0 + BJ_SIDE_HONEY: return vadd(b, u, 50);
    default: return vadd(b, u, -50);       /* insurance, towards the dealer */
    }
}

/* Where a seat's own chips live: the regular's portrait; the player's rack
   below the table. */
static Vector2 home_pos(const BjGame *g, int s)
{
    if (is_pseat(g, s)) return (Vector2){ 640, 712 };
    return polar(s, F_AVATAR);
}

static Vector2 plate_pos(int s)
{
    Vector2 a = polar(s, F_AVATAR);
    return (Vector2){ a.x, a.y + AV_D * 0.5f + 20 };
}

/* ---- cosmetic state ------------------------------------------------------------- */

typedef struct {
    int   on;
    Card  c;
    float x0, y0, r0, s0, x1, y1, r1, s1;
    float t, dur, delay, arc;
    int   moving;
    float flip, flip_to, ft, fdur, fdelay;
    int   flipping;
    float alpha;
    int   leaving;
    float x, y, rot, sc;
    float dim;
    int   ev;               /* bits this update: 1 landed, 2 flip started, 4 flip done */
} CardFx;

enum { FL_IN, FL_OUT };

typedef struct {
    int     on, kind, seat, slot, sound;
    int64_t amt;
    float   x0, y0, x1, y1, t, dur, delay, arc;
} Flight;

#define NFLIGHT 96
#define NPILL 24

typedef struct {
    float blink, blink_t, next_blink;
    float gx, gy, tgx, tgy, next_gaze;
    float ant, bob, flap;
    float tension, react, hurt;
} AvFx;

typedef struct {
    int   on;
    char  text[40];
    float x, y, t, dur, size;
    Color col;
    int   big;
} Pill;

typedef struct {
    CardFx card[NH][NC];
    int    n[NH], nh;
    int    last_hand;           /* the hand of the last play (SPLIT, DOUBLE)   */
    int64_t spot[NSLOT];        /* chips shown on each spot (landed)            */
    int     res[NH];            /* BJ_RES_* once settled                       */
    float   res_t[NH];
    AvFx   av;
    float  present;             /* a regular sits here: 0..1                   */
    int    who;                 /* avatar (AV_*) last seated                    */
    float  act_t;
    char   act[32];
    Color  act_col;
} SeatFx;

static struct {
    int inited;
    const BjGame *game;
    int demo, snd;
    SeatFx seat[NS];
    CardFx dealer[NC];
    int    dn;
    float  peek;                 /* the dealer lifting the hole card, 0..1      */
    float  peek_t;
    Flight fl[NFLIGHT];
    Pill   pill[NPILL];
    FxClock clock;
    Shake  shake;
    BulbRing bulbs;
    float  bulbs_k;
    Celebration cel;
    float  rail_mode_t;
    int    rail_mode;           /* 0 calm, 1 your turn, 2 celebration          */
    UiButton btn[6];
    float  turn_t;
    float  shuffle_t;           /* seconds into a shuffle (< 0: none)          */
    double credits_shown;
    float  bet_bump;
    float  pot_bump;
    int64_t pot_shown;
    int64_t round_net;
    int    round_jackpot, round_big, round_bj;
    float  time;
    float  deal_stagger;        /* this frame's deals so far (spread them)     */
    TablePresentStats st;
} P;

/* ---- helpers ------------------------------------------------------------------- */

static float pan_of(Vector2 p) { return clampf((p.x - 640.0f) / 640.0f * 0.7f, -0.7f, 0.7f); }

static void sfx(int id, float vol, float pitch, float pan)
{
#if defined(BPL_HAVE_AUDIO)
    if (P.snd) dv_sfx(id, vol, pitch, pan);
#else
    (void)id; (void)vol; (void)pitch; (void)pan;
#endif
}

static void copy_str(char *dst, size_t n, const char *src)
{
    if (!n) return;
    size_t l = strlen(src);
    if (l >= n) l = n - 1;
    memcpy(dst, src, l);
    dst[l] = 0;
}

static TextStyle style(Color c, TextAlign a)
{
    TextStyle t;
    memset(&t, 0, sizeof t);
    t.color = c;
    t.align = a;
    return t;
}

static void label(FontId f, const char *s, float x, float cy, float size, Color c, TextAlign a, float shadow)
{
    TextStyle t = style(c, a);
    if (shadow > 0) { t.shadow = BLACK; t.shadow_k = shadow; }
    text_draw_ex(f, s, x, cy - size * 0.56f, size, &t);
}

static void gold_label(FontId f, const char *s, float x, float cy, float size, TextAlign a, float glow)
{
    TextStyle t = style((Color){ 255, 244, 200, 255 }, a);
    t.color2 = (Color){ 236, 160, 40, 255 };
    t.glow = UI_AMBER;
    t.glow_k = glow;
    t.shadow = BLACK;
    t.shadow_k = 0.8f;
    text_draw_ex(f, s, x, cy - size * 0.56f, size, &t);
}

static Color fade(Color c, float a) { c.a = (unsigned char)(c.a * clampf(a, 0, 1)); return c; }
static Color scale_col(Color c, float k)
{
    return (Color){ (unsigned char)(c.r * k), (unsigned char)(c.g * k), (unsigned char)(c.b * k), c.a };
}

static void pill_draw(float cx, float cy, const char *s, FontId f, float size, Color text, Color edge, float alpha, float scale)
{
    if (alpha <= 0.01f || scale <= 0.01f) return;
    float w = (text_width(f, s, size, 0) + 22) * scale, h = (size + 10) * scale;
    gfx_nine(sprite_nine(NINE_RRECT), cx - w * 0.5f, cy - h * 0.5f, w, h, 14 * scale, gfx_cola((Color){ 12, 8, 14, 255 }, 0.84f * alpha));
    if (edge.a) gfx_nine(sprite_nine(NINE_RRECT_LINE), cx - w * 0.5f, cy - h * 0.5f, w, h, 14 * scale, gfx_cola(edge, alpha));
    TextStyle t = style(text, ALIGN_CENTER);
    t.opacity = alpha;
    text_draw_ex(f, s, cx, cy - size * scale * 0.56f, size * scale, &t);
}

static void say_at(float x, float y, const char *text, Color col, float dur, float size, int big)
{
    Pill *p = NULL;
    for (int i = 0; i < NPILL; i++) if (!P.pill[i].on) { p = &P.pill[i]; break; }
    if (!p) p = &P.pill[0];
    memset(p, 0, sizeof *p);
    p->on = 1;
    copy_str(p->text, sizeof p->text, text);
    p->x = x;
    p->y = y;
    p->dur = dur;
    p->col = col;
    p->size = size;
    p->big = big;
}

static int avatar_of_bot(int bot)
{
    switch (bot) {
    case BOT_STINGER: return AV_STINGER;
    case BOT_HONEY: return AV_HONEY;
    case BOT_BUZZ: return AV_BUZZ;
    case BOT_QUEENIE: return AV_QUEENIE;
    case BOT_DRONE: return AV_DRONE;
    default: return AV_BEE;
    }
}

static const char *seat_name(const BjGame *g, int s)
{
    if (g->seat[s].kind == BJ_SEAT_HUMAN) return "YOU";
    if (g->seat[s].kind == BJ_SEAT_BOT) return g->seat[s].bot == BOT_DEMO ? "GUEST" : bj_bot_name(g->seat[s].bot);
    return "";
}

/* ---- cards ---------------------------------------------------------------------- */

static float anim_k(void) { return g_effects.card_anim ? 1.0f : 0.0f; }

static void cf_place(CardFx *c, Card card, float x, float y, float rot, float sc, int face)
{
    memset(c, 0, sizeof *c);
    c->on = 1;
    c->c = card;
    c->x = c->x1 = x;
    c->y = c->y1 = y;
    c->rot = c->r1 = rot;
    c->sc = c->s1 = sc;
    c->flip = c->flip_to = face ? 1.0f : 0.0f;
    c->alpha = 1;
}

static void cf_deal(CardFx *c, Card card, float x, float y, float rot, float sc, float delay, int face)
{
    cf_place(c, card, SHOE_X - 20, SHOE_Y + 10, -0.9f, sc * 0.7f, 0);
    c->x0 = SHOE_X - 20; c->y0 = SHOE_Y + 10; c->r0 = -0.9f; c->s0 = sc * 0.7f;
    c->x1 = x; c->y1 = y; c->r1 = rot; c->s1 = sc;
    c->delay = delay * anim_k();
    c->dur = fmaxf(0.001f, 0.34f * anim_k());
    c->arc = 40;
    c->moving = 1;
    c->alpha = 0;           /* appears when it leaves the shoe */
    if (face) {
        c->flip_to = 1;
        c->fdelay = c->delay + c->dur * 0.55f;
        c->fdur = fmaxf(0.001f, 0.22f * anim_k());
        c->flipping = 1;
    }
}

static void cf_move(CardFx *c, float x, float y, float rot, float sc, float dur, float delay)
{
    c->x0 = c->x; c->y0 = c->y; c->r0 = c->rot; c->s0 = c->sc;
    c->x1 = x; c->y1 = y; c->r1 = rot; c->s1 = sc;
    c->t = 0;
    c->delay = delay * anim_k();
    c->dur = fmaxf(0.001f, dur * anim_k());
    c->arc = 0;
    c->moving = 1;
}

static void cf_flip(CardFx *c, Card card, float delay, float dur)
{
    if (card != CARD_NONE) c->c = card;
    c->flip_to = 1;
    c->fdelay = delay * anim_k();
    c->fdur = fmaxf(0.001f, dur * anim_k());
    c->ft = 0;
    c->flipping = 1;
}

static void cf_leave(CardFx *c, float tx, float ty, float delay)
{
    if (!c->on || c->leaving) return;
    cf_move(c, tx, ty, c->rot + 0.5f, c->sc * 0.7f, 0.40f, delay);
    c->leaving = 1;
}

static void cf_update(CardFx *c, float dt)
{
    c->ev = 0;
    if (!c->on) return;
    if (c->moving) {
        c->t += dt;
        float t = c->t - c->delay;
        if (t >= 0) {
            if (c->alpha <= 0 && !c->leaving) c->alpha = 1;
            float k = clampf(t / c->dur, 0, 1);
            float e = ease(c->leaving ? EASE_IN_QUAD : EASE_OUT_CUBIC, k);
            Vec2f q;
            if (c->arc != 0) {
                Vec2f a = { c->x0, c->y0 }, b = { c->x1, c->y1 };
                Vec2f m = { (a.x + b.x) * 0.5f, fminf(a.y, b.y) - c->arc };
                q = bezier2(a, m, b, e);
            } else {
                q = (Vec2f){ lerpf(c->x0, c->x1, e), lerpf(c->y0, c->y1, e) };
            }
            c->x = q.x;
            c->y = q.y;
            c->rot = lerpf(c->r0, c->r1, ease(c->leaving ? EASE_IN_QUAD : EASE_OUT_BACK, k));
            c->sc = lerpf(c->s0, c->s1, ease(EASE_OUT_QUART, k));
            if (c->leaving) c->alpha = 1 - ease(EASE_IN_QUAD, k);
            if (k >= 1) {
                c->moving = 0;
                if (c->leaving) { c->on = 0; return; }
                c->ev |= 1;
            }
        }
    }
    if (c->flipping) {
        c->ft += dt;
        float t = c->ft - c->fdelay;
        if (t >= 0) {
            if (c->flip == 0 && c->flip_to > 0) c->ev |= 2;
            float k = clampf(t / c->fdur, 0, 1);
            c->flip = card_flip_curve(k);
            if (k >= 1) { c->flipping = 0; c->flip = c->flip_to; c->ev |= 4; }
        }
    }
}

static void cf_draw(const CardFx *c, float glow, Color glow_col, float lift)
{
    if (!c->on || c->alpha <= 0.01f) return;
    CardPose p;
    memset(&p, 0, sizeof p);
    p.x = c->x;
    p.y = c->y;
    p.rot = c->rot;
    p.scale = c->sc;
    p.flip = c->flip;
    p.alpha = c->alpha;
    p.back = 0;
    p.dim = c->dim;
    p.glow = glow;
    p.glow_color = glow_col;
    p.lift = lift;
    if (c->flipping && c->flip > 0) p.lift += 10.0f * c->sc * sinf(c->flip * PI_F);
    card_draw(c->c, CARD_M, &p);
}

static int cards_moving(void)
{
    for (int s = 0; s < NS; s++)
        for (int h = 0; h < NH; h++)
            for (int k = 0; k < NC; k++) {
                const CardFx *c = &P.seat[s].card[h][k];
                if (c->on && (c->moving || c->flipping)) return 1;
            }
    for (int k = 0; k < NC; k++) if (P.dealer[k].on && (P.dealer[k].moving || P.dealer[k].flipping)) return 1;
    return 0;
}

/* Lays a seat's hands out again (after a split changed how many there are). */
static void relayout(const BjGame *g, int s, float dur)
{
    SeatFx *f = &P.seat[s];
    for (int h = 0; h < f->nh; h++)
        for (int k = 0; k < f->n[h]; k++) {
            CardFx *c = &f->card[h][k];
            if (!c->on) continue;
            float x, y, r;
            card_pos(g, s, h, f->nh, k, g->seat[s].hand[h].doubled, &x, &y, &r);
            cf_move(c, x, y, r, card_sc(g, s), dur, 0.02f * (float)k);
        }
}

/* ---- chips ------------------------------------------------------------------------ */

typedef struct { int denom, n; } ChipCol;

static int chip_columns(int64_t amount, ChipCol *cols, int maxcols, int maxchips, int per_col)
{
    if (amount <= 0) return 0;
    int top = 0;
    for (int d = HCHIP_N - 1; d >= 0; d--)
        if (amount / hart_chip_value[d] >= 2) { top = d; break; }
    for (;;) {
        int64_t left = amount;
        int total = 0, nc = 0;
        for (int d = top; d >= 0 && nc < maxcols; d--) {
            int64_t k = left / hart_chip_value[d];
            if (d == top && k > (int64_t)maxchips * 4) k = (int64_t)maxchips * 4;
            left -= k * hart_chip_value[d];
            total += (int)k;
            while (k > 0 && nc < maxcols) {
                int take = (int)(k > per_col ? per_col : k);
                cols[nc].denom = d;
                cols[nc].n = take;
                nc++;
                k -= take;
            }
        }
        if ((total <= maxchips && nc < maxcols) || top >= HCHIP_N - 1) return nc;
        top++;
    }
}

/* A pile of chips standing on (x, y) (its bottom centre). */
static void draw_pile(int64_t amount, float x, float y, float scale, float alpha, int maxcols)
{
    ChipCol cols[6];
    if (maxcols > 6) maxcols = 6;
    int nc = chip_columns(amount, cols, maxcols, 24, 8);
    if (nc == 0) return;
    float w = HCHIP_W * scale, h = HCHIP_H * scale, t = HCHIP_THICK * scale;
    float gap = w * 0.86f;
    PCol c = gfx_cola(WHITE, alpha);
    gfx_spr_rot(sprite(SPR_GLOW), x, y - t, ((float)nc * gap + w * 0.6f) * 1.1f, h * 0.9f, 0, gfx_cola(BLACK, 0.5f * alpha));
    float rx = x - (float)(nc - 1) * gap * 0.5f;
    for (int i = 0; i < nc; i++) {
        float cx = rx + (float)i * gap;
        const Spr *s = hart_chip(cols[i].denom);
        for (int k = 0; k < cols[i].n; k++) {
            float jx = (float)(((i * 7 + k * 3) % 5) - 2) * 0.35f * scale;
            gfx_spr(s, cx - w * 0.5f + jx, y - h + 2 * scale - (float)k * t, w, h, c);
        }
    }
}

static void spot_add(int s, int slot, int64_t amt)
{
    P.seat[s].spot[slot] += amt;
    if (P.seat[s].spot[slot] < 0) P.seat[s].spot[slot] = 0;
}

/* Chips moving: FL_IN lands on a spot (it shows there when it lands), FL_OUT
   leaves one (it stops showing there when it sets off). */
static Flight *fly(int kind, int s, int slot, int64_t amt, Vector2 a, Vector2 b, float delay, float dur)
{
    if (amt <= 0) return NULL;
    Flight *f = NULL;
    for (int i = 0; i < NFLIGHT; i++) if (!P.fl[i].on) { f = &P.fl[i]; break; }
    if (!f) {
        spot_add(s, slot, kind == FL_IN ? amt : -amt);
        return NULL;
    }
    memset(f, 0, sizeof *f);
    f->on = 1;
    f->kind = kind;
    f->seat = s;
    f->slot = slot;
    f->amt = amt;
    f->x0 = a.x; f->y0 = a.y; f->x1 = b.x; f->y1 = b.y;
    f->delay = g_effects.chip_anim ? delay : 0;
    f->dur = g_effects.chip_anim ? dur : 0;
    f->arc = 16 + 0.06f * sqrtf((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
    f->sound = 1;
    return f;
}

static void flights_update(float dt)
{
    int live = 0;
    for (int i = 0; i < NFLIGHT; i++) {
        Flight *f = &P.fl[i];
        if (!f->on) continue;
        f->t += dt;
        if (f->kind == FL_OUT && f->t >= f->delay) {
            spot_add(f->seat, f->slot, -f->amt);
            f->kind = -1;               /* counted out; it only flies now */
        }
        if (f->t < f->delay + f->dur) { live++; continue; }
        f->on = 0;
        if (f->kind == FL_IN) spot_add(f->seat, f->slot, f->amt);
        if (f->sound) sfx(f->amt >= 100 ? SFX_CHIP_STACK : SFX_CHIP_SINGLE, 0.6f, 1.0f, pan_of((Vector2){ f->x1, f->y1 }));
    }
    P.st.flights_live = live;
    if (live > P.st.flights_peak) P.st.flights_peak = live;
}

static void flights_draw(void)
{
    for (int i = 0; i < NFLIGHT; i++) {
        const Flight *f = &P.fl[i];
        if (!f->on || f->t < f->delay) continue;
        float k = clampf((f->t - f->delay) / fmaxf(f->dur, 0.001f), 0, 1);
        float e = ease(EASE_OUT_CUBIC, k);
        Vec2f a = { f->x0, f->y0 }, b = { f->x1, f->y1 };
        Vec2f m = { (a.x + b.x) * 0.5f, fminf(a.y, b.y) - f->arc };
        Vec2f q = bezier2(a, m, b, e);
        draw_pile(f->amt, q.x, q.y + 8, 0.62f + 0.1f * sinf(k * PI_F), 1, 3);
    }
}

/* ---- avatars ------------------------------------------------------------------------ */

static void update_avatar(const BjGame *g, int s, float dt)
{
    AvFx *a = &P.seat[s].av;
    int thinking = g && g->phase == BJ_PH_PLAY && g->turn == s && g->seat[s].kind == BJ_SEAT_BOT;
    /* Tension, from what anyone at the table can see: a stiff hand (12-16)
       against a strong up card is a hard decision. */
    float target = 0.08f;
    if (thinking) {
        const BjHand *h = bj_cur_hand(g, s);
        int tot = h ? bj_total(h->c, h->n).total : 0;
        int up = g->ndealer ? bj_card_value(g->dealer[0]) : 0;
        target = (tot >= 12 && tot <= 16) ? (up >= 7 ? 0.75f : 0.45f) : 0.25f;
    }
    a->tension += (target - a->tension) * clampf(dt * (thinking ? 5.0f : 2.0f), 0, 1);
    float t = a->tension;
    a->ant += dt * (1.1f + 7.5f * t * t) * 2 * PI_F;
    a->bob += dt * (0.3f + 1.3f * t) * 2 * PI_F;
    a->flap += dt * (t > 0.45f ? 26.0f : 3.0f);
    a->react = fmaxf(0, a->react - dt * 0.9f);
    a->hurt = fmaxf(0, a->hurt - dt * 0.8f);
    a->next_blink -= dt;
    if (a->blink_t >= 0 && a->blink_t < 0.16f) {
        a->blink_t += dt;
        a->blink = sinf(clampf(a->blink_t / 0.16f, 0, 1) * PI_F);
    } else {
        a->blink = 0;
        a->blink_t = -1;
    }
    if (a->next_blink <= 0) {
        a->blink_t = 0;
        a->next_blink = t > 0.6f ? fx_randf(0.6f, 1.8f) : fx_randf(2.2f, 5.0f);
    }
    a->next_gaze -= dt;
    if (a->next_gaze <= 0) {
        if (thinking) {
            /* Down at the cards, then up at the dealer's card. */
            if (fx_randf(0, 1) < 0.5f) { a->tgx = 0; a->tgy = 2.4f; }
            else { a->tgx = -cosf(rad(s)) * 2.6f; a->tgy = -2.0f; }
            a->next_gaze = lerpf(1.4f, 0.4f, t) * fx_randf(0.7f, 1.3f);
        } else if (g && g->phase == BJ_PH_PLAY && g->turn >= 0 && g->turn < NS && g->turn != s) {
            Vector2 o = polar(g->turn, F_CARDS), me = polar(s, F_AVATAR);
            float dx = o.x - me.x, dy = o.y - me.y, l = sqrtf(dx * dx + dy * dy);
            if (l > 1) { a->tgx = dx / l * 2.4f; a->tgy = dy / l * 2.0f; }
            a->next_gaze = fx_randf(0.8f, 2.0f);
        } else {
            a->tgx = fx_randf(-1.5f, 1.5f);
            a->tgy = fx_randf(-1.5f, 1.0f);
            a->next_gaze = fx_randf(1.5f, 3.5f);
        }
    }
    float k = clampf(dt * 16, 0, 1);
    a->gx += (a->tgx - a->gx) * k;
    a->gy += (a->tgy - a->gy) * k;
}

static void draw_avatar(int who, const AvFx *a, float cx, float cy, float size, float bright, float alpha)
{
    const AvatarRig *rg = hart_rig(who);
    const float k = size / (float)HAV_SIZE;
    float t = a ? a->tension : 0;
    float bob = a ? sinf(a->bob) * (0.6f + 2.2f * t) : 0;
    float kk = k * (1.0f + (a ? 0.08f * sinf(a->react * PI_F) : 0));
    unsigned char lv = (unsigned char)(255 * clampf(bright, 0, 1));
    Color tint = { lv, lv, lv, 255 };
    if (a && a->hurt > 0)
        tint = (Color){ lv, (unsigned char)(lv * (1 - 0.6f * a->hurt)), (unsigned char)(lv * (1 - 0.6f * a->hurt)), 255 };
    PCol col = gfx_cola(tint, alpha);
    const Spr *back = hart_avatar(who, AVL_BACK), *front = hart_avatar(who, AVL_FRONT), *over = hart_avatar(who, AVL_OVER);
    float x0 = cx - 64 * kk, y0 = cy - 64 * kk + bob;
    if (back) gfx_spr(back, x0, y0 - bob, 128 * kk, 128 * kk, col);
#define AX(px) (x0 + (px) * kk)
#define AY(py) (y0 + (py) * kk)
    if (rg->bee) {
        const Spr *w = hart_part(HPART_WING);
        float amp = 0.04f + 0.22f * fmaxf(0, t - 0.4f);
        float fl = a ? sinf(a->flap) * amp : 0;
        Spr m = *w;
        float tu = m.u0;
        m.u0 = m.u1;
        m.u1 = tu;
        PCol wc = gfx_cola(tint, alpha * 0.75f);
        gfx_spr_rot(w, AX(16), AY(76), 32 * kk, 45 * kk, -0.95f - fl, wc);
        gfx_spr_rot(&m, AX(112), AY(76), 32 * kk, 45 * kk, 0.95f + fl, wc);
        const Spr *an = hart_part(HPART_ANTENNA);
        float wig = (0.05f + 0.40f * t) * (a ? sinf(a->ant) : 0);
        Color at = scale_col(rg->ant_tint, bright);
        for (int i = 0; i < 2; i++) {
            float r = rg->ant_rot[i] + (i ? -wig : wig * 0.8f);
            float bx = AX(rg->ant_x[i]), by = AY(rg->ant_y);
            float d = 21 * kk;
            gfx_spr_rot(an, bx + d * sinf(r), by - d * cosf(r), 18 * kk, 46 * kk, r, gfx_cola(at, alpha));
        }
    }
    if (front) gfx_spr(front, x0, y0, 128 * kk, 128 * kk, col);
    if (rg->bee) {
        const Spr *eye = hart_part(HPART_EYE), *pup = hart_part(HPART_PUPIL);
        float bl = a ? a->blink : 0;
        float ew = rg->eye_rx / 8.3f * 20 * kk, eh = rg->eye_ry / 10.3f * 24 * kk * (1 - 0.92f * bl);
        float gx = a ? clampf(a->gx, -3.2f, 3.2f) : 0, gy = a ? clampf(a->gy, -2.5f, 2.6f) : 0;
        for (int i = 0; i < 2; i++) {
            float ex = AX(rg->eye_x[i]), ey = AY(rg->eye_y);
            gfx_spr(eye, ex - ew * 0.5f, ey - eh * 0.5f, ew, eh, col);
            float pw = rg->pupil_r / 4.8f * 12 * kk, ph = pw * (1 - 0.92f * bl);
            gfx_spr(pup, ex + gx * kk - pw * 0.5f, ey + gy * kk * (1 - bl) - ph * 0.5f, pw, ph, col);
        }
        if (over) gfx_spr(over, AX(rg->over_x), AY(rg->over_y), 80 * kk, 36 * kk, col);
        const Spr *br = hart_part(HPART_BROW);
        float ang = 0.05f + 0.34f * t, drop = 1.6f * t;
        Color bc = scale_col(rg->brow_col, bright);
        gfx_spr_rot(br, AX(rg->eye_x[0]), AY(rg->brow_y + drop), 22 * kk, 7 * kk, ang, gfx_cola(bc, alpha));
        gfx_spr_rot(br, AX(rg->eye_x[1]), AY(rg->brow_y + drop), 22 * kk, 7 * kk, -ang, gfx_cola(bc, alpha));
        if (t > 0.72f && a) {
            float st = fmodf(a->bob * 0.15f, 1.0f);
            float sa = clampf((t - 0.72f) / 0.15f, 0, 1) * (1 - st);
            gfx_spr_rot(sprite(SPR_DROP), AX(97), AY(40 + 14 * st), 10 * kk, 14 * kk, 0,
                        gfx_cola((Color){ 170, 230, 255, 255 }, 0.8f * sa * alpha));
        }
    }
#undef AX
#undef AY
}

/* ---- the picture from the game, when the view has to start over ------------------- */

static void snap_to_game(const BjGame *g)
{
    for (int s = 0; s < NS; s++) {
        SeatFx *f = &P.seat[s];
        const BjSeat *st = &g->seat[s];
        memset(f->card, 0, sizeof f->card);
        memset(f->n, 0, sizeof f->n);
        memset(f->spot, 0, sizeof f->spot);
        f->nh = st->nhands ? st->nhands : 1;
        for (int h = 0; h < st->nhands; h++) {
            f->n[h] = st->hand[h].n;
            for (int k = 0; k < st->hand[h].n; k++) {
                float x, y, r;
                card_pos(g, s, h, f->nh, k, st->hand[h].doubled, &x, &y, &r);
                cf_place(&f->card[h][k], st->hand[h].c[k], x, y, r, card_sc(g, s), 1);
            }
            f->res[h] = st->hand[h].result;
            f->res_t[h] = 10;
        }
        /* Chips ride on the spots from the deal until the round is settled. */
        if (st->in_round && !(g->phase == BJ_PH_BETTING && g->table_dirty)) {
            for (int h = 0; h < st->nhands; h++) f->spot[SL_HAND0 + h] = st->hand[h].bet;
            if (g->phase <= BJ_PH_DEAL)
                for (int k = 0; k < BJ_SIDE_N; k++) f->spot[SL_SIDE0 + k] = st->side[k];
            if (g->phase < BJ_PH_PLAY) f->spot[SL_INS] = st->insurance;
        }
        f->present = st->kind == BJ_SEAT_BOT ? 1.0f : 0.0f;
        f->who = st->kind == BJ_SEAT_BOT ? avatar_of_bot(st->bot) : AV_YOU;
    }
    memset(P.dealer, 0, sizeof P.dealer);
    P.dn = g->ndealer;
    for (int k = 0; k < g->ndealer; k++) {
        Vector2 p = dealer_card(k, g->ndealer);
        cf_place(&P.dealer[k], g->dealer[k], p.x, p.y, 0, DEALER_SC, k != 1 || g->hole_shown);
    }
    memset(P.fl, 0, sizeof P.fl);
}

static int matches_game(const BjGame *g)
{
    if (P.dn != g->ndealer) return 0;
    for (int s = 0; s < NS; s++) {
        const BjSeat *st = &g->seat[s];
        const SeatFx *f = &P.seat[s];
        int nh = st->nhands;
        for (int h = 0; h < NH; h++) {
            int want = h < nh ? st->hand[h].n : 0;
            if (f->n[h] != want) return 0;
        }
    }
    return 1;
}

static void reset_all(const BjGame *g)
{
    P.game = g;
    memset(P.pill, 0, sizeof P.pill);
    for (int s = 0; s < NS; s++) {
        P.seat[s].act_t = -1;
        P.seat[s].av.next_blink = 1.0f + 0.4f * (float)s;
        P.seat[s].av.blink_t = -1;
        P.seat[s].av.next_gaze = 0.5f + 0.3f * (float)s;
    }
    if (g) {
        snap_to_game(g);
        P.pot_shown = g->pot;
    }
    P.shuffle_t = -1;
    P.peek = 0;
}

/* ---- events -------------------------------------------------------------------------- */

static void set_act(int s, const char *txt, Color c)
{
    copy_str(P.seat[s].act, sizeof P.seat[s].act, txt);
    P.seat[s].act_col = c;
    P.seat[s].act_t = 0;
}

/* What a regular says now and then, in character (cosmetic). */
static const char *quip(int bot, int act)
{
    if (fx_randf(0, 1) > 0.35f) return NULL;
    switch (bot) {
    case BOT_STINGER: return act == BJ_ACT_SPLIT ? "ALWAYS SPLIT EIGHTS" : act == BJ_ACT_DOUBLE ? "BY THE BOOK" : NULL;
    case BOT_HONEY: return act == BJ_ACT_STAND ? "I HAVE A FEELING..." : act == BJ_ACT_DOUBLE ? "FEELING LUCKY!" : NULL;
    case BOT_BUZZ: return act == BJ_ACT_DOUBLE ? "LET IT RIDE!" : act == BJ_ACT_HIT ? "HIT ME!" : NULL;
    case BOT_QUEENIE: return act == BJ_ACT_STAND ? "THAT WILL DO" : act == BJ_ACT_SPLIT ? "TWO HANDS, DARLING" : NULL;
    case BOT_DRONE: return act == BJ_ACT_HIT ? "SAME AS THE DEALER" : NULL;
    }
    return NULL;
}

static Color res_color(int res)
{
    switch (res) {
    case BJ_RES_WIN: return (Color){ 120, 255, 160, 255 };
    case BJ_RES_BLACKJACK: return UI_GOLD;
    case BJ_RES_PUSH: return UI_CYAN;
    case BJ_RES_SURRENDER: return (Color){ 200, 180, 255, 255 };
    default: return (Color){ 255, 110, 120, 255 };
    }
}

static void pay_out(const BjGame *g, int s, int slot, int64_t stake, int64_t paid, Vector2 at, float delay)
{
    Vector2 house = { HOUSE_X, HOUSE_Y }, home = home_pos(g, s);
    if (paid <= 0) { fly(FL_OUT, s, slot, stake, at, house, delay, 0.45f); return; }
    /* The house pays the win onto the spot (or takes the surrendered half),
       then the whole pile goes home. */
    if (paid > stake) fly(FL_IN, s, slot, paid - stake, house, at, delay, 0.42f);
    else if (paid < stake) fly(FL_OUT, s, slot, stake - paid, at, house, delay, 0.42f);
    fly(FL_OUT, s, slot, paid, at, home, delay + 0.75f, 0.5f);
}

static void human_round_end(const TableViewInfo *v, int64_t net)
{
    const BjGame *g = v->game;
    int hs = g->cfg.human_seat;
    if (v->demo || hs < 0 || g->seat[hs].kind != BJ_SEAT_HUMAN) return;
    int64_t back = g->seat[hs].returned;
    Vector2 focus = hand_pos(g, hs, 0, 1);
    if (P.round_jackpot) return;            /* the takeover is running */
    if (P.round_big && back > 0) {
        celeb_start(&P.cel, WIN_BIG, back, "BIG WIN", focus);
        P.st.celebrations++;
    } else if (P.round_bj && net > 0) {
        celeb_start(&P.cel, WIN_MEDIUM, back, "BLACKJACK!", focus);
        P.st.celebrations++;
    } else if (net > 0) {
        celeb_start(&P.cel, WIN_SMALL, back, "WINNER", focus);
        P.st.celebrations++;
    }
}

static void on_event(const TableViewInfo *v, const GameEvent *e)
{
    const BjGame *g = v->game;
    int s = e->a;
    SeatFx *f = (s >= 0 && s < NS) ? &P.seat[s] : NULL;
    char b[48], n1[24];
    P.st.events_seen++;
    switch (e->type) {
    case EV_BJ_CLEAR:
        for (int i = 0; i < NS; i++) {
            SeatFx *q = &P.seat[i];
            for (int h = 0; h < NH; h++) {
                for (int k = 0; k < NC; k++) cf_leave(&q->card[h][k], TRAY_X, TRAY_Y, 0.02f * (float)(i + k));
                q->n[h] = 0;
                q->res[h] = 0;
            }
            q->nh = 1;
            memset(q->spot, 0, sizeof q->spot);
            q->act_t = -1;
        }
        for (int k = 0; k < NC; k++) cf_leave(&P.dealer[k], TRAY_X, TRAY_Y, 0.03f * (float)k);
        P.dn = 0;
        P.peek = 0;
        P.round_jackpot = P.round_big = P.round_bj = 0;
        sfx(SFX_CARD_SLIDE, 0.6f, 1.0f, -0.5f);
        break;
    case EV_BJ_SHUFFLE:
        P.shuffle_t = 0;
        sfx(SFX_CARD_SHUFFLE, 0.8f, 1.0f, 0.6f);
        say_at(640, 236, "SHUFFLING A NEW SHOE", UI_CYAN, 2.2f, 24, 0);
        break;
    case EV_BJ_SEAT:
        if (!f) break;
        if (e->v) {
            f->who = avatar_of_bot(e->b);
            snprintf(b, sizeof b, "%s SITS DOWN", bj_bot_name(e->b));
            say_at(plate_pos(s).x, plate_pos(s).y - 120, b, UI_GOLD, 2.4f, 20, 0);
        } else {
            snprintf(b, sizeof b, "%s IS OUT OF CHIPS", bj_bot_name(e->b));
            say_at(plate_pos(s).x, plate_pos(s).y - 120, b, (Color){ 255, 140, 150, 255 }, 2.4f, 20, 0);
        }
        break;
    case EV_BJ_STAKE: {
        if (!f) break;
        int slot = SL_INS, nh = f->nh;
        if (e->b < BJ_SIDE_N) slot = SL_SIDE0 + e->b;
        else if (e->b == BJ_BET_MAIN) slot = SL_HAND0;
        else if (e->b == BJ_BET_DOUBLE) slot = SL_HAND0 + f->last_hand;
        else if (e->b == BJ_BET_SPLIT) { slot = SL_HAND0 + f->nh; nh = f->nh + 1; }
        fly(FL_IN, s, slot, e->v, home_pos(g, s), slot_pos(g, s, slot, nh), 0.03f * (float)(slot % 4), 0.38f);
        if (is_you(g, s)) P.bet_bump = 1;
        break;
    }
    case EV_BJ_CARD: {
        Card c = (Card)(e->v & 0xFF);
        int down = (e->v & 0x100) != 0;
        float delay = 0.07f * P.deal_stagger++;
        if (s == BJ_DEALER) {
            if (P.dn >= NC) break;
            for (int k = 0; k < P.dn; k++) {
                Vector2 q = dealer_card(k, P.dn + 1);
                if (P.dealer[k].on && !P.dealer[k].leaving) cf_move(&P.dealer[k], q.x, q.y, 0, DEALER_SC, 0.25f, 0);
            }
            Vector2 p = dealer_card(P.dn, P.dn + 1);
            cf_deal(&P.dealer[P.dn], c, p.x, p.y, 0, DEALER_SC, delay, !down);
            P.dn++;
        } else if (f) {
            int h = e->b < NH ? e->b : NH - 1;
            if (f->n[h] >= NC) break;
            if (h + 1 > f->nh) f->nh = h + 1;
            int k = f->n[h]++;
            float x, y, r;
            int dbl = g->seat[s].hand[h].doubled && k == 2;
            card_pos(g, s, h, f->nh, k, dbl, &x, &y, &r);
            cf_deal(&f->card[h][k], c, x, y, r, card_sc(g, s), delay, 1);
        }
        break;
    }
    case EV_BJ_HOLE:
        if (P.dn > 1) cf_flip(&P.dealer[1], (Card)(e->v & 0xFF), 0, 0.3f);
        P.peek = 0;
        break;
    case EV_BJ_PEEK:
        P.peek_t = 0;
        P.peek = 1;
        break;
    case EV_BJ_SIDE: {
        if (!f) break;
        int k = e->b & 15, o = e->b >> 4;
        int slot = SL_SIDE0 + k;
        Vector2 at = slot_pos(g, s, slot, f->nh);
        int64_t stake = g->seat[s].side[k];
        pay_out(g, s, slot, stake, e->v, at, 0.1f);
        if (e->v > 0) {
            const char *nm = bj_side_outcome_name(k, o);
            Color c = k == BJ_SIDE_PP ? UI_MAGENTA : k == BJ_SIDE_213 ? UI_CYAN : UI_AMBER;
            int pays = k == BJ_SIDE_PP ? bj_pp_pays[o] : k == BJ_SIDE_213 ? bj_213_pays[o] : bj_hp_pays[o];
            if (o == HP_JACKPOT && k == BJ_SIDE_HONEY) snprintf(b, sizeof b, "%s!", nm);
            else snprintf(b, sizeof b, "%s %d:1", nm, pays);
            if (!(o == HP_JACKPOT && k == BJ_SIDE_HONEY && is_you(g, s) && !v->demo))
                say_at(at.x, at.y - 44, b, c, 2.6f, is_you(g, s) ? 24 : 18, is_you(g, s));
            if (is_you(g, s)) {
                pfx_sparkle(at.x, at.y, 50, 10);
                if (pays >= 25) P.round_big = 1;
                sfx(pays >= 25 ? SFX_WIN_MEDIUM : SFX_WIN_SMALL, 0.8f, 1.0f, pan_of(at));
            }
        }
        break;
    }
    case EV_BJ_JACKPOT:
        if (f && is_you(g, s) && !v->demo) {
            P.round_jackpot = 1;
            celeb_start(&P.cel, WIN_JACKPOT, e->v, "HONEY POT!", hand_pos(g, s, 0, 1));
            celeb_set_label(&P.cel, "HONEY POT PAYS");
            P.st.celebrations++;
        } else if (f) {
            snprintf(b, sizeof b, "%s HITS THE HONEY POT!", seat_name(g, s));
            say_at(640, 300, b, UI_GOLD, 3.5f, 30, 1);
            pfx_confetti_burst(polar(s, F_AVATAR).x, polar(s, F_AVATAR).y, 0.7f);
        }
        P.pot_bump = 1;
        break;
    case EV_BJ_POT:
        P.pot_bump = fmaxf(P.pot_bump, 0.35f);
        break;
    case EV_BJ_INSURE:
        if (!f) break;
        set_act(s, e->b ? "INSURED" : "NO INSURANCE", e->b ? UI_CYAN : (Color){ 170, 160, 170, 255 });
        if (e->b && g->seat[s].kind == BJ_SEAT_BOT && g->seat[s].bot == BOT_QUEENIE && fx_randf(0, 1) < 0.5f)
            set_act(s, "INSURANCE, DARLING", UI_CYAN);
        break;
    case EV_BJ_INS_RESULT:
        if (!f) break;
        pay_out(g, s, SL_INS, g->seat[s].insurance, e->v, slot_pos(g, s, SL_INS, f->nh), 0.1f);
        break;
    case EV_BJ_TURN:
        P.turn_t = 0;
        if (f && is_you(g, s) && !v->demo) sfx(SFX_YOUR_TURN, 0.7f, 1.0f, 0);
        if (f) f->last_hand = e->b;
        break;
    case EV_BJ_ACTION: {
        if (!f) break;
        f->last_hand = e->b < NH ? e->b : 0;
        static const Color cols[BJ_ACT_N] = { { 0 }, { 120, 255, 160, 255 }, { 255, 220, 120, 255 }, { 255, 150, 40, 255 },
                                               { 40, 230, 255, 255 }, { 200, 180, 255, 255 } };
        int act = e->v >= 0 && e->v < BJ_ACT_N ? e->v : 0;
        const char *q = g->seat[s].kind == BJ_SEAT_BOT ? quip(g->seat[s].bot, act) : NULL;
        set_act(s, q ? q : bj_action_name(act), cols[act]);
        if (act == BJ_ACT_STAND) sfx(SFX_CHECK, 0.6f, 1.0f, pan_of(polar(s, F_CARDS)));
        if (act == BJ_ACT_DOUBLE || act == BJ_ACT_SPLIT) sfx(SFX_HOLD_ON, 0.6f, 1.0f, pan_of(polar(s, F_CARDS)));
        if (act == BJ_ACT_SURRENDER) {
            for (int k = 0; k < f->n[f->last_hand]; k++) f->card[f->last_hand][k].dim = 0.5f;
            sfx(SFX_FOLD, 0.6f, 1.0f, pan_of(polar(s, F_CARDS)));
        }
        break;
    }
    case EV_BJ_SPLIT: {
        if (!f) break;
        int src = f->last_hand, dst = e->b < NH ? e->b : NH - 1;
        if (f->n[src] >= 2) {
            f->card[dst][0] = f->card[src][1];
            memset(&f->card[src][1], 0, sizeof f->card[src][1]);
            f->n[src] = 1;
            f->n[dst] = 1;
        }
        if (dst + 1 > f->nh) f->nh = dst + 1;
        relayout(g, s, 0.3f);
        /* The chips of every hand slide to their new places too. */
        for (int h = 0; h < f->nh; h++) {
            int64_t a = f->spot[SL_HAND0 + h];
            if (a > 0 && h != dst) {
                f->spot[SL_HAND0 + h] = 0;
                Flight *m = fly(FL_IN, s, SL_HAND0 + h, a, slot_pos(g, s, SL_HAND0 + h, f->nh - 1),
                                slot_pos(g, s, SL_HAND0 + h, f->nh), 0, 0.25f);
                if (m) m->sound = 0;
            }
        }
        break;
    }
    case EV_BJ_BUST:
        if (s == BJ_DEALER) {
            say_at(640, DEALER_Y, "DEALER BUSTS", (Color){ 120, 255, 160, 255 }, 2.2f, 28, 1);
            sfx(SFX_REVEAL, 0.6f, 1.2f, 0);
            break;
        }
        if (!f) break;
        set_act(s, "BUST", (Color){ 255, 100, 110, 255 });
        f->av.hurt = 1;
        for (int k = 0; k < f->n[e->b < NH ? e->b : 0]; k++) f->card[e->b < NH ? e->b : 0][k].dim = 0.45f;
        if (is_you(g, s)) sfx(SFX_BUST, 0.5f, 1.0f, 0);
        break;
    case EV_BJ_BLACKJACK:
        if (s == BJ_DEALER) {
            say_at(640, DEALER_Y, "DEALER BLACKJACK", (Color){ 255, 110, 120, 255 }, 2.4f, 28, 1);
            sfx(SFX_ERROR, 0.5f, 0.8f, 0);
            break;
        }
        if (!f) break;
        set_act(s, "BLACKJACK!", UI_GOLD);
        f->av.react = 1;
        pfx_sparkle(hand_pos(g, s, 0, 1).x, hand_pos(g, s, 0, 1).y, 70, 14);
        if (is_you(g, s)) { P.round_bj = 1; sfx(SFX_REVEAL, 0.8f, 1.1f, 0); }
        break;
    case EV_BJ_RESULT: {
        if (!f) break;
        int h = e->b & 15, res = e->b >> 4;
        if (h >= NH) break;
        f->res[h] = res;
        f->res_t[h] = 0;
        int64_t stake = g->seat[s].hand[h].bet;
        pay_out(g, s, SL_HAND0 + h, stake, e->v, slot_pos(g, s, SL_HAND0 + h, f->nh), 0.15f + 0.1f * (float)h);
        if (res == BJ_RES_WIN || res == BJ_RES_BLACKJACK) f->av.react = 1;
        if (res == BJ_RES_LOSE) f->av.hurt = fmaxf(f->av.hurt, 0.6f);
        break;
    }
    case EV_BJ_ROUND:
        P.st.rounds_seen++;
        P.round_net = e->v;
        human_round_end(v, e->v);
        break;
    case EV_BJ_DENIED:
        if (v->demo) break;
        sfx(SFX_ERROR, 0.5f, 1.0f, 0);
        if (e->a == BJ_DENY_CREDITS) say_at(640, 610, "NOT ENOUGH CREDITS - PRESS COIN", UI_AMBER, 2.0f, 22, 0);
        else if (e->a == BJ_DENY_MIN_BET) say_at(640, 610, "THAT IS THE TABLE MINIMUM", UI_AMBER, 1.6f, 22, 0);
        else if (g->phase == BJ_PH_BETTING) say_at(640, 610, "THAT IS THE TABLE MAXIMUM", UI_AMBER, 1.6f, 22, 0);
        else {
            snprintf(b, sizeof b, "YOU CAN'T %s NOW", bj_action_name(e->b));
            say_at(640, 610, b, UI_AMBER, 1.6f, 22, 0);
        }
        break;
    case EV_BJ_BETS:
        if (v->demo) break;
        P.bet_bump = 1;
        if (e->a == BJ_BET_MAIN) sfx(SFX_BET_ONE, 0.7f, 1.0f + 0.04f * (float)(e->v > 0 ? log10((double)e->v) : 0), 0);
        else sfx(e->v ? SFX_CHIP_SINGLE : SFX_HOLD_OFF, 0.7f, 1.0f, 0);
        break;
    default:
        break;
    }
    (void)n1;
}

/* ---- celebration sounds ------------------------------------------------------------------ */

static void on_cue(void *user, CelebCue cue, float a)
{
    (void)user;
#if defined(BPL_HAVE_AUDIO)
    switch (cue) {
    case CUE_CHIME: sfx(SFX_WIN_SMALL, 1, 1, 0); break;
    case CUE_COINS: sfx(SFX_WIN_MEDIUM, 1, 1, 0); break;
    case CUE_BIG: sfx(SFX_WIN_BIG, 1, 1, 0); break;
    case CUE_JACKPOT: sfx(SFX_WIN_JACKPOT, 1, 1, 0); break;
    case CUE_REVEAL: sfx(SFX_REVEAL, 1, 1, 0); break;
    case CUE_TICK: sfx(SFX_CREDIT_TICK, 0.45f, a, 0); break;
    case CUE_COUNT_END: sfx(SFX_CREDIT_END, 0.9f, 1, 0); break;
    case CUE_SKIP: if (P.snd) audio_stop(SFX_WIN_JACKPOT); break;
    default: break;
    }
#else
    (void)cue; (void)a;
#endif
}

static void init_once(void)
{
    if (P.inited) return;
    memset(&P, 0, sizeof P);
    clock_init(&P.clock);
    shake_init(&P.shake);
    bulbs_init(&P.bulbs, (Rectangle){ 10, 10, PLAY_W - 20, PLAY_H - 20 }, 34);
    celeb_init(&P.cel, &P.shake, &P.clock, &P.bulbs, on_cue, NULL);
    P.shuffle_t = -1;
    P.inited = 1;
}

/* ---- the button bar ------------------------------------------------------------------------ */

enum { B_1, B_2, B_3, B_4, B_5, B_DEAL, B_N };
static const uint32_t k_keys[B_N] = { BTN_HOLD1, BTN_HOLD2, BTN_HOLD3, BTN_HOLD4, BTN_HOLD5, BTN_DEAL };

typedef struct {
    UiButtonState st[B_N];
    char  label[B_N][32];
    Color col[B_N];
    int   best;             /* the hinted button, or -1 */
    int   mode;             /* 0 waiting, 1 betting, 2 your turn, 3 insurance */
} BarState;

static void bar_state(const TableViewInfo *v, BarState *b)
{
    const BjGame *g = v->game;
    char n1[24];
    memset(b, 0, sizeof *b);
    b->best = -1;
    static const char *const play[B_N] = { "HIT", "STAND", "DOUBLE", "SPLIT", "SURRENDER", "DEAL" };
    for (int i = 0; i < B_N; i++) {
        copy_str(b->label[i], 32, play[i]);
        b->st[i] = BTN_STATE_OFF;
    }
    b->col[B_1] = (Color){ 120, 255, 160, 255 };
    b->col[B_2] = (Color){ 255, 210, 90, 255 };
    b->col[B_3] = UI_AMBER;
    b->col[B_4] = UI_CYAN;
    b->col[B_5] = (Color){ 200, 170, 255, 255 };
    b->col[B_DEAL] = UI_GOLD;
    if (g->phase == BJ_PH_BETTING) {
        b->mode = 1;
        b->col[B_1] = UI_MAGENTA;
        b->col[B_2] = UI_CYAN;
        b->col[B_3] = UI_AMBER;
        b->col[B_4] = b->col[B_5] = (Color){ 255, 220, 140, 255 };
        if (g->side[BJ_SIDE_PP]) snprintf(b->label[B_1], 32, "PAIRS %s", dv_chips(g->side[BJ_SIDE_PP], n1, sizeof n1));
        else copy_str(b->label[B_1], 32, "PAIRS OFF");
        if (g->side[BJ_SIDE_213]) snprintf(b->label[B_2], 32, "21+3 %s", dv_chips(g->side[BJ_SIDE_213], n1, sizeof n1));
        else copy_str(b->label[B_2], 32, "21+3 OFF");
        copy_str(b->label[B_3], 32, g->side[BJ_SIDE_HONEY] ? "HONEY POT ON" : "HONEY POT OFF");
        copy_str(b->label[B_4], 32, "BET LESS");
        copy_str(b->label[B_5], 32, "BET MORE");
        int64_t total = g->bet;
        for (int k = 0; k < BJ_SIDE_N; k++) total += g->side[k];
        snprintf(b->label[B_DEAL], 32, "DEAL %s", dv_chips(total, n1, sizeof n1));
        for (int i = 0; i < B_N; i++) b->st[i] = BTN_STATE_ON;
        if (g->side[BJ_SIDE_PP]) b->st[B_1] = BTN_STATE_LIT;
        if (g->side[BJ_SIDE_213]) b->st[B_2] = BTN_STATE_LIT;
        if (g->side[BJ_SIDE_HONEY]) b->st[B_3] = BTN_STATE_LIT;
        b->st[B_DEAL] = v->credits >= total ? BTN_STATE_LIT : BTN_STATE_OFF;
        return;
    }
    if (v->insurance) {
        b->mode = 3;
        int hs = g->cfg.human_seat;
        const BjHand *h = &g->seat[hs].hand[0];
        int bj = h->n == 2 && bj_total(h->c, 2).total == 21;
        if (bj) copy_str(b->label[B_1], 32, "EVEN MONEY");
        else snprintf(b->label[B_1], 32, "INSURE %s", dv_chips(h->bet / 2, n1, sizeof n1));
        copy_str(b->label[B_2], 32, "NO THANKS");
        b->col[B_1] = UI_CYAN;
        b->col[B_2] = (Color){ 230, 220, 230, 255 };
        b->st[B_1] = BTN_STATE_LIT;
        b->st[B_2] = BTN_STATE_ON;
        return;
    }
    if (v->your_turn) {
        b->mode = 2;
        b->st[B_1] = b->st[B_2] = BTN_STATE_ON;
        if (v->can & BJ_CAN_DOUBLE) b->st[B_3] = BTN_STATE_ON;
        if (v->can & BJ_CAN_SPLIT) b->st[B_4] = BTN_STATE_ON;
        if (v->can & BJ_CAN_SURRENDER) b->st[B_5] = BTN_STATE_ON;
        if (v->hint_on && v->advice) {
            static const int bi[BJ_ACT_N] = { -1, B_1, B_2, B_3, B_4, B_5 };
            b->best = bi[v->advice->best];
            if (b->best >= 0) b->st[b->best] = BTN_STATE_LIT;
        }
    }
}

static void bar_rects(Rectangle r[B_N])
{
    static const float w[B_N] = { 190, 190, 190, 190, 190, 236 };
    float total = 0;
    for (int i = 0; i < B_N; i++) total += w[i];
    total += 10.0f * (B_N - 1);
    float x = (PLAY_W - total) * 0.5f;
    for (int i = 0; i < B_N; i++) {
        r[i] = (Rectangle){ x, BAR_Y + 4, w[i], 58 };
        x += w[i] + 10;
    }
}

static void handle_presses(const TableViewInfo *v, float dt)
{
    BarState b;
    bar_state(v, &b);
    for (int i = 0; i < B_N; i++) ui_button_update(&P.btn[i], b.st[i], dt);
    if (v->demo) return;
    uint32_t pr = v->pressed;
    for (int i = 0; i < B_N; i++) {
        if (!(pr & k_keys[i])) continue;
        ui_button_press(&P.btn[i]);
        if (b.mode == 2 || b.mode == 3) sfx(b.st[i] == BTN_STATE_OFF ? SFX_ERROR : SFX_BUTTON, b.st[i] == BTN_STATE_OFF ? 0.3f : 0.7f, 1, 0);
    }
}

/* ---- update ---------------------------------------------------------------------------------- */

void table_present_update(const TableViewInfo *v, const GameEvent *ev, int nev, float dt)
{
    init_once();
    const BjGame *g = v->game;
    P.snd = !v->demo;
    if (g != P.game || v->demo != P.demo) { P.demo = v->demo; reset_all(g); }
    float sdt = clock_step(&P.clock, dt);
    P.time += sdt;
    shake_update(&P.shake, dt);
    P.deal_stagger = 0;
    for (int i = 0; i < nev; i++)
        if (ev[i].type >= EV_BJ_PHASE && ev[i].type <= EV_BJ_CREDITS) on_event(v, &ev[i]);
    handle_presses(v, dt);
    if (P.cel.active && (v->pressed & ~(uint32_t)(BTN_DEBUG | BTN_COIN)) && !v->demo) celeb_skip(&P.cel);
    /* Measuring (the cabinet perf pass): BJ_TAKEOVER=1 plays the HONEY POT
       takeover over the live table again and again - the heaviest scene -
       without touching the game. Display only; the cards are the real ones. */
    static int takeover_env = -1;
    if (takeover_env < 0) takeover_env = getenv("BJ_TAKEOVER") != NULL;
    if (takeover_env && g && !P.cel.active && P.time > 3.0f) {
        celeb_start(&P.cel, WIN_JACKPOT, BJ_HONEY_SEED, "HONEY POT!", (Vector2){ 640, 380 });
        celeb_set_label(&P.cel, "HONEY POT PAYS");
    }
    celeb_update(&P.cel, sdt, dt);
    pfx_update(sdt);
    int big = P.cel.active && (P.cel.tier == WIN_BIG || P.cel.tier == WIN_JACKPOT);
    P.bulbs_k += ((big ? 1.0f : 0.0f) - P.bulbs_k) * clampf(dt * 3, 0, 1);
    if (P.bulbs_k > 0.01f) bulbs_update(&P.bulbs, dt);
    P.rail_mode = big ? 2 : v->your_turn || v->insurance ? 1 : 0;

    int landed = 0, flipped = 0;
    float land_pan = 0;
    int ncards = 0;
    for (int s = 0; s < NS; s++)
        for (int h = 0; h < NH; h++)
            for (int k = 0; k < NC; k++) {
                CardFx *c = &P.seat[s].card[h][k];
                cf_update(c, sdt);
                if (c->ev & 1) { landed++; land_pan = pan_of((Vector2){ c->x, c->y }); }
                if (c->ev & 2) flipped++;
                ncards += c->on;
            }
    for (int k = 0; k < NC; k++) {
        cf_update(&P.dealer[k], sdt);
        if (P.dealer[k].ev & 1) landed++;
        if (P.dealer[k].ev & 2) flipped++;
        ncards += P.dealer[k].on;
    }
    if (landed) sfx(SFX_CARD_DEAL, 0.65f, 1.0f, land_pan);
    if (flipped) sfx(SFX_CARD_FLIP, 0.55f, 1.0f, 0);
    P.st.cards_live = ncards;
    flights_update(sdt);

    for (int i = 0; i < NPILL; i++) {
        Pill *p = &P.pill[i];
        if (p->on && (p->t += dt) >= p->dur) p->on = 0;
    }
    for (int s = 0; s < NS; s++) {
        SeatFx *f = &P.seat[s];
        if (f->act_t >= 0) f->act_t += dt;
        for (int h = 0; h < NH; h++) f->res_t[h] += dt;
        float want = g && g->seat[s].kind == BJ_SEAT_BOT ? 1.0f : 0.0f;
        if (g && g->seat[s].kind == BJ_SEAT_BOT) f->who = avatar_of_bot(g->seat[s].bot);
        f->present += (want - f->present) * clampf(dt * 4, 0, 1);
        update_avatar(g, s, sdt);
    }
    if (P.shuffle_t >= 0 && (P.shuffle_t += dt) > 2.4f) P.shuffle_t = -1;
    P.peek_t += dt;
    if (P.peek > 0 && P.peek_t > 1.0f) P.peek = 0;
    P.turn_t += dt;
    P.bet_bump = fmaxf(0, P.bet_bump - dt * 4);
    P.pot_bump = fmaxf(0, P.pot_bump - dt * 1.5f);
    P.rail_mode_t += dt;
    /* The credit meter runs up to wins, drops at once for bets. */
    if (v->credits < P.credits_shown || P.credits_shown <= 0) P.credits_shown = (double)v->credits;
    else P.credits_shown += ((double)v->credits - P.credits_shown) * clampf(dt * 5, 0, 1);
    if (fabs((double)v->credits - P.credits_shown) < 0.5) P.credits_shown = (double)v->credits;
    if (g) {
        if (g->pot < P.pot_shown) P.pot_shown = g->pot;
        else if (g->pot > P.pot_shown) P.pot_shown += (g->pot - P.pot_shown + 9) / 10;
    }

    /* At rest, the picture must be the game's. */
    if (g && !nev && !cards_moving()) {
        int any = 0;
        for (int i = 0; i < NFLIGHT; i++) any |= P.fl[i].on;
        if (!any && !matches_game(g)) { snap_to_game(g); P.st.desyncs++; }
    }
}

const TablePresentStats *table_present_stats(void) { return &P.st; }

/* ---- drawing ------------------------------------------------------------------------------------ */

static void draw_backdrop(double time, float dim)
{
    unsigned tex = hart_table_tex();
    if (!tex) { ui_background(time, dim); return; }
    if (!post_backdrop(tex, clampf(dim, 0, 1))) {
        unsigned char lv = (unsigned char)(255 * clampf(dim, 0, 1));
        Spr s = { 0, 0, 1, 1, PLAY_W, PLAY_H, tex };
        gfx_flush();
        rlDisableColorBlend();
        gfx_spr(&s, 0, 0, PLAY_W, PLAY_H, (PCol){ lv, lv, lv, 255 });
        gfx_flush();
        rlEnableColorBlend();
    }
}

static Color hue(float h)
{
    h = fmodf(h, 1.0f) * 6;
    float x = 1 - fabsf(fmodf(h, 2) - 1);
    float r = 0, g = 0, b = 0;
    if (h < 1) { r = 1; g = x; } else if (h < 2) { r = x; g = 1; } else if (h < 3) { g = 1; b = x; }
    else if (h < 4) { g = x; b = 1; } else if (h < 5) { r = x; b = 1; } else { r = 1; b = x; }
    return (Color){ (unsigned char)(255 * r), (unsigned char)(255 * g), (unsigned char)(255 * b), 255 };
}

/* The neon inlay along the curved rail, lit. */
static void draw_rail_neon(double time)
{
    const int N = 90;
    float t = (float)time;
    const Spr *gl = sprite(SPR_GLOW);
    for (int i = 0; i < N; i++) {
        float a0 = PI_F * (float)i / N, a1 = PI_F * (float)(i + 1) / N;
        Vector2 a = { BT_CX + (BT_A - BT_NEON) * cosf(a0), BT_CY + (BT_B - BT_NEON) * sinf(a0) };
        Vector2 b = { BT_CX + (BT_A - BT_NEON) * cosf(a1), BT_CY + (BT_B - BT_NEON) * sinf(a1) };
        float u = (float)i / N;
        Color c = UI_MAGENTA;
        float k;
        switch (P.rail_mode) {
        case 1: c = UI_HONEY; k = 0.22f + 0.16f * (0.5f + 0.5f * sinf(t * 4.0f)); break;
        case 2: c = hue(u * 2 - t * 0.5f); k = 0.3f + 0.35f * powf(0.5f + 0.5f * sinf(u * 2 * PI_F * 8 - t * 12.0f), 4); break;
        default: k = 0.16f + 0.10f * (0.5f + 0.5f * sinf(u * 2 * PI_F * 2 - t * 0.9f)); break;
        }
        gfx_streak(gl, a, b, 13, gfx_add(c, k));
    }
}

/* A felt line that follows the table's curve: each glyph dropped by a
   parabola so the words arc like the printing on a real layout. */
typedef struct { float cx, k; const char *s; float size; FontId f; } ArcText;

static void arc_glyph(int i, float *dx, float *dy, float *scale, float *alpha, Color *tint, void *user)
{
    const ArcText *a = (const ArcText *)user;
    (void)scale; (void)alpha; (void)tint; (void)dx;
    /* The glyph's x is not passed in; estimate it from its index. */
    int n = (int)strlen(a->s);
    float w = text_width(a->f, a->s, a->size, 1);
    float x = -w * 0.5f + w * ((float)i + 0.5f) / (float)(n > 0 ? n : 1);
    *dy += a->k * x * x;
}

static void felt_text(FontId f, const char *s, float cx, float cy, float size, float k, Color c, float glow)
{
    ArcText a = { cx, k, s, size, f };
    TextStyle t = style(c, ALIGN_CENTER);
    t.spacing = 1;
    t.fn = arc_glyph;
    t.user = &a;
    t.glow = c;
    t.glow_k = glow;
    text_draw_ex(f, s, cx, cy - size * 0.56f, size, &t);
}

static void draw_felt_print(void)
{
    felt_text(FONT_DISP_S, "BLACKJACK PAYS 3 TO 2", 640, 286, 26, -0.0011f, (Color){ 238, 190, 92, 210 }, 0.25f);
    felt_text(FONT_UI_S, "DEALER STANDS ON ALL 17S   -   INSURANCE PAYS 2 TO 1", 640, 312, 15, -0.0007f,
              (Color){ 200, 160, 120, 170 }, 0);
}

static void draw_spots(const TableViewInfo *v, double time)
{
    const BjGame *g = v->game;
    float t = (float)time;
    for (int s = 0; s < NS; s++) {
        int seated = g->seat[s].kind != BJ_SEAT_EMPTY;
        float on = seated ? 1.0f : 0.35f;
        int you = is_you(g, s);
        Vector2 b = slot_pos(g, s, SL_HAND0, 1);
        float r = you ? 36 : 30;
        Color ring = you ? UI_GOLD : (Color){ 210, 160, 80, 255 };
        if (you && g->phase == BJ_PH_BETTING && !v->demo)
            gfx_spr_rot(sprite(SPR_GLOW), b.x, b.y, r * 4, r * 3, 0, gfx_add(UI_GOLD, 0.12f + 0.08f * sinf(t * 3)));
        gfx_ring(b.x, b.y, r, 2.0f, gfx_cola(ring, 0.55f * on));
        gfx_ring(b.x, b.y, r - 5, 1.0f, gfx_cola(ring, 0.25f * on));
        /* The side-bet spots: a small ring for each, a hexagon for the pot. */
        static const Color sc[BJ_SIDE_N] = { { 255, 40, 200, 255 }, { 40, 230, 255, 255 }, { 255, 150, 40, 255 } };
        static const char *const sl[BJ_SIDE_N] = { "PP", "21+3", "" };
        for (int k = 0; k < BJ_SIDE_N; k++) {
            Vector2 p = slot_pos(g, s, SL_SIDE0 + k, 1);
            float lit = you && g->side[k] && g->phase == BJ_PH_BETTING ? 1.0f : 0.0f;
            if (lit > 0) gfx_spr_rot(sprite(SPR_GLOW), p.x, p.y, 60, 60, 0, gfx_add(sc[k], 0.25f));
            if (k == BJ_SIDE_HONEY)
                gfx_spr_rot(sprite(SPR_HEX_GLOW), p.x, p.y, 40, 40, 0, gfx_add(sc[k], (0.18f + 0.3f * lit) * on));
            else
                gfx_ring(p.x, p.y, 17, 1.5f, gfx_cola(sc[k], (0.45f + 0.4f * lit) * on));
            if (sl[k][0]) label(FONT_UI_S, sl[k], p.x, p.y, 12, fade(sc[k], 0.55f * on), ALIGN_CENTER, 0);
        }
    }
}

static void draw_chips(const BjGame *g)
{
    for (int s = 0; s < NS; s++) {
        const SeatFx *f = &P.seat[s];
        for (int slot = 0; slot < NSLOT; slot++) {
            if (f->spot[slot] <= 0) continue;
            Vector2 p = slot_pos(g, s, slot, f->nh);
            float sc = slot >= SL_SIDE0 ? 0.48f : 0.62f;
            draw_pile(f->spot[slot], p.x, p.y + 10, sc, 1, slot >= SL_SIDE0 ? 2 : 3);
        }
    }
    /* The player's bet while betting, before it is on the table. */
}

static void draw_pending_bets(const TableViewInfo *v)
{
    const BjGame *g = v->game;
    int hs = g->cfg.human_seat;
    if (v->demo || hs < 0 || g->phase != BJ_PH_BETTING || g->seat[hs].kind != BJ_SEAT_HUMAN) return;
    /* What DEAL will bet, shown as ghost chips on the empty spots. */
    float a = g->table_dirty ? 0.0f : 0.75f;
    if (a <= 0) return;
    draw_pile(g->bet, slot_pos(g, hs, SL_HAND0, 1).x, slot_pos(g, hs, SL_HAND0, 1).y + 10, 0.62f * (1 + 0.1f * P.bet_bump), a, 3);
    for (int k = 0; k < BJ_SIDE_N; k++)
        if (g->side[k]) {
            Vector2 p = slot_pos(g, hs, SL_SIDE0 + k, 1);
            draw_pile(g->side[k], p.x, p.y + 10, 0.48f, a, 2);
        }
}

static void draw_shoe(double time)
{
    float t = (float)time;
    float glow = P.shuffle_t >= 0 ? 0.6f + 0.4f * sinf(t * 10) : 0.12f;
    gfx_spr_rot(sprite(SPR_GLOW), SHOE_X, SHOE_Y + 6, 170, 120, 0, gfx_add(UI_CYAN, 0.18f * glow));
    gfx_nine(sprite_nine(NINE_SHADOW), SHOE_X - 70, SHOE_Y - 40, 140, 96, 30, gfx_cola(BLACK, 0.6f));
    /* The shoe: a smoked-glass box with the cards' backs showing at its mouth. */
    for (int i = 0; i < 4; i++) {
        CardPose p;
        memset(&p, 0, sizeof p);
        p.x = SHOE_X - 6 + (float)i * 3;
        p.y = SHOE_Y + 2 - (float)i * 1.5f;
        p.scale = 0.42f;
        p.rot = -1.2f;
        p.no_shadow = i > 0;
        card_draw(0, CARD_M, &p);
    }
    gfx_nine(sprite_nine(NINE_RRECT), SHOE_X - 52, SHOE_Y - 30, 104, 64, 12, gfx_cola((Color){ 30, 20, 34, 255 }, 0.55f));
    gfx_nine(sprite_nine(NINE_RRECT_LINE), SHOE_X - 52, SHOE_Y - 30, 104, 64, 12, gfx_cola(UI_GOLD, 0.55f));
    label(FONT_UI_S, "SHOE", SHOE_X, SHOE_Y + 46, 13, (Color){ 200, 170, 120, 200 }, ALIGN_CENTER, 0);
    /* The discard tray. */
    gfx_nine(sprite_nine(NINE_RRECT), TRAY_X - 48, TRAY_Y - 30, 96, 60, 12, gfx_cola((Color){ 20, 12, 22, 255 }, 0.6f));
    gfx_nine(sprite_nine(NINE_RRECT_LINE), TRAY_X - 48, TRAY_Y - 30, 96, 60, 12, gfx_cola(UI_GOLD, 0.4f));
    label(FONT_UI_S, "DISCARDS", TRAY_X, TRAY_Y + 46, 13, (Color){ 200, 170, 120, 200 }, ALIGN_CENTER, 0);
}

static void hand_total_text(const Card *c, int n, char *out, size_t cap)
{
    BjTotal t = bj_total(c, n);
    if (t.total > 21) snprintf(out, cap, "BUST %d", t.total & 63);
    else if (n == 2 && t.total == 21) snprintf(out, cap, "21");
    else if (t.soft && t.total < 21) snprintf(out, cap, "%d / %d", (t.total - 10) & 63, t.total & 63);
    else snprintf(out, cap, "%d", t.total & 63);
}

static void draw_dealer(const BjGame *g, double time)
{
    for (int k = 0; k < NC; k++) {
        const CardFx *c = &P.dealer[k];
        float lift = (k == 1 && P.peek > 0) ? 6.0f * sinf(clampf(P.peek_t / 0.9f, 0, 1) * PI_F) : 0;
        cf_draw(c, 0, UI_GOLD, lift);
    }
    if (P.peek > 0 && P.dn > 1) {
        float k = sinf(clampf(P.peek_t / 0.9f, 0, 1) * PI_F);
        Vector2 p = dealer_card(1, P.dn);
        gfx_spr_rot(sprite(SPR_GLOW), p.x, p.y - 40, 90, 40, 0, gfx_add(UI_CYAN, 0.35f * k));
        label(FONT_UI_S, "CHECKING FOR BLACKJACK", 640, DEALER_Y - 72, 15, fade(UI_CYAN, k), ALIGN_CENTER, 0.6f);
    }
    /* The dealer's total: what is face up, until the hole card turns. */
    int shown = 0;
    for (int k = 0; k < P.dn; k++) if (P.dealer[k].on && P.dealer[k].flip > 0.5f && !P.dealer[k].moving) shown++;
    if (shown > 0 && g->ndealer > 0) {
        char b[24], s[32];
        int n = g->hole_shown ? shown : 1;
        if (n > g->ndealer) n = g->ndealer;
        hand_total_text(g->dealer, n, b, sizeof b);
        snprintf(s, sizeof s, "DEALER %s", b);
        BjTotal t = bj_total(g->dealer, n);
        Color c = t.total > 21 ? (Color){ 255, 110, 120, 255 } : (Color){ 240, 230, 220, 255 };
        pill_draw(640, DEALER_Y + 76, s, FONT_DISP_S, 18, c, (Color){ 150, 120, 90, 255 }, 1, 1);
    }
    (void)time;
}

static void draw_hands(const TableViewInfo *v, double time)
{
    const BjGame *g = v->game;
    float t = (float)time;
    for (int s = 0; s < NS; s++) {
        const SeatFx *f = &P.seat[s];
        const BjSeat *st = &g->seat[s];
        for (int h = 0; h < f->nh; h++) {
            int active = g->phase == BJ_PH_PLAY && g->turn == s && st->cur == h && st->nhands > h;
            float glow = active ? 0.55f + 0.3f * sinf(t * 5) : 0;
            Color gc = is_pseat(g, s) ? UI_GOLD : hart_rig(f->who)->theme;
            for (int k = 0; k < f->n[h]; k++) cf_draw(&f->card[h][k], glow, gc, 0);
            /* The total over the hand, once its cards have landed. */
            if (f->n[h] >= 1 && h < st->nhands && st->hand[h].n == f->n[h] && !cards_moving()) {
                char b[24];
                hand_total_text(st->hand[h].c, st->hand[h].n, b, sizeof b);
                if (st->nhands == 1 && st->hand[h].n == 2 && bj_total(st->hand[h].c, 2).total == 21) copy_str(b, sizeof b, "BLACKJACK");
                Vector2 c = hand_pos(g, s, h, f->nh);
                float sc = card_sc(g, s);
                float y = c.y - 92 * sc - 6;
                BjTotal tt = bj_total(st->hand[h].c, st->hand[h].n);
                Color col = tt.total > 21 ? (Color){ 255, 120, 130, 255 } : tt.total == 21 ? UI_GOLD : (Color){ 245, 236, 220, 255 };
                if (f->res[h] && f->res_t[h] > 0) {
                    copy_str(b, sizeof b, bj_result_name(f->res[h]));
                    if (f->res[h] == BJ_RES_BLACKJACK) copy_str(b, sizeof b, "BLACKJACK 3:2");
                    col = res_color(f->res[h]);
                    float k = ease(EASE_OUT_BACK, clampf(f->res_t[h] / 0.3f, 0, 1));
                    pill_draw(c.x, y, b, FONT_DISP_S, is_pseat(g, s) ? 20 : 15, col, col, 1, k);
                } else {
                    pill_draw(c.x, y, b, FONT_DISP_S, is_pseat(g, s) ? 19 : 14, col, active ? gc : (Color){ 0 }, 0.95f, 1);
                }
            }
        }
    }
}

static void draw_seats(const TableViewInfo *v, double time)
{
    const BjGame *g = v->game;
    float t = (float)time;
    char b[32];
    for (int s = 0; s < NS; s++) {
        const SeatFx *f = &P.seat[s];
        const BjSeat *st = &g->seat[s];
        if (is_pseat(g, s)) continue;
        if (f->present < 0.02f) {
            /* An empty seat: the stool's place, faint. */
            Vector2 a = polar(s, F_AVATAR);
            gfx_ring(a.x, a.y, AV_D * 0.42f, 2, gfx_cola((Color){ 120, 90, 70, 255 }, 0.35f));
            label(FONT_UI_S, "OPEN SEAT", a.x, a.y, 13, (Color){ 150, 130, 120, 160 }, ALIGN_CENTER, 0);
            continue;
        }
        Vector2 a = polar(s, F_AVATAR);
        int acting = g->phase == BJ_PH_PLAY && g->turn == s;
        const AvatarRig *rg = hart_rig(f->who);
        if (acting) {
            float pulse = 0.6f + 0.4f * sinf(t * 5);
            gfx_spr_rot(sprite(SPR_RING), a.x, a.y, AV_D * 1.5f, AV_D * 1.5f, 0, gfx_add(rg->theme, 0.8f * pulse));
            float a0 = t * 4.2f;
            for (int i = 0; i < 9; i++) {
                float ang = a0 - (float)i * 0.13f, rr = AV_D * 0.585f;
                gfx_spr_rot(sprite(SPR_GLOW), a.x + rr * cosf(ang), a.y + rr * sinf(ang), 22 - (float)i, 22 - (float)i, 0,
                            gfx_add(rg->theme, 0.9f * (1 - (float)i / 9.0f)));
            }
        }
        float bright = st->in_round || g->phase == BJ_PH_BETTING ? 1.0f : 0.7f;
        draw_avatar(f->who, &f->av, a.x, a.y + (1 - f->present) * 30, AV_D, bright, f->present);
        /* The nameplate under the portrait: name, style, chips. */
        Vector2 p = plate_pos(s);
        Rectangle r = { p.x - 74, p.y - 20, 148, 42 };
        gfx_nine_vgrad(sprite_nine(NINE_RRECT), r.x, r.y, r.width, r.height, 12, gfx_cola((Color){ 40, 28, 44, 255 }, 0.94f * f->present),
                       gfx_cola((Color){ 12, 8, 14, 255 }, 0.94f * f->present));
        gfx_nine(sprite_nine(NINE_RRECT_LINE), r.x, r.y, r.width, r.height, 12,
                 gfx_cola(acting ? rg->theme : (Color){ 160, 120, 60, 255 }, f->present));
        label(FONT_UI_M, seat_name(g, s), r.x + 10, r.y + 13, 19, fade(UI_IVORY, f->present), ALIGN_LEFT, 0);
        const char *sty = st->kind == BJ_SEAT_BOT ? bj_bot_style(st->bot) : "";
        if (sty[0]) label(FONT_UI_S, sty, r.x + r.width - 8, r.y + 12, 11, fade(rg->theme, 0.9f * f->present), ALIGN_RIGHT, 0);
        TextStyle ts = style((Color){ 255, 236, 170, 255 }, ALIGN_LEFT);
        ts.color2 = (Color){ 235, 160, 40, 255 };
        ts.opacity = f->present;
        text_draw_ex(FONT_DISP_S, dv_chips(st->mem.bank, b, sizeof b), r.x + 10, r.y + 30 - 17 * 0.56f, 17, &ts);
        /* The last thing it did, over the portrait. */
        if (f->act_t >= 0 && f->act[0] && f->act_t < 2.6f) {
            float sc = ease(EASE_OUT_BACK, clampf(f->act_t / 0.22f, 0, 1));
            float al = f->act_t > 2.0f ? 1 - (f->act_t - 2.0f) / 0.6f : 1;
            pill_draw(a.x, a.y - AV_D * 0.5f - 14, f->act, FONT_UI_M, 17, WHITE, f->act_col, al, sc);
        }
    }
    /* The player's last play, just under their cards. */
    int hs = g->cfg.human_seat;
    if (hs >= 0 && hs < NS && is_you(g, hs)) {
        const SeatFx *f = &P.seat[hs];
        if (f->act_t >= 0 && f->act[0] && f->act_t < 1.6f) {
            float sc = ease(EASE_OUT_BACK, clampf(f->act_t / 0.22f, 0, 1));
            float al = f->act_t > 1.2f ? 1 - (f->act_t - 1.2f) / 0.4f : 1;
            Vector2 c = hand_pos(g, hs, 0, 1);
            pill_draw(c.x, c.y + 70, f->act, FONT_DISP_S, 16, WHITE, f->act_col, al, sc);
        }
    }
}

static void draw_pills(double time)
{
    for (int i = 0; i < NPILL; i++) {
        const Pill *p = &P.pill[i];
        if (!p->on) continue;
        float ap = clampf(p->t / 0.25f, 0, 1), van = clampf((p->t - (p->dur - 0.4f)) / 0.4f, 0, 1);
        float k = ease(EASE_OUT_BACK, ap);
        float a = 1 - van;
        float y = p->y - (p->big ? 0 : 12 * van);
        if (p->big) {
            float w = text_width(FONT_NEON_M, p->text, p->size, 0);
            gfx_nine(sprite_nine(NINE_RRECT), p->x - w * 0.5f - 24, y - p->size * 0.72f, w + 48, p->size * 1.44f, 16,
                     gfx_cola((Color){ 10, 6, 12, 255 }, 0.85f * a));
            text_neon(FONT_NEON_M, p->text, p->x, y, p->size * (0.8f + 0.2f * k), p->col, a, 1.0f);
        } else {
            pill_draw(p->x, y, p->text, FONT_UI_M, p->size, WHITE, p->col, a, k);
        }
    }
    (void)time;
}

static void draw_hud(const TableViewInfo *v, double time)
{
    const BjGame *g = v->game;
    char b[64], n1[24], n2[24];
    float t = (float)time;
    /* Left: credits and the table's limits. */
    Rectangle l = { 14, 10, 256, 64 };
    ui_panel(l, UI_CYAN, 0.18f, 1);
    if (!v->demo) {
        snprintf(b, sizeof b, "CREDITS %s", dv_chips((int64_t)(P.credits_shown + 0.5), n1, sizeof n1));
        gold_label(FONT_DISP_S, b, l.x + 14, l.y + 22, 20, ALIGN_LEFT, 0.2f);
    } else {
        gold_label(FONT_DISP_S, "DEMO TABLE", l.x + 14, l.y + 22, 20, ALIGN_LEFT, 0.2f);
    }
    snprintf(b, sizeof b, "TABLE %s - %s   6 DECKS", dv_chips(g->cfg.min_bet, n1, sizeof n1), dv_chips(g->cfg.max_bet, n2, sizeof n2));
    label(FONT_UI_S, b, l.x + 14, l.y + 46, 15, (Color){ 170, 220, 240, 255 }, ALIGN_LEFT, 0);
    /* Centre: the HONEY POT. */
    float pk = P.pot_bump;
    Rectangle c = { 430, 6, 420, 62 };
    gfx_spr_rot(sprite(SPR_GLOW), 640, 38, 520, 110, 0, gfx_add(UI_AMBER, 0.12f + 0.25f * pk));
    ui_panel(c, UI_AMBER, 0.35f + 0.5f * pk, 1);
    text_neon(FONT_NEON_M, "HONEY POT", c.x + 92, c.y + 31, 26, UI_AMBER, 0.85f + 0.15f * sinf(t * 2.3f), 1.0f);
    TextStyle ts = style((Color){ 255, 246, 196, 255 }, ALIGN_RIGHT);
    ts.color2 = (Color){ 240, 150, 30, 255 };
    ts.glow = UI_AMBER;
    ts.glow_k = 0.5f + 0.5f * pk;
    ts.shadow = BLACK;
    ts.shadow_k = 0.8f;
    float sz = 34 * (1 + 0.1f * pk);
    text_draw_ex(FONT_DISP_M, dv_chips(P.pot_shown, n1, sizeof n1), c.x + c.width - 18, c.y + 31 - sz * 0.56f, sz, &ts);
    /* Right: the player's bets. */
    Rectangle r = { PLAY_W - 14 - 256, 10, 256, 64 };
    ui_panel(r, UI_HONEY, 0.18f + 0.6f * P.bet_bump, 1);
    int hs = g->cfg.human_seat;
    if (!v->demo && hs >= 0) {
        const BjSeat *st = &g->seat[hs];
        int64_t main = g->bet;
        if (st->in_round && g->phase != BJ_PH_BETTING) {
            main = 0;
            for (int h = 0; h < st->nhands; h++) main += st->hand[h].bet;
        }
        snprintf(b, sizeof b, "BET %s", dv_chips(main, n1, sizeof n1));
        gold_label(FONT_DISP_S, b, r.x + r.width - 14, r.y + 22, 20, ALIGN_RIGHT, 0.2f + 0.5f * P.bet_bump);
        if (g->phase == BJ_PH_BETTING && g->table_dirty && g->last_net != 0)
            snprintf(b, sizeof b, "LAST ROUND %s%s", g->last_net > 0 ? "+" : "", dv_chips(g->last_net, n2, sizeof n2));
        else
            snprintf(b, sizeof b, "PAIRS %s   21+3 %s   POT %s", g->side[BJ_SIDE_PP] ? dv_chips(g->side[BJ_SIDE_PP], n1, sizeof n1) : "-",
                     g->side[BJ_SIDE_213] ? dv_chips(g->side[BJ_SIDE_213], n2, sizeof n2) : "-", g->side[BJ_SIDE_HONEY] ? "ON" : "-");
        Color lc = g->phase == BJ_PH_BETTING && g->table_dirty && g->last_net != 0
                       ? (g->last_net > 0 ? (Color){ 120, 255, 160, 255 } : (Color){ 255, 140, 150, 255 })
                       : (Color){ 230, 210, 170, 255 };
        label(FONT_UI_S, b, r.x + r.width - 14, r.y + 46, 15, lc, ALIGN_RIGHT, 0);
    } else {
        gold_label(FONT_DISP_S, "BEESE'S BLACKJACK", r.x + r.width - 14, r.y + 22, 20, ALIGN_RIGHT, 0.2f);
        label(FONT_UI_S, "BLACKJACK PAYS 3 TO 2", r.x + r.width - 14, r.y + 46, 15, (Color){ 230, 210, 170, 255 }, ALIGN_RIGHT, 0);
    }
}

static void draw_bar(const TableViewInfo *v, double time)
{
    const BjGame *g = v->game;
    BarState b;
    Rectangle r[B_N];
    char s[96], n1[24];
    bar_state(v, &b);
    bar_rects(r);
    gfx_rect_vgrad(0, BAR_Y - 8, PLAY_W, PLAY_H - BAR_Y + 8, gfx_cola((Color){ 8, 5, 10, 255 }, 0.2f),
                   gfx_cola((Color){ 8, 5, 10, 255 }, 0.92f));
    gfx_rect_hgrad(0, BAR_Y - 8, PLAY_W * 0.5f, 2, gfx_add(UI_MAGENTA, 0), gfx_add(UI_MAGENTA, 0.8f));
    gfx_rect_hgrad(PLAY_W * 0.5f, BAR_Y - 8, PLAY_W * 0.5f, 2, gfx_add(UI_MAGENTA, 0.8f), gfx_add(UI_MAGENTA, 0));
    for (int i = 0; i < B_N; i++) {
        ui_button_draw(&P.btn[i], r[i], "", b.col[i], b.st[i], time);
        float p = ease(EASE_OUT_QUAD, P.btn[i].press);
        float cx = r[i].x + r[i].width * 0.5f, dy = 2 * p;
        /* The little panel drawing shows where the button is; its label says what it does. */
        const float gh = 15, gw = ui_panel_glyph_w(gh);
        ui_panel_glyph(cx - gw * 0.5f, r[i].y + 5 + dy, gh, k_keys[i], b.st[i] == BTN_STATE_OFF ? (Color){ 150, 140, 150, 255 } : b.col[i],
                       b.st[i] == BTN_STATE_OFF ? 0.6f : 1.0f);
        TextStyle ts = style(b.st[i] == BTN_STATE_OFF ? (Color){ 120, 112, 120, 255 } : (Color){ 255, 250, 240, 255 }, ALIGN_CENTER);
        ts.shadow = BLACK;
        ts.shadow_k = 0.8f;
        float size = i == B_DEAL ? 22 : 19;
        float tw = text_width(FONT_DISP_S, b.label[i], size, 0);
        if (tw > r[i].width - 14) size *= (r[i].width - 14) / tw;
        text_draw_ex(FONT_DISP_S, b.label[i], cx, r[i].y + 37 + dy - size * 0.56f, size, &ts);
        if (i == b.best) {
            float k = 0.6f + 0.4f * sinf((float)time * 6);
            gfx_nine(sprite_nine(NINE_GLOW), r[i].x - 24, r[i].y - 24, r[i].width + 48, r[i].height + 48, 40, gfx_add(UI_GOLD, 0.6f * k));
            pill_draw(cx, r[i].y - 12, "BEST PLAY", FONT_UI_S, 13, UI_GOLD, UI_GOLD, 1, 1);
        }
    }
    /* The line over the bar: what is going on, or what to do. */
    float y = BAR_Y - 30;
    if (b.mode == 1) {
        float pulse = 0.6f + 0.4f * sinf((float)time * 4);
        snprintf(s, sizeof s, "%sSTICK UP / DOWN: BET %s   -   PRESS DEAL TO PLAY", g->need_shuffle ? "NEW SHOE NEXT  -  " : "",
                 dv_chips(g->bet, n1, sizeof n1));
        label(FONT_UI_M, s, 640, y, 20, fade(UI_CYAN, 0.7f + 0.3f * pulse), ALIGN_CENTER, 0.6f);
    } else if (b.mode == 3) {
        text_neon(FONT_NEON_M, "INSURANCE?", 640, y - 4, 30, UI_CYAN, 1, 1);
    } else if (b.mode == 2) {
        int hs = g->cfg.human_seat;
        const BjSeat *st = &g->seat[hs];
        if (v->hint_on && v->advice && v->advice->best != BJ_ACT_NONE) {
            snprintf(s, sizeof s, "BEST PLAY: %s  (EXPECTED %+.2f A UNIT)", bj_action_name(v->advice->best), v->advice->ev[v->advice->best]);
            label(FONT_UI_M, s, 640, y, 20, UI_GOLD, ALIGN_CENTER, 0.6f);
        } else {
            snprintf(s, sizeof s, st->nhands > 1 ? "YOUR TURN  -  HAND %d OF %d" : "YOUR TURN", st->cur + 1, st->nhands);
            text_neon(FONT_NEON_M, s, 640, y - 2, 26, UI_GOLD, 0.8f + 0.2f * sinf((float)time * 4), 1);
        }
        label(FONT_UI_S, v->hint_on ? "STICK UP: HINT OFF" : "STICK UP: STRATEGY HINT", PLAY_W - 18, BAR_Y - 18, 14,
              (Color){ 150, 140, 150, 255 }, ALIGN_RIGHT, 0);
    } else if (g->phase == BJ_PH_PLAY && g->turn >= 0 && g->turn < NS && g->seat[g->turn].kind == BJ_SEAT_BOT) {
        snprintf(s, sizeof s, "%s IS PLAYING", seat_name(g, g->turn));
        label(FONT_UI_M, s, 640, y, 19, (Color){ 170, 160, 170, 255 }, ALIGN_CENTER, 0);
    } else if (g->phase == BJ_PH_DEALER) {
        label(FONT_UI_M, "THE DEALER PLAYS", 640, y, 19, (Color){ 200, 190, 170, 255 }, ALIGN_CENTER, 0);
    }
    if (g->phase == BJ_PH_BETTING) label(FONT_UI_S, "CASH OUT: LOBBY", 18, BAR_Y - 18, 14, (Color){ 130, 120, 130, 255 }, ALIGN_LEFT, 0);
}

static void draw_demo_overlay(double time)
{
    float t = (float)time;
    gfx_rect_vgrad(0, BAR_Y - 4, PLAY_W, PLAY_H - BAR_Y + 4, gfx_cola(BLACK, 0.0f), gfx_cola(BLACK, 0.85f));
    float p = 0.6f + 0.4f * sinf(t * 4.0f);
    text_neon(FONT_NEON_L, "PRESS DEAL", 640, 684, 54, UI_CYAN, 0.75f + 0.25f * p, 1.0f);
    label(FONT_UI_M, "DEMO  -  BLACKJACK WITH THE REGULARS", 24, 690, 20, (Color){ 255, 170, 220, 255 }, ALIGN_LEFT, 0.6f);
    label(FONT_UI_M, "FREE PLAY  -  REAL SHUFFLES, REAL ODDS", PLAY_W - 24, 690, 20, (Color){ 200, 190, 170, 255 }, ALIGN_RIGHT, 0.6f);
}

void table_present_draw(const TableViewInfo *v, double time)
{
    init_once();
    const BjGame *g = v->game;
    if (!g) return;
    if (g != P.game) reset_all(g);
    float dim = celeb_dim(&P.cel);
    double t = time;
    render_begin();
    draw_backdrop(t, dim);
    float dx, dy, deg;
    shake_offset(&P.shake, &dx, &dy, &deg);
    gfx_push_offset(dx, dy, deg, PLAY_W * 0.5f, PLAY_H * 0.5f);
    gfx_set_tint(dim, dim, dim);
    draw_rail_neon(t);
    draw_felt_print();
    draw_spots(v, t);
    draw_shoe(t);
    draw_pending_bets(v);
    draw_chips(g);
    draw_dealer(g, t);
    draw_seats(v, t);
    draw_hands(v, t);
    flights_draw();
    draw_pills(t);
    gfx_set_tint(1, 1, 1);
    celeb_draw_back(&P.cel, t);
    pfx_draw();
    celeb_draw_front(&P.cel, t);
    gfx_pop_offset();
    gfx_set_tint(dim, dim, dim);
    draw_hud(v, t);
    gfx_set_tint(1, 1, 1);
    if (!v->demo) draw_bar(v, t);
    else draw_demo_overlay(t);
    if (P.bulbs_k > 0.01f) bulbs_draw(&P.bulbs);
    if (v->message) pill_draw(640, 600, v->message, FONT_UI_M, 22, UI_AMBER, UI_AMBER, 1, 1);
    render_end();
    dv_fill_account(P.cel.active ? (P.cel.tier == WIN_JACKPOT ? DV_FILL_JACKPOT : DV_FILL_CELEB) : DV_FILL_PLAY);
}

/* A bee of the cast, idling, for the lobby. */
void dv_avatar(int who, float x, float y, float size, double time)
{
    AvFx a;
    memset(&a, 0, sizeof a);
    float t = (float)time;
    a.bob = t * 2.0f + (float)who;
    a.ant = t * 7.0f + (float)who;
    a.flap = t * 3.0f;
    a.gx = 1.5f * sinf(t * 0.7f + (float)who);
    a.gy = 0.8f;
    float bl = fmodf(t + (float)who * 0.37f, 4.0f);
    a.blink = bl < 0.16f ? sinf(bl / 0.16f * PI_F) : 0;
    draw_avatar(who, &a, x, y, size, 1, 1);
}
