/* SPDX-License-Identifier: MPL-2.0
 * The start of the second core (CPU1), the hold, the mailbox and its check word are written after felucca-bsp's chip/fm1_cpu1.h,
 * fm1_cpu1_stop.h and fm1_guard.h (MPL-2.0): Kerem Kilic (Ellic Studio) from Melodee (github.com/keremimo/melodee), Charles Vestal,
 * Leo Kuroshita (@kurogedelic) of Hugelton Instruments; written against the public JieLi AC79 SDK (Apache-2.0). */
#ifndef FM1_HOST_TEST
#include "cpu1_spike.h"
#include "fm1_device.h"
#include "fm1b.h"

/* What this test is, and is not: CPU1 is started the way felucca-bsp does it (debug unit's PC limits and the write guard on the top of
 * RAM opened while it boots through the chip's ROM, its entry address in the word at 0x01C7FFF8), given one job at a time that
 * computes a checksum, and held again. The limits stay open for the whole test (felucca-bsp puts them back as soon as CPU1 reports in)
 * and are restored at the end. There is no fault handler for CPU1 here: if it faults, the exception lands on CPU0 in the SDK's own
 * handler (a crash or a restart). The CPU1 code and its job are in RAM (.volatile_ram_code), so it never fetches from the flash. */
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
enum { TICKS_PER_US = 24, JOB_LOOPS = 60000, ROUNDS = 3, START_TIMEOUT_US = 20000, JOB_TIMEOUT_US = 30000, FLASH_JOB_TIMEOUT_US = 40000 };

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

/* the job: a checksum that takes a few milliseconds (in RAM, with nothing it calls) */
static __attribute__((section(".volatile_ram_code"), noinline, used)) uint32_t job_sum(uint32_t n)
{
    uint32_t sum = 0, x = 12345U;
    for (uint32_t i = 0; i < n; ++i) {
        x = x * 1664525U + 1013904223U;
        sum += (x >> 7) ^ i;
    }
    return sum;
}

/* the flash job: 8 streams of the bank, read a byte at a time like voices read samples, 512 steps each (short: a first version took so long with interrupts off that the unit restarted), 4160 bytes apart, cold for the cache.
 * arg is the address of the 32 KB; CPU0 and CPU1 read regions of their own in the "both" runs */
static __attribute__((section(".volatile_ram_code"), noinline, used)) uint32_t job_flash(uint32_t base)
{
    const volatile uint8_t *p = (const volatile uint8_t *)(uintptr_t)base;
    uint32_t sum = 0;
    for (uint32_t i = 0; i < 64U; ++i)
        for (uint32_t s = 0; s < 8U; ++s)
            sum += p[s * 4160U + i] * (s + 1U) + i;      /* (4160, not 4096: streams a power of two apart fall on the same cache set) */
    return sum;
}

/* CPU1's loop (RAM): its stack limits, then one look at the mailbox after another. Only request, fn, arg and check come from the
 * RAM; a misread of any of them fails the check (felucca-bsp's finding: on some units CPU1 now and then reads shared RAM wrong) */
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

/* hold CPU1 (felucca-bsp's fm1_cpu1_stop): holding it can abort a bus access it has outstanding and that raises a delayed bus-invalid
 * flag on CPU0: the bus traps are masked, CPU1 held, the flag given time to arrive and cleared, then everything put back */
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

