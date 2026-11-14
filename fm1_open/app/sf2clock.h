/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* A switch for the CPU clock, from the info page (EDIT): the clock the unit booted at (320 MHz under m-bootloader 1.2: the system clock's
 * source 2, PLL1 off) or 360 MHz, the clock M-VAVE's own bootloader leaves (PLL1 = 24 / 12 * 270 = 540 MHz, the system clock 2/3 of it).
 * The registers and the order of the steps are read off felucca-bsp's chip/fm1_clock.h (a description of what m-bootloader's hal_clock_a
 * does), written again here. NOT saved: every power-on starts at the clock the unit booted at, so a hang is cured by switching off and on.
 * The change itself runs from RAM with interrupts off and the audio muted (about 1.1 ms with the CPU on the 24 MHz crystal). Before it
 * the audio timer is checked against TIMER4 (it must count the CPU clock, block_ticks depends on it); after it block_ticks is measured again.
 * PLAN_DUALCORE_CLOCK.md (branch sf2-dualcore), section 4. */
#ifndef SF2CLOCK_H
#define SF2CLOCK_H
#include <stdint.h>

/* Reads the registers once (call early, harmless): what the unit booted at. */
void sf2clock_probe(void);
/* 1: this unit booted in the known 320 MHz state and the switch is offered; 0: it is not (shown only). */
int sf2clock_switchable(void);
/* The system clock in MHz from the registers when it runs on PLL1 * 2/3, else the booted value's measured rate (0: not known). */
unsigned sf2clock_mhz(void);
/* 1 while at 360 MHz because of sf2clock_toggle. */
int sf2clock_raised(void);
/* Switch between the booted clock and 360 MHz. Control task only. data/bytes: the bank (read in place) for the two self-tests that decide whether
 * 360 MHz is kept; the XIP flash's chip-select-high time is set for the new HSB clock first. Returns 0 done, or: 1 not offered, 2 the audio timer does
 * not count the CPU clock (nothing changed), 3 the registers did not read back, 4 the flash read differently at 360 MHz (CRC32 of the bank), 5 the
 * 64-bit multiplies gave another answer at 360 MHz (3, 4 and 5: the booted clock is put back). */
int sf2clock_toggle(const uint8_t *data, uint32_t bytes);
#endif
