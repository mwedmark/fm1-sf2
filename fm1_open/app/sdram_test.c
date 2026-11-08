/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* The SDRAM test: see sdram_test.h. The steps are those of a memory test with a known weakness for each:
 *   data bus      a walking one and a walking zero at one word, with the opposite value in the next word, so that a bus which only
 *                 holds the last value it carried (nothing there) reads wrong; finds stuck or shorted data lines;
 *   address bus   a pattern at the words 1, 2, 4, 8 ..., then the opposite at word 0: a line that is stuck, shorted or missing
 *                 makes some of them alias word 0. The first line that does is where the memory ends (its size, a power of two);
 *                 a line that fails with a later one passing is a fault, not a size. Then each of the lines alone against the rest;
 *   cells         every word of the size found gets a value of its own (a hash of its number), is read back, and gets the inverse,
 *                 which is read back at once (a bit stuck at 0 where the value has a 0 only shows against the inverse);
 *   retention     after the wait the inverse is read back again: a refresh that does not work loses it. */
#include "sdram_test.h"

#define PATTERN 0xaaaaaaaaU
#define ANTI 0x55555555U

static uint32_t value_of(uint32_t word) { return word * 0x9e3779b1U ^ (word << 27 | word >> 5) ^ 0x5a5ac3c3U; }
static uint32_t power_of_two_below(uint32_t v)
{
    uint32_t p = 2;
    while ((p << 1) != 0 && (p << 1) <= v)
        p <<= 1;
    return p;
}
static unsigned log2_of(uint32_t power_of_two)
{
    unsigned k = 0;
    while (power_of_two > 1) { power_of_two >>= 1; ++k; }
    return k;
}
static uint32_t get(const struct sdram_test *t, uint32_t word) { return t->ops.read(t->ops.context, word); }
static void put(const struct sdram_test *t, uint32_t word, uint32_t value) { t->ops.write(t->ops.context, word, value); }

void sdram_test_start(struct sdram_test *t, const struct sdram_ops *ops, uint32_t max_words, uint32_t required_words, uint32_t hold_ms)
{
    t->ops = *ops;
    t->max_words = power_of_two_below(max_words);
    t->required_words = power_of_two_below(required_words);
    t->hold_ms = hold_ms;
    t->phase = SDRAM_DATA_BUS;
    t->verdict = SDRAM_RUNNING;
    t->cursor = t->hold_start = t->size_words = 0;
    t->errors = t->late_errors = t->first_error = t->bit_mask = t->address_mask = 0;
}

static void finish(struct sdram_test *t, enum sdram_verdict verdict)
{
    t->verdict = verdict;
    t->phase = SDRAM_DONE;
}

static void data_bus(struct sdram_test *t)
{
    static const uint32_t fixed[4] = {0, 0xffffffffU, PATTERN, ANTI};
    uint32_t mask = 0;
    for (unsigned i = 0; i < 4U + 32U + 32U; ++i) {
        uint32_t p = i < 4 ? fixed[i] : i < 36 ? 1U << (i - 4) : ~(1U << (i - 36));
        put(t, 0, p);
        put(t, 1, ~p);                                        /* a bus that only holds what it carried last reads ~p here, not p */
        mask |= p ^ get(t, 0);
    }
    t->bit_mask |= mask;
    if (mask)
        finish(t, SDRAM_FAIL_DATA_BUS);
    else
        t->phase = SDRAM_ADDRESS_BUS;
}

static void address_bus(struct sdram_test *t)
{
    unsigned lines = log2_of(t->max_words), first = lines, k, j;
    uint32_t fault = 0;
    for (k = 0; k < lines; ++k)
        put(t, 1U << k, PATTERN);
    put(t, 0, ANTI);                                          /* every word that is word 0 under another name now reads ANTI */
    for (k = 0; k < lines; ++k) {
        if (get(t, 1U << k) == PATTERN) {
            if (first < lines)
                fault |= 1U << k;                             /* works although a lower line did not: not a size */
        } else {
            if (first == lines)
                first = k;
            t->address_mask |= 1U << k;
        }
    }
    t->size_words = 1U << first;
    if (first == 0 || fault) {                                /* under two words, or a line that is wrong in the middle of the range */
        finish(t, SDRAM_FAIL_ADDRESS_BUS);                    /* address_mask holds the lines that failed (not the ones that worked after them) */
        return;
    }
    t->address_mask = 0;                                      /* the lines past the size are only missing, not faulty */
    put(t, 0, PATTERN);
    for (k = 0; k < first; ++k)
        put(t, 1U << k, PATTERN);
    for (k = 0; k < first; ++k) {                             /* each line alone against the others: a short shows as a change somewhere else */
        put(t, 1U << k, ANTI);
        if (get(t, 0) != PATTERN)
            fault |= 1U << k;
        for (j = 0; j < first; ++j)
            if (j != k && get(t, 1U << j) != PATTERN)
                fault |= 1U << k | 1U << j;
        put(t, 1U << k, PATTERN);
    }
    if (fault) {
        t->address_mask = fault;
        finish(t, SDRAM_FAIL_ADDRESS_BUS);
        return;
    }
    t->phase = SDRAM_FILL;
    t->cursor = 0;
}

