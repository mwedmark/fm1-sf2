#ifndef FM1_HOST_TEST
/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* The MIDI file player (FM1_MIDIPLAY): song.html turns a MIDI file into a song (fm1_song.js), sends it over SysEx into RAM
 * (sf2_upload.c, commands 4..8), and PLAY plays it from the control task, through fm1_app_midi_received() like MIDI input
 * (transfer.c). The song is gone when the FM-1 is switched off.
 *
 * The song ("FM1S", little endian):
 *   0   'F' 'M' '1' 'S'
 *   4   u32 events
 *   8   u32 length in ms (the time of the last event)
 *   12  char name[32], zero padded
 *   44  the events: the time since the previous one in ms (7 bits a byte, most significant first, the high bit set on all
 *       but the last byte), then a channel message with its status byte: 2 bytes for C0..DF, 3 for the others.
 * song_end() checks every event before the song may play, so playing needs no bounds checks of its own. */
#include "song.h"
#if FM1_MIDIPLAY
#include "fm1.h"
#include "fm1b.h"
#include <stdlib.h>
#include <string.h>

void sf2player_channel_follow(unsigned channel);

enum { SONG_HEADER = 44, ROOM_KEPT = 8 /* queue places left for MIDI input and the keys */ };
static uint8_t *song;                      /* malloc()ed by song_begin */
static uint32_t song_bytes, song_events, song_length;
static unsigned song_ready, song_playing;
static uint32_t song_at;                   /* the next event's message */
static uint32_t song_due;                  /* its time in ms from the start */
static uint32_t song_started;              /* fm1_millis() at the start */
static unsigned chores;                    /* reset messages still to send before the first event (song_play) */
static char song_name[33];

static uint32_t read32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static unsigned message_bytes(unsigned status) { return status >= 0xc0U && status < 0xe0U ? 2U : 3U; }
/* The delta time at song[*at], at most 4 bytes; 0xffffffff when it does not end inside the song. */
static uint32_t read_delta(uint32_t *at)
{
    uint32_t v = 0;
    for (unsigned n = 0; n < 4 && *at < song_bytes; ++n) {
        unsigned b = song[(*at)++];
        v = v << 7 | (b & 127U);
        if (!(b & 128U))
            return v;
    }
    return 0xffffffffU;
}

static uint32_t crc32_bytes(const uint8_t *p, uint32_t bytes)
{
    static const uint32_t nibble[16] = {0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
                                        0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c, 0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c};
    uint32_t crc = 0xffffffffU;
    while (bytes--) {
        crc ^= *p++;
        crc = (crc >> 4) ^ nibble[crc & 15U];
        crc = (crc >> 4) ^ nibble[crc & 15U];
    }
    return ~crc;
}

unsigned song_begin(uint32_t bytes)
{
    song_stop();
    free(song);
    song = NULL;
    song_ready = 0;
    song_bytes = 0;
    if (bytes < SONG_HEADER || bytes > FM1_MIDIPLAY_MAX_BYTES)
        return 2;
    song = malloc(bytes);
    if (!song)
        return 6;
    song_bytes = bytes;
    return 0;
}
unsigned song_write(uint32_t offset, const uint8_t *data, uint32_t count)
{
    if (!song || song_ready)
        return 5;
    if (!count || offset > song_bytes || count > song_bytes - offset)
        return 2;
    memcpy(song + offset, data, count);
    return 0;
}
unsigned song_end(uint32_t crc)
{
    if (!song || song_ready)
        return 5;
    if (crc32_bytes(song, song_bytes) != crc || memcmp(song, "FM1S", 4))
        return 4;
    uint32_t events = 0, at = SONG_HEADER;
    while (at < song_bytes) {                                 /* every event fits and is a channel message */
        if (read_delta(&at) == 0xffffffffU || at >= song_bytes)
            return 1;
        unsigned status = song[at], n = message_bytes(status);
        if (status < 0x80U || status >= 0xf0U || n > song_bytes - at)
            return 1;
        for (unsigned i = 1; i < n; ++i)
            if (song[at + i] >= 0x80U)
                return 1;
        at += n;
        ++events;
    }
    song_events = events;
    song_length = read32(song + 8);
    memcpy(song_name, song + 12, 32);
    song_name[32] = 0;
    for (unsigned i = 0; song_name[i]; ++i)                    /* the panel's font: printable ASCII only */
        if (song_name[i] < 32 || song_name[i] > 126) song_name[i] = ' ';
    song_ready = 1;
    return 0;
}

