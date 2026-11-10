/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Bank upload over MIDI SysEx for the SF2 player firmware (see the protocol below). Kept apart from transfer.c, which must
 * stay a small file built without LTO, so that it also builds on a computer: fm1-sf2's test_upload.js runs it on WebAssembly
 * against a flash that is a byte array. */
#include "fm1.h"
#include "fm1b.h"
#include "fm1b_config.h"
#include "fm1_device.h"
#include "sf2_upload.h"
#include "song.h"
/* test_upload.js builds this file on a computer without song.c */
#if FM1_MIDIPLAY && !defined(FM1_HOST_TEST)
#define SONG_UPLOAD 1
#else
#define SONG_UPLOAD 0
#endif
#include <stddef.h>
#include <string.h>

/* ---- bank upload ------------------------------------------------------------------------------------------ */
/* A bank (FM1B file) is written to the user flash over MIDI SysEx, so that a bank too big for the firmware image can
 * be sent from a web page (fm1-sf2's index.html, Web MIDI). One request at a time, each answered once handled:
 *   request  F0 7D 'S' 'F' 02 cmd ... F7        reply  F0 7D 'S' 'F' 02 (cmd | 0x40) status F7
 *   cmd 1 begin: size (5 x 7 bits, least significant first). The bank stops playing and the flash it uses is erased.
 *   cmd 2 data:  offset (5 x 7 bits), then up to 448 bytes packed 7 to 8: a byte with the 7 high bits, then the 7
 *                low parts. Written where it says; the host sends them in order and waits for each reply.
 *   cmd 3 end:   CRC-32 of the whole bank (5 x 7 bits). When it matches, the bank is bound and plays.
 *   cmd 9 check: size, then CRC-32 (5 x 7 bits each). 0 when the flash holds exactly that bank and it plays, else 4: the page
 *                then skips the upload. (Firmware before it answers 1, as for any unknown command.)
 * status: 0 done, 1 malformed, 2 outside the bank's flash, 3 flash error, 4 CRC differs, 5 no begin first.
 * With FM1_MIDIPLAY a song (song.c) goes the same way into RAM, and plays from there:
 *   cmd 4 song begin: size (5 x 7 bits). A song playing stops, the old one is freed and RAM is taken for the new one.
 *   cmd 5 song data:  as cmd 2.     cmd 6 song end: CRC-32, as cmd 3; the song is checked and is ready for PLAY.
 *   cmd 7 play (from the start), cmd 8 stop: F0 7D S F 02 cmd 00 F7.
 *   status 2 also: too big (FM1_MIDIPLAY_MAX_BYTES); 4 also: not a song; 6 no RAM for it.
 * The callback only copies the frame (no flash access from it); the control task does the work in sf2_upload_poll(). */
enum { UPLOAD_FRAME = 640, UPLOAD_CHUNK = 448 };
static struct { uint8_t bytes[UPLOAD_FRAME], source; volatile unsigned length; } upload_in;
static uint32_t upload_size;
static unsigned upload_active;
/* What the panel shows: SysEx frames the callback saw, frames of this protocol taken, the last command and its status. */
static volatile uint16_t upload_seen, upload_taken;
static volatile uint8_t upload_last_cmd, upload_last_status = 255, upload_stage;
void sf2_upload_state(unsigned *seen, unsigned *taken, unsigned *cmd, unsigned *status)
{
    *cmd = upload_stage;
    *seen = upload_seen;
    *taken = upload_taken;
    *status = upload_last_status;
}

void sf2_upload_offer(const uint8_t *m, size_t n)
{
    ++upload_seen;
    if (!m || n < 8 || n > sizeof upload_in.bytes || m[0] != 0xf0 || m[n - 1] != 0xf7 || m[1] != 0x7d || m[2] != 'S' ||
        m[3] != 'F' || m[4] != 2 || upload_in.length)
        return;
    ++upload_taken;
    upload_in.source = (uint8_t)fm1_midi_input_source();
    memcpy(upload_in.bytes, m, n);
#ifndef FM1_HOST_TEST
    __asm__ volatile("csync" ::: "memory");
#endif
    upload_in.length = (unsigned)n;                           /* sf2_upload_poll() looks every control cycle */
}

