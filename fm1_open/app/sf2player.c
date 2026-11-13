#ifndef FM1_HOST_TEST
/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* SF2 player firmware: the whole application. libfm1_engine.a expects the groove_* functions the stock panel,
 * project and sequencer code provides; here they are stubs, and the keys play an FM1B bank (fm1b.c) that is mixed
 * into the engine's output by board/audio.c. Nothing of the stock instrument UI, projects or sequencer is built. */
#include "instrument.h"
#include "fm_patch.h"
#include "fm1b.h"
#include "fm1b_config.h"
#if FM1B_PACKED
#ifndef FM1B_CACHE_FRAMES
#define FM1B_CACHE_FRAMES 2U                             /* MP3 frames (1152 samples) decoded in one control cycle */
#endif
#endif
#ifndef FM1_STATS_MIDI
#define FM1_STATS_MIDI FM1B_PACKED                       /* the player's figures once a second as SysEx to USB (stats_midi_poll): on in the live-cache build */
#endif
#include "sf2_upload.h"
#include "sf2fx.h"
#include "sf2out.h"
#include "song.h"
#include "tuning_hook.h"
#include "fm1_device.h"
#include <string.h>
#if defined(FM1_CPU1_TEST) && FM1_CPU1_TEST
#include "cpu1_test.h"
#if FM1B_FLASH_PROBE
#error "FM1_CPU1_TEST and FM1B_FLASH_PROBE both take ARP on the info page"
#endif
static struct cpu1_tries cpu1_tries;                     /* the second core's start test (ARP and SEQ on the info page) */
#endif
#if __has_include("build_info.h")
#include "build_info.h"                                  /* written by build_fw.ps1: FM1_BUILD_NAME, FM1_BUILD_DATE */
#endif

/* sdk/Build.ps1 insists that the library's Bluetooth control calls and the tuning/edit hooks are linked, and the linker
 * drops what nothing references. The stock panel referenced them; this table does, and is itself never read. */
extern int groove_project_apply_edit(const struct groove_project *, unsigned) __attribute__((weak));
static const void *const keep_linked[] __attribute__((used)) = {
    fm1_ble_set_role, fm1_ble_scan, fm1_ble_connect, fm1_ble_disconnect, fm1_ble_get_status, fm1_ble_get_device,
    fm1_bluetooth_configure, fm1_bluetooth_get_status,
    groove_tuning_capture, groove_tuning_apply, groove_project_apply_edit,
};

/* Keys (control task) and MIDI input (routing task) both enqueue notes; the queue has one writer, so take turns. */
void sf2player_play(unsigned note, unsigned velocity)
{
    uint32_t flags = fm1_device_irq_save();
    fm1b_note(NULL, note, velocity, 100);
    fm1_device_irq_restore(flags);
}
/* The percussion kit: MIDI channel 10, and the keys while SCL has the panel in drum mode. */
void sf2player_drum(unsigned note, unsigned velocity)
{
    uint32_t flags = fm1_device_irq_save();
    fm1b_drum_note(NULL, note, velocity, 100);
    fm1_device_irq_restore(flags);
}
/* Multitimbral: what MIDI input does to one channel (0..15). Same single writer as above, so the same lock. */
void sf2player_channel_play(unsigned channel, unsigned note, unsigned velocity)
{
    uint32_t flags = fm1_device_irq_save();
    fm1b_channel_note(channel, note, velocity, 100);
    fm1_device_irq_restore(flags);
}
void sf2player_channel_drum(unsigned channel, unsigned note, unsigned velocity)
{
    uint32_t flags = fm1_device_irq_save();
    fm1b_channel_drum_note(channel, note, velocity, 100);
    fm1_device_irq_restore(flags);
}
void sf2player_channel_program(unsigned channel, unsigned program_bank, unsigned program)
{
    uint32_t flags = fm1_device_irq_save();
    fm1b_channel_program(channel, program_bank, program);
    fm1_device_irq_restore(flags);
}
void sf2player_channel_control(unsigned channel, unsigned controller, unsigned value)
{
    uint32_t flags = fm1_device_irq_save();
    fm1b_channel_control(channel, controller, value);
    fm1_device_irq_restore(flags);
}
void sf2player_channel_follow(unsigned channel)
{
    uint32_t flags = fm1_device_irq_save();
    fm1b_channel_follow(channel);
    fm1_device_irq_restore(flags);
}
/* Mute and solo only write a word the audio side reads once a block: no lock needed. */
static void sf2player_channel_mute(unsigned channel, unsigned on) { fm1b_channel_mute(channel, on); }
static void sf2player_channel_solo(int channel) { fm1b_channel_solo(channel); }
void sf2player_channel_bend(unsigned channel, unsigned value)
{
    uint32_t flags = fm1_device_irq_save();
    fm1b_channel_bend(channel, value);
    fm1_device_irq_restore(flags);
}

/* ---- ARM: layering. The channel page's EDIT arms the row under the cursor, GLO steps the MIDI channel it listens on (1..16, then 1).
 * Armed rows all play the FM-1's keys, and a note that comes in on MIDI channel m also plays on every armed row listening on m.
 * Not saved: a restart starts with nothing armed. The control task writes, the routing task reads; both are 16-bit/8-bit words. */
static uint16_t armed;                                    /* bit r: row r (MIDI channel r+1's row) is armed */
static uint8_t arm_midi[16];                               /* the MIDI channel (0..15) an armed row listens on; 0 = MIDI channel 1 */
__attribute__((noinline)) static void row_play(unsigned row, unsigned note, unsigned velocity)
{
    if (row == 9U)
        sf2player_channel_drum(row, note, velocity);
    else
        sf2player_channel_play(row, note, velocity);
}
/* MIDI input (transfer.c): the channel's own row as always, and the armed rows that listen on this channel. */
void sf2player_midi_note(unsigned channel, unsigned note, unsigned velocity)
{
    row_play(channel, note, velocity);
    unsigned mask = armed;
    for (unsigned r = 0; mask; ++r, mask >>= 1)
        if ((mask & 1U) && r != channel && arm_midi[r] == channel)
            row_play(r, note, velocity);
}

/* ---- the panel: F3..G5 are 27 keys, bit 0 = F3 (MIDI 53) -------------------------------------------------- */
enum { FIRST_NOTE = 53, KEYS = 27, BACKGROUND = 0x0210 /* dark teal */ };
static unsigned drum_mode;                                /* SCL: the keys play the percussion kit */
static uint32_t sounding_drum;                           /* per key: it started a drum note */
static int octave;                                       /* -2..2, the panel's OCT- / OCT+ (buttons 0 and 1) */

/* ---- the screen: text lines in a 5x7 font, redrawn only when their text changes --------------------------------- */
/* Five columns per glyph, bit 0 at the top. Digits, then A-Z (the same glyphs as the stock panel). */
static const uint8_t font[36][5] = {
    {0x3e, 0x51, 0x49, 0x45, 0x3e}, {0, 0x42, 0x7f, 0x40, 0},       {0x42, 0x61, 0x51, 0x49, 0x46},
    {0x21, 0x41, 0x45, 0x4b, 0x31}, {0x18, 0x14, 0x12, 0x7f, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3c, 0x4a, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03}, {0x36, 0x49, 0x49, 0x49, 0x36},
    {0x06, 0x49, 0x49, 0x29, 0x1e}, {0x7e, 0x11, 0x11, 0x11, 0x7e}, {0x7f, 0x49, 0x49, 0x49, 0x36},
    {0x3e, 0x41, 0x41, 0x41, 0x22}, {0x7f, 0x41, 0x41, 0x22, 0x1c}, {0x7f, 0x49, 0x49, 0x49, 0x41},
    {0x7f, 0x09, 0x09, 0x09, 0x01}, {0x3e, 0x41, 0x49, 0x49, 0x7a}, {0x7f, 0x08, 0x08, 0x08, 0x7f},
    {0, 0x41, 0x7f, 0x41, 0},       {0x20, 0x40, 0x41, 0x3f, 0x01}, {0x7f, 0x08, 0x14, 0x22, 0x41},
    {0x7f, 0x40, 0x40, 0x40, 0x40}, {0x7f, 0x02, 0x0c, 0x02, 0x7f}, {0x7f, 0x04, 0x08, 0x10, 0x7f},
    {0x3e, 0x41, 0x41, 0x41, 0x3e}, {0x7f, 0x09, 0x09, 0x09, 0x06}, {0x3e, 0x41, 0x51, 0x21, 0x5e},
    {0x7f, 0x09, 0x19, 0x29, 0x46}, {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7f, 0x01, 0x01},
    {0x3f, 0x40, 0x40, 0x40, 0x3f}, {0x1f, 0x20, 0x40, 0x20, 0x1f}, {0x3f, 0x40, 0x38, 0x40, 0x3f},
    {0x63, 0x14, 0x08, 0x14, 0x63}, {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43}};
