/* Runs the SF2 player firmware (fm1_open/app: sf2player.c, fm1b.c, sf2fx.c, sf2out.c, song.c, transfer.c) on a PC with a virtual
 * FM-1 around it, for the walk-through video (make_video.py). The firmware's sources are compiled unchanged; this file stands in for
 * the board: the screen, the panel's controls and LEDs, the millisecond clock, the sample flash, and the audio interrupt.
 *
 *   fm1_video <bank.fm1b> <song.fm1s> <script.txt> <seconds> <out_dir>
 *
 * It writes <out_dir>/audio.wav (44.1 kHz stereo, 16 bit) and <out_dir>/frames.bin: per video frame (30 a second) the 240x240
 * RGB565 screen, then a uint64 of LED states (bits 0..40), uint32 buttons held, uint32 keys held, int16 detents turned so far
 * on each of the seven encoders, and uint8 activity of each (nonzero for a moment after a detent).
 *
 * The script is one action per line, `<ms> <action> <arg> [count]`:
 *   press <button>        hold a button (0..13) for 120 ms           key <key> <ms>     hold a key (0..26) for <ms>
 *   turn <encoder> <n>    n detents (negative: left), one each 70 ms  master <adc>       the MASTER knob (0..1023)
 *
 * The CPU load the panel shows comes from a model of the FM-1's audio interrupt (a base load and a share per voice, measured from
 * the device's own screen); the PC renders far faster than the FM-1, so its own time would say nothing. */
#include "fm1.h"
#include "fm1_device.h"
#include "fm1b.h"
#include "sf2out.h"
#include "song.h"
#include "fm1b_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void groove_midi_init(void);
void groove_screen_init(void);
void groove_screen_control(void);

/* ---- the board ------------------------------------------------------------------------------------------------- */
static uint16_t screen[FM1_SCREEN_WIDTH * FM1_SCREEN_HEIGHT];
static uint64_t leds;
static uint32_t now_ms, spin;
static struct fm1_controls held;                           /* keys, buttons and the MASTER knob as the script sets them */
static int16_t pending_turns[7];                           /* detents not yet read by the firmware */
static int16_t turned[7];                                  /* detents since start, for the picture of the knob */
static uint8_t *flash;                                     /* the sample flash, with the bank at FM1B_FLASH_OFFSET */
static uint32_t flash_bytes;

void fm1_screen_init(void) {}
void fm1_screen_fill(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t c)
{
    for (unsigned r = y; r < y + h && r < FM1_SCREEN_HEIGHT; ++r)
        for (unsigned k = x; k < x + w && k < FM1_SCREEN_WIDTH; ++k) screen[r * FM1_SCREEN_WIDTH + k] = c;
}
int fm1_screen_blit(unsigned x, unsigned y, unsigned w, unsigned h, const uint16_t *p, size_t count)
{
    if (x + w > FM1_SCREEN_WIDTH || y + h > FM1_SCREEN_HEIGHT || count < (size_t)w * h) return -1;
    for (unsigned r = 0; r < h; ++r) memcpy(screen + (y + r) * FM1_SCREEN_WIDTH + x, p + r * w, w * 2U);
    return 0;
}
void fm1_controls_init(void) {}
void fm1_controls_read(struct fm1_controls *s)
{
    *s = held;
    memcpy(s->encoders, pending_turns, sizeof pending_turns);
    memset(pending_turns, 0, sizeof pending_turns);
}
void fm1_led_set(unsigned key, int on)
{
    if (key < 64) leds = on ? leds | 1ULL << key : leds & ~(1ULL << key);
}
/* song_stop() waits up to 10 ms for room in the voice queue; the virtual clock stands still inside a control call, so after
 * many reads in one millisecond it moves on, as the real one would. */
