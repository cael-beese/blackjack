/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#include "games/blackjack/bj_game.h"

#include <string.h>

const int64_t bj_ladder[] = { 10, 20, 50, 100, 200, 500, 1000 };
const int     bj_ladder_n = (int)(sizeof bj_ladder / sizeof bj_ladder[0]);
const int64_t bj_side_ladder[] = { 0, 5, 10, 25, 50 };
const int     bj_side_ladder_n = (int)(sizeof bj_side_ladder / sizeof bj_side_ladder[0]);

void bj_config_default(BjConfig *c)
{
    memset(c, 0, sizeof *c);
    c->human_seat = 2;
    c->bots[0] = BOT_STINGER;
    c->bots[1] = BOT_HONEY;
    c->bots[2] = -1;
    c->bots[3] = BOT_BUZZ;
    c->bots[4] = BOT_QUEENIE;
    c->bench[0] = BOT_DRONE;
    c->bench[1] = c->bench[2] = c->bench[3] = -1;
    c->min_bet = 10;
    c->max_bet = 1000;
    c->deal_gap = 14;
    c->act_gap = 16;
    c->peek_ticks = 70;
    c->clear_ticks = 30;
    c->shuffle_ticks = 150;
    c->sides_ticks = 50;
    c->big_ticks = 200;
    c->jackpot_ticks = 450;
    c->dealer_gap = 36;
    c->settle_ticks = 30;
    c->demo_bet_ticks = 100;
}

static int32_t clamp32(int64_t v) { return v > 2147483647 ? 2147483647 : v < -2147483647 ? -2147483647 : (int32_t)v; }

static int is_human(const BjGame *g, int s) { return g->seat[s].kind == BJ_SEAT_HUMAN; }

static int seat_debit(BjGame *g, int s, int64_t n, Wallet *w, EventQueue *out)
{
    BjSeat *st = &g->seat[s];
    if (is_human(g, s)) {
        if (wallet_debit(w, n) != 0) return -1;
        ev_push(out, EV_BJ_CREDITS, 0, 0, clamp32(w->credits));
    } else {
        if (st->mem.bank < n) return -1;
        st->mem.bank -= n;
    }
    st->staked += n;
    return 0;
}

static void seat_credit(BjGame *g, int s, int64_t n, Wallet *w, EventQueue *out)
{
    BjSeat *st = &g->seat[s];
    if (n <= 0) return;
    if (is_human(g, s)) {
        wallet_credit(w, n);
        ev_push(out, EV_BJ_CREDITS, 0, 0, clamp32(w->credits));
    } else {
        st->mem.bank += n;
    }
    st->returned += n;
}

static int64_t seat_funds(const BjGame *g, int s, const Wallet *w)
{
    return is_human(g, s) ? w->credits : g->seat[s].mem.bank;
}

static void set_phase(BjGame *g, int ph, EventQueue *out)
{
    g->phase = ph;
    g->timer = 0;
    g->step = 0;
    g->wait = 0;
    ev_push(out, EV_BJ_PHASE, ph, 0, 0);
}

static void sit(BjGame *g, int s, int bot)
{
    BjSeat *st = &g->seat[s];
    memset(st, 0, sizeof *st);
    if (bot < 0) { st->kind = BJ_SEAT_EMPTY; st->bot = -1; return; }
    st->kind = BJ_SEAT_BOT;
    st->bot = (int8_t)bot;
    st->mem.bank = bj_bot_bankroll(bot);
}

void bj_init(BjGame *g, const BjConfig *cfg, uint64_t seed, int64_t pot)
{
    memset(g, 0, sizeof *g);
    g->cfg = *cfg;
    uint64_t sm = seed;
    rng_seed(&g->rng, rng_splitmix64(&sm));
    rng_seed(&g->bot_rng, rng_splitmix64(&sm));
    for (int s = 0; s < BJ_SEATS; s++) {
        if (s == cfg->human_seat) {
            if (cfg->demo) sit(g, s, BOT_DEMO);
            else { memset(&g->seat[s], 0, sizeof g->seat[s]); g->seat[s].kind = BJ_SEAT_HUMAN; g->seat[s].bot = -1; }
        } else {
            sit(g, s, cfg->bots[s]);
        }
    }
    g->pot = pot < BJ_HONEY_SEED ? BJ_HONEY_SEED : pot;
    g->need_shuffle = 1;
    g->bet = cfg->min_bet > 20 ? cfg->min_bet : 20;
    g->phase = BJ_PH_BETTING;
}

/* ---- queries ------------------------------------------------------------ */

