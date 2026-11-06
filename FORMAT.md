# FM1B, a sound bank for the FM-1

A bank is one file that the CPU can use where it lies (flash, SD buffer, RAM). Nothing is parsed and nothing is searched:
a note lookup is four indexed loads. Little endian throughout, every table and every sample start on a 4-byte boundary.
Offsets are in bytes from the start of the file.

```
header (76 or 80) | info (52, optional) | pitch table | bank map | presets | key maps | zones | zone levels | envelopes | modulation | layers | sample data
```

## Header (76 bytes)

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
| 64 | u32 | bytes per modulation record, 40 (a multiple of 4, at least 20). Only there when the pitch table starts at 68 or later: a 64-byte header (banks of the `sf2-lfo` branch) has records of 20 bytes, without the filter |
| 68 | u32 | offset of the layers, 0 = none (every key plays only its key map's zone). Only there when the pitch table starts at 76 or later: a 68-byte header (banks before the layers) has none |
| 72 | u32 | number of layer entries |
| 76 | u32 | offset of the info block, 0 = none. Only there when the pitch table starts at 80 or later: a bank without an info block keeps the 76-byte header |

### Info (52 bytes, optional)

What the panel's info page (REC) shows about the bank. The writer (`writeFm1(bank, info)` in `index.html`) puts it right after an
80-byte header, so the pitch table starts at 132; without `info` the bank is laid out as before (76-byte header, no block).

| Offset | Type | Field |
| --- | --- | --- |
| 0 | char[32] | the bank file's name, ASCII, zero padded (`make_bank.js`: the output file's name; the converter page: `<sf2 name>_fm1.bin`) |
| 32 | char[20] | when it was made, `2026-11-05 14:32` (local time), zero padded |

The player reads neither for playing. The info page also shows the CRC32 of the whole bank file, which it works out itself.

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

## Proposal: packed sample data (not implemented)

Nothing in the converter, the player or `fm1b_bind()` reads this yet; it is the format a bank would have if the FM-1's SDRAM (the AC7911BA
has 2 MB, the BB 8 MB, in the package; the AC7911B datasheet, "Package Type Specification") were used to hold the sound set unpacked. The flash
holds the bank packed, the player unpacks it once at start into SDRAM and then binds the result as it binds any bank, so the engine, the
zone table and every feature stay as they are. Whether the SDRAM can be used at all (the controller's initialisation, which variant the
board has) is not settled; the bank does not depend on it, the unpacker only needs somewhere to write.

**Header.** Flags bit 4 (`0x10`) = the sample data is packed. Bits 0-3 keep saying what the sample data is **after unpacking** (so the
engine sees the same bank as before). Version is 2 when bit 4 is set; `fm1b_bind()` takes version 1 only and no unknown flag bits, so a
player without this refuses the bank. Three fields
follow the info offset; they are only there when the pitch table starts at 92 or later (the header is then 92 bytes, 144 bytes to the
pitch table with the info block):

| Offset | Type | Field |
| --- | --- | --- |
| 80 | u32 | offset of the pack table, 0 = none |
| 84 | u32 | number of pack entries |
| 88 | u32 | bytes per pack entry, 24 (a multiple of 4, at least 24) |

The zone table is unchanged and addresses the **unpacked** sample data (`offset of the first sample` counts from the start of the sample
data as it will be after unpacking). The sample data in the file is the packed streams; the `file size` is the packed file's.

**Pack entry, 24 bytes.** Entries are in the order of the unpacked data and together cover it without gaps.

| Offset | Type | Field |
| --- | --- | --- |
| 0 | u32 | unpacked offset, from the start of the unpacked sample data (4-byte aligned) |
| 4 | u32 | unpacked bytes, in the bank's sample format (bits 0-3 of the flags) |
| 8 | u32 | packed offset, from the start of the sample data in the file |
| 12 | u32 | packed bytes |
| 16 | u8 | codec: 0 stored (a copy), 1 LZMA2, 2 MP3, 3 BFP8 in LZMA2 |
| 17 | u8 | reserved, 0 |
| 18 | u16 | codec 2: samples to drop at the start of the decoded stream (the encoder delay), else 0 |
| 20 | u16 | codec 2: loop crossfade in samples, 0 = none; else 0 |
| 22 | u16 | reserved, 0 |

