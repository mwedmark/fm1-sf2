/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* The CPU clock switch (sf2clock.h). */
#include "sf2clock.h"
#include "fm1.h"
#include "fm1_device.h"
#include "fm1b.h"
#include "board/fm1_audio.h"

#define REG(address) (*(volatile uint32_t *)(address))
#define SYS_CON REG(0x10014U)        /* [7:0] the system clock's source (6: PLL1 * 2/3); bits 8..11 are the audio clock's (audio.c): never touched */
#define MUX_CON REG(0x1000CU)        /* bit 8: the system clock from the PLL mux, bits 6, 7 the mux's */
#define PLL1_CON0 REG(0x119A8U)      /* [1:0] enable, run; [6:2] reference divider - 2 */
#define PLL1_CON1 REG(0x119ACU)      /* [11:0] multiplier - 2 */
#define HSB_CON REG(0x10008U)        /* [17:16] HSB divisor - 1, [10:8] LSB divisor - 1 (HSB = system / 2, LSB = HSB / 3 as the clocks are set) */
#define SFC_CON REG(0x40200U)        /* bit 31 busy; [23:20] the chip-select-high time in HSB cycles (m-bootloader SPEC 3.6: floor(HSB MHz * 45 / 1000) + 1, ~45 ns) */
#define T4 REG(0x10804U)             /* TIMER4, 24 MHz from the crystal, free running */
enum { MUX_BITS = (1U << 6) | (1U << 7) | (1U << 8), PLL1_RAISED_CON0 = 0x09400228U, PLL1_RAISED_CON1 = 0x0750310CU };

static uint32_t base_sys, base_mux, base_p1c0, base_p1c1, base_cs;
static uint8_t probed, switchable, raised;

#define SYNC() __asm__ volatile("csync" ::: "memory")
static inline __attribute__((always_inline)) void wait_us(uint32_t us)    /* (RAM code calls nothing) */
{
    uint32_t t0 = T4;
    while ((uint32_t)(T4 - t0) < us * 24U)
        ;
}

/* The chip-select-high field of the XIP flash controller, when it is idle (the SDK's flash driver writes SFC_CON the same way). */
static inline __attribute__((always_inline)) void sfc_cs(uint32_t cs)
{
    for (uint32_t i = 0; i < 100000U && (int32_t)SFC_CON < 0; ++i)
        ;
    SFC_CON = (SFC_CON & ~0x00F00000U) | ((cs & 15U) << 20);
    SYNC();
}
/* The steps of fm1_clock_set_stock_ram: the system clock on the crystal while PLL1 starts, then PLL1 * 2/3 and back onto the mux. */
static FM1_AUDIO_CODE void clock_raise_ram(uint32_t cs)
{
    sfc_cs(cs);                             /* the longer chip-select-high first (always safe), then the faster clock */
    MUX_CON &= ~(1U << 8);
    SYNC();
    wait_us(20);
    PLL1_CON0 = PLL1_RAISED_CON0 & ~3U;     /* 24 / 12 * 270 = 540 MHz, stopped */
    PLL1_CON1 = PLL1_RAISED_CON1;
    PLL1_CON0 |= 1U;                        /* on */
    wait_us(100);
    PLL1_CON0 |= 2U;                        /* running */
    wait_us(900);                           /* its lock */
    SYS_CON = (SYS_CON & ~0xFFU) | 6U;
    MUX_CON |= (1U << 6) | (1U << 7);
    MUX_CON |= (1U << 8);
    SYNC();
    wait_us(20);
}
/* Back to what the unit booted at: onto the crystal, the old source, the mux as it was, PLL1 as it was (off). */
static FM1_AUDIO_CODE void clock_restore_ram(uint32_t sys, uint32_t mux, uint32_t p1c0, uint32_t p1c1, uint32_t cs)
{
    MUX_CON &= ~(1U << 8);
    SYNC();
    wait_us(20);
    SYS_CON = (SYS_CON & ~0xFFU) | (sys & 0xFFU);
    MUX_CON = (MUX_CON & ~MUX_BITS) | (mux & MUX_BITS);
    SYNC();
    wait_us(20);
    PLL1_CON0 = p1c0;
    PLL1_CON1 = p1c1;
    SYNC();
    sfc_cs(cs);                             /* the chip-select-high time it had */
}

void sf2clock_probe(void)
{
    if (probed) return;
    probed = 1;
    base_sys = SYS_CON & 0xFFU;
    base_mux = MUX_CON;
    base_p1c0 = PLL1_CON0;
    base_p1c1 = PLL1_CON1;
    base_cs = (SFC_CON >> 20) & 15U;
    /* the known 320 MHz state: the source 2, PLL1 off (both registers 0), the system clock on the mux */
    switchable = base_sys == 2U && !(base_p1c0 & 3U) && (base_mux & (1U << 8));
}
int sf2clock_switchable(void) { return switchable; }
int sf2clock_raised(void) { return raised; }
unsigned sf2clock_mhz(void)
{
    if ((SYS_CON & 0xFFU) == 6U && (MUX_CON & (1U << 8)) && (PLL1_CON0 & 3U) == 3U && ((PLL1_CON0 >> 2) & 31U) + 2U == 12U)
        return 24U / 12U * ((PLL1_CON1 & 0xFFFU) + 2U) * 2U / 3U;
    return 0;
}

