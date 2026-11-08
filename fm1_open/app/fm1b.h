/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Melodic sample player for FM1B banks (see FORMAT.md in the fm1-sf2 repository).
 *
 * The bank is used where it lies (flash map, RAM image): a note is four indexed loads, a SEA5 zone is decoded
 * one 20-frame group at a time with fm1_sea_decode_groups(). No allocation, no OS calls. The same file is built
 * for the browser simulator (called from the sampler track) and meant for the firmware (called from the audio
 * interrupt after fm1_engine_render(); the output is Q23 like the engine's, one channel). */
#ifndef FM1B_H
#define FM1B_H
#include <stdint.h>
/* 1: banks of version 2 whose sounds are MP3 (the live cache) can be played; see the end of this file. Set with -DFM1B_PACKED=1, together with live_cache/ and third_party/mp3. */
#ifndef FM1B_PACKED
#define FM1B_PACKED 0
#endif

/* The same in the browser and on the device. Measured in fm1-emu at 240 MHz, player at 32 kHz, flute, 32 voices: BFP8 bank
 * 42-48 % (63 % while the notes start), SEA6 bank 70-78 % (88 %): RUNNING.md, section 7b. On the device the audio interrupt's
 * own time lowers fm1b_voice_limit() when it runs short (sf2out_load), so this is a ceiling: a SEA6 bank may get fewer. */
#define FM1B_VOICES 32U
/* The rate the player renders at, voices and effects alike. The device renders at the bank's 32 kHz and board/audio.c resamples
 * that to the codec's 44.1 kHz (a voice at its root key then reads one sample a frame); the browser and the tests render at
 * 44.1 kHz. A test may set it (-DFM1B_RATE=32000U). */
#ifndef FM1B_RATE
#ifdef FM1_HOST_TEST
#define FM1B_RATE 44100U
#else
#define FM1B_RATE 32000U
#endif
#endif

/* Validate and bind a bank image, which must stay in place while bound. image NULL unbinds. dqt is the SEA5
 * quantizer table (512 entries); the engine has the SEA6 one. Returns 0, or -1 for a bank that is not FM1B version 1, does
 * not fit its size, or has a SEA code size the engine has no table for. */
int fm1b_bind(const uint8_t *image, unsigned bytes, const int16_t *dqt);
/* Select the preset a note plays: bank 0 melodic or 128 percussion, program 0..127. Returns -1 if there is none. */
int fm1b_program(unsigned bank, unsigned program);
unsigned fm1b_ready(void);
/* Note on (velocity 1..127) or off (velocity 0) for `owner`, any pointer the caller keeps rendering with.
 * level 0..127 scales the whole player. The gain follows velocity squared (the SoundFont default curve). */
void fm1b_note(const void *owner, unsigned note, unsigned velocity, unsigned level);
/* The same for the percussion kit (bank 128), which sounds beside the selected program. -1 if the bank has none. */
void fm1b_drum_note(const void *owner, unsigned note, unsigned velocity, unsigned level);
unsigned fm1b_has_drums(void);
void fm1b_panic(void);

/* Multitimbral use: the 16 MIDI channels each have their own preset, volume and expression. A channel that never got a
 * program change follows the preset fm1b_program() selected, and fm1b_note()/fm1b_drum_note() (the panel, the browser
 * sampler) are a seventeenth channel that always does, so a caller that ignores channels hears what it always heard.
 * Like the notes, these are queued and applied from the audio interrupt, and have the same single writer. */
#define FM1B_CHANNELS 16U
void fm1b_channel_note(unsigned channel, unsigned note, unsigned velocity, unsigned level);
void fm1b_channel_drum_note(unsigned channel, unsigned note, unsigned velocity, unsigned level);
/* Program change: program_bank 0 melodic or 128 percussion (a kit may be played on any channel), program 0..127. A preset
 * the bank does not have leaves the channel as it was. */
void fm1b_channel_program(unsigned channel, unsigned program_bank, unsigned program);
/* Forget the channel's program change: it follows the preset fm1b_program() selected again. */
void fm1b_channel_follow(unsigned channel);
/* 1 if the bank has a preset for program_bank 0 or 128, program 0..127. */
unsigned fm1b_has_program(unsigned program_bank, unsigned program);
/* Controllers for one channel: 1 (modulation wheel: vibrato, 5.5 Hz, up to +-50 cents), 6/38 with 101/100 (RPN 0: the
 * pitch bend range, 2 semitones until set, at most 24), 7 (volume), 11 (expression; both follow the General MIDI curve,
 * 127 and 127 is unity), 64 (sustain pedal), 120 (all sound off), 121 (reset the levels, bend, wheel and pedal) and 123
 * (all notes off), 91 and 93 (the reverb and chorus sends, FM1B_REVERB_SEND and FM1B_CHORUS_SEND until set). */
void fm1b_channel_control(unsigned channel, unsigned controller, unsigned value);
/* The voices that may sound at once, 1..FM1B_VOICES (the default). Lowering it fades the oldest out in about 5 ms; a note over
 * it fades the oldest out. Audio interrupt only, like the render. */
void fm1b_voice_limit(unsigned limit);
unsigned fm1b_voices_sounding(void);
/* Pitch bend, 0..16383 with 8192 the centre, on the notes that sound and the notes to come. */
void fm1b_channel_bend(unsigned channel, unsigned value);
/* One channel, for the panel's channel page. own: a program change gave it a preset of its own (program, kit: from the
 * percussion bank, name); otherwise it plays the panel's preset. notes counts its note-ons (it wraps), voices sound now. */
struct fm1b_channel {
    char name[21];
    uint8_t own, kit, program, volume, expression, mod, pedal, voices, notes, reverb, chorus, pan;   /* pan: CC 10, 64 the centre */
    uint8_t muted, solo, silent;               /* fm1b_channel_mute / fm1b_channel_solo; silent: muted, or another channel is soloed */
    int16_t bend_cents;
};
void fm1b_channel_info(unsigned channel, struct fm1b_channel *info);
/* Mute and solo (the panel's channel page). A silent channel (muted, or another channel soloed) fades out in about 20 ms, its
 * voices then stop (they cost nothing), and its new notes are not started; they still count in fm1b_channel.notes. Back on,
 * it fades in and its next notes sound. solo: the one channel heard, or -1 for none. Kept until the next fm1b_bind(). */
void fm1b_channel_mute(unsigned channel, unsigned on);
void fm1b_channel_solo(int channel);
/* How many channels have a preset of their own: 0 while a single preset plays on all of them. */
unsigned fm1b_multitimbral(void);
/* Add `frames` mono Q23 samples to out, unpanned. Only the voices started by `owner` sound: the browser simulator has several
 * sampler instances, each rendered once per block, and a voice must advance exactly once per block. */
void fm1b_render(const void *owner, int32_t *out, unsigned frames);
/* The same, with the effect sends and pan. While all the sounding voices' channels have the same sends, sends[] gets them and
 * the buses are the dry mix times sends/127: nothing is written to rev or cho. Otherwise the result has bit 0 (reverb) and/or
 * bit 1 (chorus) set and that bus holds each voice times its channel's send (it is cleared first; out is added to).
 * Pan: out stays the unpanned mix (the effects' input). Bit 2 set: sides[0] holds what to take from it for the left channel;
 * bit 3: sides[1] for the right (each cleared first). A clear bit means that channel is out itself. */
unsigned fm1b_render_sends(const void *owner, int32_t *out, int32_t *rev, int32_t *cho, int32_t *sides[2], unsigned frames,
                           uint8_t sends[2]);
#define FM1B_REVERB_SEND 30U
#define FM1B_CHORUS_SEND 20U
/* What the panel shows: the selected preset, the zone the latest note hit (its sample and envelope) and the state of
 * the voice that plays it. Read from the control task; the audio interrupt may change a field while it is copied. */
struct fm1b_status {
    char name[21], drum_name[21];
    uint8_t ready, drums, last_drum, program, note, zone, zones, root, loop, voices, phase, level;  /* level 0..255, phase 0..4 = A H D S R */
    uint8_t channels, program_channel, program_number;  /* channels with a preset of their own; the latest program change (channel 0..15, 255: none) */
    uint8_t voice_limit;                       /* fm1b_voice_limit(): FM1B_VOICES unless the CPU guard has lowered it */
    uint32_t voices_cut;                       /* voices faded out to keep under the limit, since start */
    uint32_t drops_held, drops_tail, drops_guard;   /* what the limit took: sounding notes, releasing notes, the CPU guard's (fm1b_drops_reset) */
    int8_t fine;
    uint8_t lo, hi, sustain_percent;
    uint16_t attack_ms, hold_ms;
    uint32_t length, loop_start, loop_end, rate;
};
void fm1b_status(struct fm1b_status *status);
void fm1b_drops_reset(void);
/* The bound bank as a whole, for the panel's info page: its bytes (NULL: no bank), and the name and date of its info block
 * (FORMAT.md, "Info"), each "" when the bank has none. name has 32 characters at most, date 20. */
struct fm1b_info {
    const uint8_t *image;
    uint32_t bytes;
    char name[33], date[21];
};
void fm1b_info(struct fm1b_info *info);
#if FM1B_PACKED
/* Banks of version 2 whose sounds are an MP3 stream (FORMAT.md, "Calculation: a live cache of decoded sounds"; build with -DFM1B_PACKED=1 and live_cache/,
 * third_party/mp3). Before fm1b_bind(): give the cache its pool (the RAM the decoded sounds live in; it must stay). The audio interrupt only starts notes and
 * reads the cache; the CONTROL TASK must call fm1b_cache_service() every few ms: it answers the notes' requests, drops what no voice plays, and decodes for
 * at most mp3_frames MP3 frames (1152 samples each). Returns the sounds still waiting or being decoded. */
void fm1b_cache_attach(uint8_t *pool, unsigned bytes);
unsigned fm1b_cache_service(unsigned mp3_frames);
struct fm1b_cache_stats {
    uint32_t hits, misses, evictions, no_room, decoded_mp3_frames, requested, lost, waiting, pinned;   /* lost: notes over before their sound came; pinned: pins held now */
};
void fm1b_cache_stats(struct fm1b_cache_stats *stats);
#endif
/* Firmware only: bind the bank compiled into the image (app/fm1b_bank.c), if there is one. */
void fm1b_device_init(void);
/* Firmware only: bind the FM1B bank in the user flash (1), or leave things as they are (0). */
int fm1b_bind_flash(void);
#endif
