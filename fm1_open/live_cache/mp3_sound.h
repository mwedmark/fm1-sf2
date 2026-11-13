/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Decode one sound of an MP3-packed sound bank into BFP8, a few MP3 frames at a time (FORMAT.md, "Calculation: a live cache of decoded sounds").
 *
 * The sounds of a bank are laid end to end in one constant-bit-rate mono MP3 stream, encoded with the bit reservoir off (ffmpeg: -reservoir 0), so that
 * every frame decodes on its own. A sound is found from its byte offset (the start of the MP3 frame before the one that holds its first sample: one
 * lead-in frame primes the filter bank) and the number of decoded samples to throw away before it starts (the lead-in frame, the sound's start inside its
 * frame, and the codec's delay). With the reservoir on, the lead-in has to be 4 frames or more (measured: with 2, a quarter of the sounds decode to silence). Decoded 16-bit samples become BFP8 as the
 * converter writes it (packBfp8 in index.html): groups of 32 samples, 33 bytes each, the smallest shift 0..8 that keeps every rounded mantissa
 * in -128..127, the last group zero padded.
 *
 * No heap and no global state: the caller owns the job (5.5 KB for minimp3's mono state) and the output buffer ((frames + 31) / 32 * 33 bytes). */
#ifndef FM1_MP3_SOUND_H
#define FM1_MP3_SOUND_H
#include <stdint.h>
/* build_fw.ps1 -Rate 44100 sets the rate for one build with a header of its own (app/fm1b_rate.h, not committed): #define FM1B_RATE 44100U. RUNNING.md, 2f. */
#if defined(__has_include)
#if __has_include("fm1b_rate.h")
#include "fm1b_rate.h"
#endif
#endif
#if defined(FM1B_RATE) && FM1B_RATE == 44100U
#define MINIMP3_FM1_44K          /* the packed stream is at 44.1 kHz instead (a bank made and packed at that rate) */
#endif
#define MINIMP3_FM1_MONO_32K    /* the packed stream is MPEG-1 layer 3, mono, 32 kHz (pack_bank.js); other frames are refused. Here, not in mp3_sound.c: it sizes mp3dec_t */
#include "minimp3.h"            /* ../third_party/mp3 */

#define MP3_SOUND_FRAME_SAMPLES 1152U

/* The packed stream a sound is in. */
struct mp3_stream {
    const uint8_t *data;
    uint32_t bytes;
};

struct mp3_sound_job {
    mp3dec_t decoder;
    const struct mp3_stream *stream;
    uint32_t position;        /* the next byte of the stream */
    uint32_t discard;         /* decoded samples still to throw away before the sound */
    uint32_t frames;          /* the sound's length, in samples */
    uint32_t taken;           /* samples of the sound taken so far */
    uint8_t *out;             /* BFP8 groups, written as they fill up */
    uint32_t groups;          /* complete groups written (the last, zero padded, counts when the sound is done) */
    int16_t carry[32];
    uint32_t carried;
    uint8_t done, error;
};

/* Number of BFP8 bytes for a sound of `frames` samples. */
static inline uint32_t mp3_sound_bytes(uint32_t frames) { return (frames + 31U) / 32U * 33U; }

/* Pack 32 samples as one BFP8 group (33 bytes): the shift byte, then 32 signed mantissas. */
void bfp8_pack_group(const int16_t *pcm, uint8_t *out);

void mp3_sound_begin(struct mp3_sound_job *job, const struct mp3_stream *stream, uint32_t byte_offset, uint32_t discard, uint32_t frames, uint8_t *out);

/* Decode at most `max_frames` MP3 frames. Returns the number of frames decoded; job->done is set when the whole sound is in `out`, job->error if the stream
 * ended or did not decode (the sound is then as far as it got). */
uint32_t mp3_sound_step(struct mp3_sound_job *job, uint32_t max_frames);


/* The loop of a sound that was MP3 coded does not close: the decoder's output near the loop end does not continue into the loop start. FORMAT.md, "Loops": the last
 * `xfade` samples before the loop end are blended into the `xfade` samples before the loop start (linear, the end side fading out), so the jump from the loop end to
 * the loop start is continuous. Works on a finished sound's BFP8 (`frames` samples long, the loop end at `loop_end` <= frames) and packs the groups it changes
 * again. Needs loop_start >= xfade. Returns the first group that was changed. */
uint32_t mp3_sound_loop_crossfade(uint8_t *bfp, uint32_t frames, uint32_t loop_start, uint32_t loop_end, uint32_t xfade);

#endif
