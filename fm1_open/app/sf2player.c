#ifndef FM1_HOST_TEST
/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* SF2 player firmware: the whole application. libfm1_engine.a expects the groove_* functions the stock panel,
 * project and sequencer code provides; here they are stubs, and the keys play an FM1B bank (fm1b.c) that is mixed
 * into the engine's output by board/audio.c. Nothing of the stock instrument UI, projects or sequencer is built. */
#include "instrument.h"
#include "fm_patch.h"
#include "fm1b.h"
#include "fm1b_config.h"
#include "sf2_upload.h"
#include "sf2fx.h"
#include "tuning_hook.h"
#include "fm1_device.h"
#include <string.h>

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
    {')', 0, 0x41, 0x22, 0x1c, 0}};
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
/* Draw line `n` at y only when its text or colour is not what is on the screen. */
static void line_show(unsigned n, unsigned y, const char *s, unsigned scale, uint16_t color)
{
    if (!strncmp(shown[n], s, LINE_CHARS) && shown_color[n] == color && shown_back[n] == line_back)
        return;
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
static unsigned page;                                     /* 0: the program and its zone, 1: the 16 channels */
static unsigned cursor;                                   /* the channel page's selected channel, 0..15: KNOB1 moves it */
enum { CURSOR_BACK = 0x3a6b };                            /* the selected row's background: slate blue */
static unsigned bar_drawn = 1000;                         /* the level bar's width on the screen (1000: not drawn) */
static uint8_t channel_notes_seen[FM1B_CHANNELS];
static uint32_t channel_hit[FM1B_CHANNELS];               /* fm1_millis() of the latest note-on seen on the channel */

static void page_clear(void)
{
    fm1_screen_fill(0, 0, FM1_SCREEN_WIDTH, FM1_SCREEN_HEIGHT, BACKGROUND);
    memset(shown, 0, sizeof shown);
    memset(shown_color, 0, sizeof shown_color);
    memset(shown_back, 0, sizeof shown_back);
    bar_drawn = 1000;
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
        *d++ = ' ';
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
        line_back = c == cursor ? CURSOR_BACK : BACKGROUND;
        line_show(2 + c, 40 + c * 11, line, 1, color);
        line_back = BACKGROUND;
    }
    line_show(19, 218, "ARP MUTE  SEQ SOLO  ALGORITHM VOLUME", 1, GREY);
    line_show(18, 228, "HOME K1 CH SEL PRG K2 PAN K3/4 FX", 1, GREY);
}