void cpu1_spike_run(struct cpu1_result *r)
{
    uint32_t flags = fm1_device_irq_save(), div, t0, pc[4] = {0, 0, 0, 0}, top = 0, i;
    static uint32_t up;                         /* CPU1 is started once and then left running: stopping and starting it again made the unit hang in half the tries */
    uint32_t fresh = !up;
    r->state = CPU1_NO_START; r->jobs = r->solo_us = r->both_us = r->sum0 = r->sum1 = r->flash_solo_us = r->flash_cpu1_us = r->flash_both_us = 0;
    if (fresh) {
    cpu1_stop();
    mb.request = mb.complete = mb.ready = mb.rejected = mb.misread = mb.jobs = mb.result = 0;
    mb.fn = 0;
    SYNC();
    for (i = 0; i < 32U; i++) ICFG(i) = 0;      /* its interrupt bank: all off (it runs polled) */
    dbg_unlock();                               /* the PC limits open while it boots through the ROM, and the protected top of RAM */
    pc[0] = PC_LIMIT0_L; pc[1] = PC_LIMIT0_H; pc[2] = PC_LIMIT1_L; pc[3] = PC_LIMIT1_H;   /* for its entry word */
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
    SYS_DIV = div;
    }
    if (mb.ready) {
        uint32_t best_solo = 0xFFFFFFFFU, best_both = 0xFFFFFFFFU;
        r->state = CPU1_OK;
        for (unsigned round = 0; round < ROUNDS && r->state == CPU1_OK; ++round) {
            uint32_t arg = JOB_LOOPS + round, request = mb.request + 1U, a, b;
            t0 = T4;
            a = job_sum(arg);                    /* CPU0 alone */
            uint32_t solo = us_since(t0);
            mb.fn = job_sum; mb.arg = arg; mb.check = CHECK(job_sum, arg, request);
            SYNC();
            t0 = T4;
            mb.request = request;                /* CPU1 starts, and CPU0 runs the same job beside it */
            SYNC();
            b = job_sum(arg);
            while (mb.complete != request && !mb.rejected && us_since(t0) < JOB_TIMEOUT_US) SYNC();
            uint32_t both = us_since(t0);
            if (mb.rejected) { r->state = CPU1_MISREAD; break; }
            if (mb.complete != request) { r->state = CPU1_LATE; break; }
            r->sum0 = b; r->sum1 = mb.result;
            if (mb.result != a || b != a) { r->state = CPU1_BAD_SUM; break; }
            if (solo < best_solo) best_solo = solo;
            if (both < best_both) best_both = both;
        }
        if (r->state == CPU1_OK) { r->solo_us = best_solo; r->both_us = best_both; }
        struct fm1b_info bank;
        fm1b_info(&bank);
        enum { GAP = 40960 + 1056 };            /* region k at k * GAP: 1056 bytes off a power of two, so no two regions share cache sets */
        if (r->state == CPU1_OK && bank.image && bank.bytes >= 9U * GAP) {   /* the flash job: two rounds on fresh (cold) regions of the bank */
            uint32_t f0 = 0xFFFFFFFFU, f1 = 0xFFFFFFFFU, fboth = 0xFFFFFFFFU;
            fm1_device_irq_restore(flags);      /* interrupts on during the flash job (the earlier packages restarted with them off); it is short now */
            for (unsigned round = 0; round < 2U && r->state == CPU1_OK; ++round) {
                uint32_t b0 = (uint32_t)(uintptr_t)bank.image + 4U * round * GAP, a, b, c, request;
                t0 = T4;
                a = job_flash(b0);                                   /* CPU0 alone, region 0 */
                uint32_t solo0 = us_since(t0);
                request = mb.request + 1U;                           /* CPU1 alone, region 1: CPU0 waits */
                mb.fn = job_flash; mb.arg = b0 + GAP; mb.check = CHECK(job_flash, mb.arg, request);
                SYNC();
                t0 = T4;
                mb.request = request;
                SYNC();
                while (mb.complete != request && !mb.rejected && us_since(t0) < FLASH_JOB_TIMEOUT_US) SYNC();
                uint32_t solo1 = us_since(t0);
                if (mb.rejected) { r->state = CPU1_MISREAD; break; }
                if (mb.complete != request) { r->state = CPU1_LATE; break; }
                c = mb.result;
                request = mb.request + 1U;                           /* both: CPU1 region 3, CPU0 region 2 */
                mb.fn = job_flash; mb.arg = b0 + 3U * GAP; mb.check = CHECK(job_flash, mb.arg, request);
                SYNC();
                t0 = T4;
                mb.request = request;
                SYNC();
                b = job_flash(b0 + 2U * GAP);
                while (mb.complete != request && !mb.rejected && us_since(t0) < FLASH_JOB_TIMEOUT_US) SYNC();
                uint32_t both = us_since(t0);
                if (mb.rejected) { r->state = CPU1_MISREAD; break; }
                if (mb.complete != request) { r->state = CPU1_LATE; break; }
                if (c != job_flash(b0 + GAP) || mb.result != job_flash(b0 + 3U * GAP) || a == 0xFFFFFFFFU || b == 0xFFFFFFFFU) { r->state = CPU1_BAD_SUM; break; }
                if (solo0 < f0) f0 = solo0;
                if (solo1 < f1) f1 = solo1;
                if (both < fboth) fboth = both;
            }
            flags = fm1_device_irq_save();
            if (r->state == CPU1_OK) { r->flash_solo_us = f0; r->flash_cpu1_us = f1; r->flash_both_us = fboth; }
        }
        r->jobs = mb.jobs;
    }
    if (r->state != CPU1_OK) { cpu1_stop(); mb.ready = 0; up = 0; }       /* a failure: held again, the next try starts afresh */
    else up = 1;                                /* a success: left running, polling the mailbox */
    if (fresh) {
        dbg_unlock();                           /* what the start opened, back as it was (with CPU1 running) */
        DBG_MSG_CLR = 0xFFFFFFFFU;
        PC_LIMIT0_L = pc[0]; PC_LIMIT0_H = pc[1]; PC_LIMIT1_L = pc[2]; PC_LIMIT1_H = pc[3];
        WR_LIMIT_EN |= top;
        dbg_lock();
    }
    fm1_device_irq_restore(flags);
}
#endif
