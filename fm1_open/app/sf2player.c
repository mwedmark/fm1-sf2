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

/* ---- the panel: F3..G5 are 27 keys, bit 0 = F3 (MIDI 53) -------------------------------------------------- */
enum { FIRST_NOTE = 53, KEYS = 27, BACKGROUND = 0x0210 /* dark teal */ };
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
enum { WHITE = 0xffff, GREY = 0x8410, YELLOW = 0xffe0, GREEN = 0x07e0, ORANGE = 0xfd20, CYAN = 0x07ff, BAR_BACK = 0x2104,
       LINE_CHARS = 41, LINES = 10, MAX_ROW_PIXELS = FM1_SCREEN_WIDTH * 14 };
static uint16_t row_pixels[MAX_ROW_PIXELS];
static char shown[LINES][LINE_CHARS + 1];

static void text_line(unsigned y, const char *s, unsigned scale, uint16_t color)
{
    unsigned h = 7U * scale, cell = 6U * scale;
    for (unsigned i = 0; i < FM1_SCREEN_WIDTH * h; ++i)
        row_pixels[i] = BACKGROUND;
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
/* Draw line `n` at y only when its text is not what is on the screen. */
static void line_show(unsigned n, unsigned y, const char *s, unsigned scale, uint16_t color)
{
    if (!strncmp(shown[n], s, LINE_CHARS))
        return;
    text_line(y, s, scale, color);
    strncpy(shown[n], s, LINE_CHARS);
    shown[n][LINE_CHARS] = 0;
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

static void screen_update(void)
{
    struct fm1b_status st;
    char line[LINE_CHARS + 1], *d;
    fm1b_status(&st);
    if (!st.ready) {
        line_show(0, 8, "NO BANK", 2, ORANGE);
        upload_line();
        return;
    }
    d = put_number(line, st.program + 1, 3);
    *d++ = ' ';
    for (unsigned i = 0; i < 16 && st.name[i]; ++i) *d++ = st.name[i];
    *d = 0;
    line_show(0, 8, line, 2, WHITE);

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
    static unsigned bar_drawn = 1000;
    unsigned bar = (unsigned)st.level * 220U / 255U;
    if (bar != bar_drawn) {
        fm1_screen_fill(10, 124, 220, 10, BAR_BACK);
        if (bar) fm1_screen_fill(10, 124, bar, 10, st.phase == 4 ? ORANGE : GREEN);
        bar_drawn = bar;
    }
    d = put_text(line, "VOICES ");
    d = put_number(d, st.voices, 0);
    d = put_text(d, "/");
    d = put_number(d, FM1B_VOICES, 0);
    if (st.level) { d = put_text(d, "   PHASE "); *d++ = phases[st.phase < 5 ? st.phase : 4]; }
    *d = 0;
    line_show(6, 140, line, 1, GREY);
    upload_line();
    d = put_text(line, "REVERB ");
    d = put_number(d, (int32_t)sf2fx_reverb(), 0);
    d = put_text(d, "  CHORUS ");
    d = put_number(d, (int32_t)sf2fx_chorus(), 0);
    d = put_text(d, "  OCT ");
    if (octave > 0) *d++ = '+';
    d = put_number(d, octave, 0);
    *d = 0;
    line_show(9, 158, line, 1, YELLOW);
    line_show(7, 228, "ENCODER 1 PROG  2 REVERB  3 CHORUS", 1, GREY);
}

static uint32_t previous_keys, previous_buttons;
static uint8_t sounding[KEYS];                           /* the note each key started, so a release stops that one */
static unsigned program;

void groove_screen_init(void)
{
    fm1_controls_init();
    fm1_screen_init();
    fm1_screen_fill(0, 0, FM1_SCREEN_WIDTH, FM1_SCREEN_HEIGHT, BACKGROUND);
    previous_keys = previous_buttons = 0;
    octave = 0;
    program = 0;
    memset(shown, 0, sizeof shown);
}
void groove_screen_control(void)
{
    sf2_upload_poll();                                        /* a bank frame that arrived over MIDI */
    struct fm1_controls controls;
    fm1_controls_read(&controls);
    (void)groove_master_update(controls.knob);               /* the volume knob; without it the output stays muted */
    if (controls.encoders[0]) {                               /* first encoder: next / previous program the bank has */
        int step = controls.encoders[0] > 0 ? 1 : -1;
        for (int clicks = controls.encoders[0] * step; clicks > 0; --clicks)
            for (int next = (int)program + step; next >= 0 && next < 128; next += step)
                if (fm1b_program(0, (unsigned)next) == 0) { program = (unsigned)next; break; }
    }
    for (unsigned which = 1; which <= 2; ++which) {            /* encoders 2 and 3: reverb and chorus send */
        if (!controls.encoders[which])
            continue;
        int level = (int)(which == 1 ? sf2fx_reverb() : sf2fx_chorus()) + controls.encoders[which] * 4;
        level = level < 0 ? 0 : level > 127 ? 127 : level;
        if (which == 1) sf2fx_set((unsigned)level, sf2fx_chorus());
        else sf2fx_set(sf2fx_reverb(), (unsigned)level);
    }
    uint32_t pressed = controls.buttons & ~previous_buttons;
    previous_buttons = controls.buttons;
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
        }
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