static const uint8_t punctuation[][6] = {
    {'-', 8, 8, 8, 8, 8}, {'/', 0x60, 0x18, 6, 1, 0}, {'+', 8, 8, 0x3e, 8, 8}, {'%', 0x63, 0x13, 8, 0x64, 0x63},
    {'.', 0, 0x60, 0x60, 0, 0}, {':', 0, 0x36, 0x36, 0, 0}, {'#', 0x14, 0x7f, 0x14, 0x7f, 0x14}, {'(', 0, 0x1c, 0x22, 0x41, 0},
    {')', 0, 0x41, 0x22, 0x1c, 0}, {'_', 0x40, 0x40, 0x40, 0x40, 0x40}};
enum { WHITE = 0xffff, GREY = 0x8410, DIM = 0x4208, DARK_GREEN = 0x0300, YELLOW = 0xffe0, GREEN = 0x07e0, ORANGE = 0xfd20, CYAN = 0x07ff, BAR_BACK = 0x2104,
       LINE_CHARS = 41, LINES = 20, MAX_ROW_PIXELS = FM1_SCREEN_WIDTH * 14 };
static uint16_t row_pixels[MAX_ROW_PIXELS];
static char shown[LINES][LINE_CHARS + 1];
static uint16_t shown_color[LINES], shown_back[LINES];
static uint16_t line_back = BACKGROUND;                  /* the background of the next line drawn (the channel page's cursor) */

static void text_line(unsigned y, const char *s, unsigned scale, uint16_t color)
{
    unsigned h = 7U * scale, cell = 6U * scale;
    for (unsigned i = 0; i < FM1_SCREEN_WIDTH * h; ++i)
        row_pixels[i] = line_back;
    for (unsigned c = 0; s[c] && (c + 1U) * cell <= FM1_SCREEN_WIDTH; ++c) {
        char ch = s[c] >= 'a' && s[c] <= 'z' ? (char)(s[c] - 'a' + 'A') : s[c];
        static const uint8_t blank[5] = {0};
        const uint8_t *shape = blank;
        if (ch >= '0' && ch <= '9') shape = font[ch - '0'];
        else if (ch >= 'A' && ch <= 'Z') shape = font[10 + ch - 'A'];
        else if (ch != ' ')
            for (unsigned i = 0; i < sizeof punctuation / sizeof punctuation[0]; ++i)
                if (punctuation[i][0] == (uint8_t)ch) { shape = punctuation[i] + 1; break; }
        for (unsigned row = 0; row < h; ++row)
            for (unsigned col = 0; col < 5U * scale; ++col)
                if (shape[col / scale] & (1U << (row / scale)))
                    row_pixels[row * FM1_SCREEN_WIDTH + c * cell + col] = color;
    }
    (void)fm1_screen_blit(0, y, FM1_SCREEN_WIDTH, h, row_pixels, FM1_SCREEN_WIDTH * h);
}
/* A screen update draws at most LINES_PER_UPDATE changed lines (a page that was just cleared draws all of them): while a song plays,
 * the program page's lines change with every note, and drawing them all took the control task about 75 ms with the audio interrupt
 * busy (measured on the FM-1 over MIDI, 2026-11-11), which kept the live cache from decoding. The changed lines take turns: an update
 * starts with the changed line after the last one the update before drew. Lines not drawn stay as they are until a later update. */
enum { LINES_PER_UPDATE = 2 };
static uint8_t lines_limited, lines_drawn, lines_changed, lines_start, lines_last;
/* Draw line `n` at y only when its text or colour is not what is on the screen. */
static void line_show(unsigned n, unsigned y, const char *s, unsigned scale, uint16_t color)
{
    if (!strncmp(shown[n], s, LINE_CHARS) && shown_color[n] == color && shown_back[n] == line_back)
        return;
    if (lines_limited) {
        uint8_t k = lines_changed++;
        if (k < lines_start || lines_drawn >= LINES_PER_UPDATE)
            return;
        ++lines_drawn;
        lines_last = k;
    }
    text_line(y, s, scale, color);
    strncpy(shown[n], s, LINE_CHARS);
    shown[n][LINE_CHARS] = 0;
    shown_color[n] = color;
    shown_back[n] = line_back;
}
static char *put_text(char *d, const char *s) { while (*s) *d++ = *s++; return d; }
static char *put_number(char *d, int32_t v, unsigned min_digits)
{
    char tmp[12];
    unsigned n = 0;
    if (v < 0) { *d++ = '-'; v = -v; }
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n < min_digits) tmp[n++] = '0';
    while (n) *d++ = tmp[--n];
    return d;
}
static char *put_note(char *d, unsigned note)
{
    static const char names[12][3] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    d = put_text(d, names[note % 12]);
    return put_number(d, (int)note / 12 - 1, 0);
}

/* SysEx traffic of a bank upload: frames the firmware saw / took, and the status of the last one (0 = done). */
static void upload_line(void)
{
    char line[LINE_CHARS + 1], *d;
    unsigned seen, taken, step, status;
    sf2_upload_state(&seen, &taken, &step, &status);
    if (!seen) {
        line_show(8, 184, "", 2, GREY);
        return;
    }
    d = put_text(line, "UPLOAD ");
    d = put_number(d, (int32_t)taken, 0);
    if (status != 255) {
        d = put_text(d, " R");
        d = put_number(d, (int32_t)status, 0);
    }
    *d = 0;
    line_show(8, 184, line, 2, GREY);
}

/* ---- the channel page (HOME): what each MIDI channel plays and its controllers ----------------------------------- */
enum { PAGE_PROGRAM, PAGE_CHANNELS, PAGE_INFO };
static unsigned page;                                     /* PAGE_PROGRAM: the program and its zone, PAGE_CHANNELS: the 16 channels */
static unsigned info_return;                              /* the page REC goes back to from the info page */
static unsigned cursor;                                   /* the channel page's selected channel, 0..15: KNOB1 moves it */
enum { CURSOR_BACK = 0x3a6b };                            /* the selected row's background: slate blue */
static unsigned bar_drawn = 1000;                         /* the level bar's width on the screen (1000: not drawn) */
static uint8_t channel_notes_seen[FM1B_CHANNELS];
static uint32_t channel_hit[FM1B_CHANNELS];               /* fm1_millis() of the latest note-on seen on the channel */

/* The audio interrupt's share of a block (sf2out_cpu): the average, and the highest block held for a second so it can be read.
 * Over 80 % the CPU guard lowers the voice limit. Read once a screen update. */
