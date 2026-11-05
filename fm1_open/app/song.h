/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* The MIDI file player (FM1_MIDIPLAY in fm1b_config.h): a song in RAM, played from the control task. See song.c. */
#ifndef SONG_H
#define SONG_H
#include "fm1b_config.h"
#include <stdint.h>
#if FM1_MIDIPLAY
/* The upload (sf2_upload.c, commands 4..6). Each returns the upload status: 0 done, 1 malformed, 2 too big or outside the
 * song, 4 CRC differs or not a song, 5 no begin first, 6 no memory for it. */
unsigned song_begin(uint32_t bytes);
unsigned song_write(uint32_t offset, const uint8_t *data, uint32_t count);
unsigned song_end(uint32_t crc);
/* Start from the beginning (PLAY, or command 7), stop (PLAY again, command 8, a new upload). */
void song_play(void);
void song_stop(void);
/* Control task, every cycle: send the events that are due. */
void song_poll(void);
/* For the panel. ready: a song is loaded; ms: where it plays (0 while stopped); bytes: what it takes in RAM. */
struct song_state {
    unsigned ready, playing;
    uint32_t ms, length_ms, events, bytes;
    char name[33];
};
void song_state(struct song_state *state);
/* The largest block malloc() gives now, in bytes (worked out by trying, to 1 KB): what a song may take. */
uint32_t song_free_ram(void);
#endif
#endif
