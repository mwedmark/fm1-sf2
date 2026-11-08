/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* For test_sound_cache.js: the real mp3_sound.c and sound_cache.c on WebAssembly, with the packed stream, the sound index and the pool in memory the test fills. */
#include <stdint.h>
#include "sound_cache.h"

#define EXPORT(name) __attribute__((export_name(name)))
enum { MAX_SOUNDS = 1024, STREAM_BYTES = 1 << 21, POOL_BYTES = 1 << 20 };
struct index_entry { uint32_t byte_offset, discard, frames, direct; };   /* direct: offset of BFP8 data in g_direct, or 0 */
static struct index_entry g_index[MAX_SOUNDS];
static uint32_t g_loop[MAX_SOUNDS][3];                   /* loop start, loop end, crossfade; the crossfade 0 where the sound has no loop */
static uint8_t g_stream[STREAM_BYTES], g_pool[POOL_BYTES], g_direct[1 << 16], g_out[1 << 18];
static struct mp3_stream g_mp3 = {g_stream, 0};
static struct sound_cache g_cache;
static struct mp3_sound_job g_job;

EXPORT("stream_ptr") uint32_t stream_ptr(void) { return (uint32_t)(uintptr_t)g_stream; }
EXPORT("index_ptr") uint32_t index_ptr(void) { return (uint32_t)(uintptr_t)g_index; }
EXPORT("pool_ptr") uint32_t pool_ptr(void) { return (uint32_t)(uintptr_t)g_pool; }
EXPORT("direct_ptr") uint32_t direct_ptr(void) { return (uint32_t)(uintptr_t)g_direct; }
EXPORT("loop_ptr") uint32_t loop_ptr(void) { return (uint32_t)(uintptr_t)g_loop; }
EXPORT("out_ptr") uint32_t out_ptr(void) { return (uint32_t)(uintptr_t)g_out; }
EXPORT("set_stream_bytes") void set_stream_bytes(uint32_t n) { g_mp3.bytes = n; }

EXPORT("pack_group") void pack_group(const int16_t *pcm, uint8_t *out) { bfp8_pack_group(pcm, out); }

/* Decode one sound in slices of `step` MP3 frames into g_out; returns the groups written, or -1 on error. */
EXPORT("decode_sound") int32_t decode_sound(uint32_t sound, uint32_t step)
{
    const struct index_entry *e = &g_index[sound];
    mp3_sound_begin(&g_job, &g_mp3, e->byte_offset, e->discard, e->frames, g_out);
    while (!g_job.done && !g_job.error)
        if (!mp3_sound_step(&g_job, step))
            break;
    return g_job.error ? -1 : (int32_t)g_job.groups;
}
EXPORT("last_frames_decoded") uint32_t last_frames_decoded(void) { return g_cache.stats.decoded_mp3_frames; }

static uint32_t src_frames(void *c, uint32_t sound) { (void)c; return g_index[sound].frames; }
static const uint8_t *src_direct(void *c, uint32_t sound) { (void)c; return g_index[sound].direct ? g_direct + g_index[sound].direct : 0; }
static void src_begin(void *c, uint32_t sound, struct mp3_sound_job *job, uint8_t *out)
{
    (void)c;
    mp3_sound_begin(job, &g_mp3, g_index[sound].byte_offset, g_index[sound].discard, g_index[sound].frames, out);
}

static int src_loop(void *c, uint32_t sound, uint32_t *start, uint32_t *end, uint32_t *xfade)
{
    (void)c;
    *start = g_loop[sound][0]; *end = g_loop[sound][1]; *xfade = g_loop[sound][2];
    return *xfade != 0;
}
EXPORT("crossfade") uint32_t crossfade(uint8_t *bfp, uint32_t frames, uint32_t ls, uint32_t le, uint32_t xf) { return mp3_sound_loop_crossfade(bfp, frames, ls, le, xf); }

EXPORT("cache_init") void cache_init(uint32_t pool_bytes)
{
    struct sound_source s = {0, src_frames, src_direct, src_begin, src_loop};
    sound_cache_init(&g_cache, &s, g_pool, pool_bytes);
}
EXPORT("cache_request") int32_t cache_request(uint32_t sound) { const uint8_t *d; return sound_cache_request(&g_cache, sound, &d); }
EXPORT("cache_unpin") void cache_unpin(int32_t e) { sound_cache_unpin(&g_cache, e); }
EXPORT("cache_step") uint32_t cache_step(uint32_t budget) { return sound_cache_step(&g_cache, budget); }
EXPORT("cache_ready") int32_t cache_ready(int32_t e) { return sound_cache_ready(&g_cache, e); }
EXPORT("cache_groups") uint32_t cache_groups(int32_t e) { return sound_cache_groups_ready(&g_cache, e); }
EXPORT("cache_pending") uint32_t cache_pending(void) { return sound_cache_pending(&g_cache); }
EXPORT("cache_data") uint32_t cache_data(int32_t e) { return (uint32_t)(uintptr_t)sound_cache_data(&g_cache, e); }
EXPORT("cache_entry_offset") uint32_t cache_entry_offset(int32_t e) { return g_cache.entry[e].offset; }
EXPORT("cache_entry_bytes") uint32_t cache_entry_bytes(int32_t e) { return g_cache.entry[e].bytes; }
EXPORT("cache_entry_state") uint32_t cache_entry_state(int32_t e) { return g_cache.entry[e].state; }
EXPORT("cache_entry_pins") uint32_t cache_entry_pins(int32_t e) { return g_cache.entry[e].pins; }
EXPORT("cache_entry_sound") uint32_t cache_entry_sound(int32_t e) { return g_cache.entry[e].sound; }
EXPORT("cache_stat") uint32_t cache_stat(uint32_t k)
{
    const struct sound_cache_stats *s = &g_cache.stats;
    return k == 0 ? s->hits : k == 1 ? s->misses : k == 2 ? s->evictions : k == 3 ? s->no_room : s->decoded_mp3_frames;
}
EXPORT("cache_entries") uint32_t cache_entries(void) { return SOUND_CACHE_ENTRIES; }
