/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* The SF2 player's output for one codec block: the voices (fm1b), the effects (sf2fx) and, when the player runs below 44.1 kHz,
 * the resampling to it. */
#ifndef SF2OUT_H
#define SF2OUT_H
#include <stdint.h>
/* Add FM1_BLOCK_FRAMES stereo frames (interleaved, Q23, saturated) at 44.1 kHz to out. Called from the audio interrupt. */
void sf2out_render(int32_t *out);
/* The voices' mix gets 6 dB of makeup gain and a peak limiter (sf2out.c) before the effects: the frames it has held under the
 * makeup gain since start. */
unsigned sf2out_limited(void);
/* The CPU guard: after each block, the audio interrupt's time and the time a block lasts (any unit). Over 80 % the voice limit
 * drops below the voices sounding (the oldest fade out); after a quarter second under 60 % it rises by one, up to FM1B_VOICES. */
void sf2out_load(uint32_t used, uint32_t block);
/* What the panel shows of that load, in percent of a block: the average over about 0.1 s, and the highest block since the
 * last call (the call starts the next peak). */
void sf2out_cpu(unsigned *average, unsigned *peak);
#endif
