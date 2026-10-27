/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* views.c - the table of views the executable installs, and the pieces the
 * views share. See views.h. */
#include "render/views.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render/render.h"
#if defined(BPL_HAVE_AUDIO)
#include "audio.h"
#endif

const AppViews app_views_render = {
    .table_update = table_present_update,
    .table_view = table_present_draw,
    .menu_update = menu_view_update,
    .menu_view = menu_view_draw,
    .attract_update = attract_view_update,
    .attract_view = attract_view_draw,
};

void dv_sfx(int id, float vol, float pitch, float pan)
{
#if defined(BPL_HAVE_AUDIO)
    static int log = -1;
    if (log < 0) log = getenv("BPL_SFX_LOG") != NULL;
    /* BPL_SFX_LOG: which sound fired, for checking the event -> sound map on
       a machine without an audio device. */
    if (log) fprintf(stderr, "SFX %s vol %.2f pitch %.2f pan %.2f\n", audio_sfx_name((SfxId)id), vol, pitch, pan);
    audio_play((SfxId)id, vol, pitch, pan);
#else
    (void)id; (void)vol; (void)pitch; (void)pan;
#endif
}

const char *dv_chips(int64_t v, char *buf, size_t n)
{
    char tmp[32], out[48];
    int neg = v < 0, o = 0;
    unsigned long long u = (unsigned long long)(neg ? -v : v);
    int len = snprintf(tmp, sizeof tmp, "%llu", u);
    if (neg) out[o++] = '-';
    for (int i = 0; i < len; i++) {
        out[o++] = tmp[i];
        int left = len - 1 - i;
        if (left > 0 && left % 3 == 0) out[o++] = ',';
    }
    out[o] = '\0';
    if (n) {
        size_t l = strlen(out);
        if (l >= n) l = n - 1;
        memcpy(buf, out, l);
        buf[l] = '\0';
    }
    return buf;
}

void dv_fill_account(int scene)
{
    static int on = -1;
    static double sum[DV_FILL_SCENES], mx[DV_FILL_SCENES];
    static long n[DV_FILL_SCENES], frames;
    double f = gfx_fill_take();
    if (on < 0) {
        const char *e = getenv("BPL_FILL_LOG");
        on = e && *e && *e != '0';
    }
    if (!on || scene < 0 || scene >= DV_FILL_SCENES) return;
    sum[scene] += f;
    n[scene]++;
    if (f > mx[scene]) mx[scene] = f;
    if (++frames % 300 == 0) {
        static const char *const nm[DV_FILL_SCENES] = { "play", "celebration", "jackpot", "menu", "title" };
        for (int i = 0; i < DV_FILL_SCENES; i++)
            if (n[i])
                fprintf(stderr, "VIEW fill %-11s mean %.2f Mpx max %.2f Mpx (%ld frames)\n", nm[i],
                        sum[i] / (double)n[i] / 1e6, mx[i] / 1e6, n[i]);
    }
}
