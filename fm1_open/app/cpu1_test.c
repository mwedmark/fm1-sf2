/* SPDX-License-Identifier: MPL-2.0
 * The start of the second core (CPU1), the hold, the mailbox and its check word are written after felucca-bsp's chip/fm1_cpu1.h,
 * fm1_cpu1_stop.h, fm1_guard.h and fm1_flash_access.h (irq_save) (MPL-2.0): Kerem Kilic (Ellic Studio) from Melodee
 * (github.com/keremimo/melodee), Charles Vestal, Leo Kuroshita (@kurogedelic) of Hugelton Instruments; written against the public
 * JieLi AC79 SDK (Apache-2.0). From the 2026-11-05 test (cpu1_spike.c, branch sf2-dualcore). */
#include "fm1b.h"                                /* FM1_CPU1_TEST comes with fm1b_rate.h */
#if defined(FM1_CPU1_TEST) && FM1_CPU1_TEST && !defined(FM1_HOST_TEST)
#include "cpu1_test.h"
#include "fm1_device.h"

/* One try: CPU1 held, its mailbox cleared, its interrupt bank off; the debug unit's PC limits and the write guard on the top of RAM opened
 * for its boot through the chip's ROM (its entry address goes in the word at 0x01C7FFF8); released; CPU0 waits up to 20 ms for it to
 * report; one job (a checksum in RAM) on both cores, the results compared; CPU1 held again and the limits put back. CPU1 never reads the
 * flash: its entry, its loop and its job are in RAM. No fault handler for CPU1: a fault lands on CPU0 in the SDK's handler (a restart). */
#define REG(address) (*(volatile uint32_t *)(address))
#define DBG_WR_EN REG(0x1EEE240U)            /* writing 0xE7 toggles the unlock */
#define DBG_MSG_CLR REG(0x1EEE248U)
#define DBG_EN REG(0x1EEE340U)
#define C1_CON REG(0x1EEE004U)
#define WR_LIMIT_EN REG(0x1EEE348U)          /* bit 2: the write guard on 0x01C7FD50..0x01C7FFFF */
#define PC_LIMIT0_H REG(0x1EEE380U)
#define PC_LIMIT0_L REG(0x1EEE384U)
#define PC_LIMIT1_H REG(0x1EEE388U)
#define PC_LIMIT1_L REG(0x1EEE38CU)
#define SYS_DIV REG(0x10008U)
#define ENTRY_WORD REG(0x01C7FFF8U)
#define ICFG(i) REG(0x1EEF300U + 4U * (i))   /* CPU1's interrupt bank (CPU0's: 0x1EEF100) */
#define C1_EMU_CON REG(0x1EEF2D0U)
#define T4 REG(0x10804U)                     /* TIMER4, 24 MHz, free running (device.c) */
#define SYNC() __asm__ volatile("csync" ::: "memory")
enum { TICKS_PER_US = 24, JOB_LOOPS = 60000, START_TIMEOUT_US = 20000, JOB_TIMEOUT_US = 30000 };

extern void cpu1_entry(void);
extern uint32_t cpu1_ustack[], cpu1_sstack_top[];
void cpu1_main(void);

typedef uint32_t (*job_fn)(uint32_t);
static struct {                              /* CPU0's words, then CPU1's on a cache line of their own */
    volatile uint32_t request;
    job_fn volatile fn;
    volatile uint32_t arg, check;
    volatile uint32_t ready __attribute__((aligned(32)));
    volatile uint32_t complete, rejected, misread, jobs, result;
} mb __attribute__((aligned(32)));
#define CHECK(fn, arg, request) ((uint32_t)(uintptr_t)(fn) ^ (arg) ^ (request) * 0x9E3779B1U)

/* ICFG on CPU0: bit 8 the global enable the SDK's local_irq_save clears, bit 9 the one cli clears (felucca-bsp's irq_save clears both:
 * bit 8 alone leaves priority-7 sources running). */
static inline uint32_t icfg_read(void) { uint32_t v; __asm__ volatile("%0 = icfg" : "=r"(v) :: "memory"); return v; }
static inline void icfg_write(uint32_t v) { __asm__ volatile("icfg = %0" :: "r"(v) : "memory"); SYNC(); }

static __attribute__((section(".volatile_ram_code"), noinline, used)) uint32_t job_sum(uint32_t n)
{
    uint32_t sum = 0, x = 12345U;
    for (uint32_t i = 0; i < n; ++i) {
        x = x * 1664525U + 1013904223U;
        sum += (x >> 7) ^ i;
    }
    return sum;
}

/* CPU1's loop (RAM): its stack limits, then the mailbox. A misread of request, fn, arg or check fails the check. */
void __attribute__((section(".volatile_ram_code"), noreturn, noinline, used)) cpu1_main(void)
{
    uint32_t lo = (uint32_t)(uintptr_t)cpu1_ustack + 256U, hi = (uint32_t)(uintptr_t)cpu1_sstack_top - 1U, done;
    REG(0x1EEF2D8U) = hi;                    /* the stack limit at CPU1's EMU bank, over both its stacks less the lowest 256 bytes */
    REG(0x1EEF2DCU) = lo;
    REG(0x1EEF2E0U) = hi;
    REG(0x1EEF2E4U) = lo;
    C1_EMU_CON = (C1_EMU_CON & ~((1U << 2) | (0x1FU << 16))) | (1U << 3);
    done = mb.complete;
    SYNC();
    mb.ready = 1;
    SYNC();
    for (;;) {
        uint32_t request = mb.request;
        SYNC();
        if (request == done)
            continue;
        if (request == done + 1U) {
            job_fn fn = mb.fn;
            uint32_t arg = mb.arg;
            if (mb.check == CHECK(fn, arg, request)) {
                mb.result = fn(arg);
                mb.jobs++;
                done = request;
                SYNC();
                mb.complete = request;
                SYNC();
                continue;
            }
        }
        mb.misread = request;
        SYNC();
        mb.rejected++;
        SYNC();
    }
}