static void screen_update(void)
{
    struct fm1b_status st;
    char line[LINE_CHARS + 1], *d;
    fm1b_status(&st);
    if (!st.ready) {
        if (page) { page = 0; page_clear(); }
        line_show(0, 8, "NO BANK", 2, ORANGE);
        upload_line();
        return;
    }
    if (page) {
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
    if (st.voice_limit < FM1B_VOICES) d = put_text(d, " CPU");   /* the guard holds the limit down */
    if (st.level) { d = put_text(d, "   PHASE "); *d++ = phases[st.phase < 5 ? st.phase : 4]; }
    *d = 0;
    line_show(6, 140, line, 1, GREY);
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
    line_show(7, 228, "SEL PRG SCL DRUM HOME CH K2 PAN K3/4 FX", 1, GREY);
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
static uint8_t sounding_channel[KEYS];                     /* per key: the MIDI channel it started a note on, 255: the panel's */
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
    page = 0;
    cursor = 0;
    leds_shown = 0xffU;
    memset(shown, 0, sizeof shown);
    memset(shown_color, 0, sizeof shown_color);
    memset(shown_back, 0, sizeof shown_back);
    bar_drawn = 1000;
}
/* ARP and SEQ light while the selected channel is muted / soloed (on the channel page only). */
enum { BUTTON_ARP = 10, BUTTON_SEQ = 11 };
static void leds_update(void)
{
    struct fm1b_channel info;
    unsigned leds = 0;
    if (page) {
        fm1b_channel_info(cursor, &info);
        leds = info.muted | info.solo << 1;
    }
    if (leds == leds_shown)
        return;
    leds_shown = leds;
    fm1_led_set(BUTTON_ARP, (int)(leds & 1U));
    fm1_led_set(BUTTON_SEQ, (int)(leds >> 1));
}
void groove_screen_control(void)
{
    sf2_upload_poll();                                        /* a bank frame that arrived over MIDI */
    struct fm1_controls controls;
    fm1_controls_read(&controls);
    (void)groove_master_update(controls.knob);               /* MASTER: the output level on every page; without it the output stays muted */
    /* The knobs: SELECT (encoder 0) programs, ALGORITHM (1) the selected channel's volume, KNOB1 (2) the channel page's
     * cursor, KNOB2 (3) pan, KNOB3 (4) reverb and KNOB4 (5) chorus. On the program page SELECT picks the panel's program,
     * KNOB2 pans the whole mix and KNOB3/KNOB4 set the effects' master levels; on the channel page SELECT picks the selected
     * channel's program, ALGORITHM sets its volume (CC 7), KNOB2 its pan (CC 10) and KNOB3/KNOB4 its sends. */
    if (controls.encoders[0] && !page) {
        int step = controls.encoders[0] > 0 ? 1 : -1;
        for (int clicks = controls.encoders[0] * step; clicks > 0; --clicks)
            for (int next = (int)program + step; next >= 0 && next < 128; next += step)
                if (fm1b_program(0, (unsigned)next) == 0) { program = (unsigned)next; break; }
    }
    if (page) {
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
        if (page)
            fm1b_channel_info(cursor, &info);
        int level = page ? (int)(which == 4 ? info.reverb : info.chorus) : (int)(which == 4 ? sf2fx_reverb() : sf2fx_chorus());
        level += controls.encoders[which] * 4;
        level = level < 0 ? 0 : level > 127 ? 127 : level;
        if (page)
            sf2player_channel_control(cursor, which == 4 ? 91U : 93U, (unsigned)level);
        else if (which == 4)
            sf2fx_set((unsigned)level, sf2fx_chorus());
        else
            sf2fx_set(sf2fx_reverb(), (unsigned)level);
    }
    if (controls.encoders[3]) {                               /* KNOB2: pan, for everything or for the selected channel */
        struct fm1b_channel info;
        if (page)
            fm1b_channel_info(cursor, &info);
        int pan = (page ? (int)info.pan : (int)sf2fx_pan()) + controls.encoders[3] * 4;
        pan = pan < 0 ? 0 : pan > 127 ? 127 : pan;
        if (page)
            sf2player_channel_control(cursor, 10U, (unsigned)pan);
        else
            sf2fx_set_pan((unsigned)pan);
    }
    uint32_t pressed = controls.buttons & ~previous_buttons;
    previous_buttons = controls.buttons;
    if ((pressed & 8U) && fm1b_has_drums())                  /* SCL: button 3 switches between the program and the drum kit */
        drum_mode ^= 1U;
    if (pressed & (1U << 8)) {                                /* HOME: the program page or the channel page */
        page ^= 1U;
        page_clear();
    }
    if (page && (pressed & (1U << BUTTON_ARP))) {             /* ARP: mute or unmute the selected channel, while it plays */
        struct fm1b_channel info;
        fm1b_channel_info(cursor, &info);
        sf2player_channel_mute(cursor, !info.muted);
    }
    if (page && (pressed & (1U << BUTTON_SEQ))) {             /* SEQ: solo the selected channel; again on it: no solo */
        struct fm1b_channel info;
        fm1b_channel_info(cursor, &info);
        sf2player_channel_solo(info.solo ? -1 : (int)cursor);
    }
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
            sounding_channel[key] = (uint8_t)(page ? cursor : 255U);   /* the channel page: the keys play the selected channel */
        }
        if (sounding_channel[key] != 255U) {
            if (sounding_channel[key] == 9U)
                sf2player_channel_drum(9U, sounding[key], on ? 100U : 0U);
            else
                sf2player_channel_play(sounding_channel[key], sounding[key], on ? 100U : 0U);
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
        screen_update();
    }
}

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
