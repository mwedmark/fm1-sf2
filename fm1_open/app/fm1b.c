/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "fm1b.h"
#include "fm1_sea.h"
#include "engine.h"
#include <string.h>
#include "fm1b_dqt6.inc"

/* An envelope step is ENV_STEP frames: 32 at 44.1 kHz (0.726 ms, the step the converter's envelope factors are made for), 23 at
 * 32 kHz (0.719 ms: envelopes run 1 % fast there). ENV_STEPS_PER_S1000 is the steps in 1000 ms, for the attack and hold times. */
enum { GROUPS_PER_CHUNK = 16, ENGINE_RATE = FM1B_RATE, ENV_STEP = FM1B_RATE == 44100U ? 32 : (int)((FM1B_RATE * 32U + 22050U) / 44100U),
       ENV_STEPS_PER_S1000 = (int)((FM1B_RATE * 1000U / (unsigned)ENV_STEP + 500U) / 1000U), ENV_ONE = 1 << 24, ENV_END = 256 };
enum { ATTACK, HOLD, DECAY, SUSTAIN, RELEASE };
/* The envelope used when a bank has none (FORMAT.md, "Envelopes"): 2 ms attack, full sustain, a 15 ms release. */
static const uint8_t default_env[12] = {2, 0, 0, 0, 0, 0xff, 0xff, 0xff, 0x00, 0xb9 /* 0xb900 = 0.72 per step */, 0, 0};

struct voice {
    uint32_t position, fraction, step, base_step, age, data, length, loop_start, loop_end;   /* step: base_step with bend and vibrato */
    int32_t gain, level, level_step;          /* gain: the voice's peak; level: the gain now, with the envelope, ramped per frame */
    const void *owner;
    uint32_t env, attack_inc, sustain, left;  /* envelope level Q24 at the start of a step, the attack's rise per step, sustain Q24 */
    uint16_t decay, release, frames;          /* Q16 factors per step; frames left in this step */
    uint8_t note, active, phase, fixed, drum, channel;
    uint8_t held;                             /* released while the channel's sustain pedal was down: releases with the pedal */
    uint8_t cut;                              /* fading out fast to make room (the voice limit): no longer counts as sounding */
    uint16_t lfo;                             /* vibrato phase, moved on only while the channel's modulation wheel is up */
    uint8_t exclusive;                        /* the zone's exclusive class (SF2 generator 57), 0: none */
    int16_t preset;                           /* the preset it plays: an exclusive class holds within one preset */
    uint8_t zone_reverb, zone_chorus;         /* the zone's own sends (SF2 generators 16, 15), added to its channel's */
    /* Pan: the zone's own (SF2 generator 17, -64..64) plus the channel's CC 10. A panned voice fades its far side by side_gain
     * (Q8): it is added to the side buffer (1: left, 2: right) that the mix takes away from that channel. 0: centred. */
    int8_t zone_pan;
    uint8_t side;
    uint16_t side_gain;
    /* SEA zones: the LMS state after group state_group (-1: none), and the two most recent groups by parity. */
    int32_t state_group, id[2], state[8];
    int16_t pcm[2][FM1_SEA_GROUP];
};
static struct {
    const uint8_t *image;
    const int16_t *dqt;
    uint32_t bytes, rate, format, sea_bits, chunk_bytes;   /* format: FORMAT_*; SEA: code bits and 28 + 40 * bits bytes per chunk */
    uint32_t pitch, bankmap, presets, keymaps, zones, envs, env_count, preset_count, zone_count;
    uint32_t extras;  /* the zone levels, exclusive classes, scale tunings and sends, 0: none (a bank made before them) */
    uint32_t extra_size;  /* their bytes a zone: 8, or 4 in a 52-byte header (no sends) */
    int32_t preset; /* -1: none selected */
    int32_t drum_preset; /* the percussion kit, -1: none */
    uint32_t age;
    int32_t last_note, last_zone, last_voice, last_drum;   /* the latest note that sounded: for the panel */
    int32_t last_program_channel, last_program;            /* the latest MIDI program change: for the panel */
    const uint8_t *last_zone_record, *last_env;
} bank;
static struct voice voices[FM1B_VOICES];
/* The voices that may sound at once (fm1b_voice_limit): FM1B_VOICES unless the audio interrupt runs short of time. Over it, the
 * oldest voices fade out in about 5 ms (CUT_RELEASE per envelope step, from full level to -96 dB in 7 steps). */
static unsigned voice_limit = FM1B_VOICES, voices_cut;
enum { CUT_RELEASE = 13500 };
enum { FORMAT_PCM8, FORMAT_PCM16, FORMAT_PCM12, FORMAT_SEA, FORMAT_BFP8 };
enum { BFP_GROUP = 32, BFP_GROUP_BYTES = 33 };   /* BFP8: a shift byte, then 32 signed 8-bit mantissas */

/* One state per MIDI channel plus the panel's: the preset a note plays (-1: bank.preset), and the channel's volume and
 * expression with their product, a Q14 gain on the General MIDI curve, which the voices apply as the envelope ramps. */
enum { PANEL_CHANNEL = FM1B_CHANNELS, CHANNEL_SLOTS = FM1B_CHANNELS + 1, SCALE_ONE = 1 << 14 };
static int32_t channel_preset[CHANNEL_SLOTS];
static uint8_t channel_volume[CHANNEL_SLOTS], channel_expression[CHANNEL_SLOTS];
static uint16_t channel_scale[CHANNEL_SLOTS];
/* Pitch bend (-8192..8191), its range from RPN 0 (semitones, cents) and the bend in cents; the modulation wheel (vibrato
 * depth), the sustain pedal, the RPN selected with CC 101/100, and what the panel's channel page shows. All zero while a
 * channel never gets these messages, and then nothing in the audio path looks at them but one test per envelope step. */
static int16_t channel_bend[CHANNEL_SLOTS], channel_cents[CHANNEL_SLOTS];
static uint8_t channel_bend_semis[CHANNEL_SLOTS], channel_bend_extra[CHANNEL_SLOTS];
static uint8_t channel_mod[CHANNEL_SLOTS], channel_pedal[CHANNEL_SLOTS], channel_program[CHANNEL_SLOTS], channel_notes[CHANNEL_SLOTS];
static uint16_t channel_rpn[CHANNEL_SLOTS];
/* Reverb and chorus sends (MIDI CC 91, CC 93; the panel's channel page), 0..127. The effects' master levels scale them. */
static uint8_t channel_reverb[CHANNEL_SLOTS], channel_chorus[CHANNEL_SLOTS];
/* Pan (MIDI CC 10; the panel's channel page), 0..127, 64 the centre: it moves the zones' own pan, as an SF2 player does. */
static uint8_t channel_pan[CHANNEL_SLOTS];
/* The balance law sf2fx uses for the whole mix: the near side keeps its level, the far one is sqrt(1 - |p| / 64), so a
 * centred voice sounds as it did before pan existed. This is 256 minus that, in Q8, for |p| = 0..64. */