const BjHand *bj_cur_hand(const BjGame *g, int s)
{
    const BjSeat *st = &g->seat[s];
    if (!st->nhands) return NULL;
    return &st->hand[st->cur < st->nhands ? st->cur : st->nhands - 1];
}

Card bj_up_card(const BjGame *g) { return g->ndealer ? g->dealer[0] : CARD_NONE; }

BjTotal bj_dealer_total(const BjGame *g)
{
    if (g->hole_shown) return bj_total(g->dealer, g->ndealer);
    return bj_total(g->dealer, g->ndealer ? 1 : 0);
}

int bj_cards_left(const BjGame *g)
{
    int left = BJ_CUT_CARD - g->shoe_pos;
    return left > 0 ? left : 0;
}

static int hand_is_bj(const BjSeat *st, const BjHand *h)
{
    return st->nhands == 1 && !h->from_split && h->n == 2 && bj_total(h->c, 2).total == 21;
}

int bj_human_turn(const BjGame *g)
{
    int hs = g->cfg.human_seat;
    if (!(g->phase == BJ_PH_PLAY && hs >= 0 && g->turn == hs && is_human(g, hs) && g->wait == 0 && g->step == 1))
        return 0;
    /* The hand just played may be finished; the turn moves on next tick. */
    const BjSeat *st = &g->seat[hs];
    const BjHand *h = &st->hand[st->cur];
    return !h->done && h->n >= 2 && bj_total(h->c, h->n).total < 21;
}

int bj_human_insurance(const BjGame *g)
{
    int hs = g->cfg.human_seat;
    return g->phase == BJ_PH_INSURANCE && hs >= 0 && is_human(g, hs) && g->seat[hs].in_round &&
           !g->seat[hs].ins_decided && g->wait == 0;
}

static unsigned seat_can(const BjGame *g, int s, const Wallet *w)
{
    const BjSeat *st = &g->seat[s];
    const BjHand *h = &st->hand[st->cur];
    unsigned can = 0;
    int64_t funds = seat_funds(g, s, w);
    if (h->n != 2 || h->split_aces) return 0;
    if (funds >= h->bet) {
        can |= BJ_CAN_DOUBLE;
        if (bj_is_pair(h->c, 2) && st->nhands < BJ_MAX_SPLIT_HANDS &&
            !(h->from_split && card_rank(h->c[0]) == RANK_A))
            can |= BJ_CAN_SPLIT;
    }
    if (!h->from_split && st->nhands == 1) can |= BJ_CAN_SURRENDER;
    return can;
}

unsigned bj_human_can(const BjGame *g, const Wallet *w)
{
    if (!bj_human_turn(g)) return 0;
    return seat_can(g, g->cfg.human_seat, w);
}

int64_t bj_pending(const BjGame *g)
{
    int hs = g->cfg.human_seat;
    if (hs < 0 || !is_human(g, hs)) return 0;
    const BjSeat *st = &g->seat[hs];
    if (!st->in_round || g->phase == BJ_PH_BETTING) return 0;
    /* A round cut short is void: everything still riding goes back. Side
       bets ride until they are settled after the deal. */
    int64_t p = 0;
    for (int i = 0; i < st->nhands; i++) p += st->hand[i].bet;
    if (g->phase <= BJ_PH_DEAL)
        for (int i = 0; i < BJ_SIDE_N; i++) p += st->side[i];
    if (g->phase <= BJ_PH_PEEK) p += st->insurance;
    return p;
}

/* ---- the shoe ----------------------------------------------------------- */

static void shuffle_shoe(BjGame *g, EventQueue *out)
{
    g->shoe_seed = rng_next(&g->rng);
    Rng r;
    rng_seed(&r, g->shoe_seed);
    for (int i = 0; i < BJ_SHOE_CARDS; i++) g->shoe[i] = (Card)(i % 52);
    for (int i = BJ_SHOE_CARDS - 1; i > 0; i--) {
        int j = (int)rng_below(&r, (uint32_t)i + 1);
        Card t = g->shoe[i]; g->shoe[i] = g->shoe[j]; g->shoe[j] = t;
    }
    g->shoe_pos = 0;
    g->shoes++;
    g->need_shuffle = 0;
    ev_push(out, EV_BJ_SHUFFLE, 0, 0, BJ_CUT_CARD);
}

static Card draw_card(BjGame *g)
{
    /* The cut card only marks the last round of a shoe; the round in play
       always finishes from the same shoe (312 cards are far more than any
       round can use after the cut at 234). */
    if (g->shoe_pos >= BJ_SHOE_CARDS) return CARD_NONE;
    return g->shoe[g->shoe_pos++];
}