uint32_t fm1_millis(void) { return ++spin > 100000U ? now_ms + 20U : now_ms; }
uint32_t fm1_device_irq_save(void) { return 0; }
void fm1_device_irq_restore(uint32_t s) { (void)s; }
const uint8_t *fm1_flash_map(uint32_t offset, size_t bytes)
{
    return offset + bytes <= flash_bytes ? flash + offset : NULL;
}
int groove_master_update(unsigned adc) { (void)adc; return 0; }
int fm1_midi_send(const uint8_t *m, size_t n) { (void)m; (void)n; return 0; }
int fm1_midi_send_port(unsigned p, const uint8_t *m, size_t n) { (void)p; (void)m; (void)n; return 0; }
unsigned fm1_midi_input_source(void) { return FM1_MIDI_USB; }
void sf2_upload_offer(const uint8_t *m, size_t n) { (void)m; (void)n; }
void sf2_upload_poll(void) {}
void sf2_upload_state(unsigned *seen, unsigned *taken, unsigned *cmd, unsigned *status) { *seen = *taken = *cmd = *status = 0; }
/* Linked but never called on this path (the firmware's keep_linked table). */
int fm1_ble_set_role(enum fm1_ble_role r) { (void)r; return 0; }
int fm1_ble_scan(void) { return 0; }
int fm1_ble_connect(unsigned i) { (void)i; return 0; }
int fm1_ble_disconnect(void) { return 0; }
void fm1_ble_get_status(struct fm1_ble_status *s) { memset(s, 0, sizeof *s); }
int fm1_ble_get_device(unsigned i, struct fm1_ble_device *d) { (void)i; memset(d, 0, sizeof *d); return -1; }
int fm1_bluetooth_configure(unsigned f) { (void)f; return 0; }
void fm1_bluetooth_get_status(struct fm1_bluetooth_status *s) { memset(s, 0, sizeof *s); }
/* The info page's CRC32 of the application image: there is no image here, so its size symbols are 0 (build.sh, --defsym) and the
 * video does not show that page. */
char text_begin[1];

/* ---- the script --------------------------------------------------------------------------------------------------- */
enum { PRESS_MS = 120, TURN_MS = 70 };
struct action { uint32_t ms; char what[8]; int a, b; };
static struct action *actions;
static unsigned n_actions;
struct timed { uint32_t start, end; int kind, which; };     /* kind 0 button, 1 key, 2 encoder turning */
static struct timed active[256];
static unsigned n_active;

static void load_script(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(1); }
    char line[256];
    while (fgets(line, sizeof line, f)) {
        struct action a = {0};
        if (line[0] == '#' || sscanf(line, "%u %7s %d %d", &a.ms, a.what, &a.a, &a.b) < 3) continue;
        actions = realloc(actions, (n_actions + 1) * sizeof *actions);
        actions[n_actions++] = a;
    }
    fclose(f);
}
static void start(int kind, int which, uint32_t ms, uint32_t length)
{
    if (n_active < 256) active[n_active++] = (struct timed){ms, ms + length, kind, which};
}
static int turning[7], turn_left[7], turn_step[7];
static uint32_t turn_next[7];
static uint8_t encoder_glow[7];
static void run_script(void)
{
    for (unsigned i = 0; i < n_actions; ++i) {
        struct action *a = &actions[i];
        if (a->ms != now_ms) continue;
        if (!strcmp(a->what, "press")) start(0, a->a, now_ms, PRESS_MS);
        else if (!strcmp(a->what, "key")) start(1, a->a, now_ms, a->b ? (uint32_t)a->b : 300U);
        else if (!strcmp(a->what, "master")) held.knob = (uint16_t)a->a;
        else if (!strcmp(a->what, "turn") && a->a >= 0 && a->a < 7 && a->b) {
            turning[a->a] = 1;
            turn_left[a->a] = abs(a->b);
            turn_step[a->a] = a->b > 0 ? 1 : -1;
            turn_next[a->a] = now_ms;
        }
    }
    held.buttons = held.keys = 0;
    for (unsigned i = 0; i < n_active; ++i) {
        struct timed *t = &active[i];
        if (now_ms < t->start || now_ms >= t->end) continue;
        if (t->kind == 0) held.buttons |= 1U << t->which;
        else held.keys |= 1U << t->which;
    }
    for (int e = 0; e < 7; ++e) {
        if (turning[e] && now_ms >= turn_next[e]) {
            pending_turns[e] += (int16_t)turn_step[e];
            turned[e] += (int16_t)turn_step[e];
            turn_next[e] = now_ms + TURN_MS;
            if (--turn_left[e] == 0) turning[e] = 0;
            encoder_glow[e] = 12;                          /* frames the knob stays lit after its last detent */
        }
    }
}

