/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* The host's "other core" for test_split.js: fm1b_split_begin runs the job at once, before the calling core's share, so the
 * two shares are rendered in the opposite order to the calls and into separate buffers; the output must still be the one core's. */
#include "fm1b.h"
#if FM1B_SPLIT
int fm1b_split_begin(void (*job)(void *), void *arg) { job(arg); return 1; }
int fm1b_split_wait(void) { return 1; }
int fm1b_split_up(void) { return 1; }
void fm1b_split_times(struct fm1b_split_times *t) { t->count = t->handshake_us = t->wait_us = t->job_us = 0; }
#endif
