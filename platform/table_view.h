/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* table_view.h - the seam between the modes (platform/mode_*.c), which own
 * the game and the flow, and the presentation (render/), which draws it.
 *
 * Once per frame a mode hands its view a read-only *ViewInfo (const game
 * state + the few app facts a screen needs):
 *
 *   xxx_update(info, events, nevents, dt)   cosmetic state, audio
 *   xxx_view(info, time)                    draw, 1280x720 play space
 *
 * Views must not change game state and hold no game logic: everything they
 * show is in the info struct or the const game it points to, and everything
 * that happened arrives as an event (games/blackjack/bj_game.h,
 * platform/app.h, platform/session.h).
 *
 * The views are reached through app_views, a table the executable defines in
 * platform/app_modes.c, so bpl_platform never links against render/ (which
 * depends on it). */
#ifndef BPL_PLATFORM_TABLE_VIEW_H
#define BPL_PLATFORM_TABLE_VIEW_H

#include <stdint.h>

#include "engine/event.h"
#include "games/blackjack/bj_game.h"

/* ---- the table ----------------------------------------------------------- */

typedef struct {
    const BjGame   *game;
    int64_t         credits;
    int             denom_cents;
    int             hint_on;
    const BjAdvice *advice;     /* the player's hand, on their turn and with the hint on; else NULL */
    unsigned        can;        /* BJ_CAN_* the player's hand allows now (0: not their turn) */
    int             your_turn;  /* waiting for the player's play           */
    int             insurance;  /* waiting for the player's insurance answer */
    int             demo;       /* attract: no meters of the player, no sounds */
    const char     *message;    /* a transient line ("FINISH THE ROUND"), or NULL */
    uint32_t        pressed;    /* logical buttons pressed this frame (feedback) */
} TableViewInfo;

/* ---- the lobby ------------------------------------------------------------- */

enum { MENU_PLAY, MENU_HINT, MENU_CONTROLS, MENU_NSEL };

typedef struct {
    int         sel;            /* MENU_*                                          */
    int         controls;       /* the CONTROLS page is open                       */
    uint32_t    down;           /* logical buttons held (the page lights them)     */
    float       map_hold;       /* CONTROLS: DEAL held towards BUTTON MAP, 0..1     */
    int         hint_on;
    int64_t     credits;
    int         denom_cents;
    int64_t     coin_credits;
    int64_t     pot;            /* the HONEY POT                                   */
    const char *message;
} MenuViewInfo;

/* ---- attract ----------------------------------------------------------------- */

enum { ATTRACT_TITLE, ATTRACT_DEMO };

typedef struct {
    int     phase;              /* ATTRACT_*                                       */
    int64_t credits;
    int64_t pot;
} AttractViewInfo;

/* ---- the table of views -------------------------------------------------------- */

typedef struct {
    void (*table_update)(const TableViewInfo *v, const GameEvent *ev, int nev, float dt);
    void (*table_view)(const TableViewInfo *v, double time);
    void (*menu_update)(const MenuViewInfo *v, const GameEvent *ev, int nev, float dt);
    void (*menu_view)(const MenuViewInfo *v, double time);
    /* Attract: the title card's cosmetic state, then (after the demo
       table's own view) the title card or the banner over the demo. */
    void (*attract_update)(const AttractViewInfo *v, const GameEvent *ev, int nev, float dt);
    void (*attract_view)(const AttractViewInfo *v, double time);
} AppViews;

extern const AppViews *const app_views;     /* platform/app_modes.c; never NULL */

/* Sounds for app / session events (coins, state changes) and the music for
 * the current app state; every mode's present_update calls it
 * (platform/app_sound.c). */
void app_common_update(int app_state, const GameEvent *ev, int nev, float dt);

#endif