static unsigned cpu_average, cpu_peak;
static uint32_t cpu_peak_at;
static void cpu_read(void)
{
    unsigned peak;
    sf2out_cpu(&cpu_average, &peak);
    uint32_t now = fm1_millis();
    if (peak >= cpu_peak || now - cpu_peak_at >= 1000U) {
        cpu_peak = peak;
        cpu_peak_at = now;
    }
}
/* "CPU 45% MAX 72%" */
static char *put_cpu(char *d)
{
    d = put_text(d, "CPU ");
    d = put_number(d, (int32_t)cpu_average, 0);
    d = put_text(d, "% MAX ");
    d = put_number(d, (int32_t)cpu_peak, 0);
    *d++ = '%';
    return d;
}
#if FM1_MIDIPLAY
/* ms as m:ss */
static char *put_time(char *d, uint32_t ms)
{
    uint32_t s = ms / 1000U;
    d = put_number(d, (int32_t)(s / 60U), 0);
    *d++ = ':';
    return put_number(d, (int32_t)(s % 60U), 2);
}
/* The song in RAM (song.c): green while it plays, with where it is; grey while it waits for PLAY. Empty with no song. */
static void song_line(unsigned n, unsigned y)
{
    struct song_state so;
    char line[LINE_CHARS + 1], *d = line;
    song_state(&so);
    if (so.ready) {
        d = put_text(d, so.playing ? "PLAY " : "SONG ");
        for (unsigned i = 0; i < 16 && so.name[i]; ++i) *d++ = so.name[i];
        *d++ = ' ';
        if (so.playing) {
            d = put_time(d, so.ms);
            *d++ = '/';
        }
        d = put_time(d, so.length_ms);
        if (!so.playing) d = put_text(d, "  PLAY STARTS");
    }
    *d = 0;
    line_show(n, y, line, 1, so.playing ? GREEN : GREY);
}
#endif
/* ---- the info page (REC): which firmware and which bank, each with its CRC32, and the two together ------------------ */
/* The firmware's name and build time: from build_fw.ps1 (build_info.h), else the package's name and the compiler's clock. */
#ifndef FM1_BUILD_NAME
#define FM1_BUILD_NAME "FM1_OFFICIAL_OPEN.fwsc"
#endif
#ifndef FM1_BUILD_DATE
#define FM1_BUILD_DATE ""
#endif
/* The application image as the packager got it (sdk/Build.ps1: .text, .data, .ram0_data, .cache_ram_data back to back) lies
 * in flash from text_begin on; the data sections' first copies follow the code (linker.ld.in, the *_lma symbols). */
extern char text_begin[], text_size[], data_size[], _ram0_data_size[], cache_ram_data_size[];
static uint32_t crc_table[256];
static uint32_t crc32_add(uint32_t crc, const uint8_t *p, uint32_t bytes)
{
    if (!crc_table[1])
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (unsigned k = 0; k < 8; ++k) c = c & 1U ? 0xedb88320U ^ (c >> 1) : c >> 1;
            crc_table[n] = c;
        }
    while (bytes--) crc = crc_table[(crc ^ *p++) & 0xffU] ^ (crc >> 8);
    return crc;
}
/* The CRC32s (as zip and 7-Zip show them, fm1_info.js prints the same): the firmware image, the bank file, and the sound set,
 * one CRC32 over the firmware image followed by the bank file. Worked out when REC opens the page: the firmware's once, the
 * bank's every time (an upload may have changed it). */
static uint32_t firmware_crc_state, firmware_crc, bank_crc, soundset_crc;
static uint32_t firmware_bytes;
static unsigned firmware_crc_done;
static struct fm1b_info info_bank;
#if FM1_MIDIPLAY
static uint32_t info_free_ram;
#endif
#if FM1B_PACKED
/* The live cache, measured from when REC opened the info page: the cache's counters then (shown as the change since), the ms the
 * control task spent in fm1b_cache_service (nearly all of it decoding; the audio interrupt's share of that time is in it), and the
 * control task's cycle, from one call to the next, average and longest. */
static struct fm1b_cache_stats cache_base;
static uint32_t cache_busy_ms, cycle_at, cycle_sum, cycle_count, cycle_max;
#if FM1_STATS_MIDI
static uint32_t total_busy_ms, total_cycles, total_cycle_ms, window_cycle_max;   /* since start, and the longest cycle since the last stats message */
#endif
static void cache_service_timed(void)
{
    uint32_t start = fm1_millis();
    if (cycle_at) {
        uint32_t gap = start - cycle_at;
        cycle_sum += gap;
        ++cycle_count;
        if (gap > cycle_max) cycle_max = gap;
#if FM1_STATS_MIDI
        total_cycle_ms += gap;
        ++total_cycles;
        if (gap > window_cycle_max) window_cycle_max = gap;
#endif
    }
    cycle_at = start;
    fm1b_cache_service(FM1B_CACHE_FRAMES);                    /* the live cache: answers the notes' requests, decodes a few MP3 frames */
    uint32_t busy = fm1_millis() - start;
    cache_busy_ms += busy;
#if FM1_STATS_MIDI
    total_busy_ms += busy;
#endif
}
#endif
#if FM1_STATS_MIDI
/* Once a second, the player's figures as one SysEx to the USB MIDI port (RUNNING.md, "Live figures over MIDI"; stats.html reads them):
 * F0 7D 46 4D 53 02 (non-commercial id, "FMS", version 3), then 23 values of 32 bits, each 5 bytes of 7 bits, lowest first, then F7.
 * Totals since start, so a reader takes differences: 0 ms since start; the cache's 1 hits 2 misses 3 dropped sounds 4 no room 5 MP3 frames decoded
 * 6 notes that asked for a sound 7 notes over before their sound 8 waiting now 9 pins now; 10 ms in fm1b_cache_service 11 control cycles 12 their ms
 * 13 the longest cycle since the last message; 14 CPU % average 15 CPU % peak (as the screen shows them) 16 voices sounding; 17..19 DROPPED HELD, TAIL, CPU;
 * 20 the longest control step (groove_screen_control) and 21 the longest screen update, both since the last message; 22 releasing voices cut
 * so that a waiting note's sound got room (fm1b.c, packed_squeeze). Version 1 had 0..19, version 2 0..21.
 * Without the live cache the cache's values are 0. */