/* hold CPU1 (felucca-bsp's fm1_cpu1_stop): the bus traps masked, CPU1 held, the delayed bus-invalid flag given time and cleared */
static void cpu1_stop(void)
{
    uint32_t locked = !(DBG_WR_EN & 1U), en, bus = 0x003F0030U;
    volatile uint32_t n;
    __asm__("" : "+r"(bus));
    if (locked) DBG_WR_EN = 0xE7U;
    en = DBG_EN;
    DBG_EN = en & ~bus;
    SYNC();
    C1_CON |= 2U;
    C1_CON &= ~8U;
    SYNC();
    for (n = 0; n < 256U; n++)
        ;
    DBG_MSG_CLR = bus;
    SYNC();
    DBG_EN = en;
    if (locked && (DBG_WR_EN & 1U)) DBG_WR_EN = 0xE7U;
    SYNC();
}
static void dbg_unlock(void) { if (!(DBG_WR_EN & 1U)) DBG_WR_EN = 0xE7U; }
static void dbg_lock(void) { if (DBG_WR_EN & 1U) DBG_WR_EN = 0xE7U; }
static uint32_t us_since(uint32_t t0) { return (T4 - t0) / TICKS_PER_US; }

void cpu1_test_try(struct cpu1_tries *t)
{
    const uint32_t mask = t->mask < CPU1_MASKS ? t->mask : CPU1_MASK_8;
    uint32_t flags = fm1_device_irq_save(), icfg = 0, div, t0, pc[4], top, i, state = CPU1_NO_START, start_us = 0;
    if (mask == CPU1_MASK_8_9) {             /* bit 9 too, until CPU1 has reported in (or 20 ms): a click in the sound */
        icfg = icfg_read();
        icfg_write(icfg & ~0x300U);
    }
    cpu1_stop();
    mb.request = mb.complete = mb.ready = mb.rejected = mb.misread = mb.jobs = mb.result = 0;
    mb.fn = 0;
    SYNC();
    for (i = 0; i < 32U; i++) ICFG(i) = 0;  /* its interrupt bank: all off (it runs polled) */
    dbg_unlock();
    pc[0] = PC_LIMIT0_L; pc[1] = PC_LIMIT0_H; pc[2] = PC_LIMIT1_L; pc[3] = PC_LIMIT1_H;
    PC_LIMIT0_L = 0U; PC_LIMIT0_H = 0xFFFFFFFFU; PC_LIMIT1_L = 0U; PC_LIMIT1_H = 0xFFFFFFFFU;
    top = WR_LIMIT_EN & 4U;
    WR_LIMIT_EN &= ~4U;
    dbg_lock();
    ENTRY_WORD = (uint32_t)(uintptr_t)cpu1_entry;
    div = SYS_DIV;
    SYS_DIV = div | 8U;
    SYNC();
    C1_CON |= 8U;
    C1_CON &= ~2U;
    t0 = T4;
    while (!mb.ready && us_since(t0) < START_TIMEOUT_US) SYNC();
    start_us = us_since(t0);
    SYS_DIV = div;
    if (mask == CPU1_MASK_8_9)
        icfg_write(icfg);                    /* bit 9 back as it was; bit 8 stays off until the end, as in the 2026-11-05 test */
    if (mb.ready) {
        uint32_t request = 1U, a = job_sum(JOB_LOOPS), b;
        mb.fn = job_sum; mb.arg = JOB_LOOPS; mb.check = CHECK(job_sum, JOB_LOOPS, request);
        SYNC();
        t0 = T4;
        mb.request = request;
        SYNC();
        b = job_sum(JOB_LOOPS);
        while (mb.complete != request && !mb.rejected && us_since(t0) < JOB_TIMEOUT_US) SYNC();
        state = mb.rejected ? CPU1_MISREAD : mb.complete != request ? CPU1_LATE : (mb.result != a || b != a) ? CPU1_BAD_SUM : CPU1_OK;
    }
    cpu1_stop();                             /* held again after every try: the next press is a start of its own */
    mb.ready = 0;
    dbg_unlock();
    DBG_MSG_CLR = 0xFFFFFFFFU;
    PC_LIMIT0_L = pc[0]; PC_LIMIT0_H = pc[1]; PC_LIMIT1_L = pc[2]; PC_LIMIT1_H = pc[3];
    WR_LIMIT_EN |= top;
    dbg_lock();
    fm1_device_irq_restore(flags);
    t->tries[mask]++;
    if (state == CPU1_OK) t->ok[mask]++;
    t->last_state = state; t->last_mask = mask; t->last_start_us = start_us;
}
#endif
