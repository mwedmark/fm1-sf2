/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "sf2fx.h"
#include "engine.h"
#include "fm1b.h"                                        /* FM1B_RATE: the effects run at the player's rate */

/* Samples are 16 bit (Q15) in the delay lines: 96 dB is plenty for a reverb tail. Everything lives in internal SRAM
 * (about 16 KB) and runs from the audio interrupt, so no tables in flash and no library calls. */
/* Lengths at 44.1 kHz, scaled to the player's rate so the times stay. The damping filter's coefficient changes with the rate:
 * 1 - (1 - 0.2747)^(44100 / 32000) = 0.3577 keeps its cutoff at 32 kHz. */
#define AT_RATE(n) ((int)(((n) * FM1B_RATE + 22050U) / 44100U))
enum {
    COMB0 = AT_RATE(1116U), COMB1 = AT_RATE(1188U), COMB2 = AT_RATE(1277U), COMB3 = AT_RATE(1356U),   /* Freeverb's comb lengths */
    ALLPASS0 = AT_RATE(556U), ALLPASS1 = AT_RATE(441U), ALLPASS2 = AT_RATE(341U), ALLPASS3 = AT_RATE(225U), /* left 0,1; right 2,3 */
    CHORUS_LENGTH = 2048, CHORUS_BASE = AT_RATE(700U), CHORUS_DEPTH = AT_RATE(300U),               /* 16 ms +- 6.8 ms */
    CHORUS_RATE = (int)(77900ULL * 44100U / FM1B_RATE),            /* 0.8 Hz as a 32-bit phase step */
    REVERB_FEEDBACK = 28800, REVERB_DAMP = FM1B_RATE == 44100U ? 9000 : FM1B_RATE == 32000U ? 11722 : 9000,   /* Q15: 0.88; 0.27 at 44.1 kHz */
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
/* Master levels, gain = level * 258 in Q15. They default to full: the channels' sends (fm1b, 30 and 20 by default) set how
 * much there is, so out of the box it sounds as the single global level of 30 and 20 did. */
static volatile int32_t reverb_gain = 127 * 258, chorus_gain = 127 * 258;
static volatile unsigned reverb_level = 127, chorus_level = 127;
static volatile int32_t pan_left = 1024, pan_right = 1024;     /* gains of the dry signal, Q10 */
static volatile unsigned pan_level = 64;

void sf2fx_set(unsigned reverb, unsigned chorus)
{
    reverb_level = reverb > 127 ? 127 : reverb;
    chorus_level = chorus > 127 ? 127 : chorus;
    reverb_gain = (int32_t)reverb_level * 258;
    chorus_gain = (int32_t)chorus_level * 258;
}
static uint32_t isqrt(uint32_t v)
{
    uint32_t r = 0;
    for (uint32_t bit = 1U << 30; bit; bit >>= 2) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
    }
    return r;
}
/* Constant power: gain = sqrt(2 * (1 - x)) for the side turned away from, x being the position 0..1, capped at 1. */
void sf2fx_set_pan(unsigned pan)
{
    pan_level = pan > 127 ? 127 : pan;
    uint32_t x = pan_level < 64 ? pan_level * 256U : pan_level == 127 ? 32768U : 16384U + (pan_level - 64U) * 260U;     /* Q15, 16384 = centre */
    uint32_t right = isqrt((32768U - x) * 64U), left = isqrt(x * 64U);                      /* Q10 of sqrt(2 * (1 - x)) and sqrt(2 * x) */
    pan_left = (int32_t)(pan_level < 64 ? 1024U : right > 1024U ? 1024U : right);
    pan_right = (int32_t)(pan_level > 64 ? 1024U : left > 1024U ? 1024U : left);
}
unsigned sf2fx_pan(void) { return pan_level; }
unsigned sf2fx_reverb(void) { return reverb_level; }
unsigned sf2fx_chorus(void) { return chorus_level; }

static FM1_DSP_CODE inline int32_t sat16(int32_t v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : v; }
/* v (Q23) times a Q10 gain without overflow in 32 bits; a gain of 1024 gives v back exactly */
#define PAN(v, g) ((((v) >> 10) * (g)) + ((((v) & 1023) * (g)) >> 10))
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
    sf2fx_mix_sends(dry, dry, 127, dry, 127, out, frames);
}
FM1_DSP_CODE void sf2fx_mix_sends(const int32_t *dry, const int32_t *rev, unsigned rev_send, const int32_t *cho, unsigned cho_send,
                                  int32_t *out, unsigned frames)
{
    const int32_t rg = reverb_gain * (int32_t)rev_send / 127, cg = chorus_gain * (int32_t)cho_send / 127;   /* send 127: the master level exactly */
    const int32_t pl = pan_left, pr = pan_right;
    int32_t peak = 0;
    for (unsigned i = 0; i < frames; ++i) {
        int32_t a = dry[i] >> 8;
        peak |= a < 0 ? -a : a;
    }
    if (peak < QUIET_LEVEL) {
        if (quiet_blocks >= QUIET_BLOCKS) {                  /* the tails are gone: dry only, no work */
            if (quiet_blocks == QUIET_BLOCKS) { clear(); ++quiet_blocks; }
            for (unsigned i = 0; i < frames; ++i) {
                out[2 * i] = sat24(out[2 * i] + PAN(dry[i], pl));
                out[2 * i + 1] = sat24(out[2 * i + 1] + PAN(dry[i], pr));
            }
            return;
        }
        ++quiet_blocks;
    } else
        quiet_blocks = 0;
    for (unsigned i = 0; i < frames; ++i) {
        int32_t x = dry[i], left = PAN(x, pl), right = PAN(x, pr);
        if (cg) {
            fx.chorus[fx.chorus_at] = (int16_t)(cho[i] >> 8);
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
            int32_t sum = 0, in = (rev[i] >> 8) >> 2;
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