- **0** copies `packed bytes` (equal to `unpacked bytes`) to the unpacked offset.
- **1** is one raw LZMA2 stream (no container, no header) that unpacks to `unpacked bytes` of the bank's format, which can be BFP8 or SEA.
  An entry should cover many samples: one stream over the whole sample area packs about 2 % better than a stream per sample.
- **2** is one MP3 stream (MPEG-1 layer III, mono, at the bank's rate, no Xing frame) of exactly one sample. The bank's format must then
  be 16-bit (bit 0): the stream is decoded to 16-bit PCM after dropping `delay` samples and cut at `unpacked bytes / 2` samples.
- **3** is a raw LZMA2 stream of BFP8 groups that is expanded to 16-bit PCM, so the bank's format is 16-bit and codecs 2 and 3 can sit in
  the same bank: the lossless samples keep their BFP8 quality, the long tonal ones go through MP3.

A bank with codec 1 or 0 only can keep BFP8 (or SEA) after unpacking, which takes half the SDRAM of 16-bit.

**Loops.** An MP3 sample's loop is not exact: the decoder's output near the loop end does not continue into the loop start. The unpacker
crossfades it: for every zone with the loop flag whose sample starts at the entry's unpacked offset, the last `xfade` samples before the
loop end are blended into the `xfade` samples before the loop start (linear, the loop end side fading out), so the jump from the loop end
to the loop start is continuous. It needs `loop start >= xfade`. Every zone that uses an MP3 sample must have the same loop; a sample whose
zones have different loops gets no MP3 entry. A sample with a loop of a few samples (a 34-sample brass loop) is better left to codec 3.

**Unpacking.** At start: copy everything before the sample data (header and tables) to the SDRAM, clear bit 4 and the pack fields, set
the file size to the unpacked one, unpack every entry to its unpacked offset, apply the crossfades, and call `fm1b_bind()` on that
image. The info page's CRC32 is of the packed file in flash, as it is of the file today. The converter's loop repeats (a loop stored
`k` times so that `k` loops are whole frames, for SEA) need not be stored in a packed bank: the unpacker can make them.

**What it saves** (`measure_zone_codecs.py`, run on `1mgm.sf2` at 32 kHz, 152 samples, 374 397 frames = 11.7 s). Today's BFP8 bank is
464 636 bytes: 47 052 of tables and 417 584 of sample data (of which 29 KB are loop repeats and 388 476 the samples).

| Bank in flash | Bytes | Against BFP8 today | Sample SNR against the source |
| --- | --- | --- | --- |
| BFP8, as it is | 464 636 | | 43.8 dB |
| BFP8 + xz, nothing lossy (codec 1) | 338 550 | -27 % | 43.8 dB |
| the smallest of BFP8 + xz and MP3 that reaches 25 dB (25 samples MP3) | 308 204 | -34 % | 25 dB and up for those 25 |
| the same at 20 dB (59 samples MP3) | 238 833 | -49 % | 20 dB and up for those 59 |
| MP3 only, 128 kbit/s | about 333 000 | -28 % | 21.7 dB |
| MP3 only, 64 kbit/s | about 174 000 | -63 % | 16.7 dB |

(The zone tables are 8 194 bytes xz packed and are in every packed row, 47 052 in the first. The MP3-only rows are the samples plus
the packed tables.) No sample reaches 30 dB at 64 or 128 kbit/s, so MP3 never comes near BFP8 in waveform terms; a sample that sounds
right at 20 dB has to be chosen by listening, not by this number. MP3 at 128 kbit/s for everything is only 1.5 % smaller than BFP8 + xz
(333 562 against 338 550) and 22 dB worse, so a lossy codec earns its place only at 64 kbit/s or on a choice of samples. The 25 samples
that reach 25 dB are mostly of 1 152 to 16 383 frames (piano, acoustic bass and guitars, voice, organ waves, timpani, steel drum). The
68 samples under one MP3 frame (1 152 frames) gain nothing at 128 kbit/s (81 396 bytes against 42 278 for BFP8 + xz) and about the same
at 64 kbit/s (42 228).

Order of work if it is built: first bit 4 with codecs 0 and 1 only (a lossless -27 %; the unpacker is an LZMA2 decoder, perhaps 10 KB of code, a guess not measured),
then codecs 3 and 2 per sample for the ones that pass a listening test.

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

## Layers

In a SoundFont every zone that covers a key sounds. The key map gives the lowest of them; the layers name the others. First one
record of 4 bytes a preset, in preset order:

| Offset | Type | Field |
| --- | --- | --- |
| 0 | u16 | the preset's first layer entry |
| 2 | u16 | its number of entries, 0 = one zone a key |

then the entries, 4 bytes each:

| Offset | Type | Field |
| --- | --- | --- |
| 0 | u8 | lowest key |
| 1 | u8 | highest key |
| 2 | u8 | zone within the preset |
| 3 | u8 | 0 |

A zone has an entry when some lower zone of its preset covers a key it covers too; its range is its whole key range. A note
starts the key map's zone and every entry of the preset whose range holds the note and whose zone is not the key map's, each on a
voice of its own, at most four zones a note. 1mgm.sf2 has 197 entries in 62 presets, at most two zones a key: 1.3 KB.

## Modulation: 40 bytes each

A zone's LFOs, how the key number scales its volume envelope, its low-pass filter and the modulation envelope, in SF2 units,
instrument plus preset. A player reads only as much of a record as the header's size gives (20 bytes: up to offset 18). Zones share them: the converter
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
| 18 | i16 | filter cutoff, absolute cents (generator 8, 1500..13500 = 20 Hz..20 kHz; 13500 = no filter) |
| 20 | i16 | filter resonance, cB (generator 9, 0..960): the peak's height above the pass band, as FluidSynth plays it |
| 22 | i16 | modulation envelope to cutoff, cents at its top (generator 11) |
| 24 | i16 | mod LFO to cutoff, cents at full excursion (generator 10) |
| 26 | i16 | modulation envelope delay, timecents (generator 25) |
| 28 | i16 | its attack, timecents (generator 26): linear from 0 to 1 |
| 30 | i16 | its hold, timecents (generator 27) |
| 32 | i16 | its decay, timecents (generator 28): linear, this long for all of 1 to 0, until the sustain level |
| 34 | i16 | its sustain, 0.1 % below the top (generator 29) |
| 36 | i16 | its release, timecents (generator 30): linear, this long for all of 1 to 0; it starts with the volume envelope's |
| 38 | i16 | modulation envelope to pitch, cents at its top (generator 7) |

What has no effect has its default: an LFO without depth its delay and rate, the modulation envelope without depth its times
(-12000) and sustain (0), a zone without a filter cutoff 13500 and resonance 0. The filter runs when the cutoff can come under 13500:
from the start, with the envelope going down, or with the LFO. It is RBJ's two-pole low-pass at the cutoff (at most 0.45 of the rate)
with Q = 10^((resonance / 10 - 3.01) / 20) and the gain 1 / sqrt(Q), FluidSynth's filter; its coefficients follow the cutoff once a
block. (The player runs a still cutoff between 1/50 and 1/4 of the rate in 32 bits, its coefficients within 0.2 dB of these.) The player moves the LFOs and the modulation envelope on once per envelope step and adds their pitch to the bend and the wheel's
vibrato.

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

The key number's scaling of the modulation envelope, velocity layers (the layer that answers to
velocity 100 is used), stereo (the left channel is used), modulators (the wheel's vibrato is the player's own), and sample start
offsets.

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
