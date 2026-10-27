/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* panel.h - the cabinet's control panel: where each button is, which encoder
 * input it is wired to, and what it does in the game.
 *
 * The panel has two sides (player 1 left, player 2 right), each a stick, two
 * rows of three buttons, and SELECT and START. The game is one player, and
 * both sides are laid out alike by POSITION, so either side plays the whole
 * game alone, and the on-screen labels, the CONTROLS screen and the README
 * can all say "top row, far left" and be right on any wiring:
 *
 *        each side (Y X L over B A R on RetroPie's usual wiring)
 *   top:  HOLD 1   HOLD 2   HOLD 3
 *   bot:  HOLD 4   HOLD 5   DEAL / DRAW
 *         SELECT = COIN     START = CASH OUT, held 3 s = SERVICE
 *   stick: up = BET ONE, right = BET MAX (Hold'em: up / down the amount,
 *          right ALL-IN) - mode_draw.c and mode_holdem.c turn the stick
 *          into these; panel_stick_for says which way, for the icons.
 *   SELECT + START together: leave the game (config.ini EXIT_COMBO).
 *
 * Which encoder input is in which position (the "wiring") comes from, last
 * wins: the built-in guess (RetroPie's usual 6-button layout, Y X L over
 * B A R, player 1 = the first joystick), [panel] in config.ini, and
 * <save dir>/panel.ini, written by the LEARN PANEL wizard in the service menu
 * (which asks for each position in turn).
 *
 * What each position does (the "mapping") is the layout above unless the
 * player has changed it on the BUTTON MAP page (service menu, or DEAL held on
 * the CONTROLS page), which asks for each of one side's eight functions in
 * turn, gives the other side the same, and saves the answer to panel.ini as
 * P1_T1_DOES = HOLD1 lines. Everything on screen that shows a button's place
 * reads the mapping, so it follows the change. */
#ifndef BPL_PLATFORM_PANEL_H
#define BPL_PLATFORM_PANEL_H

#include <stdint.h>
#include "platform/input.h"

typedef enum {
    PS_P1_T1, PS_P1_T2, PS_P1_T3, PS_P1_B1, PS_P1_B2, PS_P1_B3, PS_P1_SELECT, PS_P1_START,
    PS_P2_T1, PS_P2_T2, PS_P2_T3, PS_P2_B1, PS_P2_B2, PS_P2_B3, PS_P2_SELECT, PS_P2_START,
    PS_COUNT
} PanelSlot;
#define PS_PER_SIDE 8

/* The functions a position can have (one each, or none). The first
 * PF_MAP_COUNT are one side's eight, the ones BUTTON MAP asks for; CASH OUT
 * is also SERVICE when held 3 s. The rest are for [panel] in config.ini. */
typedef enum {
    PF_HOLD1, PF_HOLD2, PF_HOLD3, PF_HOLD4, PF_HOLD5, PF_DEAL, PF_COIN, PF_CASH_OUT,
    PF_BET_ONE, PF_BET_MAX, PF_START, PF_SERVICE, PF_COUNT
} PanelFn;
#define PF_NONE (-1)
#define PF_MAP_COUNT (PF_CASH_OUT + 1)

typedef struct {
    int8_t joy;      /* joystick index (JOY<n>)            */
    int8_t button;    /* button number (_B<k>), -1 = none    */
} PanelWire;

/* ---- set-up (platform/options.c, main.c) --------------------------------- */
void panel_defaults(void);                                  /* built-in wiring + layout */
/* [panel] P1_T1 = JOY0_B3 (wiring) or P1_T1_DOES = HOLD1 (mapping); 0 ok */
int  panel_config_set(const char *key, const char *value);
/* After session_init (the save directory is known): reads panel.ini, then
 * rebuilds *live as base plus the panel buttons. live stays the config the
 * input layer samples, so a learned wiring takes effect at once. */
void panel_init(const char *save_dir, const InputConfig *base, InputConfig *live);
/* Interactive run (no --script / --frames) on a machine with joysticks: only
 * then does the game offer the wizard and the CONTROLS screen by itself. */
void panel_set_interactive(int on);

/* ---- the layout ------------------------------------------------------------ */
uint32_t    panel_slot_buttons(PanelSlot s);   /* logical buttons it drives, 0 = none */
int         panel_slot_hold(PanelSlot s);      /* seconds to hold (SERVICE), else 0   */
int         panel_slot_of(uint32_t btn);       /* first slot driving btn, -1 = none   */
const char *panel_slot_name(PanelSlot s);      /* "P1 TOP LEFT", "P2 START", ...       */
const char *panel_slot_label(PanelSlot s);     /* what it does: "HOLD 1", "COIN", ...  */
PanelWire   panel_wire(PanelSlot s);
int         panel_slot_down(PanelSlot s);      /* the wired input is held now          */
int         panel_slot_at(int joy, int button);/* the position wired to it, -1 = none  */
/* The stick direction (BTN_UP / BTN_RIGHT) that does btn when no position
 * does (BET ONE, BET MAX), else 0. */
uint32_t    panel_stick_for(uint32_t btn);

/* ---- the mapping ------------------------------------------------------------ */
int         panel_slot_fn(PanelSlot s);        /* PanelFn, or PF_NONE                   */
const char *panel_fn_label(int f);             /* "HOLD 1", "CASH OUT", ...; "" = none   */
const char *panel_fn_key(int f);               /* "HOLD1", "CASH_OUT" (panel.ini)        */
uint32_t    panel_fn_buttons(int f);           /* logical buttons it drives              */
void        panel_default_mapping(int8_t fn[PS_COUNT]);
int         panel_mapped(void);                /* the mapping came from BUTTON MAP       */
/* A new mapping: applied at once and saved to panel.ini. 0 saved. */
int         panel_set_mapping(const int8_t fn[PS_COUNT]);
int         panel_reset_mapping(void);         /* back to the layout above; 0 saved      */

/* ---- learning ---------------------------------------------------------------- */
int  panel_learned(void);          /* the wiring came from the wizard              */
int  panel_wizard_due(void);       /* interactive, and the wizard was never offered */
void panel_wizard_offered(void);   /* remember it was (saved)                      */
int  panel_controls_due(void);     /* interactive, CONTROLS screen never shown yet  */
void panel_controls_shown(void);   /* remember it was (saved)                      */
/* Suspends the panel buttons (keyboard only) while the wizard listens to the
 * raw encoders, so learning presses do nothing else. */
void panel_suspend(int on);
/* The wizard's result: applied at once and saved to panel.ini. 0 saved. */
int  panel_set_wiring(const PanelWire w[PS_COUNT]);

/* The panel.ini text for a wiring and a mapping (tests; also what is saved).
 * fn NULL: no mapping lines (the layout above). */
int  panel_format(char *out, int n, const PanelWire w[PS_COUNT], int learned, int wizard_offered, int controls_shown,
                  const int8_t *fn);

#endif
