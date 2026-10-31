/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "voices.h"
#include "fixed.h"
#include "fm1_sea.h"
#include <string.h>
#include <stddef.h>


enum { CHUNK_BYTES = 228, FADE_FRAMES = 32 };
struct sample_asset {
    const uint8_t *data;
    uint32_t frames, bytes;
    const char *name;
};
static struct sample_asset sample_assets[SAMPLE_DRUM_PADS];
static char sample_short_names[SAMPLE_DRUM_PADS][6];
#include "sample_metadata.inc"
#include "sea_encode.h"
#include "demo_bank.h"
const uint8_t *sampledrum_factory(unsigned *bytes)
{
    return demo_bank(bytes);
}
uint8_t *sampledrum_encode(const int16_t *pcm, unsigned frames, unsigned *bytes)
{
    if (!pcm || !frames || frames > SAMPLE_DRUM_MAX_FRAMES || !bytes) return NULL;
    uint8_t *data = fm1_sea_encode(pcm, frames, sample_dqt);
    *bytes = data ? ((frames + 319U) / 320U) * CHUNK_BYTES : 0;
    return data;
}
int sampledrum_bind(unsigned pad, const uint8_t *data, unsigned bytes, unsigned frames,
                    const char *name)
{
    if (pad >= SAMPLE_DRUM_PADS || (data && (!frames || frames > SAMPLE_DRUM_MAX_FRAMES ||
        bytes != ((frames + 319U) / 320U) * CHUNK_BYTES || !name))) return -1;
    sample_assets[pad].data = data;
    sample_assets[pad].frames = data ? frames : 0;
    sample_assets[pad].bytes = data ? bytes : 0;
    sample_assets[pad].name = data ? name : NULL;
    const char *label = data && name && name[0] ? name : "EMPTY";
    unsigned n = 0;
    for (; n < 5 && label[n]; ++n)
        sample_short_names[pad][n] = label[n] >= 'a' && label[n] <= 'z' ? (char)(label[n] - 32) : label[n];
    sample_short_names[pad][n] = 0;
    return 0;
}

static const fm1_param_t parameters[SAMPLE_DRUM_PARAMS] = {
    {"Level", 127, 100, 0}, {"Tune", 48, 24, 0},    {"Length", 127, 127, 0},
    {"Choke", 4, 0, 0},     {"Variation", 3, 1, 0}, {"Vel LPF", 127, 96, 0}};
/* round(32000/44100 * 2^((pot-24)/12) *65536). Fixed ratio preserves the
 * source pitch; its maximum step is below3, so reads only move forward. */
static FM1_DSP_DATA const uint32_t increments[49] = {
    11889,  12596,  13345,  14138,  14979,  15869,  16813,  17813,  18872,  19994,
    21183,  22443,  23777,  25191,  26689,  28276,  29957,  31739,  33626,  35626,
    37744,  39988,  42366,  44885,  47554,  50382,  53378,  56552,  59915,  63478,
    67252,  71251,  75488,  79977,  84732,  89771,  95109,  100764, 106756, 113104,
    119830, 126955, 134504, 142502, 150976, 159954, 169465, 179542, 190218};