static uint32_t stats_sent_at, window_step_max, window_screen_max;
static void stats_midi_poll(void)
{
    uint32_t now = fm1_millis();
    if (now - stats_sent_at < 1000U)
        return;
    stats_sent_at = now;
    enum { VALUES = 23 + FM1B_FLASH_PROBE };                  /* version 4 (the probe build): 23 the probe, 1 when the voices read the SRAM */
    uint32_t v[VALUES] = {now};
#if FM1B_PACKED
    struct fm1b_cache_stats cs;
    fm1b_cache_stats(&cs);
    v[1] = cs.hits; v[2] = cs.misses; v[3] = cs.evictions; v[4] = cs.no_room; v[5] = cs.decoded_mp3_frames;
    v[6] = cs.requested; v[7] = cs.lost; v[8] = cs.waiting; v[9] = cs.pinned; v[22] = cs.squeezed;
    v[10] = total_busy_ms; v[11] = total_cycles; v[12] = total_cycle_ms; v[13] = window_cycle_max;
    window_cycle_max = 0;
#endif
    struct fm1b_status st;
    fm1b_status(&st);
    v[14] = cpu_average; v[15] = cpu_peak; v[16] = st.voices;
    v[17] = st.drops_held; v[18] = st.drops_tail; v[19] = st.drops_guard;
    v[20] = window_step_max; v[21] = window_screen_max;
    window_step_max = window_screen_max = 0;
#if FM1B_FLASH_PROBE
    v[23] = fm1b_probe();
#endif
    uint8_t m[6 + VALUES * 5 + 1] = {0xf0, 0x7d, 0x46, 0x4d, 0x53, VALUES == 24 ? 0x04 : 0x03}, *d = m + 6;
    for (unsigned i = 0; i < VALUES; ++i)
        for (unsigned k = 0; k < 5; ++k)
            *d++ = (uint8_t)((v[i] >> (7 * k)) & 0x7fU);
    *d = 0xf7;
    (void)fm1_midi_send_port(FM1_MIDI_USB, m, sizeof m);      /* -1 when no USB host, -2 when the queue is full: the next second tries again */
}
#endif
static void info_compute(void)
{
#if FM1_MIDIPLAY
    info_free_ram = song_free_ram();
#endif
#if FM1B_PACKED
    fm1b_cache_stats(&cache_base);
    cache_busy_ms = cycle_sum = cycle_count = cycle_max = 0;
#endif
    if (!firmware_crc_done) {
        firmware_bytes = (uint32_t)(uintptr_t)text_size + (uint32_t)(uintptr_t)data_size + (uint32_t)(uintptr_t)_ram0_data_size +
                         (uint32_t)(uintptr_t)cache_ram_data_size;
        firmware_crc_state = crc32_add(~0U, (const uint8_t *)text_begin, firmware_bytes);
        firmware_crc = ~firmware_crc_state;
        firmware_crc_done = 1;
    }
    fm1b_info(&info_bank);
    if (info_bank.image) {
        bank_crc = ~crc32_add(~0U, info_bank.image, info_bank.bytes);
        soundset_crc = ~crc32_add(firmware_crc_state, info_bank.image, info_bank.bytes);
    }
}
static char *put_hex(char *d, uint32_t v)
{
    for (int shift = 28; shift >= 0; shift -= 4) *d++ = "0123456789ABCDEF"[(v >> shift) & 15U];
    return d;
}
/* "Nov  5 2026" "14:32:10" as 2026-11-05 14:32 */
static char *put_compile_date(char *d)
{
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *date = __DATE__, *time = __TIME__;
    unsigned month = 1;
    for (unsigned m = 0; m < 12; ++m)
        if (!strncmp(months + 3 * m, date, 3)) month = m + 1;
    d = put_text(d, (char[]){date[7], date[8], date[9], date[10], '-', (char)('0' + month / 10), (char)('0' + month % 10), '-',
                             date[4] == ' ' ? '0' : date[4], date[5], ' ', time[0], time[1], ':', time[3], time[4], 0});
    return d;
}
static void info_update(void)
{
    char line[LINE_CHARS + 1], *d;
    line_show(0, 8, "INFO", 2, WHITE);
    line_show(1, 34, "FIRMWARE", 1, YELLOW);
    line_show(2, 46, FM1_BUILD_NAME, 1, WHITE);
    d = put_text(line, "BUILT ");
    d = FM1_BUILD_DATE[0] ? put_text(d, FM1_BUILD_DATE) : put_compile_date(d);
    *d = 0;
    line_show(3, 58, line, 1, CYAN);
    d = put_hex(put_text(line, "CRC32 "), firmware_crc);
    d = put_text(d, "  ");
    d = put_number(d, (int32_t)firmware_bytes, 0);
    d = put_text(d, " BYTES");
    *d = 0;
    line_show(4, 70, line, 1, CYAN);

    line_show(5, 92, "BANK", 1, YELLOW);
    if (!info_bank.image) {
        line_show(6, 104, "NO BANK", 1, ORANGE);
        line_show(7, 116, "", 1, CYAN);
        line_show(12, 128, "", 1, CYAN);
        line_show(9, 156, "", 1, YELLOW);
        line_show(10, 170, "", 2, YELLOW);
    } else {
        line_show(6, 104, info_bank.name[0] ? info_bank.name : "NO NAME (AN OLDER BANK)", 1, info_bank.name[0] ? WHITE : GREY);
        d = put_text(line, "BUILT ");
        d = put_text(d, info_bank.date[0] ? info_bank.date : "-");
        *d = 0;
        line_show(7, 116, line, 1, CYAN);
        d = put_hex(put_text(line, "CRC32 "), bank_crc);
        d = put_text(d, "  ");
        d = put_number(d, (int32_t)info_bank.bytes, 0);
        d = put_text(d, " BYTES");
        *d = 0;
        line_show(12, 128, line, 1, CYAN);
        line_show(9, 156, "SOUNDSET CHECKSUM:", 1, YELLOW);
        *put_hex(line, soundset_crc) = 0;
        line_show(10, 170, line, 2, YELLOW);
    }
    upload_line();
#if FM1_MIDIPLAY
    song_line(13, 206);
    d = put_text(line, "FREE RAM ");
    d = put_number(d, (int32_t)(info_free_ram / 1024U), 0);
    d = put_text(d, " KB FOR A SONG");
    *d = 0;
    line_show(14, 217, line, 1, GREY);
#elif FM1B_PACKED
    {   /* "CACHE H1234 M56 E7 F0 L0 P3 W0": hits, misses, sounds dropped, no room, notes lost, pins now, waiting now;
         * "MP3 5678 FR 1234 US/FR CYC 4 MAX 37 MS": frames decoded, ms in the service per frame (as us), the control cycle. FM1_TEST.md. */
        struct fm1b_cache_stats cs;
        char wide[80];
        fm1b_cache_stats(&cs);
        d = put_number(put_text(wide, "CACHE H"), (int32_t)(cs.hits - cache_base.hits), 0);
        d = put_number(put_text(d, " M"), (int32_t)(cs.misses - cache_base.misses), 0);
        d = put_number(put_text(d, " E"), (int32_t)(cs.evictions - cache_base.evictions), 0);
        d = put_number(put_text(d, " F"), (int32_t)(cs.no_room - cache_base.no_room), 0);
        d = put_number(put_text(d, " L"), (int32_t)(cs.lost - cache_base.lost), 0);
        d = put_number(put_text(d, " P"), (int32_t)cs.pinned, 0);
        d = put_number(put_text(d, " W"), (int32_t)cs.waiting, 0);
        *d = 0;
        wide[LINE_CHARS] = 0;
        line_show(13, 206, wide, 1, cs.lost != cache_base.lost ? ORANGE : GREY);
        uint32_t frames = cs.decoded_mp3_frames - cache_base.decoded_mp3_frames;
        d = put_number(put_text(wide, "MP3 "), (int32_t)frames, 0);
        d = put_number(put_text(d, " FR "), frames ? (int32_t)((uint64_t)cache_busy_ms * 1000U / frames) : 0, 0);
        d = put_number(put_text(d, " US/FR CYC "), cycle_count ? (int32_t)(cycle_sum / cycle_count) : 0, 0);
        d = put_number(put_text(d, " MAX "), (int32_t)cycle_max, 0);
        d = put_text(d, " MS");
        *d = 0;
        wide[LINE_CHARS] = 0;
        line_show(14, 217, wide, 1, GREY);
    }
#endif
#if defined(FM1_CPU1_TEST) && FM1_CPU1_TEST == 2
    {   /* "CPU1 BOOT OK 1234 JOBS 3/3", or what kept it from starting; NOT ASKED: ARP was not held at power-on */
        static const char *const why[] = {"NOT ASKED", "OK", "NO START", "LATE", "BAD SUM", "MISREAD"};
        const struct cpu1_boot *b = &cpu1_boot_state;
        d = put_text(line, b->asked == 2U ? "CPU1 LATE " : b->asked ? "CPU1 EARLY " : "CPU1 ");
        d = put_text(d, why[b->state < 6U ? b->state : 0]);
        if (!b->asked) {                                  /* what the scans at power-on saw: "E48/000 L48/008 B0000" (columns, buttons: hex) */
            static const char hex[] = "0123456789ABCDEF";
            for (unsigned k = 0; k < 2U; ++k) {
                d = put_number(put_text(d, k ? " L" : " E"), (int32_t)fm1_boot_scan[k].done, 0);
                *d++ = '/';
                for (int s = 8; s >= 0; s -= 4) *d++ = hex[(fm1_boot_scan[k].columns >> s) & 15U];
            }
            d = put_text(d, " B");
            for (int s = 12; s >= 0; s -= 4) *d++ = hex[(fm1_boot_scan[1].buttons >> s) & 15U];
        }
        if (b->asked) {
            d = put_number(put_text(d, " "), (int32_t)b->loops, 0);
            d = put_number(put_text(d, " JOBS "), (int32_t)b->jobs_ok, 0);
            d = put_number(put_text(d, "/"), (int32_t)b->jobs_tried, 0);
        }
        *d = 0;
        line_show(15, 140, line, 1, !b->asked ? GREY : b->state == CPU1_OK ? GREEN : ORANGE);
    }
    line_show(11, 228, cpu1_boot_state.state == CPU1_OK ? "REC BACK  ARP: A JOB ON CPU1" : "REC BACK  ARP AT POWER-ON: CPU1", 1, GREY);
#elif defined(FM1_CPU1_TEST) && FM1_CPU1_TEST
    {   /* "CPU1 M8 3/10 M89 9/10 OK 812US": the tries and starts that worked with each mask, the last try's result and start time */
        static const char *const why[] = {"-", "OK", "NO START", "LATE", "BAD SUM", "MISREAD"};
        const struct cpu1_tries *t = &cpu1_tries;
        d = put_number(put_text(line, "CPU1 M8 "), (int32_t)t->ok[CPU1_MASK_8], 0);
        d = put_number(put_text(d, "/"), (int32_t)t->tries[CPU1_MASK_8], 0);
        d = put_number(put_text(d, " M89 "), (int32_t)t->ok[CPU1_MASK_8_9], 0);
        d = put_number(put_text(d, "/"), (int32_t)t->tries[CPU1_MASK_8_9], 0);
        d = put_text(put_text(d, " "), why[t->last_state < 6U ? t->last_state : 0]);
        if (t->last_state != CPU1_NOT_RUN) d = put_text(put_number(put_text(d, " "), (int32_t)t->last_start_us, 0), "US");
        *d = 0;
        line_show(15, 140, line, 1, t->last_state == CPU1_NOT_RUN ? GREY : t->last_state == CPU1_OK ? GREEN : ORANGE);
    }
    line_show(11, 228, cpu1_tries.mask == CPU1_MASK_8_9 ? "REC BACK ARP TRY SEQ MASK 8+9" : "REC BACK ARP TRY SEQ MASK 8", 1, GREY);
#elif FM1B_FLASH_PROBE
    line_show(11, 228, fm1b_probe() ? "REC BACK  ARP SAMPLES: RAM" : "REC BACK  ARP SAMPLES: FLASH", 1, fm1b_probe() ? ORANGE : GREY);
#else
    line_show(11, 228, "REC BACK", 1, GREY);
#endif
}
static void page_clear(void)
{
    fm1_screen_fill(0, 0, FM1_SCREEN_WIDTH, FM1_SCREEN_HEIGHT, BACKGROUND);
    memset(shown, 0, sizeof shown);
    memset(shown_color, 0, sizeof shown_color);
    memset(shown_back, 0, sizeof shown_back);
    bar_drawn = 1000;
    lines_start = 255;                                        /* the next update draws the whole page */
}
/* Bend in semitones, right aligned in 4 characters: one decimal below 10 (+2.0), whole ones above (+12); blank at the centre. */
static char *put_bend(char *d, int32_t cents)
{
    char tmp[8], *t = tmp;
    if (cents) {
        int32_t tenths = (cents < 0 ? cents - 5 : cents + 5) / 10;   /* rounded to 0.1 semitone */
        *t++ = cents < 0 ? '-' : '+';
        if (tenths < 0) tenths = -tenths;
        if (tenths >= 95)
            t = put_number(t, (tenths + 5) / 10, 0);
        else {
            t = put_number(t, tenths / 10, 0);
            *t++ = '.';
            *t++ = (char)('0' + tenths % 10);
        }
    }
    for (unsigned pad = (unsigned)(t - tmp); pad < 4; ++pad) *d++ = ' ';
    for (char *s = tmp; s < t; ++s) *d++ = *s;
    return d;
}
/* A pan 0..127 in 3 characters: L64..L1, " C ", R1..R64 (127 is all the way right) */
static char *put_pan(char *d, unsigned pan)
{
    int p = pan == 127U ? 64 : (int)pan - 64;
    if (!p) return put_text(d, " C ");
    char tmp[3], *t = tmp;
    *t++ = p < 0 ? 'L' : 'R';
    t = put_number(t, p < 0 ? -p : p, 0);
    if (t - tmp < 3) *t++ = ' ';
    for (char *x = tmp; x < t; ++x) *d++ = *x;
    return d;
}
/* 0..127 right aligned in 3 characters; blank_zero leaves 0 blank */
static char *put_value3(char *d, unsigned v, unsigned blank_zero)
{
    if (!v && blank_zero) return put_text(d, "   ");
    if (v < 100) *d++ = ' ';
    if (v < 10) *d++ = ' ';
    return put_number(d, (int32_t)v, 0);
}
static char *put_padded(char *d, const char *s, unsigned width)
{
    unsigned n = 0;
    for (; n < width && s[n]; ++n) *d++ = s[n];
    for (; n < width; ++n) *d++ = ' ';
    return d;
}
/* CH PRG NAME LVL PAN BEND MOD REV CHO and a flag: green while it plays, white with a program of its own, grey while it follows
 * the panel's, orange for the kit on channel 10; a silent channel (muted, or another one soloed) is dark grey, dark green while
 * notes arrive. The flag: M muted, S soloed, else P while the pedal is down. The title says SOLO while a channel is soloed. */
