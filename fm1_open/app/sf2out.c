/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "sf2out.h"
#include "fm1b.h"
#include "sf2fx.h"
#include "engine.h"
#include <stddef.h>

/* The limiter: the dry mix gets LIMIT_MAKEUP (the zone levels left the mix about 6 dB under where it was) and its peaks are held
 * at LIMIT_CEILING, under full scale so the effects and the pan have room. The gain drops at once on a peak and comes back
 * with a time constant of 2^LIMIT_RELEASE frames (128 ms at 32 kHz). It is one gain for the whole mix, so it scales the
 * buses and the pan's sides alike: the voices keep their balance. */
enum { LIMIT_MAKEUP = 2, LIMIT_CEILING = 5938679 /* -3 dB of Q23 */, LIMIT_RELEASE = 12 };
#define LIMIT_UNITY ((int32_t)LIMIT_MAKEUP << 16)
static int32_t limit_gain = LIMIT_UNITY;              /* Q16 */
static unsigned limit_frames;                         /* frames held under the makeup gain, since start (for the tests) */
static inline __attribute__((always_inline)) int32_t scale(int32_t x, int32_t g) { return (int32_t)(((int64_t)x * g) >> 16); }
static FM1_DSP_CODE void limit(int32_t *dry, int32_t *rev, int32_t *cho, int32_t *left, int32_t *right, unsigned buses, unsigned n)
{
    int32_t g = limit_gain;
    for (unsigned i = 0; i < n; ++i) {
        g += ((LIMIT_UNITY - g) >> LIMIT_RELEASE) + (g < LIMIT_UNITY);   /* + 1: the last 2^RELEASE steps too */
        int32_t x = dry[i], a = x < 0 ? -x : x;
        if ((int64_t)a * g > (int64_t)LIMIT_CEILING << 16)   /* ceiling / a, in Q16 within 32 bits: a is over 2^21 here */
            g = (int32_t)(((uint32_t)LIMIT_CEILING << 8) / ((uint32_t)a >> 8));
        if (g < LIMIT_UNITY - 64) ++limit_frames;
        dry[i] = scale(x, g);
        if (buses & 1U) rev[i] = scale(rev[i], g);
        if (buses & 2U) cho[i] = scale(cho[i], g);
        if (buses & 4U) left[i] = scale(left[i], g);
        if (buses & 8U) right[i] = scale(right[i], g);
    }
    limit_gain = g;
}
unsigned sf2out_limited(void) { return limit_frames; }

#if FM1B_RATE == 44100U
static int32_t melodic[FM1_BLOCK_FRAMES], reverb_bus[FM1_BLOCK_FRAMES], chorus_bus[FM1_BLOCK_FRAMES];
static int32_t cut_left[FM1_BLOCK_FRAMES], cut_right[FM1_BLOCK_FRAMES];
FM1_DSP_CODE void sf2out_render(int32_t *out)
{
    uint8_t sends[2];
    int32_t *sides[2] = {cut_left, cut_right};
    for (unsigned i = 0; i < FM1_BLOCK_FRAMES; ++i) melodic[i] = 0;
    unsigned buses = fm1b_render_sends(NULL, melodic, reverb_bus, chorus_bus, sides, FM1_BLOCK_FRAMES, sends);
    limit(melodic, reverb_bus, chorus_bus, cut_left, cut_right, buses, FM1_BLOCK_FRAMES);
    /* dry on both channels less what the panned voices take from each, plus reverb and chorus: from the dry mix while all
     * channels send alike, else from the buses */
    sf2fx_mix_sends(melodic, buses & 4U ? cut_left : NULL, buses & 8U ? cut_right : NULL,
                    buses & 1U ? reverb_bus : melodic, buses & 1U ? 127U : sends[0],
                    buses & 2U ? chorus_bus : melodic, buses & 2U ? 127U : sends[1], out, FM1_BLOCK_FRAMES);
}
#else
/* The player renders at FM1B_RATE and is resampled to 44.1 kHz here: at 32 kHz a block of 128 output frames takes 92 or 93
 * new player frames. They are kept as 16-bit samples, one array a channel, after the last RESAMPLE_HISTORY frames before
 * them; position is where output frame 0 of the next block sits in those arrays, in frames, Q24. While the player and the
 * history are silent the filter is skipped. The filter is resample_table.inc (make_resampler.js). */
#include "resample_table.inc"
enum { RESAMPLE_HISTORY = RESAMPLE_TAPS, PLAYER_FRAMES = FM1_BLOCK_FRAMES * FM1B_RATE / 44100U + 3U };
#define RESAMPLE_STEP ((uint32_t)((((uint64_t)FM1B_RATE << 24) + 22050U) / 44100U))
static int32_t melodic[PLAYER_FRAMES], reverb_bus[PLAYER_FRAMES], chorus_bus[PLAYER_FRAMES], stereo[PLAYER_FRAMES * 2];
static int32_t cut_left[PLAYER_FRAMES], cut_right[PLAYER_FRAMES];
static int16_t left_in[RESAMPLE_HISTORY + PLAYER_FRAMES], right_in[RESAMPLE_HISTORY + PLAYER_FRAMES];
static uint32_t position = (uint32_t)(RESAMPLE_TAPS / 2 - 1) << 24;
static unsigned silent = RESAMPLE_HISTORY;               /* frames of silence at the end of the arrays (counted up to the history) */
static inline __attribute__((always_inline)) int32_t sat24(int32_t v) { return v > 8388607 ? 8388607 : v < -8388608 ? -8388608 : v; }