const fm1_param_t *sampledrum_param(unsigned p)
{
    return p < SAMPLE_DRUM_PARAMS ? &parameters[p] : NULL;
}
unsigned sampledrum_get(const sampledrum_t *s, unsigned pad, unsigned p)
{
    return s && pad < SAMPLE_DRUM_PADS && p < SAMPLE_DRUM_PARAMS ? s->pot[pad][p] : 0;
}
void sampledrum_set(sampledrum_t *s, unsigned pad, unsigned p, unsigned value)
{
    if (!s || pad >= SAMPLE_DRUM_PADS || p >= SAMPLE_DRUM_PARAMS)
        return;
    if (value > parameters[p].max)
        value = parameters[p].max;
    s->pot[pad][p] = (uint8_t)value;
}
void sampledrum_init(sampledrum_t *s)
{
    memset(s, 0, sizeof *s);
    s->limit = SAMPLE_DRUM_VOICES;
    for (unsigned pad = 0; pad < SAMPLE_DRUM_PADS; ++pad) {
        for (unsigned p = 0; p < SAMPLE_DRUM_PARAMS; ++p)
            s->pot[pad][p] = parameters[p].def;
        if (pad == 6 || pad == 7 || pad == 12) /* Closed, open and pedal hi-hat. */
            s->pot[pad][SAMPLE_DRUM_CHOKE] = 1;
    }
}
FM1_DSP_CODE unsigned sampledrum_limit(sampledrum_t *s, unsigned limit)
{
    if (!limit || limit > SAMPLE_DRUM_VOICES) return 0;
    s->limit = limit;
    unsigned active = 0, retired = 0;
    for (unsigned i = 0; i < SAMPLE_DRUM_VOICES; ++i) active += s->voice[i].active != 0;
    while (active > limit) {
        unsigned slot = SAMPLE_DRUM_VOICES;
        for (unsigned i = 0; i < SAMPLE_DRUM_VOICES; ++i) {
            if (!s->voice[i].active) continue;
            if (slot == SAMPLE_DRUM_VOICES || s->voice[i].stopping > s->voice[slot].stopping ||
                (s->voice[i].stopping == s->voice[slot].stopping &&
                 (uint32_t)(s->age - s->voice[i].age) > (uint32_t)(s->age - s->voice[slot].age))) slot = i;
        }
        s->voice[slot].active = 0;
        --active; ++retired;
    }
    return retired;
}
FM1_DSP_ENTRY void sampledrum_panic(sampledrum_t *s)
{
    for (unsigned i = 0; i < SAMPLE_DRUM_VOICES; ++i)
        s->voice[i].active = 0;
    for (unsigned i = 0; i < SAMPLE_DRUM_MELODIC_VOICES; ++i)
        s->mvoice[i].active = 0;
}

/* ---- Melodic FM1B bank (fm1-sf2/FORMAT.md) -------------------------------------------------
 * A lookup is four indexed loads: bank map -> preset -> key map -> zone. Zones hold raw signed
 * PCM at the bank's own rate, played with linear interpolation, looped until note-off. */