static void channels_update(const struct fm1b_status *st)
{
    char line[LINE_CHARS + 1], *d;
    uint32_t now = fm1_millis();
    d = put_text(line, "CHANNELS ");
    d = put_number(d, st->voices, 0);
    *d++ = '/';
    d = put_number(d, st->voice_limit, 0);              /* the CPU guard's limit: FM1B_VOICES unless it ran short */
    unsigned soloed = 0;
    for (unsigned c = 0; c < FM1B_CHANNELS; ++c) {
        struct fm1b_channel info;
        fm1b_channel_info(c, &info);
        soloed |= info.solo;
    }
    if (soloed) d = put_text(d, " SOLO");
    *d = 0;
    line_show(0, 8, line, 2, soloed ? YELLOW : WHITE);
    line_show(1, 28, "CH PRG NAME   LVL PAN BEND MOD REV CHO F", 1, GREY);
    for (unsigned c = 0; c < FM1B_CHANNELS; ++c) {
        struct fm1b_channel info;
        fm1b_channel_info(c, &info);
        if (info.notes != channel_notes_seen[c]) {
            channel_notes_seen[c] = info.notes;
            channel_hit[c] = now;
        }
        d = put_number(line, (int32_t)c + 1, 2);
        /* an armed row shows the MIDI channel it listens on after its number: 1..9, then A..G for 10..16 */
        *d++ = (armed >> c) & 1U ? (char)(arm_midi[c] + (arm_midi[c] < 9U ? '1' : 'A' - 9)) : ' ';
        uint16_t color;
        if (c == 9U) {                                        /* transfer.c: channel 10 always plays the kit */
            d = put_text(d, "KIT ");
            d = put_padded(d, st->drums ? st->drum_name : "-", 6);
            color = ORANGE;
        } else if (info.own) {
            if (info.kit) d = put_text(d, "KIT");
            else d = put_number(d, info.program + 1, 3);
            *d++ = ' ';
            d = put_padded(d, info.name, 6);
            color = WHITE;
        } else {
            d = put_number(d, st->program + 1, 3);
            *d++ = ' ';
            d = put_padded(d, st->name, 6);
            color = GREY;
        }
        *d++ = ' ';
        d = put_value3(d, (unsigned)info.volume * info.expression / 127U, 0);
        *d++ = ' ';
        d = put_pan(d, info.pan);
        *d++ = ' ';
        d = put_bend(d, info.bend_cents);
        *d++ = ' ';
        d = put_value3(d, info.mod, 1);
        *d++ = ' ';
        d = put_value3(d, info.reverb, 0);
        *d++ = ' ';
        d = put_value3(d, info.chorus, 0);
        *d++ = ' ';
        *d++ = info.muted ? 'M' : info.solo ? 'S' : info.pedal ? 'P' : ' ';
        *d = 0;
        unsigned playing = info.voices || now - channel_hit[c] < 250U;
        if (info.silent)
            color = playing ? DARK_GREEN : DIM;
        else if (playing)
            color = GREEN;
        else if ((armed >> c) & 1U)
            color = CYAN;                                     /* armed and idle */
        line_back = c == cursor ? CURSOR_BACK : BACKGROUND;
        line_show(2 + c, 40 + c * 11, line, 1, color);
        line_back = BACKGROUND;
    }
    d = put_cpu(put_text(line, "ARP MUTE SEQ SOLO  "));
    *d = 0;
    line_show(19, 218, line, 1, st->voice_limit < FM1B_VOICES ? ORANGE : GREY);   /* orange: the guard holds the limit down */
    line_show(18, 228, "K1 CH SEL PRG ALG VOL K2 PAN K3/4 FX", 1, GREY);
}

