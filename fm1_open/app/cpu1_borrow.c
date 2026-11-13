/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* The second core borrowed through the SDK's own hand-over (cpu1_borrow.h). */
#include "fm1b.h"                                /* FM1_CPU1_TEST comes with fm1b_rate.h */
#if defined(FM1_CPU1_TEST) && FM1_CPU1_TEST == 4 && !defined(FM1_HOST_TEST)
#include "cpu1_borrow.h"
#include "cpu1_sdk_addr.h"
#include "fm1.h"

#define T4 (*(volatile uint32_t *)0x10804U)     /* TIMER4, 24 MHz, free running (device.c) */
#define SYNC() __asm__ volatile("csync" ::: "memory")
enum { TICKS_PER_US = 24, JOB_LOOPS = 60000, JOB_TIMEOUT_US = 30000 };

/* The SDK's hand-over, as its flash driver calls it (sdk.elf, 2026-11-13): enter(func, priv) returns once CPU1 has taken the interrupt and
 * is about to run func(priv), with CPU0's interrupts off; exit() lets CPU1 leave its interrupt (after func) and turns CPU0's back on. The
 * flag at is4byte_mode + 1 is the one CPU1's worker sets when it runs; without it enter() would hold CPU1 instead. */
typedef void (*sdk_enter_fn)(void (*func)(void *), void *priv);
typedef void (*sdk_exit_fn)(void);
#define SDK_ENTER ((sdk_enter_fn)(uintptr_t)FM1_SDK_CPU_ENTER_SFC_CRITICAL)
#define SDK_EXIT ((sdk_exit_fn)(uintptr_t)FM1_SDK_CPU_EXIT_SFC_CRITICAL)
#define SDK_CPU1_UP (*(volatile uint8_t *)(uintptr_t)(FM1_SDK_IS4BYTE_MODE + 1U))

static volatile uint32_t job_result, job_done, job_us;

static FM1_AUDIO_CODE uint32_t job_sum(uint32_t n)             /* a checksum that takes a few ms, in RAM, calling nothing */
{
    uint32_t sum = 0, x = 12345U;
    for (uint32_t i = 0; i < n; ++i) {
        x = x * 1664525U + 1013904223U;
        sum += (x >> 7) ^ i;
    }
    return sum;
}
static FM1_AUDIO_CODE void job_on_cpu1(void *arg)             /* runs on CPU1, in its interrupt */
{
    uint32_t t0 = T4;
    job_result = job_sum((uint32_t)(uintptr_t)arg);
    job_us = (T4 - t0) / TICKS_PER_US;
    SYNC();
    job_done = 1;
    SYNC();
}

void cpu1_borrow_try(struct cpu1_borrow *b)
{
    uint32_t n = JOB_LOOPS + b->tries, a, c, t0, solo, both, state;
    b->tries++;
    if (!SDK_CPU1_UP) { b->last_state = BORROW_NOT_UP; return; }
    t0 = T4;
    a = job_sum(n);                                            /* CPU0 alone */
    solo = (T4 - t0) / TICKS_PER_US;
    job_done = 0; job_result = 0; job_us = 0;
    SYNC();
    t0 = T4;
    SDK_ENTER(job_on_cpu1, (void *)(uintptr_t)n);              /* CPU1 starts the job; CPU0 runs the same beside it */
    c = job_sum(n);
    while (!job_done && (T4 - t0) / TICKS_PER_US < JOB_TIMEOUT_US) SYNC();
    both = (T4 - t0) / TICKS_PER_US;
    SDK_EXIT();                                                /* CPU1 back to its worker loop, CPU0's interrupts on */
    state = !job_done ? BORROW_LATE : (job_result != a || c != a) ? BORROW_BAD_SUM : BORROW_OK;
    if (state == BORROW_OK) b->ok++;
    b->last_state = state; b->solo_us = solo; b->both_us = both; b->cpu1_us = job_us;
}
#endif