static const uint16_t pan_far_cut[65] = {
    0, 2, 4, 6, 8, 10, 12, 14, 17, 19, 21, 23, 25, 27, 30, 32, 34, 37, 39, 41, 44, 46, 49, 51, 54, 56, 59, 61, 64, 67, 69, 72,
    75, 78, 81, 84, 87, 90, 93, 96, 99, 103, 106, 109, 113, 117, 120, 124, 128, 132, 136, 141, 145, 150, 155, 160, 165, 171, 178,
    184, 192, 201, 211, 224, 256};
enum { RPN_NULL = 0x3fff, VIBRATO_CENTS = 50, LFO_STEP = (int)((360448U * (unsigned)ENV_STEP + FM1B_RATE / 2U) / FM1B_RATE) /* 5.5 Hz: 65536 * 5.5 per second, in steps of ENV_STEP frames */ };

static FM1_DSP_CODE void channel_rescale(unsigned channel)
{
    uint32_t x = (uint32_t)channel_volume[channel] * channel_expression[channel];   /* 0..16129 */
    uint32_t y = (x * x) >> 14;                                                     /* (x / 16129)^2 in Q14, a little low: 15877 at the top */
    y += y >> 5;
    channel_scale[channel] = (uint16_t)(x == 127U * 127U ? SCALE_ONE : y > SCALE_ONE ? SCALE_ONE : y);
}
/* The voice's side and how much of it to take away, from its zone's pan and its channel's (127 is all the way right). */
static FM1_DSP_CODE void voice_pan(struct voice *v)
{
    int32_t p = v->zone_pan + (channel_pan[v->channel] == 127U ? 64 : (int32_t)channel_pan[v->channel] - 64);
    p = p < -64 ? -64 : p > 64 ? 64 : p;
    v->side = p < 0 ? 2U : p > 0 ? 1U : 0U;                     /* panned left: the right side fades, and the other way */
    v->side_gain = pan_far_cut[p < 0 ? -p : p];
}
static void channels_reset(void)
{
    for (unsigned c = 0; c < CHANNEL_SLOTS; ++c) {
        channel_preset[c] = -1;
        channel_volume[c] = channel_expression[c] = 127;
        channel_rescale(c);
        channel_bend[c] = channel_cents[c] = 0;
        channel_bend_semis[c] = 2;
        channel_bend_extra[c] = channel_mod[c] = channel_pedal[c] = channel_program[c] = channel_notes[c] = 0;
        channel_rpn[c] = RPN_NULL;
        channel_reverb[c] = FM1B_REVERB_SEND;
        channel_chorus[c] = FM1B_CHORUS_SEND;
        channel_pan[c] = 64;
    }
    bank.last_program_channel = bank.last_program = -1;
}

static inline __attribute__((always_inline)) uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static inline __attribute__((always_inline)) uint32_t rd32(const uint8_t *p) { return rd16(p) | rd16(p + 2) << 16; }
static inline __attribute__((always_inline)) int region_ok(uint32_t offset, uint64_t size)
{
    return offset <= bank.bytes && size <= bank.bytes - offset;
}