/* ---- output ---------------------------------------------------------------------------------------------------- */
static void wav_header(FILE *f, uint32_t frames)
{
    uint32_t data = frames * 4U, riff = 36U + data, rate = 44100, byte_rate = rate * 4U, fmt = 16;
    uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt, 4, 1, f);
    fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&byte_rate, 4, 1, f);
    fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
}
static uint32_t crc32_of(const uint8_t *p, size_t n)
{
    uint32_t c = ~0U;
    while (n--) { c ^= *p++; for (int k = 0; k < 8; ++k) c = c & 1U ? 0xedb88320U ^ (c >> 1) : c >> 1; }
    return ~c;
}
static uint8_t *read_file(const char *path, uint32_t *bytes)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *p = malloc((size_t)n);
    if (fread(p, 1, (size_t)n, f) != (size_t)n) { perror(path); exit(1); }
    fclose(f);
    *bytes = (uint32_t)n;
    return p;
}

int main(int argc, char **argv)
{
    if (argc != 6) { fprintf(stderr, "fm1_video <bank.fm1b> <song.fm1s> <script.txt> <seconds> <out_dir>\n"); return 2; }
    uint32_t bank_bytes, song_bytes;
    uint8_t *bank = read_file(argv[1], &bank_bytes), *song = read_file(argv[2], &song_bytes);
    if (bank_bytes > FM1B_FLASH_BYTES) { fprintf(stderr, "the bank is larger than the sample flash\n"); return 1; }
    flash_bytes = FM1B_FLASH_OFFSET + FM1B_FLASH_BYTES;
    flash = calloc(1, flash_bytes);
    memset(flash, 0xff, flash_bytes);                      /* erased flash */
    memcpy(flash + FM1B_FLASH_OFFSET, bank, bank_bytes);
    load_script(argv[3]);
    double seconds = atof(argv[4]);
    char path[512];
    snprintf(path, sizeof path, "%s/audio.wav", argv[5]);
    FILE *wav = fopen(path, "wb");
    snprintf(path, sizeof path, "%s/frames.bin", argv[5]);
    FILE *frames = fopen(path, "wb");
    if (!wav || !frames) { perror(argv[5]); return 1; }
    held.knob = 900;

    groove_midi_init();
    groove_screen_init();
    unsigned status = song_begin(song_bytes);
    if (!status) status = song_write(0, song, song_bytes);
    if (!status) status = song_end(crc32_of(song, song_bytes));
    if (status) { fprintf(stderr, "the song was refused: R%u\n", status); return 1; }

    const uint32_t total_frames = (uint32_t)(seconds * 44100.0);
    wav_header(wav, 0);
    static int32_t block[FM1_BLOCK_FRAMES * 2];
    static int16_t pcm[FM1_BLOCK_FRAMES * 2];
    uint32_t rendered = 0, video_frame = 0;
    int32_t peak = 0;
    while (rendered < total_frames) {
        /* the control task and the script, for every millisecond up to the end of this block */
        uint32_t block_end_ms = (uint32_t)((uint64_t)(rendered + FM1_BLOCK_FRAMES) * 1000U / 44100U);
        while (now_ms < block_end_ms) {
            ++now_ms;
            spin = 0;
            run_script();
            groove_screen_control();
        }
        memset(block, 0, sizeof block);
        if (fm1b_ready()) {
            sf2out_render(block);
            unsigned voices = fm1b_voices_sounding();
            sf2out_load(300U + 210U * voices, 10000U);      /* 3 % plus 2.1 % a voice: the model of the interrupt's time */
        }
        for (unsigned i = 0; i < FM1_BLOCK_FRAMES * 2; ++i) {
            int32_t v = block[i] >> 8;                     /* Q23 to 16 bit */
            if (abs(v) > peak) peak = abs(v);
            pcm[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
        fwrite(pcm, 2, FM1_BLOCK_FRAMES * 2, wav);
        rendered += FM1_BLOCK_FRAMES;
        /* a video frame each 1/30 s */
        while ((uint64_t)video_frame * 44100U < (uint64_t)rendered * 30U) {
            fwrite(screen, 2, FM1_SCREEN_WIDTH * FM1_SCREEN_HEIGHT, frames);
            fwrite(&leds, 8, 1, frames);
            fwrite(&held.buttons, 4, 1, frames);
            fwrite(&held.keys, 4, 1, frames);
            fwrite(turned, 2, 7, frames);
            uint8_t glow[7];
            for (int e = 0; e < 7; ++e) { glow[e] = encoder_glow[e]; if (encoder_glow[e]) --encoder_glow[e]; }
            fwrite(glow, 1, 7, frames);
            ++video_frame;
        }
    }
    fseek(wav, 0, SEEK_SET);
    wav_header(wav, rendered);
    fclose(wav);
    fclose(frames);
    fprintf(stderr, "%u audio frames, %u video frames, peak %d of 32767\n", rendered, video_frame, peak);
    return 0;
}
