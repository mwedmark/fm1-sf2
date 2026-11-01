/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef SF2_UPLOAD_H
#define SF2_UPLOAD_H
#include <stddef.h>
#include <stdint.h>
/* Offer a complete SysEx frame (from fm1_app_sysex); it is copied and handled by sf2_upload_poll(). */
void sf2_upload_offer(const uint8_t *message, size_t bytes);
/* Control task, every cycle: handle the frame offered, write the flash, answer. */
void sf2_upload_poll(void);
/* For the panel: frames seen, frames taken, the last step, the last status (255 = none yet). */
void sf2_upload_state(unsigned *seen, unsigned *taken, unsigned *cmd, unsigned *status);
#endif
