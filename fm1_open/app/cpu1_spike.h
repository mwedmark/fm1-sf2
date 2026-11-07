/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* The first test of the FM-1's second core (CPU1): start it, give it a job, compare with CPU0's own run of the same job, stop it.
 * Triggered from the info page (ARP). Nothing in the sound uses it. See PLAN_DUALCORE_CLOCK.md. */
#pragma once
#include <stdint.h>

enum {
    CPU1_NOT_RUN = 0, CPU1_OK = 1,
    CPU1_NO_START = 2,                 /* CPU1 did not report in within 20 ms */
    CPU1_LATE = 3,                     /* a job was not back in time */
    CPU1_BAD_SUM = 4,                  /* its result differs from CPU0's */
    CPU1_MISREAD = 5                   /* it refused a job: its check word did not match (shared RAM misread) */
};
struct cpu1_result {
    uint32_t state;                    /* CPU1_* */
    uint32_t jobs;                     /* the jobs it finished */
    uint32_t solo_us, both_us;         /* the job on CPU0 alone; CPU0 and CPU1 each running one at the same time (best of the rounds) */
    uint32_t sum0, sum1;               /* the two results of the last round */
    uint32_t flash_solo_us, flash_cpu1_us, flash_both_us;   /* the same with a job that reads 8 streams of the bank in flash (like 8 voices): CPU0 alone, CPU1 alone, both at once; 0: no bank */
};
#ifdef FM1_HOST_TEST
static inline void cpu1_spike_run(struct cpu1_result *r) { r->state = CPU1_NOT_RUN; }
#else
void cpu1_spike_run(struct cpu1_result *r);
#endif
