/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* A model of the SDRAM with faults for test_sdram.js, built freestanding for WebAssembly together with fm1_open/app/sdram_test.c.
 * The model is 2^21 words (8 MB) of cells; `size_words` of them are distinct (above that the address wraps, as a memory with fewer
 * address lines does), and one fault at a time can be switched on. */
#include <stdint.h>
#include "sdram_test.h"

#define EXPORT(name) __attribute__((export_name(name)))
#define WORDS (1U << 21)

enum kind { GOOD, DATA_STUCK, DATA_SHORT, NO_MEMORY, FLOATING_BUS, ADDRESS_STUCK_LOW, ADDRESS_STUCK_HIGH, ADDRESS_SHORT, CELL_STUCK_LOW, NO_REFRESH };

static uint32_t cell[WORDS];
static struct {
    int kind;
    uint32_t size_words, a, b, last_bus;
    int decayed;
    unsigned long accesses;
} model;
static struct sdram_test test;
static unsigned long first_slice, max_slice, slices;
static int percent_monotonic;

static uint32_t index_of(uint32_t word)
{
    uint32_t i = word;
    if (model.kind == ADDRESS_STUCK_LOW)
        i &= ~(1U << model.a);
    else if (model.kind == ADDRESS_STUCK_HIGH)
        i |= 1U << model.a;
    else if (model.kind == ADDRESS_SHORT) {                   /* two lines that carry the same value: wired OR */
        uint32_t bit = ((i >> model.a) | (i >> model.b)) & 1U;
        i = (i & ~((1U << model.a) | (1U << model.b))) | bit << model.a | bit << model.b;
    }
    return i & (model.size_words - 1U);
}

static uint32_t read_word(void *context, uint32_t word)
{
    uint32_t v;
    (void)context;
    ++model.accesses;
    if (model.kind == NO_MEMORY)
        return 0;
    if (model.kind == FLOATING_BUS)
        return model.last_bus;
    v = cell[index_of(word)];
    if (model.kind == DATA_STUCK)
        v = model.b ? v | 1U << model.a : v & ~(1U << model.a);
    else if (model.kind == DATA_SHORT) {                      /* two lines shorted: both read as the AND of the two */
        uint32_t x = (v >> model.a) & (v >> model.b) & 1U;
        v = (v & ~((1U << model.a) | (1U << model.b))) | x << model.a | x << model.b;
    } else if (model.kind == CELL_STUCK_LOW && index_of(word) == model.a)
        v &= ~(1U << model.b);
    else if (model.kind == NO_REFRESH && model.decayed)
        v = 0;
    return v;
}

static void write_word(void *context, uint32_t word, uint32_t value)
{
    (void)context;
    ++model.accesses;
    model.last_bus = value;
    if (model.kind != NO_MEMORY)
        cell[index_of(word)] = value;
}

/* Run a whole test. kind: see the enum; size_log2: the model's distinct words as a power of two; a, b: the fault's parameters (data
 * line, address line, cell and bit); the window to look in and the size required, as powers of two of words. A millisecond passes
 * between slices. Returns the verdict. */
EXPORT("run") int run(int kind, uint32_t size_log2, uint32_t a, uint32_t b, uint32_t max_log2, uint32_t required_log2, uint32_t budget, uint32_t hold_ms)
{
    struct sdram_ops ops = {read_word, write_word, 0};
    uint32_t now = 0, last_percent = 0;
    unsigned long before;
    model.kind = kind; model.size_words = 1U << size_log2; model.a = a; model.b = b;
    model.last_bus = 0xdeadbeefU; model.decayed = 0; model.accesses = 0;
    for (uint32_t i = 0; i < model.size_words; ++i)
        cell[i] = 0x13579bdfU * i;                            /* what a memory powers up with: not the test's values */
    first_slice = max_slice = slices = 0;
    percent_monotonic = 1;
    sdram_test_start(&test, &ops, 1U << max_log2, 1U << required_log2, hold_ms);
    for (;;) {
        unsigned long used;
        int more;
        before = model.accesses;
        more = sdram_test_step(&test, now, budget);
        used = model.accesses - before;
        if (!slices)
            first_slice = used;
        else if (used > max_slice)
            max_slice = used;
        ++slices;
        uint32_t p = sdram_test_percent(&test, now);
        if (p < last_percent || p > 100)
            percent_monotonic = 0;
        last_percent = p;
        if (!more)
            break;
        ++now;
        if (kind == NO_REFRESH && test.phase == SDRAM_HOLD && now - test.hold_start >= 64U)
            model.decayed = 1;                                /* nothing refreshed the cells for 64 ms */
        if (slices > 50000000UL)
            return -1;
    }
    return (int)test.verdict;
}

EXPORT("size_words") uint32_t get_size_words(void) { return test.size_words; }
EXPORT("errors") uint32_t get_errors(void) { return test.errors; }
EXPORT("late_errors") uint32_t get_late_errors(void) { return test.late_errors; }
EXPORT("first_error") uint32_t get_first_error(void) { return test.first_error; }
EXPORT("bit_mask") uint32_t get_bit_mask(void) { return test.bit_mask; }
EXPORT("address_mask") uint32_t get_address_mask(void) { return test.address_mask; }
EXPORT("slices") uint32_t get_slices(void) { return (uint32_t)slices; }
EXPORT("first_slice") uint32_t get_first_slice(void) { return (uint32_t)first_slice; }
EXPORT("max_slice") uint32_t get_max_slice(void) { return (uint32_t)max_slice; }
EXPORT("percent_monotonic") int get_percent_monotonic(void) { return percent_monotonic; }
