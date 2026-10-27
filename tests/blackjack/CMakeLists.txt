# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md
# Blackjack tests. Each runs in seconds by default; BJ_SIM_ROUNDS sets the
# length of the return simulation (test_bj_game.c).
bpl_add_test(bj_rules     test_bj_rules.c     bpl_blackjack)
bpl_add_test(bj_strategy  test_bj_strategy.c  bpl_blackjack)
bpl_add_test(bj_game      test_bj_game.c      bpl_blackjack)
