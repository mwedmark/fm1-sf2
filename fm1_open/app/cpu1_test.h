/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* A test of the second core's start (CPU1), late, from the info page: ARP starts it once, gives it one job, compares the result with
 * CPU0's and holds it again; SEQ chooses which interrupts are off while it boots: ICFG bit 8 only (fm1_device_irq_save, as the
 * 2026-11-05 test) or bits 8 and 9 (felucca-bsp's irq_save). Nothing in the sound uses it. Only in a build with FM1_CPU1_TEST
 * (build_fw.ps1 -Cpu1Test). PLAN_DUALCORE_CLOCK.md (branch sf2-dualcore), section 7. */
#pragma once
#include <stdint.h>

enum {
    CPU1_NOT_RUN = 0, CPU1_OK = 1,
    CPU1_NO_START = 2,                 /* CPU1 did not report in within 20 ms */
    CPU1_LATE = 3,                     /* its job was not back in time */
    CPU1_BAD_SUM = 4,                  /* its result differs from CPU0's */
    CPU1_MISREAD = 5                   /* it refused the job: its check word did not match (shared RAM misread) */
};
enum { CPU1_MASK_8 = 0, CPU1_MASK_8_9 = 1, CPU1_MASKS = 2 };
struct cpu1_tries {
    uint32_t mask;                     /* CPU1_MASK_*: the next try's */
    uint32_t tries[CPU1_MASKS], ok[CPU1_MASKS];
    uint32_t last_state, last_mask, last_start_us;   /* the last try: CPU1_*, its mask, the us from release to CPU1's report */
};
void cpu1_test_try(struct cpu1_tries *t);

/* The boot start (FM1_CPU1_TEST 2, build_fw.ps1 -Cpu1Boot): in an .early.initcall, before power, Bluetooth, the flash driver, USB and the
 * audio, CPU1 is started only while ARP is held at power-on; it is then left running its RAM loop. ARP on the info page gives it one job. */
struct cpu1_boot {
    uint32_t asked;                    /* ARP was seen at power-on: 1 in the .early.initcall, 2 in the .late.initcall */
    uint32_t state;                    /* CPU1_*: NOT_RUN when not asked */
    uint32_t loops;                    /* the wait's turns until CPU1 reported in (no timer runs that early) */
    uint32_t jobs_tried, jobs_ok;
};
extern struct cpu1_boot cpu1_boot_state;
/* What the panel scan at power-on saw (app/board/panel.c), for the info page: the columns of the 48 whose DMA had finished before the wait
 * ran out, the columns (bit per column 0..10) where any contact read closed, and the buttons the decode ended with. */
struct fm1_boot_scan { uint32_t done, columns, buttons; };
extern struct fm1_boot_scan fm1_boot_scan[2];   /* [0] the .early.initcall's scan, [1] the .late.initcall's */
void cpu1_boot_job(struct cpu1_boot *b);
