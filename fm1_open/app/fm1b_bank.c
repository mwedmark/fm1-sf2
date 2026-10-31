/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* The firmware's built-in FM1B bank, a stand-in for a bank uploaded to flash. Generate the data with fm1-sf2's
 * make_bank_c.js; without fm1b_bank_data.inc the player simply has no bank. The browser simulator gets its bank
 * from the page instead, so it compiles this to nothing. */
#include "fm1b.h"
#include "fm1b_config.h"
#if !defined(FM1_HOST_TEST) && FM1B_IN_FIRMWARE
#include "fm1b_dqt.inc"
#if __has_include("fm1b_bank_data.inc")
#include "fm1b_bank_data.inc"
void fm1b_device_init(void)
{
    (void)fm1b_bind(fm1b_bank_data, sizeof fm1b_bank_data, fm1b_dqt);
}
#else
void fm1b_device_init(void) {}
#endif
#else
void fm1b_device_init(void) {}
#endif
