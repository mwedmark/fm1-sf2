/* Exports for compare_fluid.js: the SF2 player's whole output stage (fm1b, sf2fx, sf2out with the resampler at 32 kHz), fed
 * MIDI the way transfer.c feeds it on the device. */
#include "fm1b.h"
#include "sf2fx.h"
#include "sf2out.h"
#include <stdint.h>
#include "fm1b_dqt.inc"
#define EXPORT __attribute__((used, visibility("default")))
enum { BLOCK = 128, BANK_MAX = 1 << 23 };
static uint8_t bank_image[BANK_MAX];
static int32_t out[BLOCK * 2];
EXPORT uint8_t *cf_bank(void) { return bank_image; }
EXPORT int cf_bind(unsigned bytes) { return fm1b_bind(bank_image, bytes, fm1b_dqt); }
EXPORT int cf_program(unsigned program) { return fm1b_program(0, program); }
EXPORT int32_t *cf_out(void) { return out; }
EXPORT void cf_block(void) { for (unsigned i = 0; i < BLOCK * 2; ++i) out[i] = 0; sf2out_render(out); }
EXPORT void cf_note(unsigned channel, unsigned note, unsigned velocity) { fm1b_channel_note(channel, note, velocity, 100); }
EXPORT void cf_drum(unsigned channel, unsigned note, unsigned velocity) { fm1b_channel_drum_note(channel, note, velocity, 100); }
EXPORT void cf_channel_program(unsigned channel, unsigned bank, unsigned program) { fm1b_channel_program(channel, bank, program); }
EXPORT void cf_control(unsigned channel, unsigned controller, unsigned value) { fm1b_channel_control(channel, controller, value); }
EXPORT void cf_bend(unsigned channel, unsigned value) { fm1b_channel_bend(channel, value); }
EXPORT void cf_fx(unsigned reverb, unsigned chorus) { sf2fx_set(reverb, chorus); }
EXPORT unsigned cf_rate(void) { return FM1B_RATE; }
EXPORT unsigned cf_voice_max(void) { return FM1B_VOICES; }
EXPORT unsigned cf_sounding(void) { return fm1b_voices_sounding(); }
EXPORT unsigned cf_limited(void) { return sf2out_limited(); }
#if FM1B_SPLIT
/* test_split.js: the share of the voices' cost for the other core, and what the split did */
EXPORT void cf_split_share(unsigned percent) { fm1b_split_share = (uint8_t)percent; }
static struct fm1b_split_stats split_read(void) { struct fm1b_split_stats s; fm1b_split_stats(&s); return s; }
EXPORT unsigned cf_split_blocks(void) { return split_read().blocks; }
EXPORT unsigned cf_split_other(void) { return split_read().voices_other; }
EXPORT unsigned cf_split_all(void) { return split_read().voices_all; }
#endif
EXPORT unsigned cf_cut(void)
{
    struct fm1b_status st;
    fm1b_status(&st);
    return st.voices_cut;
}
/* what the limit took: 0 notes still sounding (HELD), 1 releasing ones (TAIL), 2 the CPU guard's */
EXPORT unsigned cf_drops(unsigned which)
{
    struct fm1b_status st;
    fm1b_status(&st);
    return which == 0 ? st.drops_held : which == 1 ? st.drops_tail : st.drops_guard;
}
