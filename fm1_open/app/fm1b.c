/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "fm1b.h"
#include "fm1_sea.h"
#include "engine.h"
#include <string.h>

enum { CHUNK_BYTES = 228, GROUPS_PER_CHUNK = 16, ENGINE_RATE = 44100, ENV_STEP = 32, ENV_ONE = 1 << 24, ENV_END = 256 };
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
    uint16_t lfo;                             /* vibrato phase, moved on only while the channel's modulation wheel is up */
    /* SEA5 zones: the LMS state after group state_group (-1: none), and the two most recent groups by parity. */
    int32_t state_group, id[2], state[8];
    int16_t pcm[2][FM1_SEA_GROUP];
};
static struct {
    const uint8_t *image;
    const int16_t *dqt;
    uint32_t bytes, rate, bits16, sea;
    uint32_t pitch, bankmap, presets, keymaps, zones, envs, env_count, preset_count, zone_count;
    int32_t preset; /* -1: none selected */
    int32_t drum_preset; /* the percussion kit, -1: none */
    uint32_t age;
    int32_t last_note, last_zone, last_voice, last_drum;   /* the latest note that sounded: for the panel */
    int32_t last_program_channel, last_program;            /* the latest MIDI program change: for the panel */
    const uint8_t *last_zone_record, *last_env;
} bank;
static struct voice voices[FM1B_VOICES];

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
enum { RPN_NULL = 0x3fff, VIBRATO_CENTS = 50, LFO_STEP = 262 /* 5.5 Hz in steps of ENV_STEP frames */ };

static FM1_DSP_CODE void channel_rescale(unsigned channel)
{
    uint32_t x = (uint32_t)channel_volume[channel] * channel_expression[channel];   /* 0..16129 */
    uint32_t y = (x * x) >> 14;                                                     /* (x / 16129)^2 in Q14, a little low: 15877 at the top */
    y += y >> 5;
    channel_scale[channel] = (uint16_t)(x == 127U * 127U ? SCALE_ONE : y > SCALE_ONE ? SCALE_ONE : y);
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
    bank.envs = rd32(image + 44);
    bank.env_count = 0;
    if (bank.rate < 4000 || bank.rate > 96000 || !region_ok(bank.pitch, 512) || !region_ok(bank.bankmap, 512) ||
        !region_ok(bank.presets, 24ULL * bank.preset_count) || !region_ok(bank.keymaps, 128ULL * bank.preset_count) ||
        !region_ok(bank.zones, 24ULL * bank.zone_count))
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
    uint64_t span = bank.sea ? (uint64_t)((length + 319U) / 320U) * CHUNK_BYTES
                             : (uint64_t)length * (bank.bits16 ? 2U : 1U);
    if (!length || !region_ok(data, span))
        return;
    if (!(zone[17] & 1U) || end <= start || end > length)
        end = start = 0; /* no loop */
    uint32_t env_number = rd16(zone + 18);
    const uint8_t *env = bank.envs && env_number < bank.env_count ? bank.image + bank.envs + env_number * 12U : default_env;
    int index = (int)note - (int)zone[16] + 64;
    index = index < 0 ? 0 : index > 127 ? 127 : index;
    uint32_t pitch = rd32(bank.image + bank.pitch + (uint32_t)index * 4U);
    int32_t fine = (int16_t)rd16(zone + 20);                  /* cents, -50..50: 2^(x/1200) to second order */
    if (fine) {
        int32_t t = fine * 3785 / 100;                        /* x * ln 2 in Q16 */
        pitch = (uint32_t)(((uint64_t)pitch * (uint32_t)(65536 + t + ((t * t) >> 17))) >> 16);
    }

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
    if (channel_cents[channel] || channel_mod[channel])
        retune(v);
    v->gain = (int32_t)((int64_t)velocity * level * 1073741824LL / (127 * 127 * 4));
    /* envelope: the attack starts from silence; a step is ENV_STEP frames at 44.1 kHz */
    uint32_t attack_steps = (rd16(env) * 1378U + 500U) / 1000U, hold_steps = (rd16(env + 2) * 1378U + 500U) / 1000U;
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
    case 121:                                                 /* reset all controllers: levels, bend, wheel, pedal, RPN */
        channel_volume[channel] = channel_expression[channel] = 127;
        channel_rescale(channel);
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

FM1_DSP_CODE void fm1b_render(const void *owner, int32_t *out, unsigned frames)
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
            if (!v->frames) {
                env_step(v);
                if (!v->active)
                    break;
            }
            --v->frames;
            v->level += v->level_step;
            int a = sample_at(v, pcm, position), b = next < length ? sample_at(v, pcm, next) : 0;
            int64_t mixed = (int64_t)a * 65536 + (int64_t)(b - a) * v->fraction;
            out[i] += (int32_t)((mixed * v->level) >> 37);
            uint32_t phase = v->fraction + v->step;
            v->position += phase >> 16;
            v->fraction = phase & 65535U;
            if (!v->active)
                break;
        }
    }
}
