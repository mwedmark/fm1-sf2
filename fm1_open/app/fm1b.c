/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "fm1b.h"
#include "fm1_sea.h"
#include "engine.h"
#include <string.h>

enum { CHUNK_BYTES = 228, GROUPS_PER_CHUNK = 16, ENGINE_RATE = 44100, RELEASE_FRAMES = 1500 };

struct voice {
    uint32_t position, fraction, step, age, data, length, loop_start, loop_end;
    int32_t gain;
    const void *owner;
    uint16_t release;
    uint8_t note, active, releasing;
    /* SEA5 zones: the LMS state after group state_group (-1: none), and the two most recent groups by parity. */
    int32_t state_group, id[2], state[8];
    int16_t pcm[2][FM1_SEA_GROUP];
};
static struct {
    const uint8_t *image;
    const int16_t *dqt;
    uint32_t bytes, rate, bits16, sea;
    uint32_t pitch, bankmap, presets, keymaps, zones, preset_count, zone_count;
    int32_t preset; /* -1: none selected */
    uint32_t age;
} bank;
static struct voice voices[FM1B_VOICES];

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | rd16(p + 2) << 16; }
static int region_ok(uint32_t offset, uint64_t size)
{
    return offset <= bank.bytes && size <= bank.bytes - offset;
}

int fm1b_bind(const uint8_t *image, unsigned bytes, const int16_t *dqt)
{
    fm1b_panic();
    bank.image = NULL;
    bank.preset = -1;
    if (!image)
        return 0;
    if (bytes < 48 || memcmp(image, "FM1B", 4) || rd16(image + 4) != 1 || (rd16(image + 6) & ~3U) ||
        rd32(image + 40) != bytes || ((rd16(image + 6) & 2U) && !dqt))
        return -1;
    bank.bytes = bytes;
    bank.dqt = dqt;
    bank.bits16 = rd16(image + 6) & 1U;
    bank.sea = (rd16(image + 6) >> 1) & 1U; /* flag bit 1: zones are SEA5 chunks */
    bank.rate = rd32(image + 8);
    bank.preset_count = rd16(image + 12);
    bank.zone_count = rd16(image + 14);
    bank.pitch = rd32(image + 16);
    bank.bankmap = rd32(image + 20);
    bank.presets = rd32(image + 24);
    bank.keymaps = rd32(image + 28);
    bank.zones = rd32(image + 32);
    if (bank.rate < 4000 || bank.rate > 96000 || !region_ok(bank.pitch, 512) || !region_ok(bank.bankmap, 512) ||
        !region_ok(bank.presets, 24ULL * bank.preset_count) || !region_ok(bank.keymaps, 128ULL * bank.preset_count) ||
        !region_ok(bank.zones, 20ULL * bank.zone_count))
        return -1;
    bank.image = image;
    for (unsigned slot = 0; slot < 256 && bank.preset < 0; ++slot) {
        uint32_t p = rd16(image + bank.bankmap + slot * 2U);
        if (p < bank.preset_count)
            bank.preset = (int32_t)p;
    }
    return 0;
}
int fm1b_program(unsigned program_bank, unsigned program)
{
    if (!bank.image || program > 127 || (program_bank != 0 && program_bank != 128))
        return -1;
    uint32_t p = rd16(bank.image + bank.bankmap + ((program_bank ? 128U : 0U) + program) * 2U);
    if (p >= bank.preset_count)
        return -1;
    bank.preset = (int32_t)p;
    return 0;
}
FM1_DSP_CODE unsigned fm1b_ready(void)
{
    return bank.image && bank.preset >= 0;
}
void fm1b_panic(void)
{
    for (unsigned i = 0; i < FM1B_VOICES; ++i)
        voices[i].active = 0;
}