static void deal_to(BjGame *g, int s, int h, EventQueue *out)
{
    BjHand *hd = &g->seat[s].hand[h];
    Card c = draw_card(g);
    if (hd->n < BJ_HAND_CARDS) hd->c[hd->n++] = c;
    ev_push(out, EV_BJ_CARD, s, h, c);
}

static void deal_dealer(BjGame *g, int face_down, EventQueue *out)
{
    Card c = draw_card(g);
    if (g->ndealer < BJ_HAND_CARDS) g->dealer[g->ndealer++] = c;
    ev_push(out, EV_BJ_CARD, BJ_DEALER, 0, c | (face_down ? 0x100 : 0));
}

/* ---- betting ------------------------------------------------------------ */

static int ladder_index(int64_t bet)
{
    int k = 0;
    for (int i = 0; i < bj_ladder_n; i++) if (bj_ladder[i] <= bet) k = i;
    return k;
}

static int64_t side_next(int64_t v)
{
    for (int i = 0; i < bj_side_ladder_n - 1; i++)
        if (bj_side_ladder[i] == v) return bj_side_ladder[i + 1];
    return 0;
}

static int64_t human_total_bet(const BjGame *g)
{
    int64_t t = g->bet;
    for (int i = 0; i < BJ_SIDE_N; i++) t += g->side[i];
    return t;
}

static int betting_input(BjGame *g, const InputFrame *in, const Wallet *w, EventQueue *out)
{
    uint32_t p = in->pressed;
    int changed = 0;
    int k = ladder_index(g->bet);
    if (p & (BTN_UP | BTN_BET_ONE | BTN_HOLD5)) {
        if (k + 1 < bj_ladder_n && bj_ladder[k + 1] <= g->cfg.max_bet) { g->bet = bj_ladder[k + 1]; changed = 1; }
        else ev_push(out, EV_BJ_DENIED, BJ_DENY_NOT_ALLOWED, 0, 0);
    } else if (p & (BTN_DOWN | BTN_HOLD4)) {
        if (k > 0 && bj_ladder[k - 1] >= g->cfg.min_bet) { g->bet = bj_ladder[k - 1]; changed = 1; }
        else ev_push(out, EV_BJ_DENIED, BJ_DENY_MIN_BET, 0, 0);
    } else if (p & BTN_BET_MAX) {
        int64_t room = w->credits - (human_total_bet(g) - g->bet);
        int64_t best = 0;
        for (int i = 0; i < bj_ladder_n; i++)
            if (bj_ladder[i] >= g->cfg.min_bet && bj_ladder[i] <= g->cfg.max_bet && bj_ladder[i] <= room)
                best = bj_ladder[i];
        if (best) { g->bet = best; changed = 1; }
        else ev_push(out, EV_BJ_DENIED, BJ_DENY_CREDITS, 0, 0);
    }
    if (changed) ev_push(out, EV_BJ_BETS, BJ_BET_MAIN, 0, clamp32(g->bet));
    if (p & BTN_HOLD1) { g->side[BJ_SIDE_PP] = side_next(g->side[BJ_SIDE_PP]); ev_push(out, EV_BJ_BETS, BJ_SIDE_PP, 0, (int)g->side[BJ_SIDE_PP]); changed = 1; }
    if (p & BTN_HOLD2) { g->side[BJ_SIDE_213] = side_next(g->side[BJ_SIDE_213]); ev_push(out, EV_BJ_BETS, BJ_SIDE_213, 0, (int)g->side[BJ_SIDE_213]); changed = 1; }
    if (p & BTN_HOLD3) {
        g->side[BJ_SIDE_HONEY] = g->side[BJ_SIDE_HONEY] ? 0 : BJ_HONEY_BET;
        ev_push(out, EV_BJ_BETS, BJ_SIDE_HONEY, 0, (int)g->side[BJ_SIDE_HONEY]);
        changed = 1;
    }
    return changed;
}

/* The player commits the bets: they are taken now. 0 = started. */
static int human_start(BjGame *g, Wallet *w, EventQueue *out)
{
    if (g->bet < g->cfg.min_bet) { ev_push(out, EV_BJ_DENIED, BJ_DENY_MIN_BET, 0, 0); return -1; }
    if (!wallet_can_afford(w, human_total_bet(g))) { ev_push(out, EV_BJ_DENIED, BJ_DENY_CREDITS, 0, 0); return -1; }
    return 0;
}

static void begin_round(BjGame *g, EventQueue *out)
{
    if (g->table_dirty) set_phase(g, BJ_PH_CLEAR, out);
    else if (g->need_shuffle) set_phase(g, BJ_PH_SHUFFLE, out);
    else set_phase(g, BJ_PH_DEAL, out);
}

