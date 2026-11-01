/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "sf2fx.h"
#include "engine.h"

/* Samples are 16 bit (Q15) in the delay lines: 96 dB is plenty for a reverb tail. Everything lives in internal SRAM
 * (about 16 KB) and runs from the audio interrupt, so no tables in flash and no library calls. */
enum {
    COMB0 = 1116, COMB1 = 1188, COMB2 = 1277, COMB3 = 1356,        /* Freeverb's comb lengths at 44.1 kHz */
    ALLPASS0 = 556, ALLPASS1 = 441, ALLPASS2 = 341, ALLPASS3 = 225, /* left chain 0,1; right chain 2,3 */
    CHORUS_LENGTH = 2048, CHORUS_BASE = 700, CHORUS_DEPTH = 300,    /* 16 ms +- 6.8 ms */
    CHORUS_RATE = 77900,                                            /* 0.8 Hz as a 32-bit phase step at 44.1 kHz */
    REVERB_FEEDBACK = 28800, REVERB_DAMP = 9000,                    /* Q15: 0.88 and 0.27 */
    QUIET_LEVEL = 48, QUIET_BLOCKS = 1100                           /* about 3.2 s of silence at 128 frames a block */
};
static struct {
    int16_t comb0[COMB0], comb1[COMB1], comb2[COMB2], comb3[COMB3];
    int16_t allpass0[ALLPASS0], allpass1[ALLPASS1], allpass2[ALLPASS2], allpass3[ALLPASS3];
    int16_t chorus[CHORUS_LENGTH];
    uint16_t comb_at[4], allpass_at[4], chorus_at;
    int32_t damp[4];
} fx;
static uint32_t chorus_phase, quiet_blocks;
/* Defaults: a little of both. gain = level * 258 in Q15. */
static volatile int32_t reverb_gain = 30 * 258, chorus_gain = 20 * 258;
static volatile unsigned reverb_level = 30, chorus_level = 20;

void sf2fx_set(unsigned reverb, unsigned chorus)
{
    reverb_level = reverb > 127 ? 127 : reverb;
    chorus_level = chorus > 127 ? 127 : chorus;
    reverb_gain = (int32_t)reverb_level * 258;
    chorus_gain = (int32_t)chorus_level * 258;
}
unsigned sf2fx_reverb(void) { return reverb_level; }
unsigned sf2fx_chorus(void) { return chorus_level; }

static FM1_DSP_CODE inline int32_t sat16(int32_t v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : v; }
static FM1_DSP_CODE inline int32_t sat24(int32_t v) { return v > 8388607 ? 8388607 : v < -8388608 ? -8388608 : v; }

#define COMB(n, input) do {                                                                     \
        int32_t y = fx.comb##n[fx.comb_at[n]];                                                   \
        fx.damp[n] += ((y - fx.damp[n]) * REVERB_DAMP + 16384) >> 15;      /* rounded: a floor would leave a DC offset */                                    \
        fx.comb##n[fx.comb_at[n]] = (int16_t)sat16((input) + ((fx.damp[n] * REVERB_FEEDBACK + 16384) >> 15)); \
        if (++fx.comb_at[n] == COMB##n) fx.comb_at[n] = 0;                                       \
        sum += y;                                                                                \
    } while (0)
#define ALLPASS(n, value) do {                                                                  \
        int32_t b = fx.allpass##n[fx.allpass_at[n]], x = (value);                                \
        fx.allpass##n[fx.allpass_at[n]] = (int16_t)sat16(x + ((b + 1) >> 1));                          \
        (value) = b - x;                                                                         \
        if (++fx.allpass_at[n] == ALLPASS##n) fx.allpass_at[n] = 0;                              \
    } while (0)

static FM1_DSP_CODE void clear(void)
{
    int16_t *p = fx.comb0;                               /* the whole struct as 16-bit words: delay lines, positions, filters */
    for (unsigned i = 0; i < sizeof fx / sizeof *p; ++i)
        p[i] = 0;
}

FM1_DSP_CODE void sf2fx_mix(const int32_t *dry, int32_t *out, unsigned frames)
{
    const int32_t rg = reverb_gain, cg = chorus_gain;
    int32_t peak = 0;
    for (unsigned i = 0; i < frames; ++i) {
        int32_t a = dry[i] >> 8;
        peak |= a < 0 ? -a : a;
    }
    if (peak < QUIET_LEVEL) {
        if (quiet_blocks >= QUIET_BLOCKS) {                  /* the tails are gone: dry only, no work */
            if (quiet_blocks == QUIET_BLOCKS) { clear(); ++quiet_blocks; }
            for (unsigned i = 0; i < frames; ++i) {
                out[2 * i] = sat24(out[2 * i] + dry[i]);
                out[2 * i + 1] = sat24(out[2 * i + 1] + dry[i]);
            }
            return;
        }
        ++quiet_blocks;
    } else
        quiet_blocks = 0;
    for (unsigned i = 0; i < frames; ++i) {
        int32_t x = dry[i], x16 = x >> 8, left = x, right = x;
        if (cg) {
            fx.chorus[fx.chorus_at] = (int16_t)x16;
            chorus_phase += CHORUS_RATE;
            int32_t tri = (int32_t)((chorus_phase >> 16) < 32768U ? chorus_phase >> 16 : 65535U - (chorus_phase >> 16));  /* 0..32767 */
            int32_t sweep = ((tri - 16384) * (CHORUS_DEPTH * 256)) >> 14;                                        /* Q8 samples */
            int32_t delay_l = CHORUS_BASE * 256 + sweep, delay_r = CHORUS_BASE * 256 - sweep;
            int32_t tap[2];
            for (unsigned k = 0; k < 2; ++k) {
                int32_t position = ((int32_t)fx.chorus_at << 8) - (k ? delay_r : delay_l);
                unsigned at = (unsigned)(position >> 8) & (CHORUS_LENGTH - 1), next = (at + 1) & (CHORUS_LENGTH - 1);
                int32_t a = fx.chorus[at], b = fx.chorus[next];
                tap[k] = a + (((b - a) * (position & 255)) >> 8);
            }
            left += (tap[0] * cg) >> 7;
            right += (tap[1] * cg) >> 7;
            fx.chorus_at = (uint16_t)((fx.chorus_at + 1) & (CHORUS_LENGTH - 1));
        }
        if (rg) {
            int32_t sum = 0, in = x16 >> 2;
            COMB(0, in); COMB(1, in); COMB(2, in); COMB(3, in);
            int32_t l = sum >> 2, r = l;
            ALLPASS(1, l); ALLPASS(0, l);
            ALLPASS(3, r); ALLPASS(2, r);
            left += (l * rg) >> 4;
            right += (r * rg) >> 4;
        }
        out[2 * i] = sat24(out[2 * i] + left);
        out[2 * i + 1] = sat24(out[2 * i + 1] + right);
    }
}
