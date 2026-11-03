# FM1B, a sound bank for the FM-1

A bank is one file that the CPU can use where it lies (flash, SD buffer, RAM). Nothing is parsed and nothing is searched:
a note lookup is four indexed loads. Little endian throughout, every table and every sample start on a 4-byte boundary.
Offsets are in bytes from the start of the file.

```
header (64) | pitch table | bank map | presets | key maps | zones | zone levels | envelopes | modulation | sample data
```

## Header (64 bytes)

| Offset | Type | Field |
| --- | --- | --- |
| 0 | char[4] | `FM1B` |
| 4 | u16 | version, 1 |
| 6 | u16 | flags, bit 0 = samples are 16-bit (otherwise 8-bit), bit 1 = samples are SEA chunks, bit 2 = samples are 12-bit, bit 3 = samples are BFP8 (at most one of bits 0-3 is set); bits 8..11 = the SEA code bits, 0 for 5 (set only with bit 1) |
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
| 44 | u32 | offset of the envelope table, 0 = none (every zone then plays with the default envelope) |
| 48 | u32 | offset of the zone levels, 0 = none. Only there when the pitch table starts at 52 or later: banks made before 2026-11-03 have a 48-byte header, and the player reads the field only when offset 16 says the header is that long |
| 52 | u32 | bytes per zone in the zone levels, 8 (a multiple of 4, at most 64). Only there when the pitch table starts at 56 or later: a 52-byte header (banks of 2026-11-03 before the sends) has 4 bytes a zone |
| 56 | u32 | offset of the modulation records, 0 = none. Only there when the pitch table starts at 64 or later: a 56-byte header (banks of 2026-11-03 before the LFOs) has none |
| 60 | u32 | number of modulation records |

Samples are mono. Without bit 1 they are signed PCM: 8-bit is `int8`, 16-bit is `int16`, 12-bit packs two samples in three bytes:
for the pair at byte `3k`, sample `2k` is `b0 | (b1 & 0x0f) << 8` and sample `2k+1` is `b1 >> 4 | b2 << 4`, both signed 12-bit
(an odd count ends with the first two bytes of a pair). A zone of `length` samples takes `ceil(length * 3 / 2)` bytes.
The player reads a 12-bit sample as `(int16_t)(value << 4)`.

BFP8 (flags bit 3) is block floating point: groups of 32 samples, each a shift byte (0..8) followed by 32 signed 8-bit mantissas,
so a zone of `length` samples takes `ceil(length / 32) * 33` bytes (the last group zero padded). A sample is `mantissa << shift`;
the converter picks the smallest shift that keeps the group's rounded mantissas in -128..127. 1.03 bytes a sample.

### SEA samples (flags bit 1)