static void record(struct sdram_test *t, uint32_t word, uint32_t got, uint32_t want, unsigned late)
{
    if (!t->errors)
        t->first_error = word;
    ++t->errors;
    t->late_errors += late;
    t->bit_mask |= got ^ want;
}

static void conclude(struct sdram_test *t)
{
    if (t->errors != t->late_errors)
        finish(t, SDRAM_FAIL_CELLS);
    else if (t->late_errors)
        finish(t, SDRAM_FAIL_RETENTION);
    else if (t->size_words < t->required_words)
        finish(t, SDRAM_TOO_SMALL);
    else
        finish(t, SDRAM_PASS);
}

int sdram_test_step(struct sdram_test *t, uint32_t now_ms, uint32_t budget)
{
    if (t->phase == SDRAM_IDLE || t->phase == SDRAM_DONE)
        return 0;
    if (budget < 2)
        budget = 2;                                           /* the check-and-invert step needs a read and a write to go on */
    if (t->phase == SDRAM_DATA_BUS)
        data_bus(t);
    if (t->phase == SDRAM_ADDRESS_BUS)
        address_bus(t);
    while (t->phase != SDRAM_DONE && budget) {
        if (t->phase == SDRAM_FILL) {
            while (t->cursor < t->size_words && budget) {
                put(t, t->cursor, value_of(t->cursor));
                ++t->cursor;
                --budget;
            }
            if (t->cursor == t->size_words) {
                t->phase = SDRAM_CHECK_AND_INVERT;
                t->cursor = 0;
            }
        } else if (t->phase == SDRAM_CHECK_AND_INVERT) {
            while (t->cursor < t->size_words && budget >= 2) {
                uint32_t want = value_of(t->cursor), got = get(t, t->cursor);
                if (got != want)
                    record(t, t->cursor, got, want, 0);
                put(t, t->cursor, ~want);
                ++t->cursor;
                budget -= 2;
            }
            if (t->cursor == t->size_words) {
                t->phase = SDRAM_CHECK_INVERTED;
                t->cursor = 0;
            } else if (budget < 2)
                budget = 0;
        } else if (t->phase == SDRAM_HOLD) {
            if (now_ms - t->hold_start < t->hold_ms)
                return 1;                                     /* nothing touches the memory while the refresh has to keep it */
            t->phase = SDRAM_CHECK_LATE;
        } else {                                              /* SDRAM_CHECK_INVERTED, SDRAM_CHECK_LATE */
            unsigned late = t->phase == SDRAM_CHECK_LATE;
            while (t->cursor < t->size_words && budget) {
                uint32_t want = ~value_of(t->cursor), got = get(t, t->cursor);
                if (got != want)
                    record(t, t->cursor, got, want, late);
                ++t->cursor;
                --budget;
            }
            if (t->cursor == t->size_words) {
                t->cursor = 0;
                if (late)
                    conclude(t);
                else {
                    t->phase = SDRAM_HOLD;
                    t->hold_start = now_ms;
                }
            }
        }
    }
    return t->phase != SDRAM_DONE;
}

unsigned sdram_test_percent(const struct sdram_test *t, uint32_t now_ms)
{
    unsigned shift = log2_of(t->size_words ? t->size_words : 1U);
    switch (t->phase) {
    case SDRAM_FILL: return (t->cursor * 20U) >> shift;
    case SDRAM_CHECK_AND_INVERT: return 20U + ((t->cursor * 20U) >> shift);
    case SDRAM_CHECK_INVERTED: return 40U + ((t->cursor * 20U) >> shift);
    case SDRAM_HOLD: {
        uint32_t waited = now_ms - t->hold_start;
        return 60U + (t->hold_ms && waited < t->hold_ms ? waited * 20U / t->hold_ms : 20U);
    }
    case SDRAM_CHECK_LATE: return 80U + ((t->cursor * 20U) >> shift);
    case SDRAM_DONE: return 100;
    default: return 0;
    }
}

const char *sdram_test_phase_name(enum sdram_phase phase)
{
    switch (phase) {
    case SDRAM_DATA_BUS: return "DATA BUS";
    case SDRAM_ADDRESS_BUS: return "ADDRESS BUS";
    case SDRAM_FILL: return "FILL";
    case SDRAM_CHECK_AND_INVERT: return "CHECK";
    case SDRAM_HOLD: return "WAIT";
    case SDRAM_CHECK_INVERTED: return "CHECK INVERSE";
    case SDRAM_CHECK_LATE: return "CHECK LATE";
    case SDRAM_DONE: return "DONE";
    default: return "";
    }
}

const char *sdram_test_verdict_name(enum sdram_verdict verdict)
{
    switch (verdict) {
    case SDRAM_PASS: return "PASS";
    case SDRAM_FAIL_DATA_BUS: return "FAIL DATA BUS";
    case SDRAM_FAIL_ADDRESS_BUS: return "FAIL ADDRESS BUS";
    case SDRAM_FAIL_CELLS: return "FAIL CELLS";
    case SDRAM_FAIL_RETENTION: return "FAIL RETENTION";
    case SDRAM_TOO_SMALL: return "TOO SMALL";
    default: return "RUNNING";
    }
}