static void screen_update(void)
{
    struct fm1b_status st;
    char line[LINE_CHARS + 1], *d;
    fm1b_status(&st);
    cpu_read();
    if (page == PAGE_INFO) {
        info_update();
        return;
    }
    if (!st.ready) {
        if (page) { page = PAGE_PROGRAM; page_clear(); }
        line_show(0, 8, "NO BANK", 2, ORANGE);
        upload_line();
        return;
    }
    if (page == PAGE_CHANNELS) {
        channels_update(&st);
        return;
    }
    if (drum_mode) {
        d = put_text(line, "DRUMS ");
        for (unsigned i = 0; i < 14 && st.drum_name[i]; ++i) *d++ = st.drum_name[i];
    } else {
        d = put_number(line, st.program + 1, 3);
        *d++ = ' ';
        for (unsigned i = 0; i < 16 && st.name[i]; ++i) *d++ = st.name[i];
    }
    *d = 0;
    line_show(0, 8, line, 2, drum_mode ? ORANGE : WHITE);

    if (st.length) {
        d = put_text(line, "NOTE ");
        d = put_note(d, st.note);
        d = put_text(d, "  ZONE ");
        d = put_number(d, st.zone + 1, 0);
        *d++ = '/';
        d = put_number(d, st.zones, 0);
        *d = 0;
        line_show(1, 30, line, 2, YELLOW);

        d = put_text(line, "KEYS ");
        d = put_note(d, st.lo);
        *d++ = '-';
        d = put_note(d, st.hi);
        d = put_text(d, "   ROOT ");
        d = put_note(d, st.root);
        if (st.fine) {
            *d++ = ' ';
            d = put_number(d, st.fine, 0);
            d = put_text(d, "C");
        }
        *d = 0;
        line_show(2, 62, line, 1, CYAN);

        d = put_text(line, "SAMPLE ");
        d = put_number(d, (int32_t)st.length, 0);
        d = put_text(d, " FRAMES AT ");
        d = put_number(d, (int32_t)st.rate, 0);
        d = put_text(d, " HZ");
        *d = 0;
        line_show(3, 76, line, 1, CYAN);

        if (st.loop) {
            d = put_text(line, "LOOP ");
            d = put_number(d, (int32_t)st.loop_start, 0);
            *d++ = '-';
            d = put_number(d, (int32_t)st.loop_end, 0);
            d = put_text(d, " (");
            d = put_number(d, (int32_t)(st.loop_end - st.loop_start), 0);
            d = put_text(d, ")");
        } else
            d = put_text(line, "NO LOOP");
        *d = 0;
        line_show(4, 90, line, 1, CYAN);

        d = put_text(line, "ENV A ");
        d = put_number(d, st.attack_ms, 0);
        d = put_text(d, "MS  H ");
        d = put_number(d, st.hold_ms, 0);
        d = put_text(d, "MS  S ");
        d = put_number(d, st.sustain_percent, 0);
        d = put_text(d, "%");
        *d = 0;
        line_show(5, 104, line, 1, GREEN);
    } else {
        line_show(1, 30, "PLAY A KEY", 2, GREY);
        line_show(2, 62, "", 1, CYAN);
        line_show(3, 76, "", 1, CYAN);
        line_show(4, 90, "", 1, CYAN);
        line_show(5, 104, "", 1, GREEN);
    }

    /* the voice's level, A H D S R, and how many voices sound */
    static const char phases[5] = {'A', 'H', 'D', 'S', 'R'};
    unsigned bar = (unsigned)st.level * 220U / 255U;
    if (bar != bar_drawn) {
        fm1_screen_fill(10, 124, 220, 10, BAR_BACK);
        if (bar) fm1_screen_fill(10, 124, bar, 10, st.phase == 4 ? ORANGE : GREEN);
        bar_drawn = bar;
    }
    d = put_text(line, "VOICES ");
    d = put_number(d, st.voices, 0);
    d = put_text(d, "/");
    d = put_number(d, st.voice_limit, 0);
    *d++ = ' ';
    d = put_cpu(d);
    if (st.level) { d = put_text(d, " PHASE "); *d++ = phases[st.phase < 5 ? st.phase : 4]; }
    *d = 0;
    line_show(6, 140, line, 1, st.voice_limit < FM1B_VOICES ? ORANGE : GREY);   /* orange: the guard holds the limit down */
    upload_line();
    d = put_text(line, "MASTER REV ");
    d = put_number(d, (int32_t)sf2fx_reverb(), 0);
    d = put_text(d, " CHO ");
    d = put_number(d, (int32_t)sf2fx_chorus(), 0);
    d = put_text(d, "  PAN ");
    { int pan = (int)sf2fx_pan() - 64; if (pan > 0) *d++ = '+'; d = put_number(d, pan, 0); }
    d = put_text(d, "  OCT ");
    if (octave > 0) *d++ = '+';
    d = put_number(d, octave, 0);
    *d = 0;
    line_show(9, 158, line, 1, YELLOW);
    if (st.channels) {                                        /* MIDI program changes made some channels play a preset of their own */
        d = put_text(line, "MULTI ");
        d = put_number(d, st.channels, 0);
        d = put_text(d, " CH   LAST CH");
        d = put_number(d, st.program_channel + 1, 0);
        d = put_text(d, " PRG ");
        d = put_number(d, st.program_number + 1, 3);
        *d = 0;
    } else
        *line = 0;
    line_show(10, 172, line, 1, CYAN);
    /* What the voice limit took since start or SAVE: HELD notes still sounding (heard as dropped), TAIL notes already
     * releasing (cut short), CPU the guard's. Orange while HELD is not 0. */
    d = put_text(line, "DROPPED HELD ");
    d = put_number(d, (int32_t)st.drops_held, 0);
    d = put_text(d, " TAIL ");
    d = put_number(d, (int32_t)st.drops_tail, 0);
    d = put_text(d, " CPU ");
    d = put_number(d, (int32_t)st.drops_guard, 0);
    *d = 0;
    line_show(12, 200, line, 1, st.drops_held || st.drops_guard ? ORANGE : GREY);
#if FM1_MIDIPLAY
    song_line(13, 214);
#endif
    line_show(7, 228, "SEL PRG SCL DRUM HOME CH SAVE ZERO DROPS", 1, GREY);
}

/* SELECT on the channel page: the next or previous program the bank has, from the channel's own or (while it follows the
 * panel) the panel's. Below the first one the channel follows the panel again. Channel 10 always plays the kit (transfer.c). */
static void channel_program_step(unsigned channel, int clicks)
{
    struct fm1b_channel info;
    struct fm1b_status st;
    if (channel == 9U)
        return;
    fm1b_channel_info(channel, &info);
    fm1b_status(&st);
    if (!st.ready)
        return;
    int step = clicks > 0 ? 1 : -1, own = info.own, current = info.own ? info.program : st.program;
    for (int n = clicks * step; n > 0; --n) {
        int next = current + step;
        while (next >= 0 && next < 128 && !fm1b_has_program(0, (unsigned)next))
            next += step;
        if (next < 0) { own = 0; break; }
        if (next > 127) break;
        current = next;
        own = 1;
    }
    if (own)
        sf2player_channel_program(channel, 0, (unsigned)current);
    else if (info.own)
        sf2player_channel_follow(channel);
}