The FM-1's own sample codec (`fm1_sea.h` in fm1_open): a chunk is 320 frames in `28 + 40 * bits` bytes, 20 frames per scale.
SEA5 (5-bit codes, flags bits 8..11 = 0) is 228 bytes a chunk, 0.7125 bytes per sample, with the fixed SEA5 quantizer table.
SEA6 (flags bits 8..11 = 6) is 268 bytes, 0.8375 bytes per sample: the same 16 scales at twice the resolution (`seaDqt()` in
`index.html`, which rebuilds the SEA5 table exactly; `make_dqt.js` writes the engine's copy, `fm1b_dqt6.inc`). Each chunk's
second byte is `0x40 | bits`. A zone's data is `ceil(length / 320)` chunks in a row (the last one zero padded; `length` stays the real
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

## Zone: 24 bytes each

| Offset | Type | Field |
| --- | --- | --- |
| 0 | u32 | offset of the first sample (4-byte aligned) |
| 4 | u32 | length in samples |
| 8 | u32 | loop start, in samples from the start of the sample |
| 12 | u32 | loop end (exclusive), in samples |
| 16 | u8 | root key (MIDI note the sample was recorded at) |
| 17 | u8 | flags, bit 0 = loop (otherwise the sample plays once) |
| 18 | u16 | envelope number (index into the envelope table) |
| 20 | i16 | fine tune in cents (-50..50): the step of every note in the zone is multiplied by 2^(cents/1200) |
| 22 | i16 | pan in 0.1 % (SF2 generator 17): -500 all left, 0 centre, 500 all right. Banks made before pan have 0 here |

Zones that use the same sample point at the same data.

## Zone levels: 8 bytes each

One per zone, in the zone table's order. A bank without them (header offset 48 is 0, or a 48-byte header) plays every zone at full
level, with no exclusive class and 100 cents a key.

| Offset | Type | Field |
| --- | --- | --- |
| 0 | u8 | level in 0.5 dB steps below full (0..255). The converter writes SF2 initial attenuation (generator 48, instrument plus preset) times 0.4, as the EMU hardware and FluidSynth play it |
| 1 | u8 | exclusive class (generator 57), 0 = none. A note of a class fades out the voices of the same class, preset and channel in about 5 ms (a closed hi-hat cuts the open one) |
| 2 | u16 | scale tuning in cents a key (generator 56, 0..1200), 100 the normal keyboard |
| 4 | u8 | reverb send (generator 16, instrument plus preset) in the units of MIDI CC 91, 0..127 = 0..100 %. The player adds it to the channel's CC 91, at most 127 |
| 5 | u8 | chorus send (generator 15), the same with CC 93 |
| 6 | u16 | modulation record number plus 1, 0 = none (no LFOs, no key scaling). 0 in banks made before them |

With a scale tuning other than 100 a note plays `(note - root) * scale + fine` cents from the sample's pitch: whole semitones from the
pitch table, the rest (0..99 cents) as the fine tune is. The converter then moves the root by whole multiples of the scale and keeps the
rest of the zone's tuning in `fine`, so `fine` may be outside -50..50 for those zones (all of the tuning at scale 0).

## Modulation: 20 bytes each

A zone's LFOs and how the key number scales its volume envelope, in SF2 units, instrument plus preset. Zones share them: the converter
writes each different record once. An LFO is a triangle that starts at 0 and rises; its value is -1..1 times the depth.

| Offset | Type | Field |
| --- | --- | --- |
| 0 | i16 | mod LFO to volume, cB at full excursion (generator 13, -960..960): a positive value is louder at the LFO's top. The player raises at most 12 dB |
| 2 | i16 | mod LFO to pitch, cents (generator 5) |
| 4 | i16 | vibrato LFO to pitch, cents (generator 6) |
| 6 | i16 | mod LFO delay, timecents (generator 21, default -12000 = 1 ms) |
| 8 | i16 | mod LFO rate, timecents above 8.176 Hz (generator 22, default 0) |
| 10 | i16 | vibrato LFO delay, timecents (generator 23) |
| 12 | i16 | vibrato LFO rate, timecents above 8.176 Hz (generator 24) |
| 14 | i16 | key number to hold, timecents a key (generator 39): the hold time times 2^((60 - key) * value / 1200) |
| 16 | i16 | key number to decay, timecents a key (generator 40): the decay time the same way |
| 18 | u16 | 0 |

An LFO without depth has the default delay and rate. The player moves the LFOs on once per envelope step and adds their pitch to the
bend and the wheel's vibrato.

## Lookup in C

```c
typedef struct { uint32_t off, len, loop_start, loop_end; uint8_t root, flags; uint16_t env; int16_t fine, pan; } fm1_zone;

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

Filters, the modulation envelope, the key number's scaling of the modulation envelope, velocity layers (the layer that answers to
velocity 100 is used), stacked layers (the lowest-numbered zone of a key plays), stereo (the left channel is used), modulators (the
wheel's vibrato is the player's own), and sample start offsets.

## Envelopes: 12 bytes each

The volume envelope of a zone, from the SoundFont's volume envelope generators. A *step* is 32 frames at 44.1 kHz. Decay and
release are exponential (a constant number of dB per second), so each step multiplies the level by a factor.

| Offset | Type | Field |
| --- | --- | --- |
| 0 | u16 | attack, ms (linear from 0 to full, at least 2) |
| 2 | u16 | hold, ms (full level) |
| 4 | u16 | decay factor per step, Q16 (level falls to the sustain level) |
| 6 | u16 | sustain level, Q16 linear (65535 = full; under 1/256 the voice ends at the end of the decay) |
| 8 | u16 | release factor per step, Q16 (after note off; the voice ends below -96 dB) |
| 10 | u16 | bit 0 = note off does not release (a struck sound that plays to its end) |
