/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* panel.c - see panel.h. */
#include "platform/panel.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "platform/ini.h"
#include "platform/joy_evdev.h"

/* The functions a position can have. */
static const struct {
    const char *key;       /* panel.ini value                          */
    const char *label;     /* for people                               */
    uint32_t    buttons;   /* at once                                  */
    uint32_t    held;      /* only after hold seconds held down        */
    int         hold;
} k_fn[PF_COUNT] = {
    { "HOLD1", "HIT", BTN_HOLD1, 0, 0 },
    { "HOLD2", "STAND", BTN_HOLD2, 0, 0 },
    { "HOLD3", "DOUBLE", BTN_HOLD3, 0, 0 },
    { "HOLD4", "SPLIT", BTN_HOLD4, 0, 0 },
    { "HOLD5", "SURRENDER", BTN_HOLD5, 0, 0 },
    { "DEAL", "DEAL", BTN_DEAL | BTN_OK, 0, 0 },
    { "COIN", "COIN", BTN_COIN, 0, 0 },
    { "CASH_OUT", "CASH OUT", BTN_CASH_OUT | BTN_BACK, BTN_SERVICE, 3 },
    { "BET_ONE", "BET MORE", BTN_BET_ONE, 0, 0 },
    { "BET_MAX", "BET MAX", BTN_BET_MAX, 0, 0 },
    { "START", "START", BTN_START, 0, 0 },
    { "SERVICE", "SERVICE", 0, BTN_SERVICE, 3 },
};

/* The positions, and what each does unless BUTTON MAP changed it. The deck
 * is laid out by position (panel.h), both sides alike, so this is the whole
 * default layout. */
static const struct {
    const char *key;       /* config / panel.ini key */
    const char *name;      /* for people             */
    int8_t      fn;        /* the default PanelFn    */
} k_slot[PS_COUNT] = {
    { "P1_T1", "P1 TOP LEFT", PF_HOLD1 },
    { "P1_T2", "P1 TOP MIDDLE", PF_HOLD2 },
    { "P1_T3", "P1 TOP RIGHT", PF_HOLD3 },
    { "P1_B1", "P1 BOTTOM LEFT", PF_HOLD4 },
    { "P1_B2", "P1 BOTTOM MIDDLE", PF_HOLD5 },
    { "P1_B3", "P1 BOTTOM RIGHT", PF_DEAL },
    { "P1_SELECT", "P1 SELECT", PF_COIN },
    { "P1_START", "P1 START", PF_CASH_OUT },
    { "P2_T1", "P2 TOP LEFT", PF_HOLD1 },
    { "P2_T2", "P2 TOP MIDDLE", PF_HOLD2 },
    { "P2_T3", "P2 TOP RIGHT", PF_HOLD3 },
    { "P2_B1", "P2 BOTTOM LEFT", PF_HOLD4 },
    { "P2_B2", "P2 BOTTOM MIDDLE", PF_HOLD5 },
    { "P2_B3", "P2 BOTTOM RIGHT", PF_DEAL },
    { "P2_SELECT", "P2 SELECT", PF_COIN },
    { "P2_START", "P2 START", PF_CASH_OUT },
};

/* The guess before anything is learned: RetroPie's usual 6-button layout,
 * Y X L over B A R, in EmulationStation's numbering for the cabinet's
 * DragonRise encoders (x=0 a=1 b=2 y=3 L=4 R=5 select=8 start=9). */
static const int8_t k_guess[PS_PER_SIDE] = { 3, 0, 4, 2, 1, 5, 8, 9 };

static struct {
    PanelWire wire[PS_COUNT];     /* in use                                   */
    PanelWire config[PS_COUNT];   /* built-in + config.ini, before panel.ini   */
    int8_t fn[PS_COUNT];          /* the mapping in use                       */
    int8_t config_fn[PS_COUNT];   /* built-in + config.ini, before panel.ini   */
    int learned, mapped, wizard_offered, controls_shown, have_file;
    int interactive, suspended;
    int ready;                    /* panel_defaults has run                    */
    char path[600];
    InputConfig base;
    InputConfig *live;
} P;

