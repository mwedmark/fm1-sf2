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
    /* The zone's LFOs (FORMAT.md, "Modulation"): triangles from 0, phase 2^32 a cycle, moved on per envelope step after a delay
     * of so many steps. lfos: bit 0 they (or the modulation envelope) move the pitch, bit 1 the volume, bit 2 the modulation
     * envelope runs, bit 3 the LFOs run; 0: the zone has none. */
    uint32_t mod_phase, mod_inc, vib_phase, vib_inc;
    uint16_t mod_wait, vib_wait;
    int16_t mod_pitch, vib_pitch, mod_volume;  /* cents, cents, cB at full excursion */
    int32_t lfo_cents;                         /* the LFOs' and the modulation envelope's pitch now, in cents */
    int32_t lfo_m;                             /* the mod LFO now, -32768..32768: for the filter */
    uint32_t tremolo;                          /* the mod LFO's volume now, Q16 */
    uint8_t lfos;
    /* The modulation envelope (SF2 generators 25-30): 0..2^24, linear, a phase of its own (MENV_*), stepped with the volume
     * envelope. menv_left counts the delay and hold steps. */
    uint8_t menv_phase;
    uint32_t menv, menv_left, menv_attack, menv_hold, menv_decay, menv_sustain, menv_release;
    int16_t menv_pitch;                        /* cents at its top */
    /* The low-pass filter (FORMAT.md, "Modulation"): a two-pole resonant filter as FluidSynth's, run on the voice's output once a
     * block. filtered: 0 none, 1 running, 2 not started (its coefficients start at their first values). fc in absolute cents,
     * fc_env and fc_lfo the cents the modulation envelope and the mod LFO add at their top; inv2q 1 / (2 Q) and fgain the
     * resonance's 1 / sqrt(Q), Q16. b0 (= b2, b1 = 2 b0), a1, a2 Q30; x1, x2, y1, y2 the filter's history. */
    uint8_t filtered;
    int16_t fc, fc_env, fc_lfo;
    int32_t fc_now;                            /* the cutoff the coefficients are for, cents */
    uint32_t inv2q, fgain;
    /* How a cutoff that holds still runs (FILTER_*): in 64 bits, or in 32 bits with q0 (b0 in Q(13 + qk)), q1, q2 (a1, a2 in Q13)
     * on the signal over 2^qs (as the voice's level allows: 13 bits for its loudest). qerr: the remainder of the output's shift,
     * carried to the next frame. */
    uint8_t fmode, qk, qs;
    int32_t q0, q1, q2, qerr;
    int32_t b0, a1, a2, x1, x2, y1, y2;
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
    uint32_t mods, mod_count;  /* the modulation records (LFOs, key scaling, filter), 0: none (a header under 64 bytes) */
    uint32_t mod_size;         /* their bytes: 40, or 20 in a 64-byte header (no filter) */
    uint32_t layers, layer_count;   /* stacked layers: (first, count) a preset, then the entries; 0: none (a header under 76 bytes) */
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
/* What the limit took, for the panel (fm1b_drops): notes still sounding (key or pedal down), notes already releasing, and
 * voices the CPU guard took. Since start or fm1b_drops_reset(). */
static uint32_t drops_held, drops_tail, drops_guard;
static FM1_DSP_CODE void drop_count(const struct voice *v, unsigned guard)
{
    if (guard) ++drops_guard;
    else if (v->phase == RELEASE) ++drops_tail;
    else ++drops_held;
}
enum { CUT_RELEASE = 13500 };
enum { FORMAT_PCM8, FORMAT_PCM16, FORMAT_PCM12, FORMAT_SEA, FORMAT_BFP8 };
enum { BFP_GROUP = 32, BFP_GROUP_BYTES = 33 };   /* BFP8: a shift byte, then 32 signed 8-bit mantissas */
enum { MOD_RECORD_LFO = 20, MOD_RECORD_FILTER = 40 };
enum { NOTE_LAYERS = 4 };   /* the zones one note starts at most: the key map's and up to three stacked on it */   /* a modulation record: up to its LFOs, up to its filter (i16 in SF2 units) */
enum { MENV_DELAY, MENV_ATTACK, MENV_HOLD, MENV_DECAY, MENV_SUSTAIN, MENV_RELEASE, MENV_ONE = 1 << 24 };
/* A cutoff of 0 cents (8.176 Hz) as a fraction of the rate, Q32. */
static const uint32_t FILTER_BASE = (uint32_t)((8176ULL * 4294967296ULL + 500ULL * FM1B_RATE) / (1000ULL * FM1B_RATE));
/* The filter's top, 0.45 of the rate (as FluidSynth), Q30; a filtered voice is rendered this many frames at a time. */
enum { FILTER_TOP = 483183821, FILTER_CHUNK = 128 };
/* From 1/50 to 1/4 of the rate (640 Hz to 8 kHz at 32 kHz) a still filter runs in 32 bits: its poles are far enough from 1 that
 * coefficients in Q13 put the cutoff within 1 %, and far enough from -1 that the rounding's noise, which the error feedback moves up,
 * is not lifted by a peak near the top. Elsewhere, and while the cutoff moves, it runs in 64 bits. */
enum { FILTER_FAST = 21474836, FILTER_FAST_TOP = 268435456, FILTER_SLOW = 0, FILTER_32 = 1 };
/* An LFO at 0 timecents (8.176 Hz, the SF2 default) moves its phase this much per envelope step. */
static const uint32_t LFO_BASE = (uint32_t)((8176ULL * 4294967296ULL * (unsigned)ENV_STEP + 500ULL * FM1B_RATE) / (1000ULL * FM1B_RATE));

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
/* Mute and solo, set by the panel (fm1b_channel_mute, fm1b_channel_solo): a bit per channel, and the soloed channel or -1.
 * channel_gate is each channel's fade, Q14, moved once a block towards 0 while the channel is silent and back to SCALE_ONE;
 * at SCALE_ONE it is not applied at all. The panel's own slot is never silenced. */
