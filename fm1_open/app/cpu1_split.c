/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* The device's other core for FM1B_SPLIT (fm1b.h): the SDK's own CPU1, borrowed through the SDK's hand-over in the audio interrupt.
 * cpu_enter_sfc_critical(on_cpu1, 0) interrupts CPU1, which runs on_cpu1 in its interrupt; as soon as on_cpu1 has started, CPU0 calls
 * cpu_exit_sfc_critical, so CPU0's interrupts are off only for the handshake while CPU1 goes on with the voices and returns from its
 * interrupt when done (the SDK clears its function word in exit: hence the wait for "started"). While CPU1 runs the job it cannot answer the
 * SDK's flash driver, which then waits for it, at most one share of a block. Built with build_fw.ps1 -Cpu1Split, which reads the addresses of
 * those SDK functions (local symbols of its library) off the link into cpu1_sdk_addr.h. PLAN_DUALCORE_CLOCK.md (branch sf2-dualcore), 8. */
#include "fm1b.h"
#if FM1B_SPLIT && !defined(FM1_HOST_TEST)
#include "cpu1_sdk_addr.h"
#include "fm1.h"

#define T4 (*(volatile uint32_t *)0x10804U)     /* TIMER4, 24 MHz, free running (device.c) */
#define SYNC() __asm__ volatile("csync" ::: "memory")
enum { TICKS_PER_US = 24, WAIT_US = 5000 };

typedef void (*sdk_enter_fn)(void (*func)(void *), void *priv);
typedef void (*sdk_exit_fn)(void);
#define SDK_ENTER ((sdk_enter_fn)(uintptr_t)FM1_SDK_CPU_ENTER_SFC_CRITICAL)
#define SDK_EXIT ((sdk_exit_fn)(uintptr_t)FM1_SDK_CPU_EXIT_SFC_CRITICAL)
#define SDK_CPU1_UP (*(volatile uint8_t *)(uintptr_t)(FM1_SDK_IS4BYTE_MODE + 1U))   /* set by CPU1's worker (the SDK's local after is4byte_mode) */

static void (*volatile split_fn)(void *);
static void *volatile split_arg;
static volatile uint32_t started, done, job_ticks;
static struct fm1b_split_times times;
void fm1b_split_times(struct fm1b_split_times *t) { *t = times; }

static FM1_AUDIO_CODE void on_cpu1(void *unused)      /* CPU1, in its interrupt */
{
    (void)unused;
    const uint32_t t0 = T4;
    started = 1;
    SYNC();
    split_fn(split_arg);
    job_ticks = T4 - t0;
    SYNC();
    done = 1;
    SYNC();
}

FM1_AUDIO_CODE int fm1b_split_begin(void (*job)(void *), void *arg)
{
    if (!SDK_CPU1_UP)
        return 0;
    split_fn = job; split_arg = arg; started = 0; done = 0;
    SYNC();
    const uint32_t t0 = T4;
    SDK_ENTER(on_cpu1, 0);
    while (!started) SYNC();
    SDK_EXIT();
    times.handshake_us += (T4 - t0) / TICKS_PER_US;
    return 1;
}

FM1_AUDIO_CODE int fm1b_split_wait(void)
{
    const uint32_t t0 = T4;
    while (!done) {
        if ((T4 - t0) / TICKS_PER_US > WAIT_US)
            return 0;
        SYNC();
    }
    times.wait_us += (T4 - t0) / TICKS_PER_US;
    times.job_us += job_ticks / TICKS_PER_US;
    times.count++;
    return 1;
}
#endif
