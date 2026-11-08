/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* The bank the firmware plays. First choice is an FM1B bank in the user flash (where the stock firmware keeps its
 * samples: FM1B_FLASH_OFFSET, up to FM1B_FLASH_BYTES), used in place through fm1_flash_map(); the whole GM bank of a
 * SoundFont fits there, which the firmware image does not. Without one, the bank compiled into the image
 * (fm1b_bank_data.inc, from fm1-sf2's make_bank_c.js) is used if there is one. The browser simulator gets its bank
 * from the page instead, so it compiles this to nothing. */
#include "fm1b.h"
#include "fm1b_config.h"
#if !defined(FM1_HOST_TEST) && FM1B_IN_FIRMWARE
#include "fm1.h"
#include "fm1_device.h"
#include "fm1b_dqt.inc"
#include <string.h>
#if __has_include("fm1b_bank_data.inc")
#include "fm1b_bank_data.inc"
#define HAVE_BUILT_IN 1
#endif

#if FM1B_PACKED
/* The live cache's pool: static, so the link's runtime heap (firmware-report.json, runtime_heap_bytes) is what is left of the SRAM after it. */
#ifndef FM1B_CACHE_KB
#define FM1B_CACHE_KB 96
#endif
static uint8_t cache_pool[FM1B_CACHE_KB * 1024U] __attribute__((aligned(4)));
#endif

/* Bind the bank in flash: 1 if there is a usable one, else 0. A bank is trusted for its magic, version and size only: fm1b_bind() checks the rest. */
int fm1b_bind_flash(void)
{
    const uint8_t *header = fm1_flash_map(FM1B_FLASH_OFFSET, 48);
    if (!header || memcmp(header, "FM1B", 4))
        return 0;
    uint32_t bytes = (uint32_t)header[40] | (uint32_t)header[41] << 8 | (uint32_t)header[42] << 16 | (uint32_t)header[43] << 24;
    if (bytes < 48 || bytes > FM1B_FLASH_BYTES)
        return 0;
    const uint8_t *image = fm1_flash_map(FM1B_FLASH_OFFSET, bytes);
    return image && fm1b_bind(image, bytes, fm1b_dqt) == 0;
}

void fm1b_device_init(void)
{
#if FM1B_PACKED
    fm1b_cache_attach(cache_pool, sizeof cache_pool);
#endif
}

/* Control task: the player binds the bank once it knows the FM-1 was not started in safe mode (sf2player.c). */
void fm1b_device_bind(void)
{
    if (fm1b_bind_flash())
        return;
#ifdef HAVE_BUILT_IN
    (void)fm1b_bind(fm1b_bank_data, sizeof fm1b_bank_data, fm1b_dqt);
#endif
}
#else
void fm1b_device_init(void) {}
void fm1b_device_bind(void) {}
int fm1b_bind_flash(void) { return 0; }
#endif