/* ---- the round ---------------------------------------------------------- */

static void clear_table(BjGame *g, EventQueue *out)
{
    for (int s = 0; s < BJ_SEATS; s++) {
        BjSeat *st = &g->seat[s];
        st->in_round = 0;
        st->nhands = 0;
        st->cur = 0;
        st->insurance = 0;
        st->ins_decided = 0;
        st->staked = st->returned = 0;
        memset(st->hand, 0, sizeof st->hand);
        memset(st->side, 0, sizeof st->side);
        memset(st->side_out, 0, sizeof st->side_out);
        memset(st->side_paid, 0, sizeof st->side_paid);
    }
    g->ndealer = 0;
    g->hole_shown = 0;
    g->dealer_bj = 0;
    g->table_dirty = 0;
    ev_push(out, EV_BJ_CLEAR, 0, 0, 0);

    /* Who comes and goes happens between rounds, while the table is bare. */
    for (int s = 0; s < BJ_SEATS; s++) {
        BjSeat *st = &g->seat[s];
        if (st->kind == BJ_SEAT_BOT && st->bot != BOT_DEMO && st->mem.bank < g->cfg.min_bet) {
            int who = st->bot;
            sit(g, s, -1);
            ev_push(out, EV_BJ_SEAT, s, who, 0);
            /* The bot goes to the back of the bench and will buy in again. */
            for (int b = 0; b < 4; b++)
                if (g->cfg.bench[b] < 0) { g->cfg.bench[b] = who; g->bench_wait[b] = 6; break; }
        }
    }
    for (int s = 0; s < BJ_SEATS; s++) {
        if (g->seat[s].kind != BJ_SEAT_EMPTY || s == g->cfg.human_seat) continue;
        for (int b = 0; b < 4; b++) {
            if (g->cfg.bench[b] < 0) continue;
            if (g->bench_wait[b] > 0) { g->bench_wait[b]--; continue; }
            int who = g->cfg.bench[b];
            for (int k = b; k < 3; k++) { g->cfg.bench[k] = g->cfg.bench[k + 1]; g->bench_wait[k] = g->bench_wait[k + 1]; }
            g->cfg.bench[3] = -1;
            g->bench_wait[3] = 0;
            sit(g, s, who);
            ev_push(out, EV_BJ_SEAT, s, who, 1);
            break;
        }
    }
}

static void stake(BjGame *g, int s, int kind, int64_t n, Wallet *w, EventQueue *out)
{
    if (n <= 0) return;
    if (seat_debit(g, s, n, w, out) != 0) return;
    BjSeat *st = &g->seat[s];
    if (kind == BJ_BET_MAIN) {
        st->hand[0].bet = n;
        st->nhands = 1;
        st->in_round = 1;
    } else {
        st->side[kind] = n;
        if (kind == BJ_SIDE_HONEY) {
            g->pot += BJ_HONEY_FEED;
            ev_push(out, EV_BJ_POT, 0, 0, clamp32(g->pot));
        }
    }
    ev_push(out, EV_BJ_STAKE, s, kind, clamp32(n));
}

static void place_bets(BjGame *g, Wallet *w, EventQueue *out)
{
    g->rounds++;
    g->round_seed = g->shoe_seed ^ ((uint64_t)g->rounds * 0x9E3779B97F4A7C15ull);
    for (int s = 0; s < BJ_SEATS; s++) {
        BjSeat *st = &g->seat[s];
        if (st->kind == BJ_SEAT_HUMAN) {
            /* Checked by human_start; the main bet goes first so a side bet
               can never be taken without it. */
            if (!wallet_can_afford(w, human_total_bet(g))) continue;
            stake(g, s, BJ_BET_MAIN, g->bet, w, out);
            for (int i = 0; i < BJ_SIDE_N; i++) stake(g, s, i, g->side[i], w, out);
        } else if (st->kind == BJ_SEAT_BOT) {
            int64_t side[BJ_SIDE_N];
            int64_t b = bj_bot_bet(st->bot, &st->mem, g->cfg.min_bet, g->cfg.max_bet, g->pot, &g->bot_rng, side);
            if (b <= 0) continue;
            stake(g, s, BJ_BET_MAIN, b, w, out);
            for (int i = 0; i < BJ_SIDE_N; i++) stake(g, s, i, side[i], w, out);
            st->mem.last_bet = b;
        }
    }
}

static int seats_in_round(const BjGame *g, int *list)
{
    int n = 0;
    for (int s = 0; s < BJ_SEATS; s++) if (g->seat[s].in_round) list[n++] = s;
    return n;
}

