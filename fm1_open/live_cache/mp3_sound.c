/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* See mp3_sound.h. minimp3 (CC0) is compiled in here, scalar only. Nothing big is on the stack (the FM-1's task stacks are a few KB; with minimp3's
 * 16 KB scratch and the buffers below there, every note that decoded restarted the FM-1, 2026-11-08): minimp3's scratch and the buffers here are static,
 * so one task decodes (the control task, fm1b_cache_service). */
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#define MINIMP3_STATIC_SCRATCH
#include "mp3_sound.h"

void bfp8_pack_group(const int16_t *pcm, uint8_t *out)
{
    unsigned shift = 0;
    for (; shift < 8; ++shift) {                              /* the smallest shift whose rounded mantissas all fit */
        int fits = 1;
        for (unsigned i = 0; i < 32 && fits; ++i) {
            int32_t m = shift ? ((int32_t)pcm[i] + (1 << (shift - 1))) >> shift : pcm[i];   /* round half up, as Math.round does */
            fits = m >= -128 && m <= 127;
        }
        if (fits)
            break;
    }
    out[0] = (uint8_t)shift;
    for (unsigned i = 0; i < 32; ++i) {
        int32_t m = shift ? ((int32_t)pcm[i] + (1 << (shift - 1))) >> shift : pcm[i];
        out[1 + i] = (uint8_t)(m < -128 ? -128 : m > 127 ? 127 : m);
    }
}

void mp3_sound_begin(struct mp3_sound_job *job, const struct mp3_stream *stream, uint32_t byte_offset, uint32_t discard, uint32_t frames, uint8_t *out)
{
    mp3dec_init(&job->decoder);
    job->stream = stream;
    job->position = byte_offset;
    job->discard = discard;
    job->frames = frames;
    job->taken = 0;
    job->out = out;
    job->groups = 0;
    job->carried = 0;
    job->done = frames == 0;
    job->error = 0;
}

static void take(struct mp3_sound_job *job, const int16_t *pcm, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i) {
        job->carry[job->carried++] = pcm[i];
        if (job->carried == 32) {
            bfp8_pack_group(job->carry, job->out + job->groups * 33U);
            ++job->groups;
            job->carried = 0;
        }
    }
    job->taken += n;
}

uint32_t mp3_sound_step(struct mp3_sound_job *job, uint32_t max_frames)
{
    uint32_t decoded = 0;
    while (decoded < max_frames && !job->done && !job->error) {
        static int16_t pcm[MP3_SOUND_FRAME_SAMPLES];
        mp3dec_frame_info_t info;
        if (job->position >= job->stream->bytes) {
            job->error = 1;
            break;
        }
        int samples = mp3dec_decode_frame(&job->decoder, job->stream->data + job->position, (int)(job->stream->bytes - job->position), pcm, &info);
        if (info.frame_bytes <= 0 || info.channels > 1) {
            job->error = 1;
            break;
        }
        job->position += (uint32_t)info.frame_bytes;
        ++decoded;
        if (samples <= 0) {                                   /* a frame that needs the bit reservoir of one not decoded: the lead-in, silence */
            for (uint32_t i = 0; i < MP3_SOUND_FRAME_SAMPLES; ++i)
                pcm[i] = 0;
            samples = (int)MP3_SOUND_FRAME_SAMPLES;
        }
        uint32_t from = 0, n = (uint32_t)samples;
        if (job->discard) {
            from = job->discard < n ? job->discard : n;
            job->discard -= from;
        }
        n -= from;
        if (n > job->frames - job->taken)
            n = job->frames - job->taken;
        if (n)
            take(job, pcm + from, n);
        if (job->taken == job->frames) {
            if (job->carried) {                                /* the last group, padded with zeros */
                for (uint32_t i = job->carried; i < 32; ++i)
                    job->carry[i] = 0;
                bfp8_pack_group(job->carry, job->out + job->groups * 33U);
                ++job->groups;
                job->carried = 0;
            }
            job->done = 1;
        }
    }
    return decoded;
}

static int32_t bfp_sample(const uint8_t *bfp, uint32_t i)
{
    const uint8_t *g = bfp + (i >> 5) * 33U;
    return (int32_t)(int8_t)g[1 + (i & 31U)] * (1 << g[0]);
}

static int32_t floor_div(int32_t n, int32_t d) { return n >= 0 ? n / d : -((-n + d - 1) / d); }

uint32_t mp3_sound_loop_crossfade(uint8_t *bfp, uint32_t frames, uint32_t loop_start, uint32_t loop_end, uint32_t xfade)
{
    if (loop_end > frames)
        loop_end = frames;
    if (xfade > loop_start)
        xfade = loop_start;
    if (xfade > (loop_end - loop_start) / 2)
        xfade = (loop_end - loop_start) / 2;
    if (!xfade)
        return frames / 32U;
    static int32_t blended[1024];
    if (xfade > 1024)
        xfade = 1024;
    for (uint32_t i = 0; i < xfade; ++i) {                    /* w = (i + 1) / (xfade + 1), computed on the samples as they are before any of them changes */
        int32_t end = bfp_sample(bfp, loop_end - xfade + i), before = bfp_sample(bfp, loop_start - xfade + i);
        int32_t n = end * (int32_t)(xfade - i) + before * (int32_t)(i + 1), d = (int32_t)xfade + 1;
        blended[i] = floor_div(n + d / 2, d);
    }
    uint32_t first = (loop_end - xfade) >> 5, last = (loop_end - 1U) >> 5;
    for (uint32_t g = first; g <= last; ++g) {
        int16_t pcm[32];
        for (uint32_t k = 0; k < 32; ++k) {
            uint32_t i = g * 32U + k;
            int32_t v = bfp_sample(bfp, i);
            if (i >= loop_end - xfade && i < loop_end)
                v = blended[i - (loop_end - xfade)];
            pcm[k] = (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
        }
        bfp8_pack_group(pcm, bfp + g * 33U);
    }
    return first;
}
