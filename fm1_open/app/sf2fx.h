/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Reverb and chorus for the SF2 player's mono output. The library's own reverb and delay are internal to its engine, so
 * these are written here: a four-comb, two-allpass reverb (after Freeverb, mono in, stereo out) and a two-tap chorus. */
#ifndef SF2FX_H
#define SF2FX_H
#include <stdint.h>

/* Master levels 0..127 of reverb and chorus, 127 by default: they scale the channels' sends (MIDI CC 91 and CC 93 are per
 * channel, in fm1b). 0 switches the effect off. May be called from any task. */
void sf2fx_set(unsigned reverb, unsigned chorus);
unsigned sf2fx_reverb(void);
unsigned sf2fx_chorus(void);
/* Pan of the dry signal, 0 (left) .. 64 (centre, the default) .. 127 (right), MIDI CC 10. It is a balance: the side you turn
 * away from is faded out and the other keeps its level, so the centre is the unpanned sound. Reverb and chorus stay stereo. */
void sf2fx_set_pan(unsigned pan);
unsigned sf2fx_pan(void);
/* Audio interrupt: add `dry` (mono, Q23) to both channels of `out` (interleaved stereo, Q23, saturating) together with
 * the effects' return. After a few seconds of silence the effects stop costing anything. */
void sf2fx_mix(const int32_t *dry, int32_t *out, unsigned frames);
/* The same with the effects fed from their own sends: rev and cho (mono, Q23; may be dry itself) times rev_send and cho_send
 * (0..127, 127 = all of it), then times the master levels. sf2fx_mix is all three dry at 127. */
void sf2fx_mix_sends(const int32_t *dry, const int32_t *rev, unsigned rev_send, const int32_t *cho, unsigned cho_send,
                     int32_t *out, unsigned frames);
#endif
