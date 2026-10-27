# Systems: game flow, credits, persistence, service, attract, install

How the game runs as a cabinet machine. The rules are in SPEC.md, the
architecture in CONTRACT.md, the platform in PLATFORM.md.

## 1. Flow and controls

`ATTRACT -> MENU (the lobby) -> TABLE`, with SERVICE from anywhere.

- **Attract** (`platform/mode_attract.c`): the title card (8 s), then about
  50 s of the real table played by the bees, the player's seat by the DEMO
  bot playing the hint. Any button goes to the lobby; COIN is credited too.
- **Lobby** (`platform/mode_menu.c`): HIT takes a seat, STAND turns the
  strategy hint on or off, DOUBLE opens CONTROLS; the stick and DEAL choose
  as well. The CONTROLS page opens by itself the first time a person reaches
  the lobby; DEAL held 2 s there opens BUTTON MAP.
- **Table** (`platform/mode_table.c`, `games/blackjack/bj_game.h`): set the
  bets (HIT Perfect Pairs, STAND 21+3, DOUBLE the HONEY POT, SPLIT / SURRENDER
  or the stick the main bet), DEAL. In a round: HIT, STAND, DOUBLE, SPLIT,
  SURRENDER; insurance is HIT yes, STAND no. Stick up on your turn: the hint.
  CASH OUT between rounds goes back to the lobby; during a round it is
  refused ("FINISH THE ROUND FIRST"). The table carries on across visits to
  the lobby: the same shoe, regulars and bets. 60 s idle between rounds:
  attract.
- SELECT + START held together leaves the game for EmulationStation
  (`config.ini` EXIT_COMBO).

The panel layout, LEARN PANEL and BUTTON MAP are Poker Lounge's
(`platform/panel.h`), relabelled for blackjack. They keep their own
`panel.ini` in this game's save folder.

## 2. Credits

Free-play / bar credits only; nothing is ever paid out. A COIN press adds
`coin value / denomination` credits (defaults $1 and 25c: 4 credits). A fresh
cabinet starts with 1000 credits. The bets are taken when DEAL is pressed;
doubles, splits and insurance when they are made; every payout is credited
as the round settles (side bets right after the deal).

## 3. Persistence

**What.** One file holds the credits, the HONEY POT, statistics (rounds,
hands, wagered and paid, blackjacks, doubles, splits, surrenders, insurance,
wins / pushes / losses, each side bet's stakes, payouts and hits, HONEY POTs
won, the biggest round, coins, service credits) and settings (volumes, music,
hint default, the regulars on / off, denomination, coin value, the effect
toggles).

**Where.** `[game] save_dir` in `config.ini`; default
`$XDG_DATA_HOME/beese-blackjack`, else `~/.local/share/beese-blackjack`: on
the cabinet the SD card's ext4 root, deliberately not the vfat ROM stick. If
it cannot be written the game runs from RAM and the service menu says so.

**Format and writing** are Poker Lounge's (`platform/save.c`): a header line
with a sequence number, length and CRC, then `key=value` lines; written to a
`.tmp`, fsynced, renamed over with a `.bak` kept, the directory fsynced; the
newest valid of the three is loaded. Writes run on a background thread.

**When.** The file always holds what the player is owed if the power went
now: `wallet + pending`, where pending is everything of the player's still
riding in a round (`bj_pending`: the hands' bets, side bets until they are
settled, insurance until the peek). An interrupted round is void and those
bets go back. It is rewritten when that figure changes, after every round
(stats), on a coin, a service-menu change, and on exit.

**Hand log.** `save_dir/hands.log`, one line per round: time, `TABLE`, the
shoe's seed, the round number, the dealer's cards, each of the player's hands
with its bet, cards and result, and what was staked and returned.

**Tests:** `tests/platform/test_save.c` (ctest `save_persistence`): round
trips, clamping, power cuts at every byte, truncation, bit flips, a writer
killed 40 times, ENOSPC, unwritable directories.

## 4. Service menu

START held 3 s (F2 on a keyboard). HIT / STAND up and down, SPLIT /
SURRENDER the value, DEAL choose, CASH OUT back. Lines: credits; add credits;
reset credits (DEAL twice); denomination; coin value; volumes; music; the
strategy hint at start; the bee regulars at the table (off: heads-up against
the dealer; takes effect between rounds); EFFECTS; STATISTICS (return of the
main game and of each side bet, the HONEY POT); LEARN PANEL; BUTTON MAP and
its reset; INPUT TEST; SOUND TEST; SELF-TEST; reset statistics; exit.

Self-test: hand totals and every side-bet outcome on known hands; the exact
side-bet returns (93.8907% and 95.3790%); the hint against twelve chart
decisions players get wrong; RNG replay and a 20,000-shuffle chi-square; a
thousand rounds of the table balancing to the credit with six-deck shoes;
the save directory, the audio device, the display mode.

## 5. Attract

After 60 s without input in the lobby or between rounds. The demo is the
real table run by `bj_tick` on attract's own seed, with a private wallet; the
player's credits and the saved HONEY POT are never touched (the demo's pot is
a copy).

## 6. install.sh and RetroPie

See the header of `install.sh`. Program in `/opt/beese-blackjack` (binary,
`assets/`, `config.ini`; an edited `config.ini` is kept). RetroPie: the
launcher `~/RetroPie/roms/ports/Beese's Blackjack.sh` runs
`runcommand.sh 0 _PORT_ "beese-blackjack" ""` like the other ports, and
`/opt/retropie/configs/ports/beese-blackjack/emulators.cfg` names the
binary. It is a separate entry beside WILD 7'S and Poker Lounge; nothing of
theirs is touched. `--dry-run` prints every action; `--uninstall` removes
only its own files (`--purge` also the saves).

## 7. The presentation seam

`platform/table_view.h`: the modes hand the views read-only info structs
once a frame; `platform/app_modes.c` installs `app_views_render`
(render/views.c): `table_present.c` for the table (and the attract demo),
`lounge_view.c` for the lobby and attract title, `controls_view.c` for the
CONTROLS page.