static volatile uint32_t mute_mask;
static volatile int32_t solo_channel = -1;
static uint16_t channel_gate[CHANNEL_SLOTS];
enum { GATE_FRAMES = FM1B_RATE / 50U };   /* the fade: 20 ms, a few blocks, each a 32-frame ramp of the voices: no click */
static inline __attribute__((always_inline)) uint32_t silent_mask(void)
{
    int32_t solo = solo_channel;
    return (mute_mask | (solo >= 0 ? ~(1U << solo) : 0U)) & ((1U << FM1B_CHANNELS) - 1U);
}
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
        channel_gate[c] = SCALE_ONE;
    }
    mute_mask = 0;
    solo_channel = -1;
    bank.last_program_channel = bank.last_program = -1;
}

static inline __attribute__((always_inline)) uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static inline __attribute__((always_inline)) uint32_t rd32(const uint8_t *p) { return rd16(p) | rd16(p + 2) << 16; }
static inline __attribute__((always_inline)) int region_ok(uint32_t offset, uint64_t size)
{
    return offset <= bank.bytes && size <= bank.bytes - offset;
}

/* FM1B_INTERP4: the sample between two frames from four frames (Catmull-Rom, as FluidSynth's 4-point interpolation) in
 * place of two. Linear interpolation loses -0.7 dB at 4 kHz and -3 dB at 8 kHz of a 32 kHz bank; this keeps the top within
 * a few tenths of a dB. interp4[i] are the four weights for a fraction of i / 256, Q14, summing to 16384 (2 KB, in RAM). */