FM1_DSP_CODE void fm1b_note(const void *owner, unsigned note, unsigned velocity, unsigned level)
{
    if (!fm1b_ready() || note > 127 || velocity > 127 || level > 127)
        return;
    if (!velocity) {
        /* Only looped zones sustain; a one-shot plays to its end like a struck drum. */
        for (unsigned i = 0; i < FM1B_VOICES; ++i)
            if (voices[i].active && voices[i].owner == owner && voices[i].note == note && !voices[i].releasing && voices[i].loop_end) {
                voices[i].releasing = 1;
                voices[i].release = RELEASE_FRAMES;
            }
        return;
    }
    uint32_t key = bank.image[bank.keymaps + (uint32_t)bank.preset * 128U + note];
    const uint8_t *preset = bank.image + bank.presets + (uint32_t)bank.preset * 24U;
    uint32_t first = rd16(preset), count = rd16(preset + 2);
    if (key == 0xff || key >= count || first + key >= bank.zone_count)
        return;
    const uint8_t *zone = bank.image + bank.zones + (first + key) * 20U;
    uint32_t data = rd32(zone), length = rd32(zone + 4), start = rd32(zone + 8), end = rd32(zone + 12);
    uint64_t span = bank.sea ? (uint64_t)((length + 319U) / 320U) * CHUNK_BYTES
                             : (uint64_t)length * (bank.bits16 ? 2U : 1U);
    if (!length || !region_ok(data, span))
        return;
    if (!(zone[17] & 1U) || end <= start || end > length)
        end = start = 0; /* no loop */
    int index = (int)note - (int)zone[16] + 64;
    index = index < 0 ? 0 : index > 127 ? 127 : index;
    uint32_t pitch = rd32(bank.image + bank.pitch + (uint32_t)index * 4U);

    unsigned slot = FM1B_VOICES, oldest = 0;
    for (unsigned i = 0; i < FM1B_VOICES; ++i) {
        if (!voices[i].active) { slot = i; break; }
        if ((uint32_t)(bank.age - voices[i].age) > (uint32_t)(bank.age - voices[oldest].age))
            oldest = i;
    }
    if (slot == FM1B_VOICES)
        slot = oldest;
    struct voice *v = &voices[slot];
    memset(v, 0, sizeof *v);
    v->active = 1;
    v->owner = owner;
    v->note = (uint8_t)note;
    v->age = ++bank.age;
    v->state_group = v->id[0] = v->id[1] = -1;
    v->data = data;
    v->length = length;
    v->loop_start = start;
    v->loop_end = end;
    v->step = (uint32_t)(((uint64_t)pitch * bank.rate + ENGINE_RATE / 2U) / ENGINE_RATE);
    v->gain = (int32_t)((int64_t)velocity * level * 1073741824LL / (127 * 127 * 4));
}

/* Decode SEA5 group g (20 frames) of voice v. The LMS state carries from one group to the next inside a chunk, so
 * a forward step costs one group; anything else (the first read, a loop jump, a chunk change) starts from the
 * chunk's own seed and costs up to one chunk. */
static FM1_DSP_CODE void group_at(struct voice *v, uint32_t g, int16_t *out)
{
    const uint8_t *chunk = bank.image + v->data + (g / GROUPS_PER_CHUNK) * CHUNK_BYTES;
    unsigned first = 0, last = g % GROUPS_PER_CHUNK;
    if (v->state_group >= 0 && (uint32_t)v->state_group < g &&
        (uint32_t)v->state_group / GROUPS_PER_CHUNK == g / GROUPS_PER_CHUNK)
        first = (uint32_t)v->state_group % GROUPS_PER_CHUNK + 1U;
    if (fm1_sea_decode_groups(chunk, CHUNK_BYTES, 5, bank.dqt, first, last, v->state, out)) {
        memset(out, 0, FM1_SEA_GROUP * sizeof out[0]);
        v->state_group = -1;
        return;
    }
    v->state_group = (int32_t)g;
}
static FM1_DSP_CODE int sample_at(struct voice *v, const uint8_t *pcm, uint32_t at)
{
    if (bank.sea) {
        uint32_t g = at / FM1_SEA_GROUP;
        unsigned slot = g & 1U;
        if (v->id[slot] != (int32_t)g) {
            group_at(v, g, v->pcm[slot]);
            v->id[slot] = (int32_t)g;
        }
        return v->pcm[slot][at % FM1_SEA_GROUP];
    }
    return bank.bits16 ? (int16_t)rd16(pcm + at * 2U) : (int)(int8_t)pcm[at] * 256;
}

FM1_DSP_CODE void fm1b_render(const void *owner, int32_t *out, unsigned frames)
{
    if (!bank.image)
        return;
    for (unsigned n = 0; n < FM1B_VOICES; ++n) {
        struct voice *v = &voices[n];
        if (!v->active || v->owner != owner)
            continue;
        const uint8_t *pcm = bank.image + v->data;
        uint32_t length = v->length, ls = v->loop_start, le = v->loop_end;
        for (unsigned i = 0; i < frames; ++i) {
            uint32_t position = v->position;
            if (le && position >= le) {
                position = ls + (position - le) % (le - ls);
                v->position = position;
            }
            if (position >= length) {
                v->active = 0;
                break;
            }
            uint32_t next = position + 1;
            if (le && next >= le)
                next = ls;
            int a = sample_at(v, pcm, position), b = next < length ? sample_at(v, pcm, next) : 0;
            int64_t mixed = (int64_t)a * 65536 + (int64_t)(b - a) * v->fraction;
            int32_t y = (int32_t)((mixed * v->gain) >> 37);
            if (v->releasing) {
                y = (int32_t)((int64_t)y * v->release / RELEASE_FRAMES);
                if (!--v->release)
                    v->active = 0;
            }
            out[i] += y;
            uint32_t phase = v->fraction + v->step;
            v->position += phase >> 16;
            v->fraction = phase & 65535U;
            if (!v->active)
                break;
        }
    }
}