/* Pays the side bets on the first two cards and the up card. Returns how big
   the biggest moment was: 0 none, 1 a win, 2 a win of 25 to 1 or more, 3 a
   HONEY POT - the table holds for it (BjConfig.big_ticks, jackpot_ticks). */
static int settle_sides(BjGame *g, Wallet *w, EventQueue *out)
{
    Card up = g->dealer[0];
    int moment = 0;
    for (int s = 0; s < BJ_SEATS; s++) {
        BjSeat *st = &g->seat[s];
        if (!st->in_round) continue;
        Card a = st->hand[0].c[0], b = st->hand[0].c[1];
        for (int k = 0; k < BJ_SIDE_N; k++) {
            if (!st->side[k]) continue;
            int o = 0;
            int64_t paid = 0;
            int odds = 0;
            if (k == BJ_SIDE_PP) { o = bj_pp_outcome(a, b); odds = bj_pp_pays[o]; }
            if (k == BJ_SIDE_213) { o = bj_213_outcome(a, b, up); odds = bj_213_pays[o]; }
            if (k == BJ_SIDE_HONEY) {
                o = bj_hp_outcome(a, b, up);
                odds = bj_hp_pays[o];
                if (o == HP_JACKPOT) {
                    paid = g->pot + st->side[k];
                    ev_push(out, EV_BJ_JACKPOT, s, 0, clamp32(g->pot));
                    g->pot = BJ_HONEY_SEED;
                    ev_push(out, EV_BJ_POT, 0, 0, clamp32(g->pot));
                    moment = 3;
                }
            }
            if (o && !paid) paid = st->side[k] * (odds + 1);
            if (o && moment < 2) moment = odds >= 25 ? 2 : 1;
            st->side_out[k] = (int8_t)o;
            st->side_paid[k] = paid;
            seat_credit(g, s, paid, w, out);
            ev_push(out, EV_BJ_SIDE, s, k | (o << 4), clamp32(paid));
        }
    }
    return moment;
}

static void settle(BjGame *g, Wallet *w, EventQueue *out)
{
    BjTotal d = bj_total(g->dealer, g->ndealer);
    int dbust = d.total > 21;
    for (int s = 0; s < BJ_SEATS; s++) {
        BjSeat *st = &g->seat[s];
        if (!st->in_round) continue;
        if (st->insurance) {
            int64_t paid = g->dealer_bj ? st->insurance * 3 : 0;
            seat_credit(g, s, paid, w, out);
            if (g->dealer_bj) ev_push(out, EV_BJ_INS_RESULT, s, 0, clamp32(paid));
        }
        for (int h = 0; h < st->nhands; h++) {
            BjHand *hd = &st->hand[h];
            BjTotal t = bj_total(hd->c, hd->n);
            int res;
            int64_t paid = 0;
            if (hd->surrendered) { res = BJ_RES_SURRENDER; paid = hd->bet / 2; }
            else if (t.total > 21) res = BJ_RES_BUST;
            else if (hand_is_bj(st, hd)) {
                if (g->dealer_bj) { res = BJ_RES_PUSH; paid = hd->bet; }
                else { res = BJ_RES_BLACKJACK; paid = hd->bet + hd->bet * 3 / 2; }
            }
            else if (g->dealer_bj) res = BJ_RES_LOSE;
            else if (dbust || t.total > d.total) { res = BJ_RES_WIN; paid = hd->bet * 2; }
            else if (t.total == d.total) { res = BJ_RES_PUSH; paid = hd->bet; }
            else res = BJ_RES_LOSE;
            hd->result = (uint8_t)res;
            hd->paid = paid;
            seat_credit(g, s, paid, w, out);
            ev_push(out, EV_BJ_RESULT, s, h | (res << 4), clamp32(paid));
        }
        int64_t net = st->returned - st->staked;
        if (st->kind == BJ_SEAT_BOT) st->mem.last_net = net;
        if (is_human(g, s)) g->last_net = net;
    }
    int hs = g->cfg.human_seat;
    ev_push(out, EV_BJ_ROUND, 0, 0, clamp32((hs >= 0 && is_human(g, hs)) ? g->last_net : 0));
    g->table_dirty = 1;
    if (g->shoe_pos >= BJ_CUT_CARD) g->need_shuffle = 1;
}

/* Moves the turn to the next hand that needs playing; DEALER when none. */
static void next_turn(BjGame *g, EventQueue *out)
{
    BjSeat *st = &g->seat[g->turn];
    g->step = 0;        /* the new turn has not been announced yet */
    if (st->cur + 1 < st->nhands) { st->cur++; return; }
    g->turn++;
    while (g->turn < BJ_SEATS && !g->seat[g->turn].in_round) g->turn++;
    if (g->turn >= BJ_SEATS) { set_phase(g, BJ_PH_DEALER, out); return; }
    g->seat[g->turn].cur = 0;
}

