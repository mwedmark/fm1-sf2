/* Exports for test_multitimbral.js: the real fm1b.c on WebAssembly with a bank, a block buffer and the calls the firmware makes. */
#include "fm1b.h"
#include <stdint.h>
#include "fm1b_dqt.inc"                                  /* the SEA5 quantizer table the firmware binds with */
#define EXPORT __attribute__((used, visibility("default")))
enum { BLOCK = 128, BANK_MAX = 1 << 20 };
static uint8_t bank_image[BANK_MAX];
static int32_t out[BLOCK], rev[BLOCK], cho[BLOCK], cut_left[BLOCK], cut_right[BLOCK];
static uint8_t sends[2];
EXPORT uint8_t *mt_bank(void) { return bank_image; }
EXPORT unsigned mt_bank_capacity(void) { return BANK_MAX; }
EXPORT int mt_bind(unsigned bytes) { return fm1b_bind(bank_image, bytes, fm1b_dqt); }
EXPORT int32_t *mt_out(void) { return out; }
EXPORT void mt_block(void)                               /* one audio block, as board/audio.c renders it */
{
    for (unsigned i = 0; i < BLOCK; ++i)
        out[i] = 0;
    fm1b_render(0, out, BLOCK);
}
EXPORT int mt_program(unsigned bank, unsigned program) { return fm1b_program(bank, program); }
EXPORT void mt_panel_note(unsigned note, unsigned velocity) { fm1b_note(0, note, velocity, 100); }
EXPORT void mt_note(unsigned channel, unsigned note, unsigned velocity) { fm1b_channel_note(channel, note, velocity, 100); }
EXPORT void mt_drum(unsigned channel, unsigned note, unsigned velocity) { fm1b_channel_drum_note(channel, note, velocity, 100); }
EXPORT void mt_channel_program(unsigned channel, unsigned bank, unsigned program) { fm1b_channel_program(channel, bank, program); }
EXPORT void mt_control(unsigned channel, unsigned controller, unsigned value) { fm1b_channel_control(channel, controller, value); }
EXPORT unsigned mt_multitimbral(void) { return fm1b_multitimbral(); }
EXPORT void mt_panic(void) { fm1b_panic(); }
EXPORT unsigned mt_voices(void)
{
    struct fm1b_status st;
    fm1b_status(&st);
    return st.voices;
}
EXPORT unsigned mt_status(unsigned field)                /* 0 channels, 1 program channel, 2 program number */
{
    struct fm1b_status st;
    fm1b_status(&st);
    return field == 0 ? st.channels : field == 1 ? st.program_channel : st.program_number;
}
EXPORT void mt_panel_drum(unsigned note, unsigned velocity) { fm1b_drum_note(0, note, velocity, 100); }
EXPORT unsigned mt_has_drums(void) { return fm1b_has_drums(); }
EXPORT void mt_bend(unsigned channel, unsigned value) { fm1b_channel_bend(channel, value); }
EXPORT int mt_info(unsigned channel, unsigned field)    /* 0 own, 1 kit, 2 program, 3 bend cents, 4 mod, 5 pedal, 6 voices, 7 notes, 8 volume, 9 reverb, 10 chorus, 11 pan */
{
    struct fm1b_channel c;
    fm1b_channel_info(channel, &c);
    switch (field) {
    case 0: return c.own;
    case 1: return c.kit;
    case 2: return c.program;
    case 3: return c.bend_cents;
    case 4: return c.mod;
    case 5: return c.pedal;
    case 6: return c.voices;
    case 7: return c.notes;
    case 9: return c.reverb;
    case 10: return c.chorus;
    case 11: return c.pan;
    default: return c.volume;
    }
}
EXPORT void mt_follow(unsigned channel) { fm1b_channel_follow(channel); }
EXPORT unsigned mt_has_program(unsigned bank, unsigned program) { return fm1b_has_program(bank, program); }
EXPORT int32_t *mt_rev(void) { return rev; }
EXPORT int32_t *mt_cut(unsigned right) { return right ? cut_right : cut_left; }   /* the pan sides of mt_block_sends */
EXPORT int32_t *mt_cho(void) { return cho; }
EXPORT unsigned mt_send(unsigned which) { return sends[which & 1U]; }
EXPORT unsigned mt_block_sends(void)                     /* one block as board/audio.c renders it with the effect sends */
{
    for (unsigned i = 0; i < BLOCK; ++i)
        out[i] = 0;
    int32_t *sides[2] = {cut_left, cut_right};
    return fm1b_render_sends(0, out, rev, cho, sides, BLOCK, sends);
}
EXPORT unsigned mt_voice_max(void) { return FM1B_VOICES; }
