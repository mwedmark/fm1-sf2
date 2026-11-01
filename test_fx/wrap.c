/* Exports for test_fx.js: sf2fx.c on WebAssembly with a block buffer in and out. */
#include "sf2fx.h"
#include <stdint.h>
#define EXPORT __attribute__((used, visibility("default")))
static int32_t dry[128], out[256];
EXPORT int32_t *fx_dry(void) { return dry; }
EXPORT int32_t *fx_out(void) { return out; }
EXPORT void fx_set(unsigned reverb, unsigned chorus) { sf2fx_set(reverb, chorus); }
EXPORT void fx_pan(unsigned pan) { sf2fx_set_pan(pan); }
EXPORT void fx_block(void)
{
    for (unsigned i = 0; i < 256; ++i)
        out[i] = 0;
    sf2fx_mix(dry, out, 128);
}
