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

/* The browser has CPU to spare; the device does not (measure fm1_audio_irq's peak before raising this). */
#ifdef FM1_HOST_TEST
#define FM1B_VOICES 16U
#else
/* Measured in fm1-emu at 240 MHz with the 32 kHz bank: about 19 % idle plus 4.2 % per sounding voice (8 voices 53 %, 12 voices 68 %). */
#define FM1B_VOICES 8U
#endif

/* Validate and bind a bank image, which must stay in place while bound. image NULL unbinds. dqt is the SEA5
 * quantizer table (512 entries). Returns 0, or -1 for a bank that is not FM1B version 1 or does not fit its size. */
int fm1b_bind(const uint8_t *image, unsigned bytes, const int16_t *dqt);
/* Select the preset a note plays: bank 0 melodic or 128 percussion, program 0..127. Returns -1 if there is none. */
int fm1b_program(unsigned bank, unsigned program);
unsigned fm1b_ready(void);
/* Note on (velocity 1..127) or off (velocity 0) for `owner`, any pointer the caller keeps rendering with.
 * level 0..127 scales the whole player. */
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
/* Controllers 7 (volume), 11 (expression; both follow the General MIDI curve, 127 and 127 is unity), 120 (all sound off),
 * 121 (reset the two levels) and 123 (all notes off) for one channel. */
void fm1b_channel_control(unsigned channel, unsigned controller, unsigned value);
/* How many channels have a preset of their own: 0 while a single preset plays on all of them. */
unsigned fm1b_multitimbral(void);
/* Add `frames` mono Q23 samples to out. Only the voices started by `owner` sound: the browser simulator has several
 * sampler instances, each rendered once per block, and a voice must advance exactly once per block. */
void fm1b_render(const void *owner, int32_t *out, unsigned frames);
/* What the panel shows: the selected preset, the zone the latest note hit (its sample and envelope) and the state of
 * the voice that plays it. Read from the control task; the audio interrupt may change a field while it is copied. */
struct fm1b_status {
    char name[21], drum_name[21];
    uint8_t ready, drums, last_drum, program, note, zone, zones, root, loop, voices, phase, level;  /* level 0..255, phase 0..4 = A H D S R */
    uint8_t channels, program_channel, program_number;  /* channels with a preset of their own; the latest program change (channel 0..15, 255: none) */
    int8_t fine;
    uint8_t lo, hi, sustain_percent;
    uint16_t attack_ms, hold_ms;
    uint32_t length, loop_start, loop_end, rate;
};
void fm1b_status(struct fm1b_status *status);
/* Firmware only: bind the bank compiled into the image (app/fm1b_bank.c), if there is one. */
void fm1b_device_init(void);
/* Firmware only: bind the FM1B bank in the user flash (1), or leave things as they are (0). */
int fm1b_bind_flash(void);
#endif