/* The two self-tests that decide whether a clock is kept: they must give the same answer at the booted clock and at 360 MHz, or the clock is put
 * back. flash: a CRC32 over the bank read in place (the XIP flash at the new HSB clock and chip-select time); alu: 64-bit multiplies among 16-bit
 * loads (felucca-bsp docs/CPU.md: a bit can come out wrong at 360 MHz), the loads from the bank's own bytes. */
static uint32_t crc32_bytes(const uint8_t *p, uint32_t n)
{
    static const uint32_t nib[16] = {0x00000000U, 0x1DB71064U, 0x3B6E20C8U, 0x26D930ACU, 0x76DC4190U, 0x6B6B51F4U, 0x4DB26158U, 0x5005713CU,
                                     0xEDB88320U, 0xF00F9344U, 0xD6D6A3E8U, 0xCB61B38CU, 0x9B64C2B0U, 0x86D3D2D4U, 0xA00AE278U, 0xBDBDF21CU};
    uint32_t c = 0xFFFFFFFFU;
    for (uint32_t i = 0; i < n; ++i) {
        c ^= p[i];
        c = (c >> 4) ^ nib[c & 15U];
        c = (c >> 4) ^ nib[c & 15U];
    }
    return ~c;
}
#ifndef SF2CLOCK_ALU_TEST
#define SF2CLOCK_ALU_TEST 1         /* 1: the 64-bit multiply self-test (a few hundred bytes of the image; 0 leaves it out) */
#endif
#if SF2CLOCK_ALU_TEST
/* A short loop: 8 passes over 2048 16-bit values, each step two 64-bit products among 16-bit loads (about 1 ms). The index is masked, not divided. */
static __attribute__((noinline)) uint32_t alu_test(const uint8_t *p, uint32_t n)
{
    (void)n;                                                 /* (the caller gives at least 4096 bytes) */
    uint32_t h = 0x12345678U;
    int64_t acc = 0;
    for (unsigned pass = 0; pass < 8; ++pass)
        for (uint32_t i = 0; i < 2048U; ++i) {
            const uint32_t j = (i * 7U + pass) & 2047U;
            const int32_t a = (int16_t)(p[2U * i] | (p[2U * i + 1U] << 8)), b = (int16_t)(p[2U * j] | (p[2U * j + 1U] << 8));
            const int64_t m = (int64_t)a * (int32_t)(h | 1U) + (int64_t)b * (int32_t)((h >> 5) | 1U);
            acc += m;
            h = h * 1664525U + (uint32_t)(m >> 16) + (uint32_t)((uint64_t)acc >> 32) + 1013904223U;
        }
    return h ^ (uint32_t)acc;
}
#endif
struct tests { uint32_t crc, alu; };
static void run_tests(const uint8_t *data, uint32_t bytes, struct tests *t)
{
    t->crc = crc32_bytes(data, bytes);
#if SF2CLOCK_ALU_TEST
    t->alu = alu_test(data, bytes);
#else
    t->alu = 0;
#endif
}

int sf2clock_toggle(const uint8_t *data, uint32_t bytes)
{
    sf2clock_probe();
    if (!switchable) return 1;
    struct tests before = {0, 0}, after = {0, 0};
    const int test = data && bytes >= 4096U;
    if (test && !raised) run_tests(data, bytes, &before);    /* the answers at the booted clock (before the audio is muted) */
    fm1b_split_hold(1);                                      /* nothing shared with the other core meanwhile */
    fm1_audio_suspend();
    const uint32_t flags = fm1_device_irq_save();
    int status = 0;
    if (fm1_audio_timer_check()) {                           /* the audio timer must count the CPU clock: block_ticks follows the clock */
        fm1_device_irq_restore(flags);
        fm1_audio_resume();
        fm1b_split_hold(0);
        return 2;
    }
    if (!raised) {
        /* the XIP flash's chip-select-high time for the HSB clock at 360 MHz: floor(MHz * 45 / 1000) + 1 (m-bootloader SPEC 3.6) */
        const uint32_t hsb_mhz = 360U / (((HSB_CON >> 16) & 3U) + 1U), cs = hsb_mhz * 45U / 1000U + 1U;
        clock_raise_ram(cs > base_cs ? cs : base_cs);
        if (sf2clock_mhz() != 360U) {
            clock_restore_ram(base_sys, base_mux, base_p1c0, base_p1c1, base_cs);
            status = 3;
        } else raised = 1;
    } else {
        clock_restore_ram(base_sys, base_mux, base_p1c0, base_p1c1, base_cs);
        raised = 0;
    }
    fm1_audio_retime();                                      /* block_ticks for the clock it is now (interrupts off: about 2 ms) */
    fm1_device_irq_restore(flags);
    if (raised && test) {                                    /* the same answers at 360 MHz, or back to the booted clock */
        run_tests(data, bytes, &after);
        if (after.crc != before.crc) status = 4;
        else if (after.alu != before.alu) status = 5;
        if (status) {
            const uint32_t f2 = fm1_device_irq_save();
            clock_restore_ram(base_sys, base_mux, base_p1c0, base_p1c1, base_cs);
            raised = 0;
            fm1_audio_retime();
            fm1_device_irq_restore(f2);
        }
    }
    fm1_audio_resume();
    fm1b_split_hold(0);
    return status;
}