static uint32_t previous_keys, previous_buttons;
static unsigned leds_shown = 0xffU;                       /* what the ARP and SEQ LEDs show (bit 0 mute, bit 1 solo); 0xff: unknown */
static uint16_t sounding_rows[KEYS];                       /* per key: the rows (bit per channel) it started a note on, 0: the panel's */
static uint8_t sounding[KEYS];                           /* the note each key started, so a release stops that one */
static unsigned program;

void groove_screen_init(void)
{
    fm1_controls_init();
    fm1_screen_init();
    fm1_screen_fill(0, 0, FM1_SCREEN_WIDTH, FM1_SCREEN_HEIGHT, BACKGROUND);
    previous_keys = previous_buttons = 0;
    octave = 0;
    drum_mode = 0;
    sounding_drum = 0;
    program = 0;
    page = PAGE_PROGRAM;
    info_return = PAGE_PROGRAM;
    cursor = 0;
    armed = 0;
    memset(arm_midi, 0, sizeof arm_midi);
    leds_shown = 0xffU;
    memset(shown, 0, sizeof shown);
    memset(shown_color, 0, sizeof shown_color);
    memset(shown_back, 0, sizeof shown_back);
    bar_drawn = 1000;
    lines_start = 255;
}
/* ARP and SEQ light while the selected channel is muted / soloed (on the channel page only), PLAY while a song plays. */
enum { BUTTON_EDIT = 6, BUTTON_GLO = 7, BUTTON_SAVE = 9, BUTTON_ARP = 10, BUTTON_SEQ = 11, BUTTON_PLAY = 12, BUTTON_REC = 13 };
static void leds_update(void)
{
    struct fm1b_channel info;
    unsigned leds = 0;
    if (page == PAGE_CHANNELS) {
        fm1b_channel_info(cursor, &info);
        leds = info.muted | info.solo << 1 | ((armed >> cursor) & 1U) << 3 | (armed != 0) << 4;
    }
#if FM1_MIDIPLAY
    struct song_state so;
    song_state(&so);
    leds |= so.playing << 2;                                  /* PLAY lights while a song plays */
#endif
    if (leds == leds_shown)
        return;
    leds_shown = leds;
    fm1_led_set(BUTTON_ARP, (int)(leds & 1U));
    fm1_led_set(BUTTON_SEQ, (int)((leds >> 1) & 1U));
    fm1_led_set(BUTTON_PLAY, (int)((leds >> 2) & 1U));
    fm1_led_set(BUTTON_EDIT, (int)((leds >> 3) & 1U));        /* EDIT: the selected row is armed; GLO: some row is */
    fm1_led_set(BUTTON_GLO, (int)((leds >> 4) & 1U));
}
/* Safe mode: OCT- and OCT+ (buttons 0 and 1) held while the FM-1 starts, as the stock firmware's update mode. The bank is then not bound
 * and the live cache does nothing, so a bank or a code path that crashes cannot keep the FM-1 from starting; USB-MIDI, the firmware update
 * (install.html, the workbench) and the bank upload work as always. It does not help when the SDK fails before the control task runs
 * (a runtime heap too small, 2026-11-08): that is the build's heap check and a boot in fm1-emu first (RUNNING.md). */
enum { SAFE_BUTTONS = 3U, SAFE_WINDOW_MS = 300U };
static uint8_t start_state;                               /* 0: the first SAFE_WINDOW_MS of the control task, 1: playing, 2: safe mode */
static uint32_t start_ms;
static int safe_start(void)
{
    if (start_state)
        return start_state == 2;
    struct fm1_controls controls;
    fm1_controls_read(&controls);
    uint32_t now = fm1_millis();
    if (!start_ms)
        start_ms = now | 1U;
    if ((controls.buttons & SAFE_BUTTONS) == SAFE_BUTTONS) {
        start_state = 2;
        line_show(0, 90, "SAFE MODE", 2, ORANGE);
        line_show(1, 120, "NO BANK. UPDATE OVER USB", 1, WHITE);
        return 1;
    }
    if (now - start_ms < SAFE_WINDOW_MS)
        return 1;
    start_state = 1;
#if FM1B_IN_FIRMWARE
    fm1b_device_bind();
#endif
    return 0;
}

