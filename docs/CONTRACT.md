# Beese's Blackjack - architecture contract

Binding for everyone who works on this code (people and agents). `SPEC.md` is
what the game is; this is how the pieces fit. The platform and render layers
came from Beese's Poker Lounge and keep its rules.

## 1. Layout and build targets

```
engine/           cards, RNG, deck, events, replay, fixed step      -> bpl_engine    (pure C11, no raylib)
games/blackjack/  rules, side bets, the hint, the regulars, the table -> bpl_blackjack (pure; depends engine)
audio/            sound ids, mixer wrapper, music, ducking            -> bpl_audio     (raylib)
render/           atlas, procedural cards and art, bloom, particles,
                  UI kit, the table / lobby / attract presentation     -> bpl_render    (raylib)
platform/         main loop, DRM/desktop, input + config.ini, panel,
                  save, service menu, the modes, debug overlay, perf  -> bpl_platform + executable `beese-blackjack`
tests/            ctest programs                                      -> link ONLY pure libs
tools/            scripts (raylib, screenshots, cabinet checks), audiogen
assets/           shipped files (fonts, music, sfx)
third_party/      raylib source/build (gitignored; fetched by script)
```

- C11, `-Wall -Wextra -Werror` on our code. No warnings, ever.
- `-DBPL_PLATFORM=DRM` (the Pi) or `DESKTOP` (development, WSL + Xvfb).
- `ctest` runs every test without a display, on x86 and on the Pi.

## 2. The engine

`engine/card.h` (a card is a byte, rank * 4 + suit), `engine/rng.h`
(xoshiro256** seeded through splitmix64), `engine/event.h` (GameEvent queue;
blackjack owns types 100-199, the app 300-399), `engine/wallet.h` (the
player's credits), `engine/input.h` (logical buttons, one InputFrame a tick).

## 3. The game

```c
void bj_init(BjGame *g, const BjConfig *cfg, uint64_t seed, int64_t pot);
void bj_tick(BjGame *g, const InputFrame *in, Wallet *w, EventQueue *out);
```

- **All game state is one flat struct** (no pointers into itself, no heap),
  so it can be copied, saved and compared.
- `bj_tick` runs at exactly 60 Hz, is the ONLY thing that changes game state,
  and is **deterministic**: same seed + config + input frames = same states
  and events. Timers are counted in ticks.
- The player's credits move only through the Wallet; the regulars play from
  their own banks inside the struct.
- Everything presentation or audio should react to is an event
  (games/blackjack/bj_game.h lists them with their a / b / v).

## 4. The presentation pattern (render/ and audio/)

The modes hand the views a read-only `TableViewInfo` / `MenuViewInfo` /
`AttractViewInfo` once a frame through `platform/table_view.h`:

```c
void table_present_update(const TableViewInfo *v, const GameEvent *ev, int nev, float dt);
void table_present_draw(const TableViewInfo *v, double time);   /* inside the 1280x720 target */
```

- Presentation keeps its own cosmetic state and takes `const` game state. It
  **never writes game state** and never calls the game's RNG. Cosmetic
  randomness uses its own stream (fx.h).
- Audio is triggered from presentation (reacting to events), never from game
  logic.
- No allocation per frame in render/ or audio/.
- Every effect has a settings toggle (`settings.effects.*`).
- 1280x720 is the play space, scaled 2x on the cabinet's 3440x1440 screen
  with side art in the margins.

## 5. Honesty rules (non-negotiable)

- Cards come only from the shoe: six decks, shuffled Fisher-Yates from the
  shoe's own seed (taken from the game Rng), dealt in order. Nothing inspects
  the undealt shoe to decide what to show, when to show it, or what anyone
  does.
- The game Rng is seeded per session; each shoe's seed is logged with every
  round in the hand log. The regulars' bets and thinking times come from a
  separate stream (`bot_rng`) that never touches the cards; presentation and
  attract have their own.
- No near-miss engineering, no adaptive odds, no "due" logic, no difference
  between attract and real play.
- **The regulars never see hidden cards or the shoe.** `bj_bot_play` gets the
  bot's own cards and the dealer's up card; nothing else. The hint
  (`bj_advise`) is the same: the player's cards and the up card.
- The dealer's peek looks the same whether there is a blackjack or not; its
  result comes from EV_BJ_PEEK.
- `BJ_TEST_SHOE` (stacks the shoe, for screenshots) is compiled into the
  desktop build only. The cabinet's DRM build has no such code.

## 6. Performance rules (the Pi 4 is the only judge)

- Measure on the Pi before calling anything done: the F1 overlay (frame ms,
  draw calls, RSS, texture memory) and `--perf-csv` frame logs.
- Budget: 60 fps locked in the heaviest scene, 1% lows >= 55 fps, RSS <= 150
  MB, textures <= 64 MB, cold start to attract <= 4 s, < 100 draw calls.
- Sprites through atlases; post-processing at 1/2 or 1/4 resolution.

## 7. Working rules

- Source is ASCII. Comments say why, in full sentences.
- Every source file and script starts with
  `/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */`
  (`# ...` for shell/CMake, after any shebang). Third-party code keeps its
  own license and is never committed (raylib is fetched by a script).
- The repository may be public: no machine addresses, personal paths or
  secrets in anything committed (`<pi-address>`, `/path/to/BeeseBlackjack`).
- `-Werror` clean, `ctest` green, before every commit.
- Screenshots: `beese-blackjack --shot N:file.png[,N:file.png...] --script ...`
  - look at your work.
- Report measured numbers, not expectations. If something could not be
  measured, say "not measured".
