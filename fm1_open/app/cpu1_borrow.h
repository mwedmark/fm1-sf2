/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Borrowing the second core (CPU1) the way the SDK itself does before a flash access: the SDK starts CPU1 at power-on (main, before the
 * scheduler) and leaves it in a worker loop that answers an inter-core interrupt. cpu_enter_sfc_critical(func, priv) on CPU0 sends that
 * interrupt; CPU1 acknowledges, runs func(priv) while CPU0 goes on, and waits in its interrupt until cpu_exit_sfc_critical. Those two
 * functions are local symbols of the SDK's library: build_fw.ps1 -Cpu1Borrow reads their addresses off the link (cpu1_sdk_addr.h).
 * Nothing starts, holds or restarts CPU1. Only in a build with FM1_CPU1_TEST 4. PLAN_DUALCORE_CLOCK.md (branch sf2-dualcore), section 8. */
#pragma once
#include <stdint.h>

enum { BORROW_NOT_RUN = 0, BORROW_OK = 1, BORROW_NOT_UP = 2, BORROW_LATE = 3, BORROW_BAD_SUM = 4 };
struct cpu1_borrow {
    uint32_t tries, ok;
    uint32_t last_state;                 /* BORROW_* */
    uint32_t solo_us, both_us, cpu1_us;  /* the last try: the job on CPU0 alone; CPU0's and CPU1's at once, until both were done; CPU1's own */
};
void cpu1_borrow_try(struct cpu1_borrow *b);
