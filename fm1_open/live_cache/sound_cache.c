/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* See sound_cache.h. The pool is allocated best-fit among the gaps between the sounds in it; when no gap is big enough, the sound least recently used that is not
 * pinned (and not being decoded) goes, and the search is made again. Sounds do not move, so a voice's pointer stays good while it holds its pin. */
#include "sound_cache.h"

static uint32_t align4(uint32_t n) { return (n + 3U) & ~3U; }

void sound_cache_init(struct sound_cache *c, const struct sound_source *source, uint8_t *pool, uint32_t pool_bytes)
{
    c->source = *source;
    c->pool = pool;
    c->pool_bytes = pool_bytes;
    c->clock = 0;
    for (unsigned i = 0; i < SOUND_CACHE_ENTRIES; ++i)
        c->entry[i].state = 0;
    c->queue_head = c->queue_count = 0;
    c->active = -1;
    c->stats = (struct sound_cache_stats){0, 0, 0, 0, 0};
}

/* The offset of the smallest gap that holds `bytes`, or -1. */
static int64_t find_gap(const struct sound_cache *c, uint32_t bytes)
{
    uint8_t order[SOUND_CACHE_ENTRIES];
    unsigned n = 0;
    for (unsigned i = 0; i < SOUND_CACHE_ENTRIES; ++i) {
        if (!c->entry[i].state)
            continue;
        unsigned k = n++;
        while (k && c->entry[order[k - 1]].offset > c->entry[i].offset) { order[k] = order[k - 1]; --k; }
        order[k] = (uint8_t)i;
    }
    int64_t best = -1;
    uint32_t best_size = 0xffffffffU, cursor = 0;
    for (unsigned k = 0; k <= n; ++k) {
        uint32_t end = k < n ? c->entry[order[k]].offset : c->pool_bytes;
        if (end - cursor >= bytes && end - cursor < best_size) {
            best = cursor;
            best_size = end - cursor;
        }
        if (k < n)
            cursor = c->entry[order[k]].offset + c->entry[order[k]].bytes;
    }
    return best;
}

static int evict_one(struct sound_cache *c)
{
    int victim = -1;
    for (unsigned i = 0; i < SOUND_CACHE_ENTRIES; ++i)
        if (c->entry[i].state == 2 && c->entry[i].pins == 0 && (victim < 0 || c->entry[i].last_use < c->entry[victim].last_use))
            victim = (int)i;
    if (victim < 0)
        return 0;
    c->entry[victim].state = 0;
    ++c->stats.evictions;
    return 1;
}

int sound_cache_request(struct sound_cache *c, uint32_t sound, const uint8_t **direct)
{
    const uint8_t *place = c->source.direct ? c->source.direct(c->source.context, sound) : 0;
    if (place) {
        if (direct)
            *direct = place;
        return SOUND_CACHE_DIRECT;
    }
    for (unsigned i = 0; i < SOUND_CACHE_ENTRIES; ++i)
        if (c->entry[i].state && c->entry[i].sound == sound) {
            ++c->entry[i].pins;
            c->entry[i].last_use = ++c->clock;
            ++c->stats.hits;
            return (int)i;
        }
    uint32_t frames = c->source.frames(c->source.context, sound), bytes = align4(mp3_sound_bytes(frames));
    int slot = -1;
    for (unsigned i = 0; i < SOUND_CACHE_ENTRIES && slot < 0; ++i)
        if (!c->entry[i].state)
            slot = (int)i;
    if (bytes > c->pool_bytes || c->queue_count == SOUND_CACHE_QUEUE) {
        ++c->stats.no_room;
        return SOUND_CACHE_NO_ROOM;
    }
    int64_t at = find_gap(c, bytes);
    while ((at < 0 || slot < 0) && evict_one(c)) {
        if (slot < 0)
            for (unsigned i = 0; i < SOUND_CACHE_ENTRIES && slot < 0; ++i)
                if (!c->entry[i].state)
                    slot = (int)i;
        at = find_gap(c, bytes);
    }
    if (at < 0 || slot < 0) {
        ++c->stats.no_room;
        return SOUND_CACHE_NO_ROOM;
    }
    struct sound_cache_entry *e = &c->entry[slot];
    e->sound = sound;
    e->offset = (uint32_t)at;
    e->bytes = bytes;
    e->frames = frames;
    e->groups_ready = 0;
    e->pins = 1;
    e->hold_groups = 0;
    {
        uint32_t ls, le, xf;
        if (c->source.loop && c->source.loop(c->source.context, sound, &ls, &le, &xf) && xf)
            e->hold_groups = (uint8_t)(((xf > 1024 ? 1024 : xf) + 31U) / 32U + 1U);
    }
    e->last_use = ++c->clock;
    e->state = 1;
    c->queue[(c->queue_head + c->queue_count++) % SOUND_CACHE_QUEUE] = (uint8_t)slot;
    ++c->stats.misses;
    return slot;
}

void sound_cache_unpin(struct sound_cache *c, int entry)
{
    if (entry >= 0 && c->entry[entry].pins)
        --c->entry[entry].pins;
}

const uint8_t *sound_cache_data(const struct sound_cache *c, int entry) { return c->pool + c->entry[entry].offset; }
uint32_t sound_cache_groups_ready(const struct sound_cache *c, int entry) { return c->entry[entry].groups_ready; }
int sound_cache_ready(const struct sound_cache *c, int entry) { return c->entry[entry].state == 2; }
uint32_t sound_cache_pending(const struct sound_cache *c) { return c->queue_count + (c->active >= 0 ? 1U : 0U); }

uint32_t sound_cache_step(struct sound_cache *c, uint32_t budget)
{
    uint32_t used = 0;
    while (used < budget) {
        if (c->active < 0) {
            if (!c->queue_count)
                break;
            c->active = c->queue[c->queue_head];
            c->queue_head = (c->queue_head + 1U) % SOUND_CACHE_QUEUE;
            --c->queue_count;
            struct sound_cache_entry *e = &c->entry[c->active];
            c->source.begin(c->source.context, e->sound, &c->job, c->pool + e->offset);
        }
        struct sound_cache_entry *e = &c->entry[c->active];
        uint32_t n = mp3_sound_step(&c->job, budget - used);
        used += n;
        c->stats.decoded_mp3_frames += n;
        if (c->job.done || c->job.error) {
            uint32_t ls, le, xf;
            if (c->job.done && e->hold_groups && c->source.loop(c->source.context, e->sound, &ls, &le, &xf))
                mp3_sound_loop_crossfade(c->pool + e->offset, e->frames, ls, le, xf);
            e->groups_ready = c->job.groups;
        } else
            e->groups_ready = c->job.groups > e->hold_groups ? c->job.groups - e->hold_groups : 0;
        if (c->job.done || c->job.error) {
            e->state = 2;                                      /* a sound that did not decode in full is as far as it got; its groups_ready says */
            c->active = -1;
        } else if (!n)
            break;
    }
    return used;
}
