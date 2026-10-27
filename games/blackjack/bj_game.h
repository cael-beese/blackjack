/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#ifndef BPL_BJ_GAME_H
#define BPL_BJ_GAME_H

#include <stdint.h>

#include "engine/card.h"
#include "engine/event.h"
#include "engine/input.h"
#include "engine/rng.h"
#include "engine/wallet.h"
#include "games/blackjack/bj_bots.h"
#include "games/blackjack/bj_rules.h"
#include "games/blackjack/bj_strategy.h"

/* games/blackjack/bj_game.h - the blackjack table (CONTRACT section 3).

   One flat struct, advanced only by bj_tick at 60 Hz, deterministic in
   (seed, config, input frames). Five seats, numbered in the order they play:
   seat 0 is first base, at the dealer's left (the right of the screen). The
   player sits in the middle (cfg.human_seat, 2); the bee regulars fill the
   rest (bj_bots.h). The player's credits move only through the Wallet; the
   bots play from their own banks.

   Flow
     BETTING   the last round's cards stay on the table until the next deal.
               The player sets the bets; DEAL starts a round.
     CLEAR     the cards are swept; broke bots leave, newcomers sit down.
     SHUFFLE   a new shoe, when the cut card has come out (and first of all).
     DEAL      bots bet; one card to each seat, the up card, a second card
               each, the hole card face down.
     SIDES     the side bets are settled on the first two cards (+ up card).
     INSURANCE the dealer shows an ace: each seat insures or not.
     PEEK      the dealer, showing an ace or a ten, checks for blackjack.
     PLAY      each seat plays its hands in turn.
     DEALER    the hole card turns; the dealer draws to 17 (stands on soft 17).
     SETTLE    every hand is paid -> BETTING.

   Controls (the player's seat; every other seat is a bot)
     BETTING   UP, BET ONE, HOLD5: main bet up the ladder. DOWN, HOLD4: down.
               BET MAX: the largest bet the credits allow.
               HOLD1: PERFECT PAIRS off/5/10/25/50.  HOLD2: 21+3 the same.
               HOLD3: HONEY POT on/off (BJ_HONEY_BET).
               DEAL: deal (the bets are taken now).
     INSURANCE HOLD1: insure (EVEN MONEY with a blackjack).  HOLD2: no.
     PLAY      HOLD1 HIT, HOLD2 STAND, HOLD3 DOUBLE, HOLD4 SPLIT,
               HOLD5 SURRENDER. A play that is not allowed is DENIED.
   Hand log: the round seed (bj_round_seed) and the shoe seed replay a round.

   Randomness: the game Rng gives each shoe its seed; the shoe is a
   Fisher-Yates shuffle of six decks from that seed and is dealt in order.
   Bots' bets and thinking times come from a separate stream (bot_rng)
   which never touches the cards. */

#define BJ_SEATS        5
#define BJ_DEALER       5          /* "seat" of the dealer in events          */
#define BJ_HAND_CARDS   12
#define BJ_NO_HUMAN     (-1)

enum BjPhase {
    BJ_PH_BETTING, BJ_PH_CLEAR, BJ_PH_SHUFFLE, BJ_PH_DEAL, BJ_PH_SIDES,
    BJ_PH_INSURANCE, BJ_PH_PEEK, BJ_PH_PLAY, BJ_PH_DEALER, BJ_PH_SETTLE, BJ_PH_N
};

enum { BJ_SEAT_EMPTY, BJ_SEAT_HUMAN, BJ_SEAT_BOT };

/* Results of a hand. */
enum { BJ_RES_NONE, BJ_RES_WIN, BJ_RES_BLACKJACK, BJ_RES_PUSH, BJ_RES_LOSE, BJ_RES_BUST, BJ_RES_SURRENDER };

/* Events (engine/event.h; blackjack owns 100-199). seat = 0..4, BJ_DEALER. */
enum BjEventType {
    EV_BJ_PHASE = 100,      /* a = new phase                                            */
    EV_BJ_BETS,             /* the player's chosen bets changed: a = BJ_BET_*, v = amount */
    EV_BJ_DENIED,           /* a = BJ_DENY_*                                              */
    EV_BJ_CLEAR,            /* the table was swept                                        */
    EV_BJ_SHUFFLE,          /* a new shoe: v = cards before the cut card                  */
    EV_BJ_SEAT,             /* a = seat, b = BOT_* (or -1 empty), v = 1 sat / 0 left      */
    EV_BJ_STAKE,            /* chips go down: a = seat, b = BJ_BET_*, v = amount          */
    EV_BJ_CARD,             /* a = seat, b = hand, v = card | 0x100 face down             */
    EV_BJ_HOLE,             /* the hole card turns: v = card                              */
    EV_BJ_SIDE,             /* a = seat, b = side | (outcome << 4), v = paid (0: lost)    */
    EV_BJ_JACKPOT,          /* a = seat, v = HONEY POT won                                */
    EV_BJ_POT,              /* v = the HONEY POT now                                      */
    EV_BJ_INSURE,           /* a = seat, b = 1 insured / 0 not, v = stake                 */
    EV_BJ_PEEK,             /* a = 1 dealer blackjack                                     */
    EV_BJ_TURN,             /* a = seat, b = hand: its turn begins                        */
    EV_BJ_ACTION,           /* a = seat, b = hand, v = BJ_ACT_*                           */
    EV_BJ_SPLIT,            /* a = seat, b = the new hand's index                         */
    EV_BJ_BUST,             /* a = seat (or BJ_DEALER), b = hand                          */
    EV_BJ_BLACKJACK,        /* a = seat (or BJ_DEALER), b = hand                          */
    EV_BJ_RESULT,           /* a = seat, b = hand | (BJ_RES_* << 4), v = paid back        */
    EV_BJ_INS_RESULT,       /* a = seat, v = paid (0: lost)                               */
    EV_BJ_ROUND,            /* the round is settled: v = the player's net this round      */
    EV_BJ_CREDITS,          /* v = wallet credits now (clamped to int32)                  */
};

