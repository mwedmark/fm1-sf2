/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* A test of the SDRAM in the AC7911BA / AC7911BB package (FORMAT.md, "Proposal: packed sample data"), run in slices from the control
 * task so the audio and the screen go on. It finds out what is there (does the data bus work, how many address lines, so how many
 * words) and then checks every cell with a pattern, its inverse, and the inverse again after a wait longer than the refresh period.
 *
 * The memory is reached through `struct sdram_ops`, so the same code is run on the device (plain volatile loads and stores, in
 * sf2player.c) and on the PC against a model with faults (test_sdram.js). No libc, no global state: the caller owns the struct. */
#ifndef FM1_SDRAM_TEST_H
#define FM1_SDRAM_TEST_H
#include <stdint.h>

struct sdram_ops {
    uint32_t (*read)(void *context, uint32_t word);              /* word: the index of a 32-bit word from the start of the window */
    void (*write)(void *context, uint32_t word, uint32_t value);
    void *context;
};

enum sdram_phase {
    SDRAM_IDLE, SDRAM_DATA_BUS, SDRAM_ADDRESS_BUS,
    SDRAM_FILL,               /* write each word's own value */
    SDRAM_CHECK_AND_INVERT,   /* read it back, write the inverse */
    SDRAM_CHECK_INVERTED,     /* read the inverse back at once */
    SDRAM_HOLD,               /* wait, touching nothing */
    SDRAM_CHECK_LATE,         /* read the inverse again */
    SDRAM_DONE
};

/* What the test says when it is done. The first that applies, in this order. */
enum sdram_verdict {
    SDRAM_RUNNING,            /* not done */
    SDRAM_PASS,               /* every cell right, and at least the required size */
    SDRAM_FAIL_DATA_BUS,      /* a data line stuck, shorted, or nothing answers: bit_mask says which bits */
    SDRAM_FAIL_ADDRESS_BUS,   /* an address line stuck or shorted (not just fewer lines than the window) */
    SDRAM_FAIL_CELLS,         /* a cell holds the wrong value right after it was written (in either of the two checks) */
    SDRAM_FAIL_RETENTION,     /* every cell is right at first and wrong after the wait: refresh is not working */
    SDRAM_TOO_SMALL           /* everything works, but there is less than the required size */
};

struct sdram_test {
    struct sdram_ops ops;
    uint32_t max_words;       /* the window to look for memory in: a power of two (8 MB is 1 << 21) */
    uint32_t required_words;  /* what makes it a pass (2 MB is 1 << 19) */
    uint32_t hold_ms;         /* the wait between writing the inverse and checking it (refresh is 64 ms; 3000 shows a missing refresh) */
    enum sdram_phase phase;
    enum sdram_verdict verdict;
    uint32_t cursor;          /* the next word of the phase */
    uint32_t hold_start;      /* now_ms when the wait began */
    uint32_t size_words;      /* found by the address-bus step: the words that are distinct cells */
    uint32_t errors;          /* words that read wrong, over the whole test */
    uint32_t late_errors;     /* of them, those in the check after the wait */
    uint32_t first_error;     /* the word of the first one */
    uint32_t bit_mask;        /* the bits that were ever wrong (data bus step: the lines; later: the cells) */
    uint32_t address_mask;    /* address-bus step: bit k set when line k is stuck or shorted (or, past size_words, missing) */
};

/* Begin. max_words and required_words are rounded down to powers of two (at least 2). */
void sdram_test_start(struct sdram_test *t, const struct sdram_ops *ops, uint32_t max_words, uint32_t required_words, uint32_t hold_ms);

/* One slice: at most `budget` reads and writes, except that the first has the data-bus and address-bus steps in it as well (about
 * 750 accesses for a window of 8 MB, whatever the budget). now_ms is a millisecond clock, differences by unsigned subtraction. Returns 1 while there is more to do, 0 when done. */
int sdram_test_step(struct sdram_test *t, uint32_t now_ms, uint32_t budget);

/* 0..100, by phase: the fill, the two checks, the wait and the late check are a fifth each. */
unsigned sdram_test_percent(const struct sdram_test *t, uint32_t now_ms);

const char *sdram_test_phase_name(enum sdram_phase phase);
const char *sdram_test_verdict_name(enum sdram_verdict verdict);

#endif
