/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Copyright (c) 2026 珠海笙科智能科技有限公司 (M-VAVE).
 * Connect the FM1 control API to the board scanner. */
#include "fm1.h"
#include "fm1_device.h"
#include "fm1_input.h"
#include "fm1_lcd_hw.h"
#include "lcd.inc"

static uint8_t controls_started;
#if defined(__has_include)
#if __has_include("fm1b_rate.h")
#include "fm1b_rate.h"
#endif
#endif
#if defined(FM1_CPU1_TEST) && FM1_CPU1_TEST == 2
/* The CPU1 boot test (cpu1_test.c, build_fw.ps1 -Cpu1Boot): the panel scanned a few times at power-on, before the timer that scans it
 * later runs (fm1_controls_init starts the scanner again from the beginning); nonzero while button key (0..13) is held. */
int fm1_board_key_held_at_boot(unsigned key)
{
    fm1_input_init();
    for (unsigned i = 0; i < 48U; ++i) {                  /* 48 columns: four scans of the 11, a press counts after two */
        unsigned wait = 100000;
        while (!(FM1_SCAN_REG(0) & (1u << 15)) && --wait) {}
        fm1_input_tick();
    }
    return key < 14U && ((fm1_in.buttons >> key) & 1U);
}
#endif
uint8_t fm1_io_trs_midi_enabled(void) { return 1U; }

void fm1_controls_init(void)
{
    if (!controls_started) {
        /* PB1: battery divider; PB6: master potentiometer. */
        const uint32_t analog = (1U << 1) | (1U << 6);
        FM1_PR(FM1_PB, FM1_DIR) |= analog;
        FM1_PR(FM1_PB, FM1_DIE) &= ~analog;
        FM1_PR(FM1_PB, FM1_PU) &= ~analog;
        FM1_PR(FM1_PB, FM1_PD) &= ~analog;
        fm1_input_init();
        fm1_device_start(fm1_input_tick);
        controls_started = 1U;
    }
}

void fm1_controls_read(struct fm1_controls *state)
{
    if (!state) return;
    struct fm1_controls snapshot;
    uint32_t irq_state = fm1_device_irq_save();
    snapshot.keys = fm1_in.notes;
    snapshot.buttons = fm1_in.buttons;
    unsigned index = FM1_NENC;
    while (index) {
        --index;
        snapshot.encoders[index] = fm1_in.enc_steps[index];
        fm1_in.enc_steps[index] = 0;
    }
    fm1_device_irq_restore(irq_state);
    snapshot.knob = (uint16_t)fm1_device_adc(4U);
    snapshot.battery = (uint16_t)fm1_device_adc(3U);
    *state = snapshot;
}

void fm1_led_set(unsigned key, int on)
{
    fm1_led_key(key, on);
}