FM1_DSP_CODE void sf2out_render(int32_t *out)
{
    const uint32_t last = position + (FM1_BLOCK_FRAMES - 1U) * RESAMPLE_STEP;
    /* the last output frame reads up to sample floor(last) + TAPS / 2, which must be the newest one: HISTORY + n - 1 */
    const unsigned n = (last >> 24) + RESAMPLE_TAPS / 2 + 1U - RESAMPLE_HISTORY;
    uint8_t sends[2];
    int32_t *sides[2] = {cut_left, cut_right};
    for (unsigned i = 0; i < n; ++i) { melodic[i] = 0; stereo[2 * i] = stereo[2 * i + 1] = 0; }
    unsigned buses = fm1b_render_sends(NULL, melodic, reverb_bus, chorus_bus, sides, n, sends);
    limit(melodic, reverb_bus, chorus_bus, cut_left, cut_right, buses, n);
    sf2fx_mix_sends(melodic, buses & 4U ? cut_left : NULL, buses & 8U ? cut_right : NULL,
                    buses & 1U ? reverb_bus : melodic, buses & 1U ? 127U : sends[0],
                    buses & 2U ? chorus_bus : melodic, buses & 2U ? 127U : sends[1], stereo, n);
    int16_t *l = left_in + RESAMPLE_HISTORY, *r = right_in + RESAMPLE_HISTORY;
    for (unsigned i = 0; i < n; ++i) {                   /* Q23 (saturated) to Q15 */
        l[i] = (int16_t)(stereo[2 * i] >> 8);
        r[i] = (int16_t)(stereo[2 * i + 1] >> 8);
        silent = (l[i] | r[i]) ? 0U : silent < RESAMPLE_HISTORY + PLAYER_FRAMES ? silent + 1U : silent;
    }
    uint32_t at = position;
    if (silent < RESAMPLE_HISTORY + n)                   /* something to hear in what this block reads */
        for (unsigned j = 0; j < FM1_BLOCK_FRAMES; ++j, at += RESAMPLE_STEP) {
            const int16_t *c = resample_table[(at >> 16) & (RESAMPLE_PHASES - 1U)];
            const unsigned from = (at >> 24) - (RESAMPLE_TAPS / 2 - 1);
            const int16_t *xl = left_in + from, *xr = right_in + from;
            int32_t sl = 0, sr = 0;
            for (unsigned k = 0; k < RESAMPLE_TAPS; ++k) {    /* Q15 samples times Q14 taps: the sum stays within 31 bits */
                sl += xl[k] * c[k];
                sr += xr[k] * c[k];
            }
            out[2 * j] = sat24(out[2 * j] + (sl >> 6));
            out[2 * j + 1] = sat24(out[2 * j + 1] + (sr >> 6));
        }
    else
        at += FM1_BLOCK_FRAMES * RESAMPLE_STEP;
    position = at - (n << 24);
    for (unsigned i = 0; i < RESAMPLE_HISTORY; ++i) {    /* the newest frames are the next block's history */
        left_in[i] = left_in[n + i];
        right_in[i] = right_in[n + i];
    }
}
#endif

enum { GUARD_HIGH = 80, GUARD_LOW = 60, GUARD_HOLD = 8 /* blocks after a cut: the faded voices' load leaves over 5 ms */,
       GUARD_CALM = 86 /* blocks under GUARD_LOW before a voice comes back: 0.25 s */, GUARD_FLOOR = 4 };
static unsigned guard_limit = FM1B_VOICES, guard_hold, guard_calm;
FM1_DSP_CODE void sf2out_load(uint32_t used, uint32_t block)
{
    uint64_t percent = block ? (uint64_t)used * 100U / block : 0;
    if (guard_hold) {
        --guard_hold;
        return;
    }
    if (percent > GUARD_HIGH) {
        unsigned sounding = fm1b_voices_sounding();
        unsigned limit = (sounding < guard_limit ? sounding : guard_limit) - (percent > 95U ? 2U : 1U);   /* far over: two at once */
        if (sounding > 2U && limit >= GUARD_FLOOR && limit < guard_limit) {
            guard_limit = limit;
            fm1b_voice_limit(limit);
            guard_hold = GUARD_HOLD;
        }
        guard_calm = 0;
    } else if (percent < GUARD_LOW && guard_limit < FM1B_VOICES) {
        if (++guard_calm >= GUARD_CALM) {
            fm1b_voice_limit(++guard_limit);
            guard_calm = 0;
        }
    } else
        guard_calm = 0;
}