enum { BJ_BET_MAIN = BJ_SIDE_N, BJ_BET_DOUBLE, BJ_BET_SPLIT, BJ_BET_INSURANCE };
/* b of EV_BJ_STAKE / EV_BJ_BETS is a side (BJ_SIDE_*) or one of these. */

enum { BJ_DENY_CREDITS = 1, BJ_DENY_NOT_ALLOWED, BJ_DENY_MIN_BET };

typedef struct {
    Card    c[BJ_HAND_CARDS];
    uint8_t n;
    uint8_t done, doubled, from_split, split_aces, surrendered, result;
    int64_t bet;
    int64_t paid;            /* returned at settle (stake + winnings)    */
} BjHand;

typedef struct {
    int8_t  kind;            /* BJ_SEAT_*                                 */
    int8_t  bot;             /* BOT_* for a bot                           */
    BjBotMemory mem;         /* a bot's bank and last round               */
    uint8_t in_round;
    int64_t side[BJ_SIDE_N];
    int8_t  side_out[BJ_SIDE_N];
    int64_t side_paid[BJ_SIDE_N];
    int64_t insurance;
    uint8_t ins_decided;
    BjHand  hand[BJ_MAX_SPLIT_HANDS];
    uint8_t nhands, cur;
    int64_t staked, returned;  /* this round, all bets                   */
} BjSeat;

typedef struct {
    int     human_seat;      /* 0..4, or BJ_NO_HUMAN                      */
    int     demo;            /* the player's seat is played by BOT_DEMO   */
    int     bots[BJ_SEATS];  /* starting seats: BOT_* or -1 (human seat ignored) */
    int     bench[4];        /* who sits down when a seat frees: BOT_* or -1 */
    int64_t min_bet, max_bet;
    int     deal_gap;        /* ticks between cards dealt                 */
    int     act_gap;         /* ticks after a play before the next input  */
    int     peek_ticks, clear_ticks, shuffle_ticks, sides_ticks;
    int     dealer_gap;      /* ticks between the dealer's draws          */
    int     settle_ticks;    /* from the last dealer card to the payouts  */
    int     demo_bet_ticks;  /* demo: ticks in BETTING before it deals    */
} BjConfig;

typedef struct {
    BjConfig cfg;
    Rng      rng;            /* shoe seeds                                 */
    Rng      bot_rng;        /* bots: bets, thinking time                  */
    uint64_t shoe_seed, round_seed;
    Card     shoe[BJ_SHOE_CARDS];
    int      shoe_pos;
    int      shoes, rounds;
    uint8_t  need_shuffle;
    BjSeat   seat[BJ_SEATS];
    Card     dealer[BJ_HAND_CARDS];
    uint8_t  ndealer, hole_shown, dealer_bj;
    int      phase, timer, step;
    int      turn;           /* seat to act (PLAY)                         */
    int      wait;           /* ticks before the current seat may act      */
    int      bench_wait[4];  /* rounds before a bench bot sits             */
    uint8_t  table_dirty;    /* last round's cards still showing           */
    uint8_t  auto_deal;
    int64_t  pot;            /* the HONEY POT                              */
    /* The player's chosen bets for the next round. */
    int64_t  bet, side[BJ_SIDE_N];
    int64_t  last_net;       /* the player's net last round                 */
    uint64_t bot_round_seed;
} BjGame;

void bj_config_default(BjConfig *c);
/* pot: the HONEY POT carried over from the save (< BJ_HONEY_SEED: seeded). */
void bj_init(BjGame *g, const BjConfig *cfg, uint64_t seed, int64_t pot);
void bj_tick(BjGame *g, const InputFrame *in, Wallet *w, EventQueue *out);

/* What the player has on the table that a power cut would have to give
   back (the stakes of a round in progress). */
int64_t bj_pending(const BjGame *g);
/* The plays the player's current hand allows now (BJ_CAN_*, for the hint
   and the buttons); 0 when it is not the player's turn. */
unsigned bj_human_can(const BjGame *g, const Wallet *w);
int      bj_human_turn(const BjGame *g);      /* 1 while waiting for the player's play */
int      bj_human_insurance(const BjGame *g); /* 1 while waiting for the insurance answer */
const BjHand *bj_cur_hand(const BjGame *g, int seat);
Card     bj_up_card(const BjGame *g);
BjTotal  bj_dealer_total(const BjGame *g);    /* the shown cards only, until the hole turns */
int      bj_cards_left(const BjGame *g);      /* before the cut card */

/* The bet ladder. */
extern const int64_t bj_ladder[];
extern const int     bj_ladder_n;
extern const int64_t bj_side_ladder[];        /* 0 5 10 25 50 */
extern const int     bj_side_ladder_n;

const char *bj_phase_name(int phase);
const char *bj_result_name(int res);

#endif
