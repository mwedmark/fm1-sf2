/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* A whole MP3 stream to 16-bit samples with the firmware's own decoder (minimp3, set up as in fm1_open/live_cache/mp3_sound.c), for the
 * converter page (index.html), which packs MP3 banks with pack_bank.js. Built to WebAssembly by make_mp3_decoder.js. */
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#define MINIMP3_STATIC_SCRATCH
#include "minimp3.h"

#define EXPORT(name) __attribute__((export_name(name)))

static mp3dec_t dec;

/* The stream at in (bytes long) into out (at most out_max samples, room for a frame more): the number of samples. Frames are decoded in
 * order from byte 0, as ffmpeg does with a file; a stream that stops in the middle of a frame ends there. */
EXPORT("decode") unsigned decode(const uint8_t *in, unsigned bytes, int16_t *out, unsigned out_max)
{
    mp3dec_frame_info_t info;
    unsigned at = 0, n = 0;
    mp3dec_init(&dec);
    while (at < bytes && n + MINIMP3_MAX_SAMPLES_PER_FRAME <= out_max) {
        int got = mp3dec_decode_frame(&dec, in + at, (int)(bytes - at), out + n, &info);
        if (!info.frame_bytes)
            break;
        at += (unsigned)info.frame_bytes;
        n += (unsigned)got * (unsigned)info.channels;
    }
    return n;
}