#ifndef FM1B_INTERP4
#define FM1B_INTERP4 0   /* off since 2026-11-05: on the FM-1 it cost voices (more notes dropped); 1 builds it */
#endif
#if FM1B_INTERP4
static int16_t interp4[256][4];
static void interp4_make(void)
{
    for (int32_t i = 0; i < 256; ++i) {                       /* t = i / 256; each weight times 2^25, rounded to Q14 (>> 11) */
        int32_t t2 = i * i * 256, t3 = i * i * i, t1 = i * 65536;
        int32_t c0 = (-t3 + 2 * t2 - t1 + 1024) >> 11, c2 = (-3 * t3 + 4 * t2 + t1 + 1024) >> 11, c3 = (t3 - t2 + 1024) >> 11;
        interp4[i][0] = (int16_t)c0;
        interp4[i][1] = (int16_t)(16384 - c0 - c2 - c3);
        interp4[i][2] = (int16_t)c2;
        interp4[i][3] = (int16_t)c3;
    }
}
#endif
int fm1b_bind(const uint8_t *image, unsigned bytes, const int16_t *dqt)
{
    fm1b_panic();
#if FM1B_INTERP4
    if (!interp4[0][1])
        interp4_make();
#endif
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
    bank.mods = bank.pitch >= 64U ? rd32(image + 56) : 0U;   /* a 64-byte header has the modulation records */
    bank.mod_count = bank.mods ? rd32(image + 60) : 0U;
    bank.mod_size = bank.pitch >= 68U ? rd32(image + 64) : MOD_RECORD_LFO;   /* a 68-byte header has their size */
    bank.layers = bank.pitch >= 76U ? rd32(image + 68) : 0U;   /* a 76-byte header has the stacked layers */
    bank.layer_count = bank.layers ? rd32(image + 72) : 0U;
    bank.env_count = 0;
    if ((bank.layers && (bank.layers % 4U || bank.layer_count > 65535U ||
                         !region_ok(bank.layers, 4ULL * bank.preset_count + 4ULL * bank.layer_count))) ||
        (bank.mods && (bank.mods % 4U || bank.mod_count > 65535U || bank.mod_size < MOD_RECORD_LFO || bank.mod_size > 256U ||
                       bank.mod_size % 4U || !region_ok(bank.mods, (uint64_t)bank.mod_size * bank.mod_count))) || bank.rate < 4000 || bank.rate > 96000 || !region_ok(bank.pitch, 512) || !region_ok(bank.bankmap, 512) ||
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
void fm1b_drops_reset(void) { drops_held = drops_tail = drops_guard = 0; }
void fm1b_info(struct fm1b_info *info)
{
    memset(info, 0, sizeof *info);
    if (!bank.image)
        return;
    info->image = bank.image;
    info->bytes = bank.bytes;
    uint32_t at = bank.pitch >= 80U ? rd32(bank.image + 76) : 0U;   /* an 80-byte header has the info block's offset */
    if (!at || !region_ok(at, 52))
        return;
    for (unsigned i = 0; i < 32 && bank.image[at + i] >= ' ' && bank.image[at + i] < 127; ++i) info->name[i] = (char)bank.image[at + i];
    for (unsigned i = 0; i < 20 && bank.image[at + 32 + i] >= ' ' && bank.image[at + 32 + i] < 127; ++i) info->date[i] = (char)bank.image[at + 32 + i];
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
    status->drops_held = drops_held;
    status->drops_tail = drops_tail;
    status->drops_guard = drops_guard;
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
/* base * 2^(tc / 1200) for timecents of any size, at most 2^32 - 1: for LFO rates and delays and the key scaling. */
static FM1_DSP_CODE uint32_t scale_tc(uint32_t base, int32_t tc)
{
    int32_t octave = tc >= 0 ? tc / 1200 : -((1199 - tc) / 1200);
    uint64_t x = ((uint64_t)base * cents_factor(tc - octave * 1200)) >> 16;
    if (octave < 0)
        return octave < -40 ? 0U : (uint32_t)(x >> -octave);
    return octave > 31 || x > (0xffffffffULL >> octave) ? 0xffffffffU : (uint32_t)(x << octave);
}
/* -log2(f / 65536) in Q24 for a factor f of 1..65535 (Q16), a bit at a time: square, and halve past 2. */
static FM1_DSP_CODE uint32_t neglog2_q24(uint32_t f)
{
    uint32_t n = 0;
    if (!f)
        return 17U << 24;                                     /* below 2^-16: as good as nothing */
    while (f < 32768U) { f <<= 1; ++n; }                      /* f / 65536 in 0.5..1: its log2 is log2(f / 32768) - 1 */
    uint64_t q = (uint64_t)f << 15;                           /* f / 32768 in Q30, 1..2 */
    uint32_t frac = 0;
    for (int bit = 23; bit >= 0; --bit) {
        q = (q * q) >> 30;
        if (q >= 2ULL << 30) { q >>= 1; frac |= 1U << bit; }
    }
    return ((n + 1U) << 24) - frac;
}
/* 2^(-l) in Q16 for l in Q24: the whole part a shift, the fraction a product of 2^(-2^-i), i = 1..24. */
static const uint32_t exp2_neg_bit[24] = {759250125, 902905651, 984625594, 1028218693, 1050733751, 1062175491, 1067942999, 1070838486,
    1072289173, 1073015252, 1073378477, 1073560135, 1073650976, 1073696399, 1073719111, 1073730468, 1073736146, 1073738985,
    1073740404, 1073741114, 1073741469, 1073741647, 1073741735, 1073741780};   /* Q30 */
static FM1_DSP_CODE uint32_t exp2neg_q16(uint32_t l)
{
    if (l >> 24 >= 17U)
        return 0;
    uint64_t r = 1ULL << 30;
    for (unsigned i = 0; i < 24; ++i)
        if (l & (1U << (23 - i)))
            r = (r * exp2_neg_bit[i]) >> 30;
    return (uint32_t)((r + (1ULL << (13 + (l >> 24)))) >> (14 + (l >> 24)));
}
/* A triangle from 0 for phase 0: up to 32768 at a quarter cycle, down to -32768 at three quarters. */
static inline __attribute__((always_inline)) int32_t triangle(uint32_t phase)
{
    int32_t q = (int32_t)(phase >> 16);
    return q < 16384 ? 2 * q : q < 49152 ? 65536 - 2 * q : 2 * q - 131072;
}
/* The voice's step with its channel's bend, while the modulation wheel is up the vibrato (a triangle of +-50 cents at full
 * depth), and its zone's LFOs. A voice without any of them plays base_step untouched. */
static FM1_DSP_CODE void retune(struct voice *v)
{
    int32_t cents = channel_cents[v->channel] + v->lfo_cents;
    unsigned mod = channel_mod[v->channel];
    if (mod) {
        int32_t tri = v->lfo < 32768U ? (int32_t)v->lfo : 65535 - (int32_t)v->lfo;   /* 0..32767 */
        cents += (tri - 16384) * (int32_t)mod * VIBRATO_CENTS / (16384 * 127);
    }
    cents = cents < -4800 ? -4800 : cents > 4800 ? 4800 : cents;
    v->step = cents ? (uint32_t)(((uint64_t)v->base_step * cents_factor(cents)) >> 16) : v->base_step;
}
/* The zone's LFOs one envelope step on: the pitch they give and, for the mod LFO, the volume (cB, as 19.93 cents of amplitude
 * each; at most +12 dB). */
static FM1_DSP_CODE void lfo_step(struct voice *v)
{
    if (v->mod_wait) --v->mod_wait; else v->mod_phase += v->mod_inc;
    if (v->vib_wait) --v->vib_wait; else v->vib_phase += v->vib_inc;
    int32_t m = triangle(v->mod_phase), b = triangle(v->vib_phase);
    v->lfo_m = m;
    v->lfo_cents = ((m * v->mod_pitch + b * v->vib_pitch) >> 15) + (int32_t)(((int64_t)v->menv * v->menv_pitch) >> 24);
    if (v->lfos & 2U) {
        int32_t cents = ((m * v->mod_volume) >> 15) * 1993 / 100;
        v->tremolo = cents_factor(cents > 2400 ? 2400 : cents);
    }
}
/* The modulation envelope one step on. Its release starts with the volume envelope's. */
static FM1_DSP_CODE void menv_step(struct voice *v)
{
    if (v->phase == RELEASE && v->menv_phase != MENV_RELEASE)
        v->menv_phase = MENV_RELEASE;
    switch (v->menv_phase) {
    case MENV_DELAY:
        if (v->menv_left) --v->menv_left;
        if (!v->menv_left) v->menv_phase = MENV_ATTACK;
        break;
    case MENV_ATTACK:
        v->menv += v->menv_attack;
        if (v->menv >= MENV_ONE) { v->menv = MENV_ONE; v->menv_left = v->menv_hold; v->menv_phase = v->menv_left ? MENV_HOLD : MENV_DECAY; }
        break;
    case MENV_HOLD:
        if (v->menv_left) --v->menv_left;
        if (!v->menv_left) v->menv_phase = MENV_DECAY;
        break;
    case MENV_DECAY:
        if (v->menv > v->menv_sustain + v->menv_decay) v->menv -= v->menv_decay;
        else { v->menv = v->menv_sustain; v->menv_phase = MENV_SUSTAIN; }
        break;
    case MENV_RELEASE:
        v->menv = v->menv > v->menv_release ? v->menv - v->menv_release : 0U;
        break;
    default:
        break;
    }
}
/* sin(x) in Q30 for x in 0..pi/2 (Q30 radians): the Taylor series to x^9, within 4e-8. */
static FM1_DSP_CODE int32_t sin_q30(int32_t x)
{
    /* 1/72, 1/42, 1/20, 1/6 in Q32: products, as a 64-bit division is a library call on the device */
    int64_t x2 = ((int64_t)x * x) >> 30, t = (1LL << 30) - ((x2 * 59652324LL) >> 32);
    t = (1LL << 30) - (((x2 * t) >> 30) * 102261126LL >> 32);
    t = (1LL << 30) - (((x2 * t) >> 30) * 214748365LL >> 32);
    t = (1LL << 30) - (((x2 * t) >> 30) * 715827883LL >> 32);
    return (int32_t)(((int64_t)x * t) >> 30);
}
/* The filter's coefficients for its cutoff now: the zone's, plus the modulation envelope's and the mod LFO's, 1500..13500 cents
 * (20 Hz..20 kHz) and at most 0.45 of the rate. RBJ's low-pass with FluidSynth's gain: b0 = (1 - cos w) / 2 / a0 * gain,
 * a1 = -2 cos w / a0, a2 = (1 - alpha) / a0, a0 = 1 + alpha, alpha = sin w / (2 Q). 1 - cos w is 2 sin^2(w / 2), exact at
 * low cutoffs. */
static inline __attribute__((always_inline)) int32_t filter_cents(const struct voice *v)
{
    int32_t fc = v->fc + (int32_t)(((int64_t)v->menv * v->fc_env) >> 24) + ((v->lfo_m * v->fc_lfo) >> 15);
    return fc < 1500 ? 1500 : fc > 13500 ? 13500 : fc;
}
static FM1_DSP_CODE void filter_coefs_at(const struct voice *v, uint32_t turn, int32_t *b0, int32_t *a1, int32_t *a2)
{
    const int32_t half_pi = 1686629713;
    int64_t w = (int64_t)(((uint64_t)turn * (uint32_t)half_pi) >> 28);   /* 2 pi times it, Q30 radians: up to 0.9 pi */
    int32_t s = sin_q30((int32_t)(w > half_pi ? 2LL * half_pi - w : w)), h = sin_q30((int32_t)(w >> 1));
    int64_t omc = ((int64_t)h * h) >> 29;                     /* 1 - cos w */
    int64_t alpha = ((int64_t)s * v->inv2q) >> 16;
    /* 1 / a0 in Q30, a0 = 1..1.8: a 32-bit division to 16 bits, then one Newton step (x (2 - a0 x)) to 30 */
    int64_t a0 = (1LL << 30) + alpha, inv = (int64_t)((0x80000000U / (uint32_t)(a0 >> 14)) << 15);
    inv = (inv * ((2LL << 30) - ((a0 * inv) >> 30))) >> 30;
    *a1 = (int32_t)(-((((1LL << 30) - omc) * inv) >> 29));
    *a2 = (int32_t)((((1LL << 30) - alpha) * inv) >> 30);
    *b0 = (int32_t)((((omc * inv) >> 31) * v->fgain) >> 16);
}
static FM1_DSP_CODE uint32_t filter_coefs(const struct voice *v, int32_t fc, int32_t *b0, int32_t *a1, int32_t *a2)
{
    uint32_t turn = scale_tc(FILTER_BASE, fc) >> 2;           /* the cutoff over the rate, Q30 */
    turn = turn > (uint32_t)FILTER_TOP ? (uint32_t)FILTER_TOP : turn;
    filter_coefs_at(v, turn, b0, a1, a2);
    return turn;
}
/* The voice's block x through its filter, added to out; with keep also written back to x (for the buses). While the cutoff moves
 * the coefficients go from where they were to their value now across the block (glide); otherwise they stay in registers. */
static inline __attribute__((always_inline)) void filter_loop(struct voice *v, int32_t *x, int32_t *out, unsigned n, int32_t c0, int32_t c1,
                                                             int32_t c2, int32_t d0, int32_t d1, int32_t d2, const int glide, const int keep)
{
    int32_t x1 = v->x1, x2 = v->x2, y1 = v->y1, y2 = v->y2, e = v->qerr;
    for (unsigned i = 0; i < n; ++i) {
        int32_t in = x[i];
        int64_t acc = (int64_t)c0 * (in + 2 * x1 + x2) - (int64_t)c1 * y1 - (int64_t)c2 * y2 + e;
        int32_t y = (int32_t)(acc >> 30);
        e = (int32_t)(acc & ((1 << 30) - 1));                /* what the shift drops, to the next frame: no DC from rounding */
        y = y > (1 << 28) ? (1 << 28) : y < -(1 << 28) ? -(1 << 28) : y;
        x2 = x1; x1 = in; y2 = y1; y1 = y;
        out[i] += y;
        if (keep) x[i] = y;
        if (glide) { c0 += d0; c1 += d1; c2 += d2; }
    }
    v->x1 = x1; v->x2 = x2; v->y1 = y1; v->y2 = y2; v->qerr = e;
}
/* The same filter in 32 bits, for a cutoff that holds still at 1/50 of the rate or more. The signal is shifted so that the voice's
 * loudest possible frame (peak, from its level) has 13 bits: as many for a soft note as for a loud one, and 12 dB of room under the
 * 15-bit limit for the resonance. b0 has qk more bits than a1, a2, and what the shift to the output drops is carried to the next frame
 * (error feedback), so the rounding's noise does not pile up where the poles are. */
static inline __attribute__((always_inline)) void filter_loop32(struct voice *v, int32_t *x, int32_t *out, unsigned n, int32_t peak,
                                                               const int keep)
{
    /* the shift that takes the voice's loudest possible frame to 13 bits; the remainder belongs to the shift it was made with */
    int32_t sh = 0;
    while ((peak >> sh) >= (1 << 13)) ++sh;
    if (sh != v->qs) { v->qerr = 0; v->qs = (uint8_t)sh; }
    const int32_t q0 = v->q0, q1 = v->q1, q2 = v->q2, k = v->qk;
    /* The empty asm keeps the loads apart from the shifts (and below the shifts apart from the stores). Without it the compiler
     * paired a shift by a register with a load or store that uses the shift's result register in one bundle; fm1-emu runs such a
     * pair as if the shift came first, and the history it read and saved was wrong at every block: a buzz of a few hundred Hz. */
    int32_t x1 = v->x1, x2 = v->x2, y1 = v->y1, y2 = v->y2, e = v->qerr;
    __asm__ volatile("" : "+r"(x1), "+r"(x2), "+r"(y1), "+r"(y2));
    x1 >>= sh; x2 >>= sh; y1 >>= sh; y2 >>= sh;
    for (unsigned i = 0; i < n; ++i) {
        int32_t in = x[i] >> sh;
        int32_t acc = ((q0 * (in + 2 * x1 + x2)) >> k) - q1 * y1 - q2 * y2 + e;
        int32_t y = acc >> 13;
        e = acc - (y << 13);
        y = y > (1 << 15) ? (1 << 15) : y < -(1 << 15) ? -(1 << 15) : y;
        x2 = x1; x1 = in; y2 = y1; y1 = y;
        out[i] += y << sh;
        if (keep) x[i] = y << sh;
    }
    x1 <<= sh; x2 <<= sh; y1 <<= sh; y2 <<= sh;
    __asm__ volatile("" : "+r"(x1), "+r"(x2), "+r"(y1), "+r"(y2));
    v->x1 = x1; v->x2 = x2; v->y1 = y1; v->y2 = y2; v->qerr = e;
}
/* The 32-bit coefficients from the 64-bit ones: a1, a2 in Q13, b0 with as many more bits as keep it under 2^13. The 64-bit ones
 * become exactly what the 32-bit ones are, so that the block that glides to them in 64 bits ends where the 32-bit blocks start. */
static FM1_DSP_CODE void filter_quantize(struct voice *v, int32_t *b0, int32_t *a1, int32_t *a2)
{
    int32_t k = 0, b = *b0;
    while (k < 16 && (b >> (17 - k - 1)) < (1 << 13)) ++k;
    int32_t q1 = ((*a1 >> 16) + 1) >> 1;
    q1 = q1 > 16383 ? 16383 : q1 < -16383 ? -16383 : q1;
    v->qk = (uint8_t)k;
    v->q0 = ((b >> (16 - k)) + 1) >> 1;
    v->q1 = q1;
    v->q2 = ((*a2 >> 16) + 1) >> 1;
    *b0 = v->q0 << (17 - k);
    *a1 = v->q1 * (1 << 17);
    *a2 = v->q2 * (1 << 17);
}
static FM1_DSP_CODE __attribute__((noinline)) void filter_run(struct voice *v, int32_t *x, int32_t *out, unsigned n, int32_t peak, int keep)
{
    int32_t fc = filter_cents(v);
    if (v->filtered == 1 && fc == v->fc_now) {
        switch (v->fmode) {
        case FILTER_32:
            if (keep) filter_loop32(v, x, out, n, peak, 1); else filter_loop32(v, x, out, n, peak, 0);
            break;
        default:
            if (keep) filter_loop(v, x, out, n, v->b0, v->a1, v->a2, 0, 0, 0, 0, 1);
            else filter_loop(v, x, out, n, v->b0, v->a1, v->a2, 0, 0, 0, 0, 0);
            break;
        }
        return;
    }
    int32_t b0, a1, a2;
    uint32_t turn = filter_coefs(v, fc, &b0, &a1, &a2);
    int fast = turn >= (uint32_t)FILTER_FAST && turn < (uint32_t)FILTER_FAST_TOP;
    if (fast)
        filter_quantize(v, &b0, &a1, &a2);
    v->fc_now = fc;
    if (v->filtered == 2) { v->b0 = b0; v->a1 = a1; v->a2 = a2; v->filtered = 1; }
    /* a1 goes from about -2 to 2 (Q30): its change needs 33 bits. A step is the change times 1 / n (Q16). */
    int64_t r = (int64_t)(65536U / n);
    int32_t c0 = v->b0, c1 = v->a1, c2 = v->a2;
    int32_t d0 = (int32_t)((((int64_t)b0 - c0) * r) >> 16), d1 = (int32_t)((((int64_t)a1 - c1) * r) >> 16), d2 = (int32_t)((((int64_t)a2 - c2) * r) >> 16);
    if (keep) filter_loop(v, x, out, n, c0, c1, c2, d0, d1, d2, 1, 1);
    else filter_loop(v, x, out, n, c0, c1, c2, d0, d1, d2, 1, 0);
    v->b0 = b0; v->a1 = a1; v->a2 = a2;
    /* the next blocks, while the cutoff stays here: in 32 bits, or in 64. The remainder is in this block's (64-bit) scale. */
    v->fmode = fast ? FILTER_32 : FILTER_SLOW;
    if (fast)
        v->qerr = 0;
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
/* Fade out the oldest sounding voices until at most room sound. guard: the CPU guard lowered the limit (else: room for a note). */
static FM1_DSP_CODE void voices_fit(unsigned room, unsigned guard)
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
        drop_count(v, guard);
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
    voices_fit(voice_limit, 1);
}
FM1_DSP_CODE unsigned fm1b_voices_sounding(void) { return voices_sounding(); }

static FM1_DSP_CODE __attribute__((noinline)) void zone_start(const void *owner, unsigned channel, unsigned note, unsigned velocity,
                                                             unsigned level, unsigned drum,
                       int32_t preset_index, uint32_t number, int32_t shown);
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
    if ((silent_mask() >> channel) & 1U)                      /* muted, or another channel soloed: not heard, not started */
        return;
    uint32_t key = bank.image[bank.keymaps + (uint32_t)preset_index * 128U + note];
    const uint8_t *preset = bank.image + bank.presets + (uint32_t)preset_index * 24U;
    uint32_t first = rd16(preset), count = rd16(preset + 2);
    if (key == 0xff || key >= count || first + key >= bank.zone_count)
        return;
    /* The key map's zone, and the zones stacked on it at this key (FORMAT.md, "Layers"): all of them sound, as in the SoundFont. */
    uint32_t keys[NOTE_LAYERS];
    unsigned n = 0;
    keys[n++] = key;
    if (bank.layers) {
        const uint8_t *index = bank.image + bank.layers + (uint32_t)preset_index * 4U;
        uint32_t e = rd16(index), left = rd16(index + 2);
        for (; left && e < bank.layer_count && n < NOTE_LAYERS; ++e, --left) {
            const uint8_t *entry = bank.image + bank.layers + bank.preset_count * 4U + e * 4U;
            if (note >= entry[0] && note <= entry[1] && entry[2] != key && entry[2] < count && first + entry[2] < bank.zone_count)
                keys[n++] = entry[2];
        }
    }
    n = n > voice_limit ? voice_limit : n;
    /* A closed hi-hat cuts the open one: the class's other notes end fast. All layers first, so that one layer never cuts another. */
    for (unsigned l = 0; l < n && bank.extras; ++l) {
        uint32_t exclusive = bank.image[bank.extras + (first + keys[l]) * bank.extra_size + 1] & 127U;
        if (exclusive)
            for (unsigned i = 0; i < FM1B_VOICES; ++i) {
                struct voice *o = &voices[i];
                if (o->active && !o->cut && o->exclusive == exclusive && o->preset == preset_index && o->channel == channel && o->owner == owner) {
                    o->cut = 1;                               /* fading: it no longer counts as sounding */
                    o->held = 0;
                    o->phase = RELEASE;
                    o->release = CUT_RELEASE;
                }
            }
    }
    voices_fit(voice_limit - n, 0);                              /* room for these under the limit */
    for (unsigned l = 0; l < n; ++l)
        zone_start(owner, channel, note, velocity, level, drum, preset_index, first + keys[l], l ? -1 : (int32_t)keys[l]);
}
/* One zone of a note: a voice for it, the oldest if none is free. shown is the zone's number in its preset for the panel's
 * status, or -1 for a stacked layer (the status shows the key map's zone). */
static FM1_DSP_CODE __attribute__((noinline)) void zone_start(const void *owner, unsigned channel, unsigned note, unsigned velocity,
                                                             unsigned level, unsigned drum, int32_t preset_index, uint32_t number, int32_t shown)
{
    const uint8_t *zone = bank.image + bank.zones + number * 24U;
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
    const uint8_t *extra = bank.extras ? bank.image + bank.extras + number * bank.extra_size : NULL;
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

    unsigned slot = FM1B_VOICES, oldest = 0;
    for (unsigned i = 0; i < FM1B_VOICES; ++i) {
        if (!voices[i].active) { slot = i; break; }
        if ((uint32_t)(bank.age - voices[i].age) > (uint32_t)(bank.age - voices[oldest].age))
            oldest = i;
    }
    if (slot == FM1B_VOICES) {                                /* all busy (some fading): the oldest stops at once */
        slot = oldest;
        if (!voices[slot].cut) drop_count(&voices[slot], 0);
    }
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
    /* The zone's modulation record (FORMAT.md, "Modulation"): its LFOs, and how the key number scales hold and decay. */
    const uint8_t *mod = NULL;
    int32_t key_hold = 0, key_decay = 0;
    if (extra && bank.extra_size >= 8U && bank.mods) {
        uint32_t number = rd16(extra + 6);                    /* the record's index plus 1, 0: none */
        if (number && number <= bank.mod_count)
            mod = bank.image + bank.mods + (number - 1U) * bank.mod_size;
    }
    if (mod) {
        int32_t volume = (int16_t)rd16(mod), pitch = (int16_t)rd16(mod + 2), vib = (int16_t)rd16(mod + 4);
        v->mod_volume = (int16_t)(volume < -960 ? -960 : volume > 960 ? 960 : volume);
        v->mod_pitch = (int16_t)(pitch < -12000 ? -12000 : pitch > 12000 ? 12000 : pitch);
        v->vib_pitch = (int16_t)(vib < -12000 ? -12000 : vib > 12000 ? 12000 : vib);
        int32_t fc = 13500, q = 0, fc_env = 0, fc_lfo = 0, menv_pitch = 0;
        if (bank.mod_size >= MOD_RECORD_FILTER) {
            fc = (int16_t)rd16(mod + 18); q = (int16_t)rd16(mod + 20); fc_env = (int16_t)rd16(mod + 22); fc_lfo = (int16_t)rd16(mod + 24);
            menv_pitch = (int16_t)rd16(mod + 38);
            fc = fc < 1500 ? 1500 : fc > 13500 ? 13500 : fc;
            q = q < 0 ? 0 : q > 960 ? 960 : q;
            fc_env = fc_env < -12000 ? -12000 : fc_env > 12000 ? 12000 : fc_env;
            fc_lfo = fc_lfo < -12000 ? -12000 : fc_lfo > 12000 ? 12000 : fc_lfo;
            menv_pitch = menv_pitch < -12000 ? -12000 : menv_pitch > 12000 ? 12000 : menv_pitch;
        }
        if (fc < 13500 || fc_env < 0 || fc_lfo) {             /* a cutoff that can come down: the filter runs */
            v->filtered = 2;
            v->fc = (int16_t)fc; v->fc_env = (int16_t)fc_env; v->fc_lfo = (int16_t)fc_lfo;
            /* Q in cB is the resonance's height (dB times 10); minus 3.01 dB, so that 0 is flat (Q = 0.707) */
            int32_t q_cents = (q * 1993 + 50) / 100 - 600;     /* Q as cents of amplitude: 19.93 a cB */
            uint32_t q_lin = cents_factor(q_cents);           /* Q16 */
            v->inv2q = (uint32_t)((1ULL << 31) / q_lin);      /* 1 / (2 Q), Q16 */
            v->fgain = cents_factor(-q_cents / 2);            /* 1 / sqrt(Q) */
        }
        if (fc_env || menv_pitch) {                           /* the modulation envelope: delay, attack, hold, decay, sustain, release */
            uint32_t steps;
            v->menv_pitch = (int16_t)menv_pitch;
            v->menv_left = scale_tc(ENV_STEPS_PER_S1000, (int16_t)rd16(mod + 26));
            v->menv_phase = v->menv_left ? MENV_DELAY : MENV_ATTACK;
            steps = scale_tc(ENV_STEPS_PER_S1000, (int16_t)rd16(mod + 28));
            v->menv_attack = MENV_ONE / (steps ? steps : 1U);
            v->menv_hold = scale_tc(ENV_STEPS_PER_S1000, (int16_t)rd16(mod + 30));
            steps = scale_tc(ENV_STEPS_PER_S1000, (int16_t)rd16(mod + 32));
            v->menv_decay = MENV_ONE / (steps ? steps : 1U);
            int32_t sustain = (int16_t)rd16(mod + 34);        /* 0.1 % below the top */
            sustain = sustain < 0 ? 0 : sustain > 1000 ? 1000 : sustain;
            v->menv_sustain = (uint32_t)(((uint64_t)MENV_ONE * (uint32_t)(1000 - sustain)) / 1000U);
            steps = scale_tc(ENV_STEPS_PER_S1000, (int16_t)rd16(mod + 36));
            v->menv_release = MENV_ONE / (steps ? steps : 1U);
        }
        if (v->mod_volume || v->mod_pitch || fc_lfo) {        /* delay in steps (at most 47 s), rate as a phase step */
            uint32_t wait = scale_tc(ENV_STEPS_PER_S1000, (int16_t)rd16(mod + 6));
            v->mod_wait = (uint16_t)(wait > 65535U ? 65535U : wait);
            v->mod_inc = scale_tc(LFO_BASE, (int16_t)rd16(mod + 8));
        }
        if (v->vib_pitch) {
            uint32_t wait = scale_tc(ENV_STEPS_PER_S1000, (int16_t)rd16(mod + 10));
            v->vib_wait = (uint16_t)(wait > 65535U ? 65535U : wait);
            v->vib_inc = scale_tc(LFO_BASE, (int16_t)rd16(mod + 12));
        }
        v->lfos = (uint8_t)((v->mod_pitch || v->vib_pitch || menv_pitch ? 1U : 0U) | (v->mod_volume ? 2U : 0U) |
                            (fc_env || menv_pitch ? 4U : 0U) | (v->mod_pitch || v->vib_pitch || v->mod_volume || fc_lfo ? 8U : 0U));
        v->tremolo = 65536U;
        key_hold = (int16_t)rd16(mod + 14);
        key_decay = (int16_t)rd16(mod + 16);
    }
    /* envelope: the attack starts from silence; a step is ENV_STEP frames */
    uint32_t attack_steps = (rd16(env) * (uint32_t)ENV_STEPS_PER_S1000 + 500U) / 1000U, hold_steps = (rd16(env + 2) * (uint32_t)ENV_STEPS_PER_S1000 + 500U) / 1000U;
    attack_steps = attack_steps ? attack_steps : 1U;
    v->attack_inc = ENV_ONE / attack_steps;
    if (key_hold)                                             /* SF2: the time times 2^((60 - key) * k / 1200) */
        hold_steps = scale_tc(hold_steps, (60 - (int32_t)note) * key_hold);
    v->left = hold_steps;
    v->decay = (uint16_t)rd16(env + 4);
    if (key_decay && v->decay) {                              /* a decay s times as long: the factor to the power 1 / s */
        uint32_t f = exp2neg_q16(scale_tc(neglog2_q24(v->decay), ((int32_t)note - 60) * key_decay));
        v->decay = (uint16_t)(f > 65535U ? 65535U : f ? f : 1U);
    }
    v->sustain = rd16(env + 6) << 8;
    v->release = (uint16_t)rd16(env + 8);
    v->fixed = (uint8_t)(rd16(env + 10) & 1U);
    v->phase = ATTACK;
    if (shown < 0)
        return;
    bank.last_note = (int32_t)note;
    bank.last_drum = (int32_t)drum;
    bank.last_zone = shown;
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
    info->muted = (mute_mask >> channel) & 1U;
    info->solo = solo_channel == (int32_t)channel;
    info->silent = (silent_mask() >> channel) & 1U;
    for (unsigned i = 0; i < FM1B_VOICES; ++i)
        info->voices += voices[i].active && voices[i].channel == channel;
}
void fm1b_channel_mute(unsigned channel, unsigned on)
{
    if (channel < FM1B_CHANNELS)
        mute_mask = on ? mute_mask | 1U << channel : mute_mask & ~(1U << channel);
}
void fm1b_channel_solo(int channel) { solo_channel = channel >= 0 && channel < (int)FM1B_CHANNELS ? channel : -1; }
/* Each channel's fade one block on, before the voices of the block run (the audio side). */
static FM1_DSP_CODE void gates_update(unsigned frames)
{
    uint32_t silent = silent_mask(), step = (SCALE_ONE * frames + GATE_FRAMES - 1U) / GATE_FRAMES;
    for (unsigned c = 0; c < FM1B_CHANNELS; ++c) {
        uint32_t gate = channel_gate[c];
        if ((silent >> c) & 1U)
            gate = gate > step ? gate - step : 0U;
        else if (gate < SCALE_ONE)
            gate = gate + step < SCALE_ONE ? gate + step : SCALE_ONE;
        channel_gate[c] = (uint16_t)gate;
    }
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
    if (v->lfos & 4U)
        menv_step(v);
    if (v->lfos & 12U)
        lfo_step(v);
    if (channel_mod[v->channel] || (v->lfos & 1U)) {          /* vibrato: the step follows the LFOs, once per envelope step */
        if (channel_mod[v->channel])
            v->lfo = (uint16_t)(v->lfo + LFO_STEP);
        retune(v);
    }
    uint32_t scale = channel_scale[v->channel], gate = channel_gate[v->channel];
    if (gate != SCALE_ONE) {                                  /* mute or solo fades it */
        if (!gate && (v->level < 0 ? -v->level : v->level) < ENV_STEP) {   /* faded out (the ramp leaves under ENV_STEP): it stops, so it costs nothing */
            v->active = 0;
            return;
        }
        scale = (scale * gate) >> 14;
    }
    int32_t next = (int32_t)((((int64_t)v->gain * target >> 24) * scale) >> 14);   /* the channel's volume applies live */
    if (v->lfos & 2U)                                         /* tremolo */
        next = (int32_t)(((int64_t)next * v->tremolo) >> 16);
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
#if FM1B_INTERP4
            const int16_t *w = interp4[fraction >> 8];
            int32_t x;
            uint32_t in = (position - 1U) % BFP_GROUP;
            if (format == FORMAT_BFP8 && position && in < BFP_GROUP - 3U && position + 2U < length && (!le || position + 2U < le)) {
                /* the four frames in one group, no loop end among them (29 frames of 32): one shift, after the weighted sum */
                const uint8_t *group = pcm + ((position - 1U) / BFP_GROUP) * BFP_GROUP_BYTES;
                const int8_t *m = (const int8_t *)group + 1 + in;
                unsigned shift = group[0] < 8U ? group[0] : 8U;
                x = ((m[0] * w[0] + m[1] * w[1] + m[2] * w[2] + m[3] * w[3]) * (1 << shift)) >> 10;
            } else {
                uint32_t after = next + 1;                    /* the frame before is the one before in the bank, also at a loop start */
                if (le && after >= le)
                    after = ls;
                int32_t a = sample_at(v, pcm, position, format), b = next < length ? sample_at(v, pcm, next, format) : 0;
                int32_t p = position ? sample_at(v, pcm, position - 1, format) : a, c = after < length ? sample_at(v, pcm, after, format) : 0;
                x = (p * w[0] + a * w[1] + b * w[2] + c * w[3]) >> 10;   /* Q4; the weights' sum of magnitudes under 1.25 */
            }
#else
            int32_t a = sample_at(v, pcm, position, format), b = next < length ? sample_at(v, pcm, next, format) : 0;
            int32_t x = a * 16 + (((b - a) * (int32_t)(fraction >> 1)) >> 11);   /* Q4; (b - a) * 15 bits stays in 32 */
#endif
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
/* A filtered voice: played into a block of its own, through its filter, then added to the mix and, times rg, cg and sg as
 * voice_run does, to the buses it has. */
static int32_t filter_block[FILTER_CHUNK];
static FM1_DSP_CODE __attribute__((noinline)) void voice_filtered(struct voice *v, int32_t *out, int32_t *rev, int32_t *cho, int32_t *side,
                                                                 unsigned frames, int32_t rg, int32_t cg, int32_t sg)
{
    for (unsigned at = 0; at < frames && v->active; at += FILTER_CHUNK) {
        unsigned n = frames - at < FILTER_CHUNK ? frames - at : FILTER_CHUNK;
        memset(filter_block, 0, n * sizeof *filter_block);
        int32_t before = v->level;
        voice_play(v, filter_block, n);
        /* a frame is a sample (Q4, under 2^19) times the level over 2^25: at most the level over 64, and the level only ramps */
        int32_t level = before > v->level ? before : v->level;
        filter_run(v, filter_block, out + at, n, (level < 0 ? -level : level) >> 6, rg || cg || sg);
        if (rg) for (unsigned i = 0; i < n; ++i) rev[at + i] += (filter_block[i] * rg) >> 7;
        if (cg) for (unsigned i = 0; i < n; ++i) cho[at + i] += (filter_block[i] * cg) >> 7;
        if (sg) for (unsigned i = 0; i < n; ++i) side[at + i] += (filter_block[i] * sg) >> 8;
    }
}
FM1_DSP_CODE void fm1b_render(const void *owner, int32_t *out, unsigned frames)
{
    queue_drain();
    gates_update(frames);
    if (!bank.image)
        return;
    for (unsigned n = 0; n < FM1B_VOICES; ++n)
        if (voices[n].active && voices[n].owner == owner) {
            if (voices[n].filtered)
                voice_filtered(&voices[n], out, NULL, NULL, NULL, frames, 0, 0, 0);
            else
                voice_play(&voices[n], out, frames);
        }
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
    gates_update(frames);
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
        if (!buses) {
            if (v->filtered)
                voice_filtered(v, out, NULL, NULL, NULL, frames, 0, 0, 0);
            else
                voice_play(v, out, frames);
        } else {
            int32_t r = voice_reverb(v), c = voice_chorus(v);
            int32_t rg = buses & 1U ? r + (r >> 6) : 0, cg = buses & 2U ? c + (c >> 6) : 0, sg = v->side ? v->side_gain : 0;
            int32_t *side = v->side ? sides[v->side - 1U] : NULL;
            if (v->filtered)
                voice_filtered(v, out, rev, cho, side, frames, rg, cg, sg);
            else
                voice_play_buses(v, out, rev, cho, side, frames, rg, cg, sg);
        }
    }
    return buses;
}