/* Before the first event every channel starts as after power on: controllers reset (121), bank 0, pan centre, the default
 * sends, and the panel's preset (a program change in the song gives it its own). Seven messages a channel; they go out
 * as fast as the player's queue takes them. */
enum { CHORES_PER_CHANNEL = 7 };
static void chore(unsigned n)
{
    unsigned channel = n / CHORES_PER_CHANNEL, step = n % CHORES_PER_CHANNEL;
    static const uint8_t controls[CHORES_PER_CHANNEL - 1][2] = {{121, 0}, {0, 0}, {32, 0}, {10, 64}, {91, FM1B_REVERB_SEND}, {93, FM1B_CHORUS_SEND}};
    if (step == CHORES_PER_CHANNEL - 1) {
        sf2player_channel_follow(channel);
        return;
    }
    const uint8_t m[3] = {(uint8_t)(0xb0U | channel), controls[step][0], controls[step][1]};
    fm1_app_midi_received(FM1_MIDI_USB, m, 3);
}

void song_play(void)
{
    if (!song_ready)
        return;
    song_stop();
    song_at = SONG_HEADER;
    song_due = song_at < song_bytes ? read_delta(&song_at) : 0;
    chores = FM1B_CHANNELS * CHORES_PER_CHANNEL;
    song_playing = 1;
}
/* Stop: the pedal up and all notes off on every channel (they fade out with their release). */
void song_stop(void)
{
    if (!song_playing)
        return;
    song_playing = 0;
    for (unsigned c = 0; c < FM1B_CHANNELS; ++c) {
        const uint8_t pedal[3] = {(uint8_t)(0xb0U | c), 64, 0}, off[3] = {(uint8_t)(0xb0U | c), 123, 0};
        for (uint32_t t = fm1_millis(); fm1b_queue_room() < 2 && fm1_millis() - t < 10U;)
            ;                                                 /* the audio interrupt empties it every 2.9 ms */
        fm1_app_midi_received(FM1_MIDI_USB, pedal, 3);
        fm1_app_midi_received(FM1_MIDI_USB, off, 3);
    }
}

void song_poll(void)
{
    if (!song_playing)
        return;
    while (chores && fm1b_queue_room() > ROOM_KEPT) {
        chore(FM1B_CHANNELS * CHORES_PER_CHANNEL - chores);
        if (!--chores)
            song_started = fm1_millis();                      /* the clock starts when the channels are ready */
    }
    if (chores)
        return;
    uint32_t now = fm1_millis() - song_started;
    /* What is due goes out while the queue has room; the rest goes in the next cycle, a moment late. */
    while (song_playing && song_due <= now && fm1b_queue_room() > ROOM_KEPT) {
        unsigned n = message_bytes(song[song_at]);
        fm1_app_midi_received(FM1_MIDI_USB, song + song_at, n);
        song_at += n;
        if (song_at >= song_bytes)
            song_playing = 0;                                 /* the end: what still sounds rings out */
        else
            song_due += read_delta(&song_at);
    }
}

void song_state(struct song_state *s)
{
    s->ready = song_ready;
    s->playing = song_playing;
    s->ms = song_playing && !chores ? fm1_millis() - song_started : 0;
    s->length_ms = song_length;
    s->events = song_events;
    s->bytes = song_ready ? song_bytes : 0;
    memcpy(s->name, song_name, sizeof s->name);
}

uint32_t song_free_ram(void)
{
    uint32_t low = 0, high = 512U;                            /* in KB */
    while (low < high) {
        uint32_t mid = (low + high + 1U) / 2U;
        void *p = malloc(mid * 1024U);
        if (p) { free(p); low = mid; } else high = mid - 1U;
    }
    return low * 1024U;
}
#endif
#endif