enum { MELODIC_RELEASE = 1500, MELODIC_ENGINE_RATE = 44100 };
static struct {
    const uint8_t *image;
    uint32_t bytes, rate, bits16, sea;
    uint32_t pitch, bankmap, presets, keymaps, zones, preset_count, zone_count;
    int32_t preset; /* -1: none selected */
} fm1b;
static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | rd16(p + 2) << 16; }
static int region_ok(uint32_t offset, uint64_t size)
{
    return offset <= fm1b.bytes && size <= fm1b.bytes - offset;
}
int sampledrum_melodic_bind(const uint8_t *image, unsigned bytes)
{
    fm1b.image = NULL;
    fm1b.preset = -1;
    if (!image)
        return 0;
    if (bytes < 48 || memcmp(image, "FM1B", 4) || rd16(image + 4) != 1 || (rd16(image + 6) & ~3U) ||
        rd32(image + 40) != bytes)
        return -1;
    fm1b.bytes = bytes;
    fm1b.bits16 = rd16(image + 6) & 1U;
    fm1b.sea = (rd16(image + 6) >> 1) & 1U; /* flag bit 1: zones are SEA5 chunks */
    fm1b.rate = rd32(image + 8);
    fm1b.preset_count = rd16(image + 12);
    fm1b.zone_count = rd16(image + 14);
    fm1b.pitch = rd32(image + 16);
    fm1b.bankmap = rd32(image + 20);
    fm1b.presets = rd32(image + 24);
    fm1b.keymaps = rd32(image + 28);
    fm1b.zones = rd32(image + 32);
    if (fm1b.rate < 4000 || fm1b.rate > 96000 || !region_ok(fm1b.pitch, 512) || !region_ok(fm1b.bankmap, 512) ||
        !region_ok(fm1b.presets, 24ULL * fm1b.preset_count) || !region_ok(fm1b.keymaps, 128ULL * fm1b.preset_count) ||
        !region_ok(fm1b.zones, 20ULL * fm1b.zone_count))
        return -1;
    fm1b.image = image;
    for (unsigned slot = 0; slot < 256 && fm1b.preset < 0; ++slot) {
        uint32_t p = rd16(image + fm1b.bankmap + slot * 2U);
        if (p < fm1b.preset_count)
            fm1b.preset = (int32_t)p;
    }
    return 0;
}
int sampledrum_melodic_program(unsigned bank, unsigned program)
{
    if (!fm1b.image || program > 127 || (bank != 0 && bank != 128))
        return -1;
    uint32_t p = rd16(fm1b.image + fm1b.bankmap + ((bank ? 128U : 0U) + program) * 2U);
    if (p >= fm1b.preset_count)
        return -1;
    fm1b.preset = (int32_t)p;
    return 0;
}
FM1_DSP_CODE unsigned sampledrum_melodic_ready(void)
{
    return fm1b.image && fm1b.preset >= 0;
}
FM1_DSP_CODE void sampledrum_play(sampledrum_t *s, unsigned note, unsigned velocity)
{
    if (!s || !sampledrum_melodic_ready() || note > 127 || velocity > 127)
        return;
    if (!velocity) {
        /* Only looped zones sustain; a one-shot plays to its end like a struck drum. */
        for (unsigned i = 0; i < SAMPLE_DRUM_MELODIC_VOICES; ++i)
            if (s->mvoice[i].active && s->mvoice[i].note == note && !s->mvoice[i].releasing &&
                s->mvoice[i].loop_end) {
                s->mvoice[i].releasing = 1;
                s->mvoice[i].release = MELODIC_RELEASE;
            }
        return;
    }
    uint32_t key = fm1b.image[fm1b.keymaps + (uint32_t)fm1b.preset * 128U + note];
    const uint8_t *preset = fm1b.image + fm1b.presets + (uint32_t)fm1b.preset * 24U;
    uint32_t first = rd16(preset), count = rd16(preset + 2);
    if (key == 0xff || key >= count || first + key >= fm1b.zone_count)
        return;
    const uint8_t *zone = fm1b.image + fm1b.zones + (first + key) * 20U;
    uint32_t data = rd32(zone), length = rd32(zone + 4), start = rd32(zone + 8), end = rd32(zone + 12);
    unsigned unit = fm1b.bits16 ? 2U : 1U;
    uint64_t span = fm1b.sea ? (uint64_t)((length + 319U) / 320U) * CHUNK_BYTES : (uint64_t)length * unit;
    if (!length || !region_ok(data, span))
        return;
    if (!(zone[17] & 1U) || end <= start || end > length)
        end = start = 0; /* no loop */
    int index = (int)note - (int)zone[16] + 64;
    index = index < 0 ? 0 : index > 127 ? 127 : index;
    uint32_t pitch = rd32(fm1b.image + fm1b.pitch + (uint32_t)index * 4U);

    unsigned slot = SAMPLE_DRUM_MELODIC_VOICES, oldest = 0;
    for (unsigned i = 0; i < SAMPLE_DRUM_MELODIC_VOICES; ++i) {
        if (!s->mvoice[i].active) { slot = i; break; }
        if ((uint32_t)(s->mage - s->mvoice[i].age) > (uint32_t)(s->mage - s->mvoice[oldest].age))
            oldest = i;
    }
    if (slot == SAMPLE_DRUM_MELODIC_VOICES)
        slot = oldest;
    memset(&s->mvoice[slot], 0, sizeof s->mvoice[slot]);
    s->mvoice[slot].active = 1;
    s->mvoice[slot].note = (uint8_t)note;
    s->mvoice[slot].age = ++s->mage;
    s->mvoice[slot].cache_index[0] = s->mvoice[slot].cache_index[1] = -1;
    s->mvoice[slot].data = data;
    s->mvoice[slot].length = length;
    s->mvoice[slot].loop_start = start;
    s->mvoice[slot].loop_end = end;
    s->mvoice[slot].step = (uint32_t)(((uint64_t)pitch * fm1b.rate + MELODIC_ENGINE_RATE / 2U) / MELODIC_ENGINE_RATE);
    s->mvoice[slot].gain = (int32_t)((int64_t)velocity * s->pot[0][SAMPLE_DRUM_LEVEL] * FM_Q30_ONE / (127 * 127 * 4));
}
/* A SEA5 chunk is self-contained (it carries its own LMS seed), so a read anywhere, including a
 * loop jump, decodes the one chunk it lands in. Chunks go in the cache slot of their parity, so
 * the chunk being read and the next one never evict each other. */
