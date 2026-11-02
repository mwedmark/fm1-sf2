/* Exports for test_rate.js: the SF2 player's whole output stage (sf2out.c: fm1b, sf2fx and, below 44.1 kHz, the resampler),
 * built once at 44.1 kHz and once at 32 kHz (-DFM1B_RATE). */
#include "fm1b.h"
#include "sf2fx.h"
#include "sf2out.h"
#include <stdint.h>
#include "fm1b_dqt.inc"
#define EXPORT __attribute__((used, visibility("default")))
enum { BLOCK = 128, BANK_MAX = 1 << 20 };
static uint8_t bank_image[BANK_MAX];
static int32_t out[BLOCK * 2];
EXPORT uint8_t *rt_bank(void) { return bank_image; }
EXPORT int rt_bind(unsigned bytes) { return fm1b_bind(bank_image, bytes, fm1b_dqt); }
EXPORT int rt_program(unsigned program) { return fm1b_program(0, program); }
EXPORT int32_t *rt_out(void) { return out; }
EXPORT void rt_block(void) { for (unsigned i = 0; i < BLOCK * 2; ++i) out[i] = 0; sf2out_render(out); }
EXPORT void rt_note(unsigned channel, unsigned note, unsigned velocity) { fm1b_channel_note(channel, note, velocity, 100); }
EXPORT void rt_control(unsigned channel, unsigned controller, unsigned value) { fm1b_channel_control(channel, controller, value); }
EXPORT void rt_fx(unsigned reverb, unsigned chorus) { sf2fx_set(reverb, chorus); }
EXPORT unsigned rt_rate(void) { return FM1B_RATE; }
