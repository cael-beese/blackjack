/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* app_sound.c - the music for each app state and the sounds of app-level
 * events (a coin, a change of screen). See table_view.h. */
#include "audio.h"
#include "platform/app.h"
#include "platform/session.h"
#include "platform/table_view.h"

void app_common_update(int app_state, const GameEvent *ev, int nev, float dt)
{
    static int music_state = -1, music_on = -1, audio_seen = 0;
    int want_on = session_settings()->music_on;
    int audio_now = session_info()->audio_ok;
    if (audio_now != audio_seen) {      /* audio arrived after the first frame: start the music */
        audio_seen = audio_now;
        music_state = -1;
    }
    if (app_state != music_state || want_on != music_on) {
        MusicId m = MUSIC_NONE;
        if (app_state == APP_ATTRACT) m = MUSIC_ATTRACT;
        else if (app_state == APP_MENU || app_state == APP_TABLE) m = MUSIC_LOUNGE;
        /* The lounge loop carries on from the lobby to the table. */
        int same = (music_state == APP_MENU || music_state == APP_TABLE) && m == MUSIC_LOUNGE && want_on == music_on;
        if (!same) audio_music(m, m != MUSIC_NONE && want_on);
        audio_ambience(want_on && app_state == APP_TABLE);
        music_state = app_state;
        music_on = want_on;
    }
    for (int i = 0; i < nev; i++) {
        const GameEvent *e = &ev[i];
        if (e->type == APP_EV_COIN) audio_play(SFX_COIN_INSERT, 1.0f, 1.0f, 0.0f);
        else if (e->type == APP_EV_STATE && e->b != APP_SERVICE && e->a != APP_SERVICE)
            audio_play(SFX_WHOOSH, 0.6f, 1.0f, 0.0f);
    }
    audio_update(dt);
}
