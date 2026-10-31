#ifndef FM1_HOST_TEST
/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* SF2 player firmware: the whole application. libfm1_engine.a expects the groove_* functions the stock panel,
 * project and sequencer code provides; here they are stubs, and the keys play an FM1B bank (fm1b.c) that is mixed
 * into the engine's output by board/audio.c. Nothing of the stock instrument UI, projects or sequencer is built. */
#include "instrument.h"
#include "fm_patch.h"
#include "fm1b.h"
#include "fm1b_config.h"
#include "tuning_hook.h"
#include "fm1_device.h"
#include <string.h>

/* sdk/Build.ps1 insists that the library's Bluetooth control calls and the tuning/edit hooks are linked, and the linker
 * drops what nothing references. The stock panel referenced them; this table does, and is itself never read. */
extern int groove_project_apply_edit(const struct groove_project *, unsigned) __attribute__((weak));
static const void *const keep_linked[] __attribute__((used)) = {
    fm1_ble_set_role, fm1_ble_scan, fm1_ble_connect, fm1_ble_disconnect, fm1_ble_get_status, fm1_ble_get_device,
    fm1_bluetooth_configure, fm1_bluetooth_get_status,
    groove_tuning_capture, groove_tuning_apply, groove_project_apply_edit,
};

/* Keys (control task) and MIDI input (routing task) both enqueue notes; the queue has one writer, so take turns. */
void sf2player_play(unsigned note, unsigned velocity)
{
    uint32_t flags = fm1_device_irq_save();
    fm1b_note(NULL, note, velocity, 100);
    fm1_device_irq_restore(flags);
}

/* ---- the panel: F3..G5 are 27 keys, bit 0 = F3 (MIDI 53) -------------------------------------------------- */
enum { FIRST_NOTE = 53, KEYS = 27, BACKGROUND = 0x0210 /* dark teal */ };
static uint32_t previous_keys;
static unsigned program;

void groove_screen_init(void)
{
    fm1_controls_init();
    fm1_screen_init();
    fm1_screen_fill(0, 0, FM1_SCREEN_WIDTH, FM1_SCREEN_HEIGHT, BACKGROUND);
    previous_keys = 0;
    program = 0;
}
void groove_screen_control(void)
{
    struct fm1_controls controls;
    fm1_controls_read(&controls);
    (void)groove_master_update(controls.knob);               /* the volume knob; without it the output stays muted */
    if (controls.encoders[0]) {                               /* first encoder: next / previous program */
        int next = (int)program + controls.encoders[0];
        if (next >= 0 && next < 128 && fm1b_program(0, (unsigned)next) == 0)
            program = (unsigned)next;
    }
    uint32_t changed = controls.keys ^ previous_keys;
    for (unsigned key = 0; key < KEYS; ++key) {
        if (!(changed & (1U << key)))
            continue;
        unsigned on = (controls.keys >> key) & 1U;
        sf2player_play(FIRST_NOTE + key, on ? 100U : 0U);
        fm1_led_set(14 + key, (int)on);
    }
    previous_keys = controls.keys;
}

/* ---- the bank --------------------------------------------------------------------------------------------- */
void groove_midi_init(void)
{
#if FM1B_IN_FIRMWARE
    fm1b_device_init();
#endif
}

/* ---- what the library calls and the player does not need ---------------------------------------------------- */
void groove_midi_clock_reset(void) {}
void groove_midi_panic(void) { fm1b_panic(); }
void groove_midi_poll(void) {}
FM1_DSP_CODE void groove_midi_clock(unsigned owner, unsigned tempo) { (void)owner; (void)tempo; }
FM1_DSP_CODE void groove_midi_transport(unsigned owner, uint8_t status) { (void)owner; (void)status; }
FM1_DSP_CODE void groove_midi_track_reset(unsigned track) { (void)track; }
FM1_DSP_CODE unsigned groove_midi_mode(unsigned track) { (void)track; return 0; }
/* The engine reports the notes its tracks play (sequencer, MIDI input): forward them to the player. */
FM1_DSP_CODE void groove_midi_note(unsigned track, unsigned note, unsigned velocity)
{
    (void)track;
    sf2player_play(note, velocity);
}

void groove_fm_library_init(void) {}
FM1_DSP_CODE void groove_fm_program(uint8_t patch[156], unsigned program_number)
{
    (void)program_number;
    memset(patch, 0, 156);                                    /* an all-zero DX7 voice is silent */
}

void groove_project_init(void) {}
void groove_project_poll(void) {}
void groove_project_metadata(const struct groove_project *p) { (void)p; }
void groove_project_name(char name[13]) { memcpy(name, "SF2 PLAYER\0\0", 13); }
struct groove_project *groove_project_scratch(void)
{
    static struct groove_project scratch;
    return &scratch;
}
void groove_samples_init(void) {}
void groove_samples_poll(void) {}
void groove_transfer_init(void) {}
void groove_transfer_poll(void) {}
#endif