static FM1_DSP_CODE int melodic_at(sampledrum_t *s, unsigned v, const uint8_t *pcm, uint32_t at)
{
    if (fm1b.sea) {
        int32_t chunk = (int32_t)(at / 320U);
        unsigned slot = (unsigned)chunk & 1U;
        if (s->mvoice[v].cache_index[slot] != chunk) {
            if (fm1_sea_decode(pcm + (uint32_t)chunk * CHUNK_BYTES, CHUNK_BYTES, 5, sample_dqt,
                               s->mvoice[v].cache[slot]))
                memset(s->mvoice[v].cache[slot], 0, sizeof s->mvoice[v].cache[slot]);
            s->mvoice[v].cache_index[slot] = chunk;
        }
        return s->mvoice[v].cache[slot][at % 320U];
    }
    return fm1b.bits16 ? (int16_t)rd16(pcm + at * 2U) : (int)(int8_t)pcm[at] * 256;
}
static FM1_DSP_CODE void melodic_render(sampledrum_t *s, int32_t *out, unsigned frames)
{
    if (!fm1b.image)
        return;
    for (unsigned v = 0; v < SAMPLE_DRUM_MELODIC_VOICES; ++v) {
        if (!s->mvoice[v].active)
            continue;
        const uint8_t *pcm = fm1b.image + s->mvoice[v].data;
        uint32_t length = s->mvoice[v].length, ls = s->mvoice[v].loop_start, le = s->mvoice[v].loop_end;
        for (unsigned i = 0; i < frames; ++i) {
            uint32_t position = s->mvoice[v].position;
            if (le && position >= le) {
                position = ls + (position - le) % (le - ls);
                s->mvoice[v].position = position;
            }
            if (position >= length) {
                s->mvoice[v].active = 0;
                break;
            }
            uint32_t next = position + 1;
            if (le && next >= le)
                next = ls;
            int a = melodic_at(s, v, pcm, position), b = next < length ? melodic_at(s, v, pcm, next) : 0;
            int64_t mixed = (int64_t)a * 65536 + (int64_t)(b - a) * s->mvoice[v].fraction;
            int32_t y = (int32_t)((mixed * s->mvoice[v].gain) >> 37);
            if (s->mvoice[v].releasing) {
                y = (int32_t)((int64_t)y * s->mvoice[v].release / MELODIC_RELEASE);
                if (!--s->mvoice[v].release)
                    s->mvoice[v].active = 0;
            }
            out[i] += y;
            uint32_t phase = s->mvoice[v].fraction + s->mvoice[v].step;
            s->mvoice[v].position += phase >> 16;
            s->mvoice[v].fraction = phase & 65535U;
            if (!s->mvoice[v].active)
                break;
        }
    }
}
FM1_DSP_CODE unsigned sampledrum_note(unsigned pad)
{
    return pad < SAMPLE_DRUM_PADS ? sample_notes[pad] : 128U;
}
FM1_DSP_CODE int sampledrum_index(unsigned note)
{
    return note >= 35 && note <= 48 && sample_map[note - 35] < SAMPLE_DRUM_PADS ? sample_map[note - 35] : -1;
}
const char *sampledrum_name(unsigned pad)
{
    return pad < SAMPLE_DRUM_PADS ? sample_assets[pad].name ? sample_assets[pad].name : "EMPTY" : "";
}
const char *sampledrum_label(unsigned pad)
{
    return pad < SAMPLE_DRUM_PADS ? sample_short_names[pad] : "";
}
FM1_DSP_ENTRY void sampledrum_trigger(sampledrum_t *s, unsigned pad, unsigned velocity)
{
    if (!s || pad >= SAMPLE_DRUM_PADS || !velocity || velocity > 127 || !sample_assets[pad].data)
        return;
    unsigned slot = SAMPLE_DRUM_VOICES, oldest = SAMPLE_DRUM_VOICES, active = 0;
    unsigned choke = s->pot[pad][SAMPLE_DRUM_CHOKE];
    for (unsigned i = 0; i < SAMPLE_DRUM_VOICES; ++i) {
        if (!s->voice[i].active) {
            if (slot == SAMPLE_DRUM_VOICES)
                slot = i;
        } else {
            ++active;
            if (oldest == SAMPLE_DRUM_VOICES || (uint32_t)(s->age - s->voice[i].age) > (uint32_t)(s->age - s->voice[oldest].age))
                oldest = i;
            if (choke && s->voice[i].choke == choke && !s->voice[i].stopping) {
                s->voice[i].stopping = 1;
                s->voice[i].fade = FADE_FRAMES;
            }
        }
    }
    if (slot == SAMPLE_DRUM_VOICES || active >= s->limit)
        slot = oldest;
    /* A full pool crossfades the stolen voice's last output into the new
     * hit over32 frames. Repeated hits may otherwise overlap naturally. */
    int32_t tail = s->voice[slot].active ? s->voice[slot].last : 0;
    memset(&s->voice[slot], 0, sizeof s->voice[slot]);
    s->voice[slot].tail = tail;
    s->voice[slot].fade = tail != 0 ? FADE_FRAMES : 0;
    s->voice[slot].pad = (uint8_t)pad;
    s->voice[slot].active = 1;
    s->voice[slot].choke = (uint8_t)choke;
    s->voice[slot].age = ++s->age;
    s->voice[slot].next_group = ~0U;
    s->voice[slot].step = increments[s->pot[pad][SAMPLE_DRUM_TUNE]];
    s->voice[slot].end =
        (sample_assets[pad].frames * (s->pot[pad][SAMPLE_DRUM_LENGTH] + 1U)) / 128U;
    /* Four gentle articulations, using one sample. Only trigger-time scalar
     * work is added; the renderer already supports fractional phase/gain. */
    static FM1_DSP_DATA const int16_t pitch_delta[4] = {-151, 76, 0, 152};
    static FM1_DSP_DATA const int8_t gain_delta[4] = {-1, 1, 0, -2};
    static FM1_DSP_DATA const uint8_t starts[4] = {0, 2, 4, 1};
    unsigned variation = s->pot[pad][SAMPLE_DRUM_VARIATION];
    unsigned round = s->round[pad]++ & 3U;
    /* The maximum product is below 87 million; a 32-bit multiply suffices. */
    int32_t pitch = (int32_t)s->voice[slot].step * pitch_delta[round] * (int32_t)variation;
    s->voice[slot].step = (uint32_t)((int32_t)s->voice[slot].step + ((pitch + 32768) >> 16));
    unsigned fade_span = s->voice[slot].step * FADE_FRAMES;
    s->voice[slot].fade_reciprocal =
        (uint32_t)((UINT64_C(1) << 48) / fade_span);
    s->voice[slot].gain = (int32_t)((int64_t)velocity * s->pot[pad][SAMPLE_DRUM_LEVEL] *
        (100 + gain_delta[round] * (int)variation) * FM_Q30_ONE / (127 * 127 * 4 * 100));
    s->voice[slot].position = starts[round] * variation;
    int32_t brightness = FM_Q30_ONE - (int32_t)((int64_t)s->pot[pad][SAMPLE_DRUM_VELOCITY_LPF] *
        (127 - velocity) * FM_Q30_ONE / (127 * 127));
    s->voice[slot].lowpass = 64424509 + fm_mul30(1009317315, fm_mul30(brightness, brightness));
    if (!s->pot[pad][SAMPLE_DRUM_VELOCITY_LPF] || velocity == 127)
        s->voice[slot].lowpass = FM_Q30_ONE;
}
/* Retain two groups so linear interpolation across a boundary never evicts
 * the previous endpoint. The decoder state follows the newest group only. */
