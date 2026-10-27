# Beese's Blackjack - what is being built

The third game for the RetroPie cabinet, after Beese's WILD 7'S and Beese's
Poker Lounge, in the same neon honey lounge look. Started 2026-10-27.

## The owner's choices

- **Its own game and Port**: its own repository, Ports entry, save folder,
  LEARN PANEL and BUTTON MAP. It shares Poker Lounge's platform and render
  code (copied, not linked) and never reads or writes another game's files.
- **You and the bee regulars**: a five-seat table, the player in the middle
  seat, four regulars in the others.
- **Extras**: the strategy hint, the Perfect Pairs and 21+3 side bets, and a
  bee progressive (the HONEY POT).
- **Rules**: Vegas Strip.

## Rules

| | |
|---|---|
| Shoe | six decks, a new shuffle when the cut card (234 of 312) has come out |
| Dealer | stands on all 17s, soft too; peeks under an ace or a ten |
| Blackjack | pays 3 to 2; a split hand's two-card 21 is 21, not blackjack |
| Double | on any first two cards, and after a split |
| Split | any two cards of the same value (K-Q too), to four hands; split aces take one card each and do not split again |
| Surrender | late, on the first two cards, not after a split; half the bet back |
| Insurance | offered when the dealer shows an ace, half the bet, pays 2 to 1; with a blackjack it is offered as EVEN MONEY |
| Limits | main bet 10 to 1,000 on the ladder 10 20 50 100 200 500 1,000 |

Basic strategy (the hint's play) returns 99.64% +/- 0.21% over 300,000
simulated rounds (tests/blackjack/test_bj_game.c).

## Side bets

Exact returns over a six-deck shoe (tests/blackjack/test_bj_rules.c):

| Perfect Pairs (first two cards) | pays |
|---|---|
| perfect pair (same card) | 25:1 |
| coloured pair | 12:1 |
| mixed pair | 6:1 |
| **return** | **93.89%** |

| 21+3 (two cards + the up card) | pays |
|---|---|
| suited trips | 100:1 |
| straight flush | 40:1 |
| three of a kind | 30:1 |
| straight (A-2-3 and Q-K-A count) | 10:1 |
| flush | 5:1 |
| **return** | **95.38%** |

| HONEY POT (5 credits; sevens in order: card 1, card 2, up card) | pays |
|---|---|
| three suited sevens | the pot (at least 10,000) |
| three sevens of one colour | 500:1 |
| three sevens | 100:1 |
| two suited sevens | 50:1 |
| two sevens | 25:1 |
| a seven first | 3:1 |
| **return** | 51.9% fixed + the pot (55.1% at the seed); 1 credit of every 5 feeds the pot |

The pot is won about once in 62,700 hands. It is kept in the save file and
fed by every HONEY POT bet at the table, the regulars' too.

## The regulars

| | style | play | bets |
|---|---|---|---|
| STINGER | the pro | basic strategy | flat 50 |
| HONEY | hunches | stands on 12 and up, doubles 11, splits aces and eights | 10-50, often Perfect Pairs |
| BUZZ | hothead | basic strategy, doubles every soft hand he can | lets a win ride once; always the HONEY POT |
| QUEENIE | high roller | basic strategy, always insures | 100-500, every side bet |
| DRONE | the mimic | hits to 17 like the dealer, never doubles or splits | flat 20 |

A regular who runs out of chips leaves; the next on the bench sits down
after a few rounds, and the one who left buys back in later. The service
menu can seat no regulars at all (heads-up against the dealer).

## The hint

For the player's own cards against the up card: the expected value of every
legal play, for a six-deck shoe less the cards in sight, with the dealer
known not to have blackjack. On the player's turn with the hint on, the best
play's button glows with BEST PLAY and the bar says what it is worth.

## Presentation

The D-shaped table painted at start-up (brass dealer's edge, padded rail
with a neon inlay, honeycomb felt, "BLACKJACK PAYS 3 TO 2"), the regulars'
animated portraits over the rail, chips flying to and from every spot, the
dealer's peek, split hands spreading along the arc, a doubled card laid
across, result tags, the HONEY POT sign and takeover, celebrations by size
(WINNER, BLACKJACK!, BIG WIN, HONEY POT), the button bar with panel icons,
the lobby, the attract title and demo.