void panel_defaults(void)
{
    for (int s = 0; s < PS_COUNT; s++) {
        P.config[s].joy = (int8_t)(s / PS_PER_SIDE);
        P.config[s].button = k_guess[s % PS_PER_SIDE];
    }
    memcpy(P.wire, P.config, sizeof P.wire);
    panel_default_mapping(P.config_fn);
    memcpy(P.fn, P.config_fn, sizeof P.fn);
    P.learned = P.mapped = 0;
    P.ready = 1;
}

/* The tables are not all-zero (zero is HOLD 1 and JOY0_B0), so anything
 * read before panel_defaults gets the defaults first. */
static void ready(void)
{
    if (!P.ready) panel_defaults();
}

void panel_default_mapping(int8_t fn[PS_COUNT])
{
    for (int s = 0; s < PS_COUNT; s++) fn[s] = k_slot[s].fn;
}

/* "P1_T1" -> slot; with does, "P1_T1_DOES" -> slot and *does = 1. */
static int slot_from_key(const char *key, int *does)
{
    *does = 0;
    for (int s = 0; s < PS_COUNT; s++) {
        size_t n = strlen(k_slot[s].key);
        if (strncasecmp(key, k_slot[s].key, n) != 0) continue;
        if (key[n] == '\0') return s;
        if (strcasecmp(key + n, "_DOES") == 0) { *does = 1; return s; }
    }
    return -1;
}

/* "HOLD1" or "NONE". */
static int parse_fn(const char *value, int8_t *f)
{
    if (strcasecmp(value, "NONE") == 0) { *f = PF_NONE; return 0; }
    for (int i = 0; i < PF_COUNT; i++)
        if (strcasecmp(value, k_fn[i].key) == 0) { *f = (int8_t)i; return 0; }
    return -1;
}

/* "JOY0_B3" or "NONE". */
static int parse_wire(const char *value, PanelWire *w)
{
    if (strcasecmp(value, "NONE") == 0) { w->joy = 0; w->button = -1; return 0; }
    Binding b;
    if (input_parse_binding(value, &b) != 0 || b.kind != BIND_JOY_BUTTON || b.joy < 0 || b.hold) return -1;
    w->joy = b.joy;
    w->button = (int8_t)b.code;
    return 0;
}

int panel_config_set(const char *key, const char *value)
{
    ready();
    int does, s = slot_from_key(key, &does);
    if (s < 0) return -1;
    if (does) {
        if (parse_fn(value, &P.config_fn[s]) != 0) return -1;
        P.fn[s] = P.config_fn[s];
        return 0;
    }
    if (parse_wire(value, &P.config[s]) != 0) return -1;
    P.wire[s] = P.config[s];
    return 0;
}

/* Rebuilds the live input config: the base (keyboard, sticks) plus every
 * wired position's buttons. */
static void apply(void)
{
    if (!P.live) return;
    *P.live = P.base;
    if (P.suspended) return;
    for (int s = 0; s < PS_COUNT; s++) {
        int f = panel_slot_fn((PanelSlot)s);
        if (P.wire[s].button < 0 || f == PF_NONE) continue;
        Binding b;
        memset(&b, 0, sizeof b);
        b.kind = BIND_JOY_BUTTON;
        b.joy = P.wire[s].joy;
        b.code = P.wire[s].button;
        /* What it does at once, then what it does only when held. */
        for (int pass = 0; pass < 2; pass++) {
            uint32_t does = pass ? k_fn[f].held : k_fn[f].buttons;
            b.hold = (uint8_t)(pass ? k_fn[f].hold : 0);
            for (int i = 0; i < INPUT_NBUTTONS; i++)
                if (does & (1u << i))
                    if (input_add_binding(P.live, 1u << i, &b) != 0)
                        fprintf(stderr, "PANEL: no free binding for %s on %s\n", input_button_name(i), k_slot[s].key);
        }
    }
}

typedef struct { PanelWire w[PS_COUNT]; int8_t fn[PS_COUNT]; int learned, mapped, offered, shown, bad; } FileCtx;

