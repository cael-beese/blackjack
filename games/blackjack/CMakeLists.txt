# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md
# bpl_blackjack: the table's rules, side bets, strategy hint, bee regulars and
# the round state machine. Pure C11 + pthreads.
file(GLOB BPL_BJ_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.c)
add_library(bpl_blackjack STATIC ${BPL_BJ_SOURCES})
target_link_libraries(bpl_blackjack PUBLIC bpl_engine Threads::Threads)