#if FM1_STATS_MIDI
static void control_step(void)
#else
void groove_screen_control(void)
#endif
{
    sf2_upload_poll();                                        /* a bank frame that arrived over MIDI */
    if (safe_start())
        return;
#if FM1B_PACKED
    cache_service_timed();
#endif
#if FM1_STATS_MIDI
    stats_midi_poll();
#endif
#if FM1_MIDIPLAY
    song_poll();                                              /* the song's events that are due */
#endif
    struct fm1_controls controls;
    fm1_controls_read(&controls);
    (void)groove_master_update(controls.knob);               /* MASTER: the output level on every page; without it the output stays muted */
    /* The info page shows over the page REC was pressed on: the knobs, keys, ARP and SEQ go on working as they do there. */
    unsigned channels = (page == PAGE_INFO ? info_return : page) == PAGE_CHANNELS;
    /* The knobs: SELECT (encoder 0) programs, ALGORITHM (1) the selected channel's volume, KNOB1 (2) the channel page's
     * cursor, KNOB2 (3) pan, KNOB3 (4) reverb and KNOB4 (5) chorus. On the program page SELECT picks the panel's program,
     * KNOB2 pans the whole mix and KNOB3/KNOB4 set the effects' master levels; on the channel page SELECT picks the selected
     * channel's program, ALGORITHM sets its volume (CC 7), KNOB2 its pan (CC 10) and KNOB3/KNOB4 its sends. */
    if (controls.encoders[0] && !channels) {
        int step = controls.encoders[0] > 0 ? 1 : -1;
        for (int clicks = controls.encoders[0] * step; clicks > 0; --clicks)
            for (int next = (int)program + step; next >= 0 && next < 128; next += step)
                if (fm1b_program(0, (unsigned)next) == 0) { program = (unsigned)next; break; }
    }
    if (channels) {
        int moved = (int)cursor + controls.encoders[2];
        cursor = (unsigned)(moved < 0 ? 0 : moved > (int)FM1B_CHANNELS - 1 ? (int)FM1B_CHANNELS - 1 : moved);
        if (controls.encoders[0])
            channel_program_step(cursor, controls.encoders[0]);
        if (controls.encoders[1]) {                           /* ALGORITHM: the selected channel's volume, 4 steps a click */
            struct fm1b_channel info;
            fm1b_channel_info(cursor, &info);
            int volume = (int)info.volume + controls.encoders[1] * 4;
            sf2player_channel_control(cursor, 7U, (unsigned)(volume < 0 ? 0 : volume > 127 ? 127 : volume));
        }
    }
    for (unsigned which = 4; which <= 5; ++which) {
        if (!controls.encoders[which])
            continue;
        struct fm1b_channel info;
        if (channels)
            fm1b_channel_info(cursor, &info);
        int level = channels ? (int)(which == 4 ? info.reverb : info.chorus) : (int)(which == 4 ? sf2fx_reverb() : sf2fx_chorus());
        level += controls.encoders[which] * 4;
        level = level < 0 ? 0 : level > 127 ? 127 : level;
        if (channels)
            sf2player_channel_control(cursor, which == 4 ? 91U : 93U, (unsigned)level);
        else if (which == 4)
            sf2fx_set((unsigned)level, sf2fx_chorus());
        else
            sf2fx_set(sf2fx_reverb(), (unsigned)level);
    }
    if (controls.encoders[3]) {                               /* KNOB2: pan, for everything or for the selected channel */
        struct fm1b_channel info;
        if (channels)
            fm1b_channel_info(cursor, &info);
        int pan = (channels ? (int)info.pan : (int)sf2fx_pan()) + controls.encoders[3] * 4;
        pan = pan < 0 ? 0 : pan > 127 ? 127 : pan;
        if (channels)
            sf2player_channel_control(cursor, 10U, (unsigned)pan);
        else
            sf2fx_set_pan((unsigned)pan);
    }
    uint32_t pressed = controls.buttons & ~previous_buttons;
    previous_buttons = controls.buttons;
    if ((pressed & 8U) && fm1b_has_drums())                  /* SCL: button 3 switches between the program and the drum kit */
        drum_mode ^= 1U;
    if (pressed & (1U << 8)) {                                /* HOME: the program page or the channel page */
        page = (page == PAGE_INFO ? info_return : page) == PAGE_CHANNELS ? PAGE_PROGRAM : PAGE_CHANNELS;
        channels = page == PAGE_CHANNELS;
        page_clear();
    }
    if (pressed & (1U << BUTTON_SAVE))                        /* SAVE: count the dropped notes from zero */
        fm1b_drops_reset();
    if (pressed & (1U << BUTTON_REC)) {                       /* REC: the info page, and back */
        if (page == PAGE_INFO)
            page = info_return;
        else {
            info_return = page;
            page = PAGE_INFO;
            info_compute();
        }
        page_clear();
    }
#if FM1_MIDIPLAY
    if (pressed & (1U << BUTTON_PLAY)) {                      /* PLAY: the song from the start, or stop it */
        struct song_state so;
        song_state(&so);
        if (so.playing) song_stop();
        else song_play();
    }
#endif
#if FM1B_FLASH_PROBE
    if (page == PAGE_INFO && (pressed & (1U << BUTTON_ARP)))      /* ARP on the info page: the voices' samples from the flash or the SRAM probe */
        fm1b_probe_set(!fm1b_probe());
#endif
#if defined(FM1_CPU1_TEST) && FM1_CPU1_TEST == 2
    if (page == PAGE_INFO && (pressed & (1U << BUTTON_ARP)))      /* ARP on the info page: one job on the CPU1 the start left running */
        cpu1_boot_job(&cpu1_boot_state);
#elif defined(FM1_CPU1_TEST) && FM1_CPU1_TEST
    if (page == PAGE_INFO && (pressed & (1U << BUTTON_ARP)))      /* ARP on the info page: one start of the second core */
        cpu1_test_try(&cpu1_tries);
    if (page == PAGE_INFO && (pressed & (1U << BUTTON_SEQ)))      /* SEQ on the info page: the interrupts off while it starts */
        cpu1_tries.mask ^= 1U;
#endif
    if (channels && (pressed & (1U << BUTTON_ARP))) {             /* ARP: mute or unmute the selected channel, while it plays */
        struct fm1b_channel info;
        fm1b_channel_info(cursor, &info);
        sf2player_channel_mute(cursor, !info.muted);
    }
    if (channels && (pressed & (1U << BUTTON_SEQ))) {             /* SEQ: solo the selected channel; again on it: no solo */
        struct fm1b_channel info;
        fm1b_channel_info(cursor, &info);
        sf2player_channel_solo(info.solo ? -1 : (int)cursor);
    }
    if (channels && (pressed & (1U << BUTTON_EDIT))) {            /* EDIT: arm or disarm the selected row (MIDI channel 1 when armed) */
        armed ^= (uint16_t)(1U << cursor);
        arm_midi[cursor] = 0;
    }
    if (channels && (pressed & (1U << BUTTON_GLO)) && ((armed >> cursor) & 1U))   /* GLO: the armed row's MIDI channel up one, 16 to 1 */
        arm_midi[cursor] = (uint8_t)((arm_midi[cursor] + 1U) & 15U);
    leds_update();
    if (pressed & 3U) {                                       /* OCT-: bit 0, OCT+: bit 1 */
        octave += (int)((pressed >> 1) & 1U) - (int)(pressed & 1U);
        octave = octave < -2 ? -2 : octave > 2 ? 2 : octave;
    }
    uint32_t changed = controls.keys ^ previous_keys;
    for (unsigned key = 0; key < KEYS; ++key) {
        if (!(changed & (1U << key)))
            continue;
        unsigned on = (controls.keys >> key) & 1U;
        if (on) {
            int note = FIRST_NOTE + (int)key + 12 * octave;
            sounding[key] = (uint8_t)(note < 0 ? 0 : note > 127 ? 127 : note);
            sounding_drum = (sounding_drum & ~(1U << key)) | (drum_mode << key);
            /* the channel page: the keys play every armed row, or the selected channel when none is armed */
            sounding_rows[key] = (uint16_t)(channels ? (armed ? armed : 1U << cursor) : 0U);
        }
        if (sounding_rows[key]) {
            for (unsigned r = 0; r < FM1B_CHANNELS; ++r)
                if ((sounding_rows[key] >> r) & 1U)
                    row_play(r, sounding[key], on ? 100U : 0U);
        } else if ((sounding_drum >> key) & 1U)
            sf2player_drum(sounding[key], on ? 100U : 0U);
        else
            sf2player_play(sounding[key], on ? 100U : 0U);
        fm1_led_set(14 + key, (int)on);
    }
    previous_keys = controls.keys;
    static uint32_t last_draw;
    uint32_t now = fm1_millis();
    if (now - last_draw >= 40U) {                             /* the LCD is slow; 25 updates a second is plenty */
        last_draw = now;
        lines_limited = lines_start != 255;                   /* line_show: at most LINES_PER_UPDATE changed lines, in turn */
        if (!lines_limited) lines_start = 0;
        lines_drawn = lines_changed = 0;
        screen_update();
        /* the next update starts after the last line drawn, if changed lines were left; else from the top */
        lines_start = lines_limited && lines_drawn >= LINES_PER_UPDATE && lines_changed > lines_last + 1U ? (uint8_t)(lines_last + 1U) : 0;
        lines_limited = 0;                                    /* line_show outside a screen update (safe mode) draws at once */
#if FM1_STATS_MIDI
        uint32_t took = fm1_millis() - now;
        if (took > window_screen_max) window_screen_max = took;
#endif
    }
}
#if FM1_STATS_MIDI
/* The control task's step, timed: the longest step and the longest screen update since the last stats message (values 20 and 21). */
void groove_screen_control(void)
{
    uint32_t start = fm1_millis();
    control_step();
    uint32_t took = fm1_millis() - start;
    if (took > window_step_max) window_step_max = took;
}
#endif

/* ---- the bank --------------------------------------------------------------------------------------------- */
void groove_midi_init(void)
{
#if FM1B_IN_FIRMWARE
    fm1b_device_init();
#endif
}

/* ---- what the library calls and the player does not need ---------------------------------------------------- */
void groove_midi_clock_reset(void) {}
void groove_midi_panic(void) { fm1b_panic(); }
void groove_midi_poll(void) {}
FM1_DSP_CODE void groove_midi_clock(unsigned owner, unsigned tempo) { (void)owner; (void)tempo; }
FM1_DSP_CODE void groove_midi_transport(unsigned owner, uint8_t status) { (void)owner; (void)status; }
FM1_DSP_CODE void groove_midi_track_reset(unsigned track) { (void)track; }
FM1_DSP_CODE unsigned groove_midi_mode(unsigned track) { (void)track; return 0; }
/* The engine reports the notes its tracks play (sequencer, MIDI input): forward them to the player. */
FM1_DSP_CODE void groove_midi_note(unsigned track, unsigned note, unsigned velocity)
{
    (void)track;
    sf2player_play(note, velocity);
}

void groove_fm_library_init(void) {}
FM1_DSP_CODE void groove_fm_program(uint8_t patch[156], unsigned program_number)
{
    (void)program_number;
    memset(patch, 0, 156);                                    /* an all-zero DX7 voice is silent */
}

void groove_project_init(void) {}
void groove_project_poll(void) {}
void groove_project_metadata(const struct groove_project *p) { (void)p; }
void groove_project_name(char name[13]) { memcpy(name, "SF2 PLAYER\0\0", 13); }
struct groove_project *groove_project_scratch(void)
{
    static struct groove_project scratch;
    return &scratch;
}
void groove_samples_init(void) {}
void groove_samples_poll(void) {}
void groove_transfer_init(void) {}
void groove_transfer_poll(void) { sf2_upload_poll(); }
#endif
