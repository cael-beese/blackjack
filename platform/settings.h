/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* settings.h - what the cabinet remembers: credits, statistics and operator
 * settings (SPEC section 6), and how that is written as text.
 *
 * Pure C (no raylib), so the tests link it. platform/save.h stores it on disk
 * atomically; platform/session.h owns the live copy.
 *
 * The payload is "key=value" lines. Unknown keys are ignored and missing keys
 * keep their defaults, so a newer build reads an older file and the other way
 * round; every numeric setting is clamped to its range when read, so a file
 * edited by hand cannot put the game in a bad state. */
#ifndef BPL_PLATFORM_SETTINGS_H
#define BPL_PLATFORM_SETTINGS_H

#include <stddef.h>
#include <stdint.h>

#include "platform/effects.h"

#define SAVE_SIDES    3        /* side bets: Perfect Pairs, 21+3, HONEY POT   */

typedef struct {
    uint8_t  vol_master, vol_music, vol_sfx;   /* 0..10                          */
    uint8_t  music_on;                          /* 0/1                            */
    uint8_t  hint_default;                      /* strategy hint on at start      */
    uint8_t  regulars;                          /* the bee regulars sit in (0/1)  */
    uint16_t denom_cents;                       /* what one credit is shown as    */
    uint16_t coin_cents;                        /* value of one COIN press        */
    EffectSettings effects;
} Settings;

/* The player's own play only; the bee regulars' rounds are not counted. */
typedef struct {
    uint64_t rounds;                            /* rounds the player bet on       */
    uint64_t hands;                             /* hands played (splits count)    */
    uint64_t wagered, paid;                     /* main bet credits (+ doubles, splits) */
    uint64_t blackjacks, doubles, splits, surrenders, insured;
    uint64_t wins, pushes, losses;              /* by hand                        */
    uint64_t side_bet[SAVE_SIDES], side_paid[SAVE_SIDES], side_hits[SAVE_SIDES];
    uint64_t jackpots;                          /* HONEY POTs won                 */
    int64_t  biggest_win;                       /* one round, credits paid back   */
    uint64_t coins_in;                          /* COIN presses                   */
    uint64_t credits_added;                     /* by the service menu            */
    uint64_t credit_resets;
} Stats;

typedef struct {
    int64_t  credits;
    int64_t  honey_pot;                         /* the progressive, carried over  */
    Settings settings;
    Stats    stats;
} SaveData;

void settings_defaults(Settings *s);
void save_data_defaults(SaveData *d);

/* Writes the payload. Returns its length (like snprintf: >= cap means it was
 * cut), or -1. SAVE_PAYLOAD_MAX is always enough. */
#define SAVE_PAYLOAD_MAX 8192
int  save_data_format(const SaveData *d, char *out, size_t cap);

/* Reads a payload into d (which should hold defaults first). Returns the
 * number of lines that were not understood (0 = all good); never fails. */
int  save_data_parse(SaveData *d, const char *payload, size_t len);

/* Credits for one COIN press: coin value / denomination, at least 1. */
int64_t settings_coin_credits(const Settings *s);

/* Allowed values, for the service menu's LEFT / RIGHT. */
extern const uint16_t settings_denoms[];      extern const int settings_ndenoms;
extern const uint16_t settings_coins[];       extern const int settings_ncoins;

#endif