static void start_play(BjGame *g, EventQueue *out)
{
    set_phase(g, BJ_PH_PLAY, out);
    g->turn = 0;
    while (g->turn < BJ_SEATS && !g->seat[g->turn].in_round) g->turn++;
    if (g->turn >= BJ_SEATS) { set_phase(g, BJ_PH_DEALER, out); return; }
    g->seat[g->turn].cur = 0;
}

static void apply_play(BjGame *g, int s, int act, Wallet *w, EventQueue *out)
{
    BjSeat *st = &g->seat[s];
    BjHand *h = &st->hand[st->cur];
    ev_push(out, EV_BJ_ACTION, s, st->cur, act);
    switch (act) {
    case BJ_ACT_HIT:
        deal_to(g, s, st->cur, out);
        break;
    case BJ_ACT_STAND:
        h->done = 1;
        break;
    case BJ_ACT_DOUBLE:
        seat_debit(g, s, h->bet, w, out);
        ev_push(out, EV_BJ_STAKE, s, BJ_BET_DOUBLE, clamp32(h->bet));
        h->bet *= 2;
        h->doubled = 1;
        deal_to(g, s, st->cur, out);
        h->done = 1;
        break;
    case BJ_ACT_SPLIT: {
        int nh = st->nhands;
        BjHand *n2 = &st->hand[nh];
        seat_debit(g, s, h->bet, w, out);
        memset(n2, 0, sizeof *n2);
        n2->bet = h->bet;
        n2->c[0] = h->c[1];
        n2->n = 1;
        h->n = 1;
        h->from_split = n2->from_split = 1;
        if (card_rank(h->c[0]) == RANK_A) h->split_aces = n2->split_aces = 1;
        st->nhands++;
        ev_push(out, EV_BJ_STAKE, s, BJ_BET_SPLIT, clamp32(n2->bet));
        ev_push(out, EV_BJ_SPLIT, s, nh, 0);
        break;
    }
    case BJ_ACT_SURRENDER:
        h->surrendered = 1;
        h->done = 1;
        break;
    }
    BjTotal t = bj_total(h->c, h->n);
    if (h->n >= 2 && t.total > 21) { h->done = 1; ev_push(out, EV_BJ_BUST, s, st->cur, 0); }
    else if (h->n >= 2 && t.total == 21) h->done = 1;
    g->wait = act == BJ_ACT_SPLIT ? g->cfg.deal_gap : g->cfg.act_gap;
}

static int read_play(uint32_t p)
{
    if (p & BTN_HOLD1) return BJ_ACT_HIT;
    if (p & BTN_HOLD2) return BJ_ACT_STAND;
    if (p & BTN_HOLD3) return BJ_ACT_DOUBLE;
    if (p & BTN_HOLD4) return BJ_ACT_SPLIT;
    if (p & BTN_HOLD5) return BJ_ACT_SURRENDER;
    return BJ_ACT_NONE;
}

static int legal(unsigned can, int act)
{
    switch (act) {
    case BJ_ACT_HIT: case BJ_ACT_STAND: return 1;
    case BJ_ACT_DOUBLE: return (can & BJ_CAN_DOUBLE) != 0;
    case BJ_ACT_SPLIT: return (can & BJ_CAN_SPLIT) != 0;
    case BJ_ACT_SURRENDER: return (can & BJ_CAN_SURRENDER) != 0;
    }
    return 0;
}

static void tick_play(BjGame *g, const InputFrame *in, Wallet *w, EventQueue *out)
{
    if (g->wait > 0) { g->wait--; return; }
    int s = g->turn;
    BjSeat *st = &g->seat[s];
    BjHand *h = &st->hand[st->cur];

    /* A split hand gets its second card when its turn comes. */
    if (h->n == 1) {
        deal_to(g, s, st->cur, out);
        if (h->split_aces || bj_total(h->c, 2).total == 21) h->done = 1;
        g->wait = g->cfg.deal_gap;
        return;
    }
    if (h->done || bj_total(h->c, h->n).total >= 21 || hand_is_bj(st, h)) {
        h->done = 1;
        next_turn(g, out);
        return;
    }
    if (g->step == 0) {
        g->step = 1;
        ev_push(out, EV_BJ_TURN, s, st->cur, 0);
        if (st->kind == BJ_SEAT_BOT) {
            int act = bj_bot_play(st->bot, h->c, h->n, g->dealer[0], seat_can(g, s, w));
            g->wait = bj_bot_think(st->bot, act, &g->bot_rng);
        }
        return;
    }
    int act;
    unsigned can = seat_can(g, s, w);
    if (st->kind == BJ_SEAT_BOT) {
        act = bj_bot_play(st->bot, h->c, h->n, g->dealer[0], can);
        if (!legal(can, act)) act = BJ_ACT_STAND;
    } else {
        act = read_play(in->pressed);
        if (act == BJ_ACT_NONE) return;
        if (!legal(can, act)) {
            ev_push(out, EV_BJ_DENIED, (act == BJ_ACT_DOUBLE || act == BJ_ACT_SPLIT) && seat_funds(g, s, w) < h->bet
                                       ? BJ_DENY_CREDITS : BJ_DENY_NOT_ALLOWED, act, 0);
            return;
        }
    }
    apply_play(g, s, act, w, out);
}