static FM1_DSP_CODE int sample_at(sampledrum_t *s, unsigned slot, unsigned position)
{
    const struct sample_asset *a = &sample_assets[s->voice[slot].pad];
    unsigned group = position / FM1_SEA_GROUP, next = s->voice[slot].next_group;
    if (next == ~0U || group >= next || next - group > 2U) {
        unsigned chunk = group / 16U, last = group % 16U;
        unsigned first = next != ~0U && next <= group && next / 16U == chunk ? next % 16U : 0;
        size_t offset = (size_t)chunk * CHUNK_BYTES;
        if (offset > a->bytes || CHUNK_BYTES > a->bytes - offset ||
            fm1_sea_decode_groups(a->data + offset, CHUNK_BYTES, 5, sample_dqt, first, last,
                                  s->voice[slot].state,
                                  s->voice[slot].pcm + (group & 1U) * FM1_SEA_GROUP)) {
            s->voice[slot].active = 0;
            return 0;
        }
        /* Filter newly decoded 32 kHz samples once, before interpolation.
         * One multiply and two additions per source sample; no trig, filter
         * redesign or random generator in the per-sample loop. */
        if (s->voice[slot].lowpass < FM_Q30_ONE) {
            int16_t *pcm = s->voice[slot].pcm + (group & 1U) * FM1_SEA_GROUP;
            int32_t z = s->voice[slot].filter, a = s->voice[slot].lowpass;
            for (unsigned i = 0; i < FM1_SEA_GROUP; ++i) {
                z += fm_mul30((int32_t)pcm[i] * 32768 - z, a);
                pcm[i] = (int16_t)(z / 32768);
            }
            s->voice[slot].filter = z;
        }
        s->voice[slot].next_group = group + 1;
    }
    return s->voice[slot].pcm[(group & 1U) * FM1_SEA_GROUP + position % FM1_SEA_GROUP];
}

