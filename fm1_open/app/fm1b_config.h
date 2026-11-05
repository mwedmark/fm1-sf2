/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef FM1B_CONFIG_H
#define FM1B_CONFIG_H
/* 1: the firmware plays an FM1B bank (the hooks in board/audio.c and midi_router.c, the bank in fm1b_bank.c).
 * It is 0 because the stock image has 345 bytes to spare in the FM1 layout (sdk/Package.cs) and the player needs about
 * 3.6 KB plus a bank, so a build with 1 stops at "Application does not fit the FM1 layout." until something else shrinks. */
#define FM1B_IN_FIRMWARE 1
/* 1: board/audio.c does not call fm1_engine_render() (the stock instrument is unused by the SF2 player). */
#define FM1B_SKIP_ENGINE 1
/* Where the FM1B bank lives in the user flash (offsets start at zero in the firmware's user partition): the stock
 * firmware's sample region, GROOVE_SAMPLE_OFFSET and GROOVE_SAMPLE_BYTES in instrument.h. */
#define FM1B_FLASH_OFFSET 69632U
#define FM1B_FLASH_BYTES 466944U
/* 1: the MIDI file player (song.c): a song converted from a MIDI file by song.html is uploaded over SysEx into RAM and PLAY
 * plays it. 0 leaves out all of it, its code and its RAM (the song itself is allocated only when one is uploaded). */
#ifndef FM1_MIDIPLAY
#define FM1_MIDIPLAY 1
#endif
/* The largest song the player takes, in bytes (the heap is about 215 KB, part of it used by the firmware). */
#define FM1_MIDIPLAY_MAX_BYTES 131072U
#endif