static int file_handler(void *user, const char *section, const char *key, const char *value)
{
    FileCtx *f = user;
    (void)section;
    if (strcasecmp(key, "learned") == 0) { f->learned = strcmp(value, "1") == 0; return 0; }
    if (strcasecmp(key, "mapped") == 0) { f->mapped = strcmp(value, "1") == 0; return 0; }
    if (strcasecmp(key, "wizard_offered") == 0) { f->offered = strcmp(value, "1") == 0; return 0; }
    if (strcasecmp(key, "controls_shown") == 0) { f->shown = strcmp(value, "1") == 0; return 0; }
    int does, s = slot_from_key(key, &does);
    if (s < 0 || (does ? parse_fn(value, &f->fn[s]) : parse_wire(value, &f->w[s])) != 0) { f->bad++; return -1; }
    return 0;
}

void panel_init(const char *save_dir, const InputConfig *base, InputConfig *live)
{
    P.base = *base;
    P.live = live;
    P.path[0] = '\0';
    if (save_dir && *save_dir) snprintf(P.path, sizeof P.path, "%s/panel.ini", save_dir);
    FileCtx f;
    memset(&f, 0, sizeof f);
    memcpy(f.w, P.config, sizeof f.w);
    memcpy(f.fn, P.config_fn, sizeof f.fn);
    if (P.path[0] && ini_parse(P.path, file_handler, &f) >= 0) {
        P.have_file = 1;
        P.wizard_offered = f.offered;
        P.controls_shown = f.shown;
        if (f.learned) {
            memcpy(P.wire, f.w, sizeof P.wire);
            P.learned = 1;
        }
        if (f.mapped) {
            memcpy(P.fn, f.fn, sizeof P.fn);
            P.mapped = 1;
        }
        fprintf(stderr, "PANEL: %s (%s, %s%s)\n", P.path, P.learned ? "learned wiring" : "configured wiring",
                P.mapped ? "own button map" : "standard button map", f.bad ? ", some lines ignored" : "");
    } else {
        fprintf(stderr, "PANEL: no panel.ini yet, configured wiring\n");
    }
    apply();
}

void panel_set_interactive(int on) { P.interactive = on; }

int panel_slot_fn(PanelSlot s)
{
    ready();
    return (unsigned)s < PS_COUNT ? P.fn[s] : PF_NONE;
}
const char *panel_fn_label(int f) { return (unsigned)f < PF_COUNT ? k_fn[f].label : ""; }
const char *panel_fn_key(int f) { return (unsigned)f < PF_COUNT ? k_fn[f].key : "NONE"; }
uint32_t panel_fn_buttons(int f) { return (unsigned)f < PF_COUNT ? k_fn[f].buttons | k_fn[f].held : 0; }

uint32_t panel_slot_buttons(PanelSlot s) { return panel_fn_buttons(panel_slot_fn(s)); }
int panel_slot_hold(PanelSlot s)
{
    int f = panel_slot_fn(s);
    return (unsigned)f < PF_COUNT ? k_fn[f].hold : 0;
}
const char *panel_slot_name(PanelSlot s) { return (unsigned)s < PS_COUNT ? k_slot[s].name : "?"; }
const char *panel_slot_label(PanelSlot s) { return panel_fn_label(panel_slot_fn(s)); }

int panel_slot_of(uint32_t btn)
{
    for (int s = 0; s < PS_COUNT; s++)
        if (panel_slot_buttons((PanelSlot)s) & btn) return s;
    return -1;
}

uint32_t panel_stick_for(uint32_t btn)
{
    if (panel_slot_of(btn) >= 0) return 0;
    if (btn & BTN_BET_ONE) return BTN_UP;
    if (btn & BTN_BET_MAX) return BTN_RIGHT;
    return 0;
}

int panel_slot_at(int joy, int button)
{
    ready();
    for (int s = 0; s < PS_COUNT; s++)
        if (P.wire[s].button >= 0 && P.wire[s].joy == joy && P.wire[s].button == button) return s;
    return -1;
}

PanelWire panel_wire(PanelSlot s)
{
    ready();
    PanelWire none = { 0, -1 };
    return (unsigned)s < PS_COUNT ? P.wire[s] : none;
}

int panel_slot_down(PanelSlot s)
{
    PanelWire w = panel_wire(s);
    return w.button >= 0 && joy_button_down(w.joy, w.button);
}

int panel_learned(void) { return P.learned; }

