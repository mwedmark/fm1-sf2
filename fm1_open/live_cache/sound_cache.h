/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* A cache of decoded sounds in a fixed pool of RAM (FORMAT.md, "Calculation: a live cache of decoded sounds"). A sound that a note needs and that is not in the pool
 * is decoded into it, a few MP3 frames each time sound_cache_step is called; the sounds that no voice has pinned and that were used longest ago are dropped when
 * the pool has no room. Control task only (the audio interrupt reads the sounds and `groups_ready`, nothing else); the owner of a voice calls sound_cache_unpin
 * from the control task when the voice is over.
 *
 * What a sound is, and where its MP3 is, is the caller's (`struct sound_source`), so that the cache knows nothing of the bank format. A sound can also be one that
 * needs no decoding because it is stored as BFP8 and plays in place (the lossless tier): the source then gives its address and the cache does nothing. */
#ifndef FM1_SOUND_CACHE_H
#define FM1_SOUND_CACHE_H
#include <stdint.h>
#include "mp3_sound.h"

#define SOUND_CACHE_ENTRIES 160U                              /* sounds in the pool at once */
#define SOUND_CACHE_QUEUE 32U                                 /* sounds waiting to be decoded */

enum sound_cache_result {
    SOUND_CACHE_NO_ROOM = -1,                                 /* every sound in the pool is pinned or being decoded, or the sound is bigger than the pool */
    SOUND_CACHE_DIRECT = -2                                   /* the sound plays in place from `direct`; nothing was pinned */
};

struct sound_source {
    void *context;
    uint32_t (*frames)(void *context, uint32_t sound);                              /* the sound's length in samples */
    const uint8_t *(*direct)(void *context, uint32_t sound);                         /* its BFP8 where it plays in place, else 0 */
    void (*begin)(void *context, uint32_t sound, struct mp3_sound_job *job, uint8_t *out);   /* start the job that decodes it into `out` */
    int (*loop)(void *context, uint32_t sound, uint32_t *start, uint32_t *end, uint32_t *xfade);   /* 1 and its loop where it has one that needs the crossfade, else 0 (may be 0) */
};

struct sound_cache_entry {
    uint32_t sound, offset, bytes, frames, last_use;
    volatile uint32_t groups_ready;                           /* BFP8 groups that are in the pool, for the voice that reads the sound while it is decoded */
    uint16_t pins;
    uint8_t state;                                            /* 0 unused, 1 being decoded, 2 ready */
    uint8_t hold_groups;                                      /* the last groups, which the loop crossfade changes, are not ready before the sound is done */
};

struct sound_cache_stats {
    uint32_t hits, misses, evictions, no_room, decoded_mp3_frames;
};

struct sound_cache {
    struct sound_source source;
    uint8_t *pool;
    uint32_t pool_bytes, clock;
    struct sound_cache_entry entry[SOUND_CACHE_ENTRIES];
    uint8_t queue[SOUND_CACHE_QUEUE];
    uint32_t queue_head, queue_count;
    struct mp3_sound_job job;
    int active;                                               /* the entry being decoded, -1 if none */
    struct sound_cache_stats stats;
};

void sound_cache_init(struct sound_cache *cache, const struct sound_source *source, uint8_t *pool, uint32_t pool_bytes);

/* A note needs `sound`. Returns an entry number and pins it (hit, or a new decode started or queued), SOUND_CACHE_DIRECT with the address in *direct, or
 * SOUND_CACHE_NO_ROOM. */
int sound_cache_request(struct sound_cache *cache, uint32_t sound, const uint8_t **direct);

void sound_cache_unpin(struct sound_cache *cache, int entry);
const uint8_t *sound_cache_data(const struct sound_cache *cache, int entry);
uint32_t sound_cache_groups_ready(const struct sound_cache *cache, int entry);
int sound_cache_ready(const struct sound_cache *cache, int entry);

/* Decode for at most `budget` MP3 frames in all, in the order the sounds were asked for. Returns the frames decoded. */
uint32_t sound_cache_step(struct sound_cache *cache, uint32_t budget);

/* Sounds waiting or being decoded. */
uint32_t sound_cache_pending(const struct sound_cache *cache);

#endif