static void upload_reply(unsigned cmd, unsigned status)
{
    const uint8_t reply[] = {0xf0, 0x7d, 'S', 'F', 2, (uint8_t)(cmd | 0x40U), (uint8_t)status, 0xf7};
    if (upload_in.source <= FM1_MIDI_BLE)
        (void)fm1_midi_send_port(upload_in.source, reply, sizeof reply);
    else
        (void)fm1_midi_send(reply, sizeof reply);
}
__attribute__((noinline)) static uint32_t read35(const uint8_t *p)   /* not inlined: the live-cache image is tight */
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 7 | (uint32_t)p[2] << 14 | (uint32_t)p[3] << 21 | (uint32_t)p[4] << 28;
}
__attribute__((noinline)) static uint32_t crc32_flash(uint32_t bytes)   /* called twice: one copy */
{
    static const uint32_t nibble[16] = {0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
                                        0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c, 0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c};
    uint32_t crc = 0xffffffffU;
    for (uint32_t at = 0; at < bytes;) {
        uint32_t part = bytes - at > 4096U ? 4096U : bytes - at;
        const uint8_t *data = fm1_flash_map(FM1B_FLASH_OFFSET + at, part);
        if (!data)
            return 0;
        for (uint32_t i = 0; i < part; ++i) {
            crc ^= data[i];
            crc = (crc >> 4) ^ nibble[crc & 15U];
            crc = (crc >> 4) ^ nibble[crc & 15U];
        }
        at += part;
    }
    return ~crc;
}
static void upload_stop_playing(void)
{
#if SONG_UPLOAD
    song_stop();                                                 /* no song plays into a bank that is being written */
#endif
    uint32_t flags = fm1_device_irq_save();
    (void)fm1b_bind(NULL, 0, NULL);
    fm1_device_irq_restore(flags);
}

/* A data frame's payload (after the offset), 7 to 8: a byte with the 7 high bits, then the 7 low parts. 0: too long. */
static uint8_t data[UPLOAD_CHUNK];                               /* not on the control task's stack */
static unsigned unpack(const uint8_t *m, unsigned n, unsigned *count)
{
    *count = 0;
    for (const uint8_t *p = m + 11; p < m + n - 1;) {
        unsigned high = *p++, group = (unsigned)(m + n - 1 - p);
        group = group > 7 ? 7 : group;
        if (*count + group > sizeof data)
            return 0;
        for (unsigned i = 0; i < group; ++i)
            data[(*count)++] = (uint8_t)(p[i] | ((high >> i) & 1U) << 7);
        p += group;
    }
    return 1;
}

void sf2_upload_poll(void)
{
    if (!upload_in.length)
        return;
    const uint8_t *m = upload_in.bytes;
    unsigned n = upload_in.length, cmd = m[5], status = 1;
    if (cmd == 1 && n == 12) {                                   /* F0 7D S F 02 01 size*5 F7 */
        uint32_t size = read35(m + 6);
        status = 2;
        if (size >= 48 && size <= FM1B_FLASH_BYTES) {
            upload_stop_playing();
            status = 0;
            for (uint32_t at = 0; at < size && !status; at += FM1_FLASH_SECTOR_BYTES)
                if (fm1_flash_erase(FM1B_FLASH_OFFSET + at) != FM1_FLASH_OK)
                    status = 3;
            upload_size = status ? 0 : size;
            upload_active = !status;
        }
    } else if (cmd == 2 && n >= 12 + 2) {                        /* F0 7D S F 02 02 offset*5 packed F7 */
        status = upload_active ? 0 : 5;
        uint32_t offset = read35(m + 6);
        unsigned count = 0;
        if (!status && !unpack(m, n, &count))
            status = 1;
        if (!status && (!count || offset > upload_size || count > upload_size - offset))
            status = 2;
        upload_stage = 10;
        if (!status && fm1_flash_write(FM1B_FLASH_OFFSET + offset, data, count) != FM1_FLASH_OK)
            status = 3;
        upload_stage = 11;
    } else if (cmd == 9 && n == 17) {                            /* F0 7D S F 02 09 size*5 crc*5 F7 */
        uint32_t size = read35(m + 6);
        const uint8_t *h = fm1_flash_map(FM1B_FLASH_OFFSET, 48);  /* the bank's own size (header bytes 40..43, LE) must be that size */
        uint32_t own = 0;
        if (h)
            memcpy(&own, h + 40, 4);
        status = fm1b_ready() && own == size && size <= FM1B_FLASH_BYTES && crc32_flash(size) == read35(m + 11) ? 0 : 4;   /* mid-upload: CRC differs */
    } else if (cmd == 3 && n == 12) {
        status = upload_active ? 0 : 5;
        if (!status) {
            upload_active = 0;
            status = crc32_flash(upload_size) == read35(m + 6) ? 0 : 4;
            if (!status && !fm1b_bind_flash())
                status = 1;
        }
    }
#if SONG_UPLOAD
    else if (cmd == 4 && n == 12)
        status = song_begin(read35(m + 6));
    else if (cmd == 5 && n >= 12 + 2) {
        unsigned count = 0;
        status = unpack(m, n, &count) ? song_write(read35(m + 6), data, count) : 1;
    } else if (cmd == 6 && n == 12)
        status = song_end(read35(m + 6));
    else if (cmd == 7 && n == 8) {
        struct song_state st;
        song_state(&st);
        status = st.ready ? 0 : 5;
        song_play();
    } else if (cmd == 8 && n == 8) {
        song_stop();
        status = 0;
    }
#endif
    upload_last_cmd = (uint8_t)cmd;
    upload_last_status = (uint8_t)status;
    upload_stage = 20;
    upload_reply(cmd, status);
    upload_stage = 21;
    upload_in.length = 0;
}