int fm1b_bind(const uint8_t *image, unsigned bytes, const int16_t *dqt)
{
    fm1b_panic();
    bank.image = NULL;
    bank.preset = -1;
    bank.drum_preset = -1;
    bank.last_note = -1;
    channels_reset();                                         /* preset numbers belong to a bank */
    if (!image)
        return 0;
    /* flags: bit 0 16-bit, bit 1 SEA chunks, bit 2 12-bit, bit 3 BFP8 (at most one of them; none: 8-bit), bits 8..11 the SEA
     * code bits (0: 5) */
    uint32_t flags = bytes < 48 ? 0 : rd16(image + 6), sea_bits = (flags >> 8) & 15U;
    if (bytes < 48 || memcmp(image, "FM1B", 4) || rd16(image + 4) != 1 || (flags & ~0x0f0fU) || __builtin_popcount(flags & 15U) > 1 ||
        rd32(image + 40) != bytes || (sea_bits && !(flags & 2U)))
        return -1;
    bank.format = flags & 1U ? FORMAT_PCM16 : flags & 4U ? FORMAT_PCM12 : flags & 2U ? FORMAT_SEA : flags & 8U ? FORMAT_BFP8 : FORMAT_PCM8;
    bank.sea_bits = sea_bits ? sea_bits : 5U;
    bank.chunk_bytes = 28U + FM1_SEA_FRAMES * bank.sea_bits / 8U;
    bank.dqt = bank.sea_bits == 5U ? dqt : bank.sea_bits == 6U ? fm1b_dqt6 : NULL;   /* the caller has the SEA5 table, the engine the others */
    if (bank.format == FORMAT_SEA && !bank.dqt)
        return -1;
    bank.bytes = bytes;
    bank.rate = rd32(image + 8);
    bank.preset_count = rd16(image + 12);
    bank.zone_count = rd16(image + 14);
    bank.pitch = rd32(image + 16);
    bank.bankmap = rd32(image + 20);
    bank.presets = rd32(image + 24);
    bank.keymaps = rd32(image + 28);
    bank.zones = rd32(image + 32);
    bank.envs = rd32(image + 44);
    bank.extras = bank.pitch >= 52U ? rd32(image + 48) : 0U;  /* a 52-byte header has the offset; banks before it end at 48 */
    bank.extra_size = bank.pitch >= 56U ? rd32(image + 52) : 4U;   /* a 56-byte header has their size */
    bank.env_count = 0;
    if (bank.rate < 4000 || bank.rate > 96000 || !region_ok(bank.pitch, 512) || !region_ok(bank.bankmap, 512) ||
        !region_ok(bank.presets, 24ULL * bank.preset_count) || !region_ok(bank.keymaps, 128ULL * bank.preset_count) ||
        !region_ok(bank.zones, 24ULL * bank.zone_count) || (bank.extras && (bank.extras % 4U || bank.extra_size < 4U || bank.extra_size > 64U || bank.extra_size % 4U ||
                                                    !region_ok(bank.extras, (uint64_t)bank.extra_size * bank.zone_count))))
        return -1;
    if (bank.envs) {
        bank.env_count = (bank.bytes - (bank.envs <= bank.bytes ? bank.envs : bank.bytes)) / 12U;
        if (bank.envs > bank.bytes || bank.envs % 4U)
            return -1;
    }
    bank.image = image;
    for (unsigned slot = 0; slot < 256 && bank.preset < 0; ++slot) {
        uint32_t p = rd16(image + bank.bankmap + slot * 2U);
        if (p < bank.preset_count)
            bank.preset = (int32_t)p;
    }
    for (unsigned slot = 128; slot < 256 && bank.drum_preset < 0; ++slot) {   /* the first percussion kit the bank has */
        uint32_t p = rd16(image + bank.bankmap + slot * 2U);
        if (p < bank.preset_count)
            bank.drum_preset = (int32_t)p;
    }
    return 0;
}
/* The preset the bank maps bank 0 or 128, program 0..127 to, or -1. */
static FM1_DSP_CODE int32_t preset_of(unsigned program_bank, unsigned program)
{
    if (!bank.image || program > 127 || (program_bank != 0 && program_bank != 128))
        return -1;
    uint32_t p = rd16(bank.image + bank.bankmap + ((program_bank ? 128U : 0U) + program) * 2U);
    return p < bank.preset_count ? (int32_t)p : -1;
}
int fm1b_program(unsigned program_bank, unsigned program)
{
    int32_t p = preset_of(program_bank, program);
    if (p < 0)
        return -1;
    bank.preset = p;
    bank.last_note = -1;
    bank.last_zone_record = NULL;
    return 0;
}
void fm1b_status(struct fm1b_status *status)
{
    memset(status, 0, sizeof *status);
    if (!bank.image || bank.preset < 0)
        return;
    status->ready = 1;
    for (unsigned c = 0; c < FM1B_CHANNELS; ++c)
        status->channels += channel_preset[c] >= 0;
    status->program_channel = bank.last_program_channel < 0 ? 255 : (uint8_t)bank.last_program_channel;
    status->program_number = bank.last_program < 0 ? 0 : (uint8_t)bank.last_program;
    status->drums = bank.drum_preset >= 0;
    status->last_drum = bank.last_drum > 0;
    if (bank.drum_preset >= 0)
        memcpy(status->drum_name, bank.image + bank.presets + (uint32_t)bank.drum_preset * 24U + 4, 20);
    status->rate = bank.rate;
    const uint8_t *preset = bank.image + bank.presets + (uint32_t)bank.preset * 24U;
    memcpy(status->name, preset + 4, 20);
    status->zones = (uint8_t)rd16(preset + 2);
    for (unsigned slot = 0; slot < 128; ++slot)
        if (rd16(bank.image + bank.bankmap + slot * 2U) == (uint32_t)bank.preset) {
            status->program = (uint8_t)slot;
            break;
        }
    for (unsigned i = 0; i < FM1B_VOICES; ++i)
        status->voices += voices[i].active;
    status->voice_limit = (uint8_t)voice_limit;
    status->voices_cut = voices_cut;
    if (bank.last_note < 0 || !bank.last_zone_record)
        return;
    const uint8_t *zone = bank.last_zone_record, *env = bank.last_env;
    if (bank.last_drum > 0)
        status->zones = (uint8_t)rd16(bank.image + bank.presets + (uint32_t)bank.drum_preset * 24U + 2);
    status->note = (uint8_t)bank.last_note;
    status->zone = (uint8_t)bank.last_zone;
    status->root = zone[16];
    status->loop = zone[17] & 1U;
    status->fine = (int8_t)rd16(zone + 20);
    status->length = rd32(zone + 4);
    status->loop_start = rd32(zone + 8);
    status->loop_end = rd32(zone + 12);
    unsigned found = 0;
    for (unsigned n = 0; n < 128; ++n)                        /* the key range the zone answers: the notes whose key map points at it */
        if (bank.image[bank.keymaps + (uint32_t)(bank.last_drum > 0 ? bank.drum_preset : bank.preset) * 128U + n] == (uint32_t)bank.last_zone) {
            if (!found++)
                status->lo = (uint8_t)n;
            status->hi = (uint8_t)n;
        }
    status->attack_ms = (uint16_t)rd16(env);
    status->hold_ms = (uint16_t)rd16(env + 2);
    status->sustain_percent = (uint8_t)(rd16(env + 6) * 100U / 65535U);
    const struct voice *v = &voices[bank.last_voice];
    if (v->active && v->note == bank.last_note) {
        status->phase = v->phase;
        status->level = (uint8_t)(v->env >> 16 > 255U ? 255U : v->env >> 16);
    }
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

/* 2^(cents / 1200) in Q16, for -3600..3600 cents: an octave shift, a semitone from the table, and the cents left over
 * to second order (at most 0.05 cent off). */
static const uint32_t semitone_q16[12] = {65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032, 110218, 116772, 123715};
static FM1_DSP_CODE uint32_t cents_factor(int32_t cents)
{
    int32_t octave = cents >= 0 ? cents / 1200 : -((1199 - cents) / 1200);
    int32_t rest = cents - octave * 1200;                    /* 0..1199 */
    int32_t t = rest % 100 * 3785 / 100;                      /* x * ln 2 in Q16 */
    uint32_t f = (uint32_t)(((uint64_t)semitone_q16[rest / 100] * (uint32_t)(65536 + t + ((t * t) >> 17))) >> 16);
    return octave >= 0 ? f << octave : f >> -octave;
}
/* The voice's step with its channel's bend and, while the modulation wheel is up, the vibrato: a triangle of +-50 cents
 * at full depth. A channel without either plays base_step untouched. */
static FM1_DSP_CODE void retune(struct voice *v)
{
    int32_t cents = channel_cents[v->channel];
    unsigned mod = channel_mod[v->channel];
    if (mod) {
        int32_t tri = v->lfo < 32768U ? (int32_t)v->lfo : 65535 - (int32_t)v->lfo;   /* 0..32767 */
        cents += (tri - 16384) * (int32_t)mod * VIBRATO_CENTS / (16384 * 127);
    }
    v->step = cents ? (uint32_t)(((uint64_t)v->base_step * cents_factor(cents)) >> 16) : v->base_step;
}
static FM1_DSP_CODE void channel_retune(unsigned channel)
{
    for (unsigned i = 0; i < FM1B_VOICES; ++i)
        if (voices[i].active && voices[i].channel == channel)
            retune(&voices[i]);
}
static FM1_DSP_CODE void channel_repan(unsigned channel)
{
    for (unsigned i = 0; i < FM1B_VOICES; ++i)
        if (voices[i].active && voices[i].channel == channel)
            voice_pan(&voices[i]);
}
static FM1_DSP_CODE void bend_update(unsigned channel)
{
    int32_t range = channel_bend_semis[channel] * 100 + channel_bend_extra[channel];
    channel_cents[channel] = (int16_t)(channel_bend[channel] * range / 8192);
    channel_retune(channel);
}
/* Pedal up: every note released while it was down releases now. */
static FM1_DSP_CODE void pedal_release(unsigned channel)
{
    for (unsigned i = 0; i < FM1B_VOICES; ++i)
        if (voices[i].active && voices[i].channel == channel && voices[i].held) {
            voices[i].held = 0;
            if (voices[i].phase != RELEASE)
                voices[i].phase = RELEASE;
        }
}

static FM1_DSP_CODE unsigned voices_sounding(void)
{
    unsigned n = 0;
    for (unsigned i = 0; i < FM1B_VOICES; ++i)
        n += voices[i].active && !voices[i].cut;
    return n;
}
/* Fade out the oldest sounding voices until at most room sound. */
static FM1_DSP_CODE void voices_fit(unsigned room)
{
    for (unsigned n = voices_sounding(); n > room; --n) {
        int oldest = -1;
        for (unsigned i = 0; i < FM1B_VOICES; ++i)
            if (voices[i].active && !voices[i].cut &&
                (oldest < 0 || (uint32_t)(bank.age - voices[i].age) > (uint32_t)(bank.age - voices[oldest].age)))
                oldest = (int)i;
        if (oldest < 0)
            return;
        struct voice *v = &voices[oldest];
        v->cut = 1;
        v->held = 0;
        v->phase = RELEASE;
        v->release = CUT_RELEASE;
        ++voices_cut;
    }
}
FM1_DSP_CODE void fm1b_voice_limit(unsigned limit)
{
    voice_limit = limit < 1U ? 1U : limit > FM1B_VOICES ? FM1B_VOICES : limit;
    voices_fit(voice_limit);
}
FM1_DSP_CODE unsigned fm1b_voices_sounding(void) { return voices_sounding(); }

static FM1_DSP_CODE void note_apply(const void *owner, unsigned channel, unsigned note, unsigned velocity, unsigned level, unsigned drum)
{
    if (!fm1b_ready() || note > 127 || velocity > 127 || level > 127 || (drum && bank.drum_preset < 0))
        return;
    int32_t preset_index = drum ? bank.drum_preset : channel_preset[channel] >= 0 ? channel_preset[channel] : bank.preset;
    if (!velocity) {
        /* A zone with the fixed flag (percussion) plays to its end like a struck drum. */
        for (unsigned i = 0; i < FM1B_VOICES; ++i)
            if (voices[i].active && voices[i].owner == owner && voices[i].channel == channel && voices[i].note == note && voices[i].drum == drum && !voices[i].fixed && voices[i].phase != RELEASE) {
                if (channel_pedal[channel])
                    voices[i].held = 1;                       /* the pedal holds it */
                else
                    voices[i].phase = RELEASE;
            }
        return;
    }
    ++channel_notes[channel];
    uint32_t key = bank.image[bank.keymaps + (uint32_t)preset_index * 128U + note];
    const uint8_t *preset = bank.image + bank.presets + (uint32_t)preset_index * 24U;
    uint32_t first = rd16(preset), count = rd16(preset + 2);
    if (key == 0xff || key >= count || first + key >= bank.zone_count)
        return;
    const uint8_t *zone = bank.image + bank.zones + (first + key) * 24U;
    uint32_t data = rd32(zone), length = rd32(zone + 4), start = rd32(zone + 8), end = rd32(zone + 12);
    uint64_t span = bank.format == FORMAT_SEA ? (uint64_t)((length + 319U) / 320U) * bank.chunk_bytes
                  : bank.format == FORMAT_PCM12 ? ((uint64_t)length * 3U + 1U) / 2U
                  : bank.format == FORMAT_BFP8 ? (uint64_t)((length + BFP_GROUP - 1U) / BFP_GROUP) * BFP_GROUP_BYTES
                  : (uint64_t)length * (bank.format == FORMAT_PCM16 ? 2U : 1U);
    if (!length || !region_ok(data, span))
        return;
    if (bank.format == FORMAT_SEA) {                          /* the chunk header the decoder no longer checks per group */
        const uint8_t *chunk = bank.image + data;
        if (chunk[0] != 1U || chunk[1] != (0x40U | bank.sea_bits) || chunk[2] != FM1_SEA_GROUP)
            return;
    }
    if (!(zone[17] & 1U) || end <= start || end > length)
        end = start = 0; /* no loop */
    uint32_t env_number = rd16(zone + 18);
    const uint8_t *env = bank.envs && env_number < bank.env_count ? bank.image + bank.envs + env_number * 12U : default_env;
    /* The zone's level (0.5 dB steps), exclusive class and scale tuning (cents a key): FORMAT.md, "Zone levels". */
    const uint8_t *extra = bank.extras ? bank.image + bank.extras + (first + key) * bank.extra_size : NULL;
    const uint8_t *sends = extra && bank.extra_size >= 6U ? extra + 4 : NULL;
    uint32_t atten = extra ? extra[0] : 0U, exclusive = extra ? extra[1] & 127U : 0U, scale = extra ? rd16(extra + 2) : 100U;
    int index = (int)note - (int)zone[16] + 64;
    int32_t fine = (int16_t)rd16(zone + 20);                  /* cents, -50..50 at 100 cents a key: 2^(x/1200) to second order */
    if (scale != 100U) {                                      /* (note - root) * scale + fine cents: whole semitones from the table */
        int32_t cents = ((int)note - (int)zone[16]) * (int32_t)(scale > 1200U ? 1200U : scale) + fine;
        int32_t semis = cents >= 0 ? cents / 100 : -((99 - cents) / 100);
        index = semis + 64;
        fine = cents - semis * 100;                           /* 0..99 */
    }
    index = index < 0 ? 0 : index > 127 ? 127 : index;
    uint32_t pitch = rd32(bank.image + bank.pitch + (uint32_t)index * 4U);
    if (fine) {
        int32_t t = fine * 3785 / 100;                        /* x * ln 2 in Q16 */
        pitch = (uint32_t)(((uint64_t)pitch * (uint32_t)(65536 + t + ((t * t) >> 17))) >> 16);
    }

    if (exclusive)                                            /* a closed hi-hat cuts the open one: the class's other notes end fast */
        for (unsigned i = 0; i < FM1B_VOICES; ++i) {
            struct voice *o = &voices[i];
            if (o->active && !o->cut && o->exclusive == exclusive && o->preset == preset_index && o->channel == channel && o->owner == owner) {
                o->cut = 1;                                   /* fading: it no longer counts as sounding */
                o->held = 0;
                o->phase = RELEASE;
                o->release = CUT_RELEASE;
            }
        }
    voices_fit(voice_limit - 1U);                             /* room for this one under the limit */
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
    v->drum = (uint8_t)drum;
    v->channel = (uint8_t)channel;
    v->age = ++bank.age;
    v->state_group = v->id[0] = v->id[1] = -1;
    v->data = data;
    v->length = length;
    v->loop_start = start;
    v->loop_end = end;
    v->base_step = v->step = (uint32_t)(((uint64_t)pitch * bank.rate + ENGINE_RATE / 2U) / ENGINE_RATE);
    v->lfo = 16384;                                           /* the triangle's centre: vibrato starts at the note's own pitch */
    int32_t zone_pan = (int16_t)rd16(zone + 22);              /* SF2 units, -500..500 (0.1 %); 0 in banks made before pan */
    zone_pan = zone_pan < -500 ? -500 : zone_pan > 500 ? 500 : zone_pan;
    v->zone_pan = (int8_t)((zone_pan * 64 + (zone_pan < 0 ? -250 : 250)) / 500);
    voice_pan(v);
    if (channel_cents[channel] || channel_mod[channel])
        retune(v);
    /* velocity squared: the SoundFont default (velocity to attenuation, concave, 960 cB at 0.4) is 40 log10(127 / velocity) dB,
     * so 64 is 12 dB under 127 and 32 is 24 dB under */
    v->gain = (int32_t)((int64_t)velocity * velocity * level * 1073741824LL / (127 * 127 * 127 * 4));
    if (atten)                                                /* 0.5 dB a step: 2^(-atten * 99.66 / 1200) */
        v->gain = (int32_t)(((int64_t)v->gain * cents_factor(-(int32_t)((atten * 99658U + 500U) / 1000U))) >> 16);
    v->exclusive = (uint8_t)exclusive;
    v->preset = (int16_t)preset_index;
    if (sends) {
        v->zone_reverb = sends[0] > 127U ? 127U : sends[0];
        v->zone_chorus = sends[1] > 127U ? 127U : sends[1];
    }
    /* envelope: the attack starts from silence; a step is ENV_STEP frames */
    uint32_t attack_steps = (rd16(env) * (uint32_t)ENV_STEPS_PER_S1000 + 500U) / 1000U, hold_steps = (rd16(env + 2) * (uint32_t)ENV_STEPS_PER_S1000 + 500U) / 1000U;
    attack_steps = attack_steps ? attack_steps : 1U;
    v->attack_inc = ENV_ONE / attack_steps;
    v->left = hold_steps;
    v->decay = (uint16_t)rd16(env + 4);
    v->sustain = rd16(env + 6) << 8;
    v->release = (uint16_t)rd16(env + 8);
    v->fixed = (uint8_t)(rd16(env + 10) & 1U);
    v->phase = ATTACK;
    bank.last_note = (int32_t)note;
    bank.last_drum = (int32_t)drum;
    bank.last_zone = (int32_t)key;
    bank.last_voice = (int32_t)slot;
    bank.last_zone_record = zone;
    bank.last_env = env;
}

/* Notes, program changes and controllers arrive from the panel or MIDI and are applied from the audio interrupt, so they
 * go through a small single-producer, single-consumer ring in the order they came: the fm1b_* calls only write an event
 * and fm1b_render() applies it. For a note, a is the note and b the velocity; for a program change a is the program and
 * b is 1 for the percussion bank; for a controller a is its number and b its value. */
enum { QUEUE_SIZE = 32 };
enum { EVENT_NOTE, EVENT_PROGRAM, EVENT_CONTROL, EVENT_BEND };   /* a bend: a is the low 7 bits, b the high 7 */
enum { PROGRAM_FOLLOW = 2 };                                       /* a program event's b: back to the panel's preset */
static struct { const void *owner; uint8_t kind, channel, a, b, level, drum; } queue[QUEUE_SIZE];
static volatile uint32_t queue_head, queue_tail;
static void queue_event(unsigned kind, const void *owner, unsigned channel, unsigned a, unsigned b, unsigned level, unsigned drum)
{
    uint32_t head = queue_head;
    if (head - queue_tail >= QUEUE_SIZE || channel >= CHANNEL_SLOTS || a > 127 || b > 127 || level > 127)
        return; /* full: the event is lost, like a dropped MIDI byte */
    queue[head % QUEUE_SIZE].owner = owner;
    queue[head % QUEUE_SIZE].kind = (uint8_t)kind;
    queue[head % QUEUE_SIZE].channel = (uint8_t)channel;
    queue[head % QUEUE_SIZE].a = (uint8_t)a;
    queue[head % QUEUE_SIZE].b = (uint8_t)b;
    queue[head % QUEUE_SIZE].level = (uint8_t)level;
    queue[head % QUEUE_SIZE].drum = (uint8_t)drum;
    __asm__ volatile("" ::: "memory");
    queue_head = head + 1U;
}
void fm1b_note(const void *owner, unsigned note, unsigned velocity, unsigned level) { queue_event(EVENT_NOTE, owner, PANEL_CHANNEL, note, velocity, level, 0); }
void fm1b_drum_note(const void *owner, unsigned note, unsigned velocity, unsigned level) { queue_event(EVENT_NOTE, owner, PANEL_CHANNEL, note, velocity, level, 1); }
void fm1b_channel_note(unsigned channel, unsigned note, unsigned velocity, unsigned level)
{
    if (channel < FM1B_CHANNELS)
        queue_event(EVENT_NOTE, NULL, channel, note, velocity, level, 0);
}
void fm1b_channel_drum_note(unsigned channel, unsigned note, unsigned velocity, unsigned level)
{
    if (channel < FM1B_CHANNELS)
        queue_event(EVENT_NOTE, NULL, channel, note, velocity, level, 1);
}
void fm1b_channel_program(unsigned channel, unsigned program_bank, unsigned program)
{
    if (channel < FM1B_CHANNELS && (program_bank == 0 || program_bank == 128))
        queue_event(EVENT_PROGRAM, NULL, channel, program, program_bank != 0, 0, 0);
}
void fm1b_channel_follow(unsigned channel)
{
    if (channel < FM1B_CHANNELS)
        queue_event(EVENT_PROGRAM, NULL, channel, 0, PROGRAM_FOLLOW, 0, 0);
}
unsigned fm1b_has_program(unsigned program_bank, unsigned program) { return preset_of(program_bank, program) >= 0; }
void fm1b_channel_control(unsigned channel, unsigned controller, unsigned value)
{
    if (channel < FM1B_CHANNELS)
        queue_event(EVENT_CONTROL, NULL, channel, controller, value, 0, 0);
}
void fm1b_channel_bend(unsigned channel, unsigned value)
{
    if (channel < FM1B_CHANNELS && value < 16384U)
        queue_event(EVENT_BEND, NULL, channel, value & 127U, value >> 7, 0, 0);
}
void fm1b_channel_info(unsigned channel, struct fm1b_channel *info)
{
    memset(info, 0, sizeof *info);
    if (channel >= FM1B_CHANNELS || !bank.image)
        return;
    int32_t p = channel_preset[channel];
    if (p >= 0) {
        info->own = 1;
        info->program = channel_program[channel] & 127U;
        info->kit = channel_program[channel] >> 7;
        memcpy(info->name, bank.image + bank.presets + (uint32_t)p * 24U + 4, 20);
    }
    info->volume = channel_volume[channel];
    info->expression = channel_expression[channel];
    info->bend_cents = channel_cents[channel];
    info->mod = channel_mod[channel];
    info->pedal = channel_pedal[channel];
    info->reverb = channel_reverb[channel];
    info->chorus = channel_chorus[channel];
    info->pan = channel_pan[channel];
    info->notes = channel_notes[channel];
    for (unsigned i = 0; i < FM1B_VOICES; ++i)
        info->voices += voices[i].active && voices[i].channel == channel;
}
unsigned fm1b_has_drums(void) { return bank.image && bank.drum_preset >= 0; }
unsigned fm1b_multitimbral(void)
{
    unsigned n = 0;
    for (unsigned c = 0; c < FM1B_CHANNELS; ++c)
        n += channel_preset[c] >= 0;
    return n;
}

static FM1_DSP_CODE void program_apply(unsigned channel, unsigned program_bank, unsigned program)
{
    if (program_bank == PROGRAM_FOLLOW) {
        channel_preset[channel] = -1;
        channel_program[channel] = 0;
        return;
    }
    int32_t p = preset_of(program_bank ? 128U : 0U, program);
    if (p < 0)
        return;
    channel_preset[channel] = p;
    channel_program[channel] = (uint8_t)(program | (program_bank ? 128U : 0U));
    bank.last_program_channel = (int32_t)channel;
    bank.last_program = (int32_t)program;
}
static FM1_DSP_CODE void control_apply(unsigned channel, unsigned controller, unsigned value)
{
    switch (controller) {
    case 1:                                                   /* modulation wheel: vibrato depth */
        channel_mod[channel] = (uint8_t)value;
        if (!value)
            channel_retune(channel);                          /* back to the note's own pitch (and the bend) */
        break;
    case 91:                                                  /* reverb send */
        channel_reverb[channel] = (uint8_t)value;
        break;
    case 93:                                                  /* chorus send */
        channel_chorus[channel] = (uint8_t)value;
        break;
    case 64:                                                  /* sustain pedal */
        channel_pedal[channel] = value >= 64U;
        if (!channel_pedal[channel])
            pedal_release(channel);
        break;
    case 101:
        channel_rpn[channel] = (uint16_t)((channel_rpn[channel] & 127U) | value << 7);
        break;
    case 100:
        channel_rpn[channel] = (uint16_t)((channel_rpn[channel] & ~127U) | value);
        break;
    case 6:                                                   /* data entry: RPN 0 is the bend range, up to 24 semitones */
        if (channel_rpn[channel] == 0) {
            channel_bend_semis[channel] = (uint8_t)(value > 24U ? 24U : value);
            bend_update(channel);
        }
        break;
    case 38:
        if (channel_rpn[channel] == 0) {
            channel_bend_extra[channel] = (uint8_t)(value > 99U ? 99U : value);
            bend_update(channel);
        }
        break;
    case 7:
        channel_volume[channel] = (uint8_t)value;
        channel_rescale(channel);
        break;
    case 11:
        channel_expression[channel] = (uint8_t)value;
        channel_rescale(channel);
        break;
    case 10:
        channel_pan[channel] = (uint8_t)value;
        channel_repan(channel);
        break;
    case 121:                                                 /* reset all controllers: levels, pan, bend, wheel, pedal, RPN */
        channel_volume[channel] = channel_expression[channel] = 127;
        channel_rescale(channel);
        channel_pan[channel] = 64;
        channel_repan(channel);
        channel_bend[channel] = 0;
        channel_mod[channel] = 0;
        channel_pedal[channel] = 0;
        channel_rpn[channel] = RPN_NULL;
        pedal_release(channel);
        bend_update(channel);
        break;
    case 120:
    case 123:
        for (unsigned i = 0; i < FM1B_VOICES; ++i)
            if (voices[i].active && voices[i].channel == channel) {
                voices[i].held = 0;
                if (controller == 120)
                    voices[i].active = 0;
                else if (!voices[i].fixed && voices[i].phase != RELEASE)
                    voices[i].phase = RELEASE;
            }
        break;
    default:
        break;
    }
}

static inline __attribute__((always_inline)) int32_t clamp16(int32_t x) { return x > 32767 ? 32767 : x < -32768 ? -32768 : x; }
/* Decode SEA group g (20 frames) of voice v. The LMS state carries from one group to the next inside a chunk, so
 * a forward step costs one group; anything else (the first read, a loop jump, a chunk change) starts from the
 * chunk's own seed and costs up to one chunk. The arithmetic is fm1_sea_decode_groups()'s (web/synth/sea_decode.c) for
 * any code size: the codes are read from a bit buffer instead of one bit at a time, and the state lives in registers. */
static FM1_DSP_CODE void group_at(struct voice *v, uint32_t g, int16_t *out)
{
    const unsigned bits = bank.sea_bits, mask = (1U << bits) - 1U;
    const uint8_t *chunk = bank.image + v->data + (g / GROUPS_PER_CHUNK) * bank.chunk_bytes;
    unsigned first = 0, last = g % GROUPS_PER_CHUNK;
    if (v->state_group >= 0 && (uint32_t)v->state_group < g &&
        (uint32_t)v->state_group / GROUPS_PER_CHUNK == g / GROUPS_PER_CHUNK)
        first = (uint32_t)v->state_group % GROUPS_PER_CHUNK + 1U;
    int32_t s0, s1, s2, s3, w0, w1, w2, w3;
    if (first) {
        s0 = v->state[0]; s1 = v->state[1]; s2 = v->state[2]; s3 = v->state[3];
        w0 = v->state[4]; w1 = v->state[5]; w2 = v->state[6]; w3 = v->state[7];
    } else {
        s0 = (int16_t)rd16(chunk + 4); s1 = (int16_t)rd16(chunk + 6); s2 = (int16_t)rd16(chunk + 8); s3 = (int16_t)rd16(chunk + 10);
        w0 = (int16_t)rd16(chunk + 12); w1 = (int16_t)rd16(chunk + 14); w2 = (int16_t)rd16(chunk + 16); w3 = (int16_t)rd16(chunk + 18);
    }
    for (unsigned group = first; group <= last; ++group) {
        const int16_t *dqt = bank.dqt + (((chunk[20 + group / 2U] >> ((group & 1U) ? 0 : 4)) & 15U) << bits);
        unsigned at = group * FM1_SEA_GROUP * bits;
        const uint8_t *p = chunk + 28 + (at >> 3);
        uint32_t buffer = *p++ & (0xffU >> (at & 7U));
        int have = 8 - (int)(at & 7U);
        for (unsigned i = 0; i < FM1_SEA_GROUP; ++i) {
            if (have < (int)bits) { buffer = buffer << 8 | *p++; have += 8; }
            have -= (int)bits;
            int32_t residual = dqt[(buffer >> have) & mask];
            /* (s0 w0 + ... + s3 w3) >> 13 in 32 bits, exactly: each product fits (|s|, |w| <= 32768), their sum may not, so
             * the products' high parts (>> 13) and low parts (& 8191, at most 4 x 8191) are added apart. */
            int32_t p0 = s0 * w0, p1 = s1 * w1, p2 = s2 * w2, p3 = s3 * w3;
            int32_t sum = (p0 >> 13) + (p1 >> 13) + (p2 >> 13) + (p3 >> 13) + (((p0 & 8191) + (p1 & 8191) + (p2 & 8191) + (p3 & 8191)) >> 13);
            int32_t value = clamp16(sum + residual), delta = residual >> 4;
            w0 = clamp16(w0 + (s0 < 0 ? -delta : delta));
            w1 = clamp16(w1 + (s1 < 0 ? -delta : delta));
            w2 = clamp16(w2 + (s2 < 0 ? -delta : delta));
            w3 = clamp16(w3 + (s3 < 0 ? -delta : delta));
            s0 = s1; s1 = s2; s2 = s3; s3 = value;
            out[i] = (int16_t)value;                          /* the last group's values are the ones left */
        }
    }
    v->state[0] = s0; v->state[1] = s1; v->state[2] = s2; v->state[3] = s3;
    v->state[4] = w0; v->state[5] = w1; v->state[6] = w2; v->state[7] = w3;
    v->state_group = (int32_t)g;
}
/* One sample of the zone, as 16 bits. format is a constant at each call, so each voice loop below is built for one format. */
static inline __attribute__((always_inline)) int sample_at(struct voice *v, const uint8_t *pcm, uint32_t at, const unsigned format)
{
    if (format == FORMAT_SEA) {
        uint32_t g = at / FM1_SEA_GROUP;
        unsigned slot = g & 1U;
        if (v->id[slot] != (int32_t)g) {
            group_at(v, g, v->pcm[slot]);
            v->id[slot] = (int32_t)g;
        }
        return v->pcm[slot][at - g * FM1_SEA_GROUP];
    }
    if (format == FORMAT_BFP8) {                              /* mantissa << the group's shift (at most 8) */
        const uint8_t *group = pcm + (at / BFP_GROUP) * BFP_GROUP_BYTES;
        unsigned shift = group[0] < 8U ? group[0] : 8U;
        return (int)(int8_t)group[1 + at % BFP_GROUP] * (1 << shift);
    }
    if (format == FORMAT_PCM12) { /* pair k is 3 bytes at 3k: s0 = b0 | b1 low nibble << 8, s1 = b1 high nibble | b2 << 4 */
        const uint8_t *b = pcm + (at >> 1) * 3U;
        return (at & 1U) ? (int16_t)((b[1] & 0xf0U) | (unsigned)b[2] << 8) : (int16_t)((unsigned)b[0] << 4 | (b[1] & 0x0fU) << 12);
    }
    if (format == FORMAT_PCM16)
        return (int16_t)rd16(pcm + at * 2U);
    return (int)(int8_t)pcm[at] * 256;
}

/* Move the envelope one step on and ramp the voice's gain towards the level at the end of it. */
static FM1_DSP_CODE void env_step(struct voice *v)
{
    uint32_t target = v->env;
    switch (v->phase) {
    case ATTACK:
        target += v->attack_inc;
        if (target >= ENV_ONE) { target = ENV_ONE; v->phase = v->left ? HOLD : DECAY; }
        break;
    case HOLD:
        if (v->left) --v->left;
        if (!v->left) v->phase = DECAY;
        break;
    case DECAY:
        target = (uint32_t)(((uint64_t)target * v->decay) >> 16);
        if (target <= v->sustain) { target = v->sustain; v->phase = SUSTAIN; }
        break;
    case RELEASE:
        target = (uint32_t)(((uint64_t)target * v->release) >> 16);
        break;
    default:
        break;
    }
    v->env = target;
    if (v->phase >= DECAY && target < ENV_END) {
        v->active = 0;
        return;
    }
    if (channel_mod[v->channel]) {                            /* vibrato: the step follows the LFO, once per envelope step */
        v->lfo = (uint16_t)(v->lfo + LFO_STEP);
        retune(v);
    }
    int32_t next = (int32_t)((((int64_t)v->gain * target >> 24) * channel_scale[v->channel]) >> 14);   /* the channel's volume applies live */
    v->level_step = (next - v->level) / ENV_STEP;
    v->frames = ENV_STEP;
}

static FM1_DSP_CODE void queue_drain(void)
{
    while (queue_tail != queue_head) {
        uint32_t at = queue_tail % QUEUE_SIZE;
        if (queue[at].kind == EVENT_PROGRAM)
            program_apply(queue[at].channel, queue[at].b, queue[at].a);
        else if (queue[at].kind == EVENT_CONTROL)
            control_apply(queue[at].channel, queue[at].a, queue[at].b);
        else if (queue[at].kind == EVENT_BEND) {
            channel_bend[queue[at].channel] = (int16_t)(((int32_t)queue[at].b << 7 | queue[at].a) - 8192);
            bend_update(queue[at].channel);
        }
        else
            note_apply(queue[at].owner, queue[at].channel, queue[at].a, queue[at].b, queue[at].level, queue[at].drum);
        __asm__ volatile("" ::: "memory");
        queue_tail = queue_tail + 1U;
    }
}
/* One voice for one block. With bus (a constant at each call, so the plain path has no trace of it) the voice is also
 * added to the reverb and chorus buses, times its channel's sends rg and cg (Q7, 128 = all of it), and a panned voice to its
 * side buffer times sg (Q8, voice_pan): the mix takes that from the far channel, so one product pans it. format is a constant
 * too: each format gets its own loop. The loop runs in stretches up to the next envelope step, with the voice's state in
 * registers; the sample between two frames is interpolated in Q4 and scaled by the level with one 32 x 32 multiply. */
static inline __attribute__((always_inline)) void voice_run(struct voice *v, int32_t *out, int32_t *rev, int32_t *cho, int32_t *side,
                                                           unsigned frames, int32_t rg, int32_t cg, int32_t sg, const int bus, const unsigned format)
{
    const uint8_t *pcm = bank.image + v->data;
    const uint32_t length = v->length, ls = v->loop_start, le = v->loop_end;
    uint32_t position = v->position, fraction = v->fraction, step = v->step;
    int32_t level = v->level, level_step = v->level_step;
    unsigned left = v->frames, i = 0;
    while (i < frames) {
        if (!left) {
            v->level = level;                                 /* env_step ramps from here */
            env_step(v);
            if (!v->active)
                break;
            level_step = v->level_step;
            step = v->step;                                   /* vibrato may have moved it */
            left = v->frames;
        }
        unsigned n = frames - i < left ? frames - i : left;
        left -= n;
        for (unsigned end = i + n; i < end; ++i) {
            if (le && position >= le)
                position = ls + (position - le) % (le - ls);
            if (position >= length) {
                v->active = 0;
                goto done;
            }
            uint32_t next = position + 1;
            if (le && next >= le)
                next = ls;
            level += level_step;
            int32_t a = sample_at(v, pcm, position, format), b = next < length ? sample_at(v, pcm, next, format) : 0;
            int32_t x = a * 16 + (((b - a) * (int32_t)(fraction >> 1)) >> 11);   /* Q4; (b - a) * 15 bits stays in 32 */
            int32_t y = (int32_t)(((int64_t)x * level) >> 25);
            out[i] += y;
            if (bus) {
                if (rg) rev[i] += (y * rg) >> 7;
                if (cg) cho[i] += (y * cg) >> 7;
                if (sg) side[i] += (y * sg) >> 8;             /* |y| < 2^23, sg <= 256: within 32 bits */
            }
            fraction += step;
            position += fraction >> 16;
            fraction &= 65535U;
        }
    }
done:
    v->position = position;
    v->fraction = fraction;
    v->level = level;
    v->frames = (uint16_t)left;
}
/* voice_run for the bank's format, without and with the effect buses: ten loops in all. */
static FM1_DSP_CODE __attribute__((noinline)) void voice_play(struct voice *v, int32_t *out, unsigned frames)
{
    switch (bank.format) {
    case FORMAT_SEA: voice_run(v, out, NULL, NULL, NULL, frames, 0, 0, 0, 0, FORMAT_SEA); break;
    case FORMAT_PCM16: voice_run(v, out, NULL, NULL, NULL, frames, 0, 0, 0, 0, FORMAT_PCM16); break;
    case FORMAT_PCM12: voice_run(v, out, NULL, NULL, NULL, frames, 0, 0, 0, 0, FORMAT_PCM12); break;
    case FORMAT_BFP8: voice_run(v, out, NULL, NULL, NULL, frames, 0, 0, 0, 0, FORMAT_BFP8); break;
    default: voice_run(v, out, NULL, NULL, NULL, frames, 0, 0, 0, 0, FORMAT_PCM8); break;
    }
}
static FM1_DSP_CODE __attribute__((noinline)) void voice_play_buses(struct voice *v, int32_t *out, int32_t *rev, int32_t *cho, int32_t *side,
                                                                   unsigned frames, int32_t rg, int32_t cg, int32_t sg)
{
    switch (bank.format) {
    case FORMAT_SEA: voice_run(v, out, rev, cho, side, frames, rg, cg, sg, 1, FORMAT_SEA); break;
    case FORMAT_PCM16: voice_run(v, out, rev, cho, side, frames, rg, cg, sg, 1, FORMAT_PCM16); break;
    case FORMAT_PCM12: voice_run(v, out, rev, cho, side, frames, rg, cg, sg, 1, FORMAT_PCM12); break;
    case FORMAT_BFP8: voice_run(v, out, rev, cho, side, frames, rg, cg, sg, 1, FORMAT_BFP8); break;
    default: voice_run(v, out, rev, cho, side, frames, rg, cg, sg, 1, FORMAT_PCM8); break;
    }
}
FM1_DSP_CODE void fm1b_render(const void *owner, int32_t *out, unsigned frames)
{
    queue_drain();
    if (!bank.image)
        return;
    for (unsigned n = 0; n < FM1B_VOICES; ++n)
        if (voices[n].active && voices[n].owner == owner)
            voice_play(&voices[n], out, frames);
}
/* A voice's sends: its channel's (CC 91, CC 93) plus its zone's own, at most 127. */
static inline __attribute__((always_inline)) int voice_reverb(const struct voice *v)
{
    int r = channel_reverb[v->channel] + v->zone_reverb;
    return r > 127 ? 127 : r;
}
static inline __attribute__((always_inline)) int voice_chorus(const struct voice *v)
{
    int c = channel_chorus[v->channel] + v->zone_chorus;
    return c > 127 ? 127 : c;
}
static uint8_t last_sends[2] = {FM1B_REVERB_SEND, FM1B_CHORUS_SEND};                  /* the sends of the latest block that had voices: the tails keep them */
FM1_DSP_CODE unsigned fm1b_render_sends(const void *owner, int32_t *out, int32_t *rev, int32_t *cho, int32_t *sides[2], unsigned frames,
                                        uint8_t sends[2])
{
    queue_drain();
    sends[0] = last_sends[0];
    sends[1] = last_sends[1];
    if (!bank.image)
        return 0;
    /* While every sounding voice has the same sends (its channel's plus its zone's), the bus is the dry mix times them: no bus needed. */
    int r0 = -1, c0 = -1;
    unsigned buses = 0;
    for (unsigned n = 0; n < FM1B_VOICES; ++n) {
        const struct voice *v = &voices[n];
        if (!v->active || v->owner != owner)
            continue;
        int r = voice_reverb(v), c = voice_chorus(v);
        if (r0 < 0) { r0 = r; c0 = c; }
        if (r != r0) buses |= 1U;
        if (c != c0) buses |= 2U;
        if (v->side) buses |= 2U << v->side;                  /* 4: something to take from the left, 8: from the right */
    }
    if (buses & 1U) memset(rev, 0, frames * sizeof *rev);
    if (buses & 2U) memset(cho, 0, frames * sizeof *cho);
    if (buses & 4U) memset(sides[0], 0, frames * sizeof *sides[0]);
    if (buses & 8U) memset(sides[1], 0, frames * sizeof *sides[1]);
    if (r0 >= 0) {
        last_sends[0] = sends[0] = (uint8_t)r0;
        last_sends[1] = sends[1] = (uint8_t)c0;
    }
    for (unsigned n = 0; n < FM1B_VOICES; ++n) {
        struct voice *v = &voices[n];
        if (!v->active || v->owner != owner)
            continue;
        if (!buses)
            voice_play(v, out, frames);
        else {
            int32_t r = voice_reverb(v), c = voice_chorus(v);
            voice_play_buses(v, out, rev, cho, v->side ? sides[v->side - 1U] : NULL, frames, buses & 1U ? r + (r >> 6) : 0,
                             buses & 2U ? c + (c >> 6) : 0, v->side ? v->side_gain : 0);
        }
    }
    return buses;
}
