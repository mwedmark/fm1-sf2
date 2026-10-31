/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef FM1B_CONFIG_H
#define FM1B_CONFIG_H
/* 1: the firmware plays an FM1B bank (the hooks in board/audio.c and midi_router.c, the bank in fm1b_bank.c).
 * It is 0 because the stock image has 345 bytes to spare in the FM1 layout (sdk/Package.cs) and the player needs about
 * 3.6 KB plus a bank, so a build with 1 stops at "Application does not fit the FM1 layout." until something else shrinks. */
#define FM1B_IN_FIRMWARE 1
/* 1: board/audio.c does not call fm1_engine_render() (the stock instrument is unused by the SF2 player). */
#define FM1B_SKIP_ENGINE 1
#endif