/* Process the interior of one cached group. The last endpoint and all fades
 * stay in the scalar path. No decoding, division or state stores per frame. */
static FM1_DSP_CODE unsigned mix_group(sampledrum_t *s, unsigned v, int32_t *out,
                                      unsigned frames, unsigned offset)
{
    const int16_t *pcm = s->voice[v].pcm +
                        ((s->voice[v].position / FM1_SEA_GROUP) & 1U) * FM1_SEA_GROUP;
    unsigned phase = (offset << 16) + s->voice[v].fraction;
    unsigned count = 0;
    while (count < frames && phase < ((FM1_SEA_GROUP - 1U) << 16)) {
        unsigned at = phase >> 16;
        int a = pcm[at], b = pcm[at + 1];
        int32_t interpolated = (int32_t)((int64_t)a * 65536 + (int64_t)(b - a) * (phase & 65535U));
        int32_t y = (int32_t)(((int64_t)interpolated * s->voice[v].gain) >> 37);
        out[count++] += y;
        s->voice[v].last = y;
        phase += s->voice[v].step;
    }
    s->voice[v].position += (phase >> 16) - offset;
    s->voice[v].fraction = phase & 65535U;
    return count;
}

FM1_DSP_ENTRY void sampledrum_render(sampledrum_t *s, int32_t *out, unsigned frames)
{
    if (!s || !out || frames > FM1_BLOCK_FRAMES)
        return;
    memset(out, 0, frames * sizeof *out);
    for (unsigned v = 0; v < SAMPLE_DRUM_VOICES; ++v) {
        if (!s->voice[v].active)
            continue;
        for (unsigned i = 0; i < frames;) {
            unsigned position = s->voice[v].position, end = s->voice[v].end;
            if (position >= end) {
                s->voice[v].active = 0;
                break;
            }
            int a = sample_at(s, v, position);
            if (!s->voice[v].active)
                break;
            unsigned offset = position % FM1_SEA_GROUP;
            /* Leave a whole group before the existing 128-frame tail guard. */
            if (!s->voice[v].fade && end - position >= 128U + FM1_SEA_GROUP &&
                offset < FM1_SEA_GROUP - 1U) {
                i += mix_group(s, v, out + i, frames - i, offset);
                continue;
            }
            int b = position + 1 < end ? sample_at(s, v, position + 1) : 0;
            if (!s->voice[v].active)
                break;
            int32_t interpolated = (int32_t)((int64_t)a * 65536 + (int64_t)(b - a) * s->voice[v].fraction);
            int32_t y = (int32_t)(((int64_t)interpolated * s->voice[v].gain) >> 37);
            /* Fade a deliberately shortened hit as well as the natural EOF. */
            if (end - position < 128U) {
                unsigned remaining = (end - position) * 65536U - s->voice[v].fraction;
                unsigned fade_span = s->voice[v].step * FADE_FRAMES;
                if (remaining < fade_span) {
                    /* Resolve the reciprocal at note-on, not once per tail
                     * sample. Two integer products retain a Q30 fade factor;
                     * error is under two Q24 units at the maximum hit gain. */
                    int32_t fade = (int32_t)(((uint64_t)remaining *
                        s->voice[v].fade_reciprocal) >> 18);
                    y = fm_mul30(y, fade);
                }
            }
            if (s->voice[v].fade) {
                unsigned amount = s->voice[v].fade;
                y = s->voice[v].stopping ? (int32_t)((int64_t)y * amount / FADE_FRAMES)
                    : (int32_t)(((int64_t)y * (FADE_FRAMES - amount) + (int64_t)s->voice[v].tail * amount) / FADE_FRAMES);
                if (!--s->voice[v].fade && s->voice[v].stopping)
                    s->voice[v].active = 0;
            }
            s->voice[v].last = y;
            out[i] += y;
            unsigned phase = s->voice[v].fraction + s->voice[v].step;
            s->voice[v].position += phase >> 16;
            s->voice[v].fraction = phase & 65535U;
            ++i;
            if (!s->voice[v].active)
                break;
        }
    }
    melodic_render(s, out, frames);
}

size_t sampledrum_size(void) { return sizeof(sampledrum_t); }
FM1_DSP_CODE unsigned sampledrum_active(const sampledrum_t *s)
{
    unsigned count = 0;
    for (unsigned i = 0; i < SAMPLE_DRUM_VOICES; ++i) count += s->voice[i].active != 0;
    return count;
}
