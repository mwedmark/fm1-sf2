# FM1B, a sound bank for the FM-1

A bank is one file that the CPU can use where it lies (flash, SD buffer, RAM). Nothing is parsed and nothing is searched:
a note lookup is four indexed loads. Little endian throughout, every table and every sample start on a 4-byte boundary.
Offsets are in bytes from the start of the file.

```
header (48) | pitch table | bank map | presets | key maps | zones | sample data
```

## Header (48 bytes)

| Offset | Type | Field |
| --- | --- | --- |
| 0 | char[4] | `FM1B` |
| 4 | u16 | version, 1 |
| 6 | u16 | flags, bit 0 = samples are 16-bit (otherwise 8-bit), bit 1 = samples are SEA5 chunks (bit 0 is then 0) |
| 8 | u32 | sample rate of all sample data, in Hz |
| 12 | u16 | number of presets |
| 14 | u16 | number of zones |
| 16 | u32 | offset of the pitch table |
| 20 | u32 | offset of the bank map |
| 24 | u32 | offset of the preset table |
| 28 | u32 | offset of the key maps |
| 32 | u32 | offset of the zone table |
| 36 | u32 | offset of the sample data |
| 40 | u32 | file size |
| 44 | u32 | reserved, 0 |

Samples are mono. Without bit 1 they are signed PCM: 8-bit is `int8`, 16-bit is `int16`.

### SEA5 samples (flags bit 1)

The FM-1's own sample codec (`fm1_sea.h` in fm1_open): a chunk is 320 frames in 228 bytes (0.7125 bytes per sample), 20 frames
per scale, 5-bit codes. A zone's data is `ceil(length / 320)` chunks in a row (the last one zero padded; `length` stays the real
number of samples). Every chunk carries its own LMS seed, so a decoder can start in any chunk: a note starts at chunk 0 and a loop
jump decodes just the chunk it lands in, so loop points need no alignment. The bank rate is free; 32 000 Hz is the codec's native one.
The decoder is `fm1_sea_decode`; `index.html` has the encoder (`seaEncode`, bit-exact with `sea_encode.h`, checked in `test_core.js`).

## Pitch table: 128 x u32

`pitch[i] = round(2^((i-64)/12) * 65536)`, a 16.16 step. Playing a zone at its original pitch is a step of `0x10000`
(one sample per output sample at the bank's rate). For a note `n` in a zone with root key `r`:

```
step = pitch[clamp(n - r + 64, 0, 127)]
phase += step;   // 16.16, the sample index is phase >> 16
```

The table saves the CPU an `exp2`. Scale the step by `bank_rate / output_rate` if the two differ.

## Bank map: 256 x u16

Index = `program` for the melodic bank (MIDI bank 0), `128 + program` for percussion (bank 128). The value is a preset
number, or `0xFFFF` for none. Other banks are not in the file.

## Preset: 24 bytes each

| Offset | Type | Field |
| --- | --- | --- |
| 0 | u16 | first zone (index into the zone table) |
| 2 | u16 | number of zones (at most 254) |
| 4 | char[20] | name, zero padded (for tools, the CPU never reads it) |

## Key map: 128 bytes per preset

Preset `p` has its map at `keymaps + p*128`. Entry `note` is the zone number **within the preset**
(`first + entry` is the zone table index), or `0xFF` when the note is silent. Where zones overlap, the
lowest-numbered zone wins, so the CPU never has to choose.

## Zone: 20 bytes each

| Offset | Type | Field |
| --- | --- | --- |
| 0 | u32 | offset of the first sample (4-byte aligned) |
| 4 | u32 | length in samples |
| 8 | u32 | loop start, in samples from the start of the sample |
| 12 | u32 | loop end (exclusive), in samples |
| 16 | u8 | root key (MIDI note the sample was recorded at) |
| 17 | u8 | flags, bit 0 = loop (otherwise the sample plays once) |
| 18 | u16 | reserved, 0 |

Zones that use the same sample point at the same data.

## Lookup in C

```c
typedef struct { uint32_t off, len, loop_start, loop_end; uint8_t root, flags; uint16_t rsv; } fm1_zone;

const fm1_zone *fm1_find(const uint8_t *f, int bank128, int program, int note, uint32_t *step) {
    const uint32_t *h = (const uint32_t *)f;
    uint16_t slot = ((const uint16_t *)(f + h[5]))[(bank128 ? 128 : 0) + program];   /* h[5] = offset 20 */
    if (slot == 0xFFFF) return 0;
    const uint16_t *pre = (const uint16_t *)(f + h[6] + slot * 24);                 /* h[6] = offset 24 */
    uint8_t k = f[h[7] + slot * 128 + note];                                        /* h[7] = offset 28 */
    if (k == 0xFF) return 0;
    const fm1_zone *z = (const fm1_zone *)(f + h[8]) + pre[0] + k;                  /* h[8] = offset 32 */
    int i = note - z->root + 64;
    *step = ((const uint32_t *)(f + h[4]))[i < 0 ? 0 : i > 127 ? 127 : i];          /* h[4] = offset 16 */
    return z;
}
```

`test_core.js` contains the same lookup in JavaScript and checks every note of every preset against the converter's own zones.

## What is left out

Envelopes, filters, modulation, velocity layers (the layer that answers to velocity 100 is used), stereo (the left channel is used),
fine tuning and sample start offsets.
