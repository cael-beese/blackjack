/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* views.h - the presentation of every screen: the blackjack table
 * (table_present.c), the lobby and attract title (lounge_view.c) and the
 * CONTROLS page (controls_view.c).
 *
 * These implement platform/table_view.h's seam: the modes own the game and
 * the flow and hand over a read-only *ViewInfo once per frame. The views
 * react to the frame's events (games/blackjack/bj_game.h 100-199, app 300+),
 * keep only cosmetic state (animations, particles, count-up meters, their
 * own Rng through fx.h), never write game state and allocate nothing after
 * start-up. Sound goes out from here, in reaction to events (audio/).
 *
 * The *_update functions make no GL calls, so a test can drive them
 * headless; the *_draw functions draw into the 1280x720 play space. */
#ifndef BPL_RENDER_VIEWS_H
#define BPL_RENDER_VIEWS_H

#include <stddef.h>
#include <stdint.h>

#include "raylib.h"
#include "platform/table_view.h"

/* The table platform/app_modes.c installs when render/ is built. */
extern const AppViews app_views_render;

/* ---- the table (table_present.c) ------------------------------------------ */
void table_present_update(const TableViewInfo *v, const GameEvent *ev, int nev, float dt);
void table_present_draw(const TableViewInfo *v, double time);

typedef struct {
    int  cards_live, flights_live, flights_peak;
    long events_seen, rounds_seen, celebrations;
    int  desyncs;              /* times the view had to snap to the game state */
} TablePresentStats;
const TablePresentStats *table_present_stats(void);

/* ---- lobby and attract (lounge_view.c) --------------------------------------- */
void menu_view_update(const MenuViewInfo *v, const GameEvent *ev, int nev, float dt);
void menu_view_draw(const MenuViewInfo *v, double time);
/* The lobby's CONTROLS page (controls_view.c), drawn by menu_view_draw. */
void controls_view_draw(const MenuViewInfo *v, double time);
void attract_view_update(const AttractViewInfo *v, const GameEvent *ev, int nev, float dt);
void attract_view_draw(const AttractViewInfo *v, double time);

/* ---- shared pieces ------------------------------------------------------------- */

/* A sound effect (no-op without the audio library). pan -1..1. */
void dv_sfx(int sfx_id, float vol, float pitch, float pan);

/* A bee of the cast (AV_*, render/holdem_art.h) idling at (x, y). */
void dv_avatar(int who, float x, float y, float size, double time);

/* "12,345" */
const char *dv_chips(int64_t v, char *buf, size_t n);

/* Fill accounting: call once per frame after render_end(); takes the frame's
 * gfx_fill_take() and, with BPL_FILL_LOG=1, prints mean / max Mpx per scene
 * every 300 frames. */
enum { DV_FILL_PLAY, DV_FILL_CELEB, DV_FILL_JACKPOT, DV_FILL_MENU, DV_FILL_TITLE, DV_FILL_SCENES };
void dv_fill_account(int scene);

#endif
