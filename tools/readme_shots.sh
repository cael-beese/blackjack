#!/bin/bash
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md
#
# readme_shots.sh - the screenshots in README.md, as JPEGs in
# docs/screenshots/. Desktop build under Xvfb (never the cabinet); every
# frame is the real game from a seed and a --script of button presses. The
# scenes that need particular cards (a split, the HONEY POT) stack the shoe
# with BJ_TEST_SHOE, which exists in the desktop build only.
#
#   tools/readme_shots.sh [build-dir]        (default: build)
#
# Run from anywhere; the game finds assets/ from the source root. Needs
# xvfb-run and ImageMagick's convert.
set -euo pipefail
cd "$(dirname "$0")/.."
B=${1:-build}
BIN="$B/platform/beese-blackjack"
T=out/readme
D=docs/screenshots
mkdir -p "$T" "$D"
[ -x "$BIN" ] || { echo "no $BIN - build first" >&2; exit 1; }

run() {    # run <name> <beese-blackjack args...>
    local name=$1; shift
    export XDG_DATA_HOME
    XDG_DATA_HOME=$(mktemp -d)
    xvfb-run -a -s "-screen 0 3440x1440x24" "$BIN" --lockstep "$@" > "$T/$name.log" 2>&1 < /dev/null || true
    rm -rf "$XDG_DATA_HOME"
}

# Attract: the title card, then the demo table.
run attract --mode attract --frames 1460 --shot "300:$T/attract.png,1450:$T/demo.png"
# The lobby, and the CONTROLS page with STAND held.
run lobby --mode menu --frames 260 --script "150:HOLD3;200-250:HOLD2" --shot "100:$T/lobby.png,240:$T/controls.png"
# Betting with every side bet on.
run betting --mode table --seed 5EED --frames 60 --script "10:HOLD1;14:HOLD2;18:HOLD3;22:UP;26:UP" --shot "50:$T/betting.png"
# 8-8 against a 6 with the hint on: split, double both, the dealer busts.
# Deal order: the four regulars and the player, the up card, again, the hole.
export BJ_TEST_SHOE="Ts Th 8s Qd Kd 6h 9c 8c 8d 7c Ks Tc 3c Td 2d 9s 7h"
run split --mode menu --seed 77 --frames 1150 \
    --script "10:HOLD2;30:HOLD1;50:HOLD1;70:DEAL;620:HOLD4;690:HOLD3;760:HOLD3" \
    --shot "600:$T/hint.png,800:$T/split.png,1050:$T/split_won.png"
# Three suited sevens: the HONEY POT.
export BJ_TEST_SHOE="Ts Th 7h Qd Kd 7h 9c 8c 7h 7c Ks Tc"
run honey --mode table --seed 78 --frames 600 --script "20:HOLD3;40:DEAL" --shot "520:$T/honey_pot.png"
unset BJ_TEST_SHOE
# The LEARN PANEL wizard, from the service menu (14 lines down, DEAL).
S=""; for i in $(seq 0 13); do S="$S;$((10 + i * 4)):HOLD2"; done
run learn --mode service --frames 120 --script "${S#;};80:DEAL" --shot "110:$T/learn_panel.png"
# The whole cabinet screen: 3440x1440, the play space 2x with the side art.
run cabinet --size 3440x1440 --mode table --seed 5EED --frames 640 --script "40:DEAL" --shot "620:screen:$T/cabinet.png"

jpg() {    # jpg <source.png> <name> [resize]
    convert "$1" ${3:+-resize "$3"} -strip -quality 86 "$D/$2.jpg"
}
jpg "$T/attract.png" attract
jpg "$T/demo.png" demo
jpg "$T/lobby.png" lobby
jpg "$T/controls.png" controls
jpg "$T/betting.png" betting
jpg "$T/hint.png" hint
jpg "$T/split.png" split
jpg "$T/split_won.png" split-won
jpg "$T/honey_pot.png" honey-pot
jpg "$T/learn_panel.png" learn-panel
jpg "$T/cabinet.png" cabinet 1720x720
ls -la "$D"