int panel_format(char *out, int n, const PanelWire w[PS_COUNT], int learned, int wizard_offered, int controls_shown,
                 const int8_t *fn)
{
    int p = snprintf(out, (size_t)n,
                     "; Beese's Blackjack - where each panel button is wired, and what it does\n"
                     "; (see platform/panel.h). Written by the game; LEARN PANEL and BUTTON MAP in\n"
                     "; the service menu rewrite it.\n"
                     "learned = %d\nmapped = %d\nwizard_offered = %d\ncontrols_shown = %d\n",
                     learned, fn != NULL, wizard_offered, controls_shown);
    for (int s = 0; s < PS_COUNT && p < n; s++) {
        if (w[s].button < 0) p += snprintf(out + p, (size_t)(n - p), "%-9s = NONE\n", k_slot[s].key);
        else p += snprintf(out + p, (size_t)(n - p), "%-9s = JOY%d_B%d\n", k_slot[s].key, w[s].joy, w[s].button);
    }
    for (int s = 0; fn && s < PS_COUNT && p < n; s++) {
        char k[24];
        snprintf(k, sizeof k, "%s_DOES", k_slot[s].key);
        p += snprintf(out + p, (size_t)(n - p), "%-14s = %s\n", k, panel_fn_key(fn[s]));
    }
    return p < n ? p : -1;
}

/* panel.ini: a temporary file, fsync, rename over, fsync the directory - a
 * power cut leaves the old file or the new one. */
static int save(void)
{
    if (!P.path[0]) return -1;
    char buf[2048], tmp[640];
    int n = panel_format(buf, sizeof buf, P.wire, P.learned, P.wizard_offered, P.controls_shown, P.mapped ? P.fn : NULL);
    if (n < 0) return -1;
    snprintf(tmp, sizeof tmp, "%s.tmp", P.path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { fprintf(stderr, "PANEL: cannot write %s: %s\n", tmp, strerror(errno)); return -1; }
    int ok = write(fd, buf, (size_t)n) == n && fsync(fd) == 0;
    close(fd);
    if (!ok || rename(tmp, P.path) != 0) { unlink(tmp); fprintf(stderr, "PANEL: cannot save %s\n", P.path); return -1; }
    char dir[600];
    snprintf(dir, sizeof dir, "%s", P.path);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        int dfd = open(dir, O_RDONLY);
        if (dfd >= 0) { fsync(dfd); close(dfd); }
    }
    P.have_file = 1;
    return 0;
}

int panel_wizard_due(void) { return P.interactive && joy_count() > 0 && !P.wizard_offered && !P.learned; }

void panel_wizard_offered(void)
{
    if (P.wizard_offered) return;
    P.wizard_offered = 1;
    save();
}

int panel_controls_due(void) { return P.interactive && !P.controls_shown; }

void panel_controls_shown(void)
{
    if (P.controls_shown) return;
    P.controls_shown = 1;
    save();
}

void panel_suspend(int on)
{
    if (P.suspended == on) return;
    P.suspended = on;
    apply();
}

int panel_set_wiring(const PanelWire w[PS_COUNT])
{
    memcpy(P.wire, w, sizeof P.wire);
    P.learned = 1;
    P.wizard_offered = 1;
    apply();
    int rc = save();
    fprintf(stderr, "PANEL: learned wiring %s\n", rc == 0 ? "saved" : "NOT saved (kept for this session)");
    return rc;
}

int panel_mapped(void) { return P.mapped; }

int panel_set_mapping(const int8_t fn[PS_COUNT])
{
    for (int s = 0; s < PS_COUNT; s++) P.fn[s] = fn[s] >= 0 && fn[s] < PF_COUNT ? fn[s] : PF_NONE;
    P.mapped = 1;
    apply();
    int rc = save();
    fprintf(stderr, "PANEL: button map %s\n", rc == 0 ? "saved" : "NOT saved (kept for this session)");
    return rc;
}

int panel_reset_mapping(void)
{
    memcpy(P.fn, P.config_fn, sizeof P.fn);
    P.mapped = 0;
    apply();
    int rc = save();
    fprintf(stderr, "PANEL: button map back to standard%s\n", rc == 0 ? "" : " (NOT saved)");
    return rc;
}
