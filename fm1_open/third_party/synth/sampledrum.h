/* SPDX-License-Identifier: MIT */
#ifndef FM1_WEB_SAMPLEDRUM_H
#define FM1_WEB_SAMPLEDRUM_H
#include "engine.h"
#include "../../app/synth/sampledrum.h"

/* Track-local voices and decoded blocks; immutable sample data is shared. */
typedef struct sampledrum sampledrum_t;
/* Caller-owned, 32-byte-aligned storage; query its size with sampledrum_size().
 * Initialize before use. Live calls belong to one audio owner or a held gate. */
size_t sampledrum_size(void);

void sampledrum_init(sampledrum_t *s);
FM1_DSP_ENTRY void sampledrum_trigger(sampledrum_t *s, unsigned pad, unsigned velocity);
FM1_DSP_ENTRY void sampledrum_render(sampledrum_t *s, int32_t *out, unsigned frames);
FM1_DSP_ENTRY void sampledrum_panic(sampledrum_t *s);
/* Parameter edits apply on the next hit; tune is -24..+24 semitones, encoded
 * as0..48. Length0..127 selects1/128..128/128 of the sample. Choke0 is off;
 * 1..4 are groups. Variation0..3 sets four-hit cyclic pitch/gain/start changes;
 * velocity LPF0..127 sets how much softer hits darken. Both are bypassable. */
unsigned sampledrum_get(const sampledrum_t *s, unsigned pad, unsigned parameter);
void sampledrum_set(sampledrum_t *s, unsigned pad, unsigned parameter, unsigned value);
FM1_DSP_CODE unsigned sampledrum_note(unsigned pad);
FM1_DSP_CODE int sampledrum_index(unsigned note);
const char *sampledrum_name(unsigned pad);
const char *sampledrum_label(unsigned pad);
/* sampledrum_bind() borrows storage until unbound; pause rendering before
 * replacing it. NULL clears a slot. SEA5 uses 228 bytes per 320 frames. */
/* Factory bank for browser initialization; never linked into device firmware. */
const uint8_t *sampledrum_factory(unsigned *bytes);
/* Offline encoder, never the audio thread. Caller frees the result. */
uint8_t *sampledrum_encode(const int16_t *pcm, unsigned frames, unsigned *bytes);
/* Audio owner only. 1..24; choked/old voices retire first. Returns retired count. */
FM1_DSP_CODE unsigned sampledrum_limit(sampledrum_t *synth, unsigned voices);
FM1_DSP_CODE unsigned sampledrum_active(const sampledrum_t *synth);

/* Melodic FM1B bank (fm-sf2/FORMAT.md): key-mapped zones of raw PCM with loops.
 * bind() borrows the image until the next bind; NULL unbinds. Returns 0 or -1. */
int sampledrum_melodic_bind(const uint8_t *image, unsigned bytes);
int sampledrum_melodic_program(unsigned bank, unsigned program);
FM1_DSP_CODE unsigned sampledrum_melodic_ready(void);
/* velocity 0 releases the note. */
FM1_DSP_CODE void sampledrum_play(sampledrum_t *s, unsigned note, unsigned velocity);
#endif
