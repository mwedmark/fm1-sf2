/* The SF2 player's bank upload (fm1_open/app/sf2_upload.c) on a computer: the device calls it uses are stubs here, the
 * flash is a byte array that behaves like the real one (erase to 0xFF, a write may only clear bits), and the frames and
 * replies go through exported functions. Built to WebAssembly by test_upload.js's build step, run by test_upload.js. */
#include "fm1.h"
#include "fm1b.h"
#include "fm1b_config.h"
#include "fm1_device.h"
#include "sf2_upload.h"
#include <string.h>

#define EXPORT __attribute__((used, visibility("default")))
enum { FLASH_BYTES = FM1B_FLASH_OFFSET + FM1B_FLASH_BYTES };
static uint8_t flash[FLASH_BYTES];
static uint8_t replies[64][16];
static unsigned reply_count, erases, writes, binds, unbinds;
static uint32_t bound_bytes;
static unsigned input_source = FM1_MIDI_USB;
static int fail_write_at = -1;

EXPORT uint8_t *test_flash(void) { return flash; }
EXPORT uint8_t *test_replies(void) { return &replies[0][0]; }
EXPORT unsigned test_reply_count(void) { return reply_count; }
EXPORT unsigned test_counter(unsigned which) { return which == 0 ? erases : which == 1 ? writes : which == 2 ? binds : unbinds; }
EXPORT uint32_t test_bound_bytes(void) { return bound_bytes; }
EXPORT void test_reset(void)
{
    memset(flash, 0xff, sizeof flash);
    reply_count = erases = writes = binds = unbinds = 0;
    bound_bytes = 0;
    fail_write_at = -1;
}
EXPORT void test_clear_replies(void) { reply_count = 0; }
EXPORT void test_fail_write_at(int offset) { fail_write_at = offset; }
EXPORT uint8_t *test_frame_buffer(void) { static uint8_t frame[1100]; return frame; }
EXPORT void test_offer(unsigned bytes) { sf2_upload_offer(test_frame_buffer(), bytes); }
EXPORT void test_poll(void) { sf2_upload_poll(); }

uint32_t fm1_flash_size(void) { return FLASH_BYTES; }
const uint8_t *fm1_flash_map(uint32_t offset, size_t bytes)
{
    return offset <= FLASH_BYTES && bytes <= FLASH_BYTES - offset ? flash + offset : NULL;
}
int fm1_flash_erase(uint32_t offset)
{
    if (offset % FM1_FLASH_SECTOR_BYTES || offset + FM1_FLASH_SECTOR_BYTES > FLASH_BYTES)
        return FM1_FLASH_INVALID;
    memset(flash + offset, 0xff, FM1_FLASH_SECTOR_BYTES);
    ++erases;
    return FM1_FLASH_OK;
}
int fm1_flash_write(uint32_t offset, const void *data, size_t bytes)
{
    if (offset + bytes > FLASH_BYTES)
        return FM1_FLASH_INVALID;
    if (fail_write_at >= 0 && (uint32_t)fail_write_at == offset)
        return FM1_FLASH_IO;
    for (size_t i = 0; i < bytes; ++i) {
        uint8_t v = ((const uint8_t *)data)[i];
        if ((flash[offset + i] & v) != v)
            return FM1_FLASH_NEEDS_ERASE;                     /* a write may only clear bits */
        flash[offset + i] = v;
    }
    ++writes;
    return FM1_FLASH_OK;
}
int fm1_midi_send_port(unsigned port, const uint8_t *message, size_t bytes)
{
    (void)port;
    if (reply_count >= 64 || bytes > 16)
        return -2;
    memcpy(replies[reply_count++], message, bytes);
    return 0;
}
int fm1_midi_send(const uint8_t *message, size_t bytes) { return fm1_midi_send_port(FM1_MIDI_USB, message, bytes); }
unsigned fm1_midi_input_source(void) { return input_source; }
uint32_t fm1_device_irq_save(void) { return 0; }
void fm1_device_irq_restore(uint32_t state) { (void)state; }

int fm1b_bind(const uint8_t *image, unsigned bytes, const int16_t *dqt)
{
    (void)dqt;
    if (!image) { ++unbinds; bound_bytes = 0; return 0; }
    ++binds;
    bound_bytes = bytes;
    return 0;
}
int fm1b_bind_flash(void)
{
    const uint8_t *h = flash + FM1B_FLASH_OFFSET;
    if (memcmp(h, "FM1B", 4))
        return 0;
    uint32_t bytes = (uint32_t)h[40] | (uint32_t)h[41] << 8 | (uint32_t)h[42] << 16 | (uint32_t)h[43] << 24;
    return fm1b_bind(h, bytes, NULL) == 0;
}