static int any_live_hand(const BjGame *g)
{
    for (int s = 0; s < BJ_SEATS; s++) {
        const BjSeat *st = &g->seat[s];
        if (!st->in_round) continue;
        for (int h = 0; h < st->nhands; h++) {
            const BjHand *hd = &st->hand[h];
            if (hd->surrendered || bj_total(hd->c, hd->n).total > 21 || hand_is_bj(st, hd)) continue;
            return 1;
        }
    }
    return 0;
}

void bj_tick(BjGame *g, const InputFrame *in, Wallet *w, EventQueue *out)
{
    static const InputFrame none;
    int hs = g->cfg.human_seat;
    int human = hs >= 0 && is_human(g, hs);
    if (!in) in = &none;
    g->timer++;

    switch (g->phase) {
    case BJ_PH_BETTING:
        if (human) {
            betting_input(g, in, w, out);
            if ((in->pressed & BTN_DEAL) || g->auto_deal) {
                g->auto_deal = 0;
                if (human_start(g, w, out) == 0) begin_round(g, out);
            }
        } else if (g->timer >= g->cfg.demo_bet_ticks) {
            begin_round(g, out);
        }
        break;

    case BJ_PH_CLEAR:
        if (g->timer == 1) clear_table(g, out);
        if (g->timer >= g->cfg.clear_ticks) {
            if (g->need_shuffle) set_phase(g, BJ_PH_SHUFFLE, out);
            else set_phase(g, BJ_PH_DEAL, out);
        }
        break;

    case BJ_PH_SHUFFLE:
        if (g->timer == 1) shuffle_shoe(g, out);
        if (g->timer >= g->cfg.shuffle_ticks) set_phase(g, BJ_PH_DEAL, out);
        break;

    case BJ_PH_DEAL: {
        int list[BJ_SEATS];
        if (g->timer == 1) {
            place_bets(g, w, out);
            if (seats_in_round(g, list) == 0) {
                /* Nobody could bet: back to betting with a bare table. */
                set_phase(g, BJ_PH_BETTING, out);
                break;
            }
            g->wait = g->cfg.deal_gap;
            break;
        }
        if (g->wait > 0) { g->wait--; break; }
        int n = seats_in_round(g, list);
        int per = n + 1;                       /* each seat, then the dealer */
        int k = g->step;
        if (k < 2 * per) {
            int round = k / per, i = k % per;
            if (i < n) deal_to(g, list[i], 0, out);
            else deal_dealer(g, round == 1, out);
            g->step++;
            g->wait = g->cfg.deal_gap;
            break;
        }
        for (int s = 0; s < BJ_SEATS; s++) {
            const BjSeat *st = &g->seat[s];
            if (st->in_round && hand_is_bj(st, &st->hand[0])) ev_push(out, EV_BJ_BLACKJACK, s, 0, 0);
        }
        set_phase(g, BJ_PH_SIDES, out);
        break;
    }

    case BJ_PH_SIDES: {
        if (g->timer == 1) {
            int any = 0;
            for (int s = 0; s < BJ_SEATS; s++)
                for (int k = 0; k < BJ_SIDE_N; k++) any |= g->seat[s].in_round && g->seat[s].side[k] > 0;
            int moment = settle_sides(g, w, out);
            /* How long the table waits before play goes on: a moment for the
               side bets to be paid, longer for a big win, and the whole
               HONEY POT takeover. */
            g->step = !any ? 0 : moment == 3 ? g->cfg.jackpot_ticks : moment == 2 ? g->cfg.big_ticks : g->cfg.sides_ticks;
        }
        if (g->timer >= g->step) {
            int upv = bj_card_value(g->dealer[0]);
            if (upv == 11) set_phase(g, BJ_PH_INSURANCE, out);
            else if (upv == 10) set_phase(g, BJ_PH_PEEK, out);
            else start_play(g, out);
        }
        break;
    }

    case BJ_PH_INSURANCE: {
        if (g->timer == 1) {
            for (int s = 0; s < BJ_SEATS; s++) {
                BjSeat *st = &g->seat[s];
                if (!st->in_round || st->kind != BJ_SEAT_BOT) continue;
                int64_t cost = st->hand[0].bet / 2;
                int take = bj_bot_insure(st->bot, st->hand[0].c, 2) && st->mem.bank >= cost;
                if (take) { seat_debit(g, s, cost, w, out); st->insurance = cost; ev_push(out, EV_BJ_STAKE, s, BJ_BET_INSURANCE, clamp32(cost)); }
                st->ins_decided = 1;
                ev_push(out, EV_BJ_INSURE, s, take, clamp32(take ? cost : 0));
            }
            g->wait = g->cfg.act_gap;
        }
        if (g->wait > 0) { g->wait--; break; }
        if (human && g->seat[hs].in_round && !g->seat[hs].ins_decided) {
            BjSeat *st = &g->seat[hs];
            int64_t cost = st->hand[0].bet / 2;
            if (in->pressed & BTN_HOLD1) {
                if (!wallet_can_afford(w, cost)) { ev_push(out, EV_BJ_DENIED, BJ_DENY_CREDITS, 0, 0); break; }
                seat_debit(g, hs, cost, w, out);
                st->insurance = cost;
                st->ins_decided = 1;
                ev_push(out, EV_BJ_STAKE, hs, BJ_BET_INSURANCE, clamp32(cost));
                ev_push(out, EV_BJ_INSURE, hs, 1, clamp32(cost));
                g->wait = g->cfg.act_gap;
            } else if (in->pressed & BTN_HOLD2) {
                st->ins_decided = 1;
                ev_push(out, EV_BJ_INSURE, hs, 0, 0);
                g->wait = g->cfg.act_gap;
            }
            break;
        }
        set_phase(g, BJ_PH_PEEK, out);
        break;
    }

    case BJ_PH_PEEK:
        if (g->timer == 1) {
            g->dealer_bj = bj_total(g->dealer, 2).total == 21;
            ev_push(out, EV_BJ_PEEK, g->dealer_bj, 0, 0);
        }
        if (g->timer < g->cfg.peek_ticks) break;
        if (g->dealer_bj) {
            g->hole_shown = 1;
            ev_push(out, EV_BJ_HOLE, 0, 0, g->dealer[1]);
            ev_push(out, EV_BJ_BLACKJACK, BJ_DEALER, 0, 0);
            set_phase(g, BJ_PH_SETTLE, out);
            g->wait = g->cfg.dealer_gap;
            break;
        }
        for (int s = 0; s < BJ_SEATS; s++)
            if (g->seat[s].insurance) ev_push(out, EV_BJ_INS_RESULT, s, 0, 0);
        start_play(g, out);
        break;

    case BJ_PH_PLAY:
        tick_play(g, in, w, out);
        break;

    case BJ_PH_DEALER:
        if (g->timer == 1) {
            g->hole_shown = 1;
            ev_push(out, EV_BJ_HOLE, 0, 0, g->dealer[1]);
            g->wait = g->cfg.dealer_gap;
            g->step = any_live_hand(g);
            break;
        }
        if (g->wait > 0) { g->wait--; break; }
        if (g->step && bj_total(g->dealer, g->ndealer).total < 17) {
            deal_dealer(g, 0, out);
            if (bj_total(g->dealer, g->ndealer).total > 21) ev_push(out, EV_BJ_BUST, BJ_DEALER, 0, 0);
            g->wait = g->cfg.dealer_gap;
            break;
        }
        set_phase(g, BJ_PH_SETTLE, out);
        g->wait = g->cfg.settle_ticks;
        break;

    case BJ_PH_SETTLE:
        if (g->wait > 0) { g->wait--; break; }
        settle(g, w, out);
        set_phase(g, BJ_PH_BETTING, out);
        break;
    }
}

const char *bj_phase_name(int ph)
{
    static const char *const n[BJ_PH_N] = { "BETTING", "CLEAR", "SHUFFLE", "DEAL", "SIDES", "INSURANCE",
                                            "PEEK", "PLAY", "DEALER", "SETTLE" };
    return (ph >= 0 && ph < BJ_PH_N) ? n[ph] : "?";
}

const char *bj_result_name(int r)
{
    static const char *const n[] = { "", "WIN", "BLACKJACK", "PUSH", "LOSE", "BUST", "SURRENDER" };
    return (r >= 0 && r <= BJ_RES_SURRENDER) ? n[r] : "";
}
