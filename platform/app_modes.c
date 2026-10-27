/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* app_modes.c - which AppMode serves which app state, and which views draw
 * them.
 *
 * This file is compiled into the executable, not into bpl_platform, so the
 * table can name modes and views from libraries that themselves depend on
 * bpl_platform (render/) without a circular static-library link. */
#include "platform/app.h"
#include "platform/table_view.h"

extern const AppMode mode_gputest;
#if defined(BPL_HAVE_RENDER)
extern const AppMode mode_rendertest;
#define RENDERTEST_MODE (&mode_rendertest)
#else
#define RENDERTEST_MODE (&mode_gputest)
#endif

extern const AppMode mode_attract;
extern const AppMode mode_menu;
extern const AppMode mode_table;
extern const AppMode mode_service;

/* The presentation of the table, the lobby and attract (render/views.h). */
#if defined(BPL_HAVE_RENDER)
extern const AppViews app_views_render;
const AppViews *const app_views = &app_views_render;
#else
static const AppViews g_no_views = { 0 };
const AppViews *const app_views = &g_no_views;
#endif

const AppMode *const app_modes[APP_NSTATES] = {
    [APP_ATTRACT] = &mode_attract,
    [APP_MENU]    = &mode_menu,
    [APP_TABLE]   = &mode_table,     /* presentation: render/table_present.c via app_views */
    [APP_SERVICE] = &mode_service,
    [APP_GPUTEST] = &mode_gputest,
    [APP_RENDERTEST] = RENDERTEST_MODE,   /* render/rendertest.c */
};
