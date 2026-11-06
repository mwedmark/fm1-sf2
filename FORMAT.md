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
| 18 | u16 | codec 2: samples to drop at the start of the decoded stream (the encoder delay, once per stream), else 0 |
| 20 | u16 | codec 2: loop crossfade in samples, 0 = none; else 0. It applies to every zone with the loop flag whose sample lies in the entry |
| 22 | u16 | reserved, 0 |

- **0** copies `packed bytes` (equal to `unpacked bytes`) to the unpacked offset.
- **1** is one raw LZMA2 stream (no container, no header) that unpacks to `unpacked bytes` of the bank's format, which can be BFP8 or SEA.
  An entry should cover many samples: one stream over the whole sample area packs about 2 % better than a stream per sample.
- **2** is one MP3 stream (MPEG-1 layer III, mono, at the bank's rate, no Xing frame) of one sample **or of many in a row**: the entry
  covers a contiguous range of the unpacked data and the stream is the whole range laid end to end, as the converter lays the samples out
  (a gap of silence between them is part of the range, see below). The bank's format must then be 16-bit (bit 0) or BFP8 (bit 3).
  The stream is decoded to 16-bit PCM after dropping `delay` samples and cut at the range's sample count (`unpacked bytes / 2` for
  16-bit, `unpacked bytes * 32 / 33` for BFP8); in a BFP8 bank the PCM is then put into groups of 32 with the converter's rule (the smallest shift that keeps the rounded mantissas in -128..127). BFP8's own noise is about 44 dB below the
  signal, far below MP3's (17 to 22 dB), so the extra step costs next to nothing.
- **3** is a raw LZMA2 stream of BFP8 groups that is expanded to 16-bit PCM, so it is for a 16-bit bank, where codecs 2 and 3 can sit
  together: the lossless samples keep their BFP8 quality, the long tonal ones go through MP3. In a BFP8 bank a lossless sample is codec 1.

A bank with codec 1 or 0 only can keep BFP8 (or SEA) after unpacking, which takes half the SDRAM of 16-bit. A bank with MP3 samples can
too, if it is a BFP8 bank (codec 2 as above); 16-bit is only worth it for samples that BFP8 would spoil, and BFP8 is 43.8 dB
against the source on `1mgm.sf2`.

**Loops.** An MP3 sample's loop is not exact: the decoder's output near the loop end does not continue into the loop start. The unpacker
crossfades it: for every zone with the loop flag whose sample starts at the entry's unpacked offset, the last `xfade` samples before the
loop end are blended into the `xfade` samples before the loop start (linear, the loop end side fading out), so the jump from the loop end
to the loop start is continuous. It needs `loop start >= xfade`. Every zone that uses an MP3 sample must have the same loop; a sample whose
zones have different loops gets no MP3 entry. A sample with a loop of a few samples (a 34-sample brass loop) is better left to codec 3 (codec 1 in a BFP8 bank).

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

**What it holds.** The flash limit is `FM1B_FLASH_BYTES`, 466 944 bytes. Of those 8 194 go to the packed tables, the rest to samples. The
sample data of `1mgm.sf2` is 11.7 s at 32 kHz; the bytes per second of audio, and the SDRAM the same audio takes once unpacked, are:

| In flash | Sample bytes per second | Seconds that fit | Against today | SDRAM unpacked, BFP8 | SDRAM unpacked, 16-bit |
| --- | --- | --- | --- | --- | --- |
| BFP8, as it is (the bank is in flash, no SDRAM) | 33 000 | 11.7 (today's bank, 99.5 % full) | 1 | | |
| BFP8 + xz (codec 1) | 28 200 | about 16 | 1.4 | 0.53 MB | 1.04 MB |
| MP3 64 kbit/s, one stream per sample (codec 2) | 14 200 | about 32 | 2.7 | 1.07 MB | 2.07 MB |
| MP3 64 kbit/s, one stream over all samples, no gap (codec 2) | 8 030 | about 57 | 4.9 | 1.88 MB | 3.65 MB |
| the same with a 256-frame gap after each sample | 8 870 | about 52 | 4.4 | 1.71 MB | 3.31 MB |

(1 MB here is 1 000 000 bytes; the SDRAM of the 2 MB variant is 2 097 152. The MP3 rows are measured on the 11.7 s of `1mgm.sf2` at 32 kHz
with ffmpeg/libmp3lame: 93 933 bytes as one stream, 103 725 with the gaps, 166 104 as 152 streams. The nominal 64 kbit/s is 8 000 bytes a
second; a stream per sample costs more because every one starts and ends on whole frames of 1 152 frames (36 ms) and most samples are
shorter than that. The gap is a guard against one sample's start ringing into the one before it; whether it is needed is for the
listening test below. It is silence in the unpacked data that the zone table never points at, so it costs `gap * bytes per frame` of SDRAM
a sample, 40 to 80 KB for 152 samples.) A SoundFont that fills 52 to 57 s at 32 kHz is about 4.6 to 5.0 MB of 16-bit samples at 44.1 kHz.
So, if a working SDRAM is found:

- On the **8 MB variant** it is the flash that limits: about 52 to 57 s with one MP3 stream at 64 kbit/s, 4.4 to 4.9 times today's bank
  (2.7 with a stream per sample). BFP8 after unpacking is 1.7 to 1.9 MB, 16-bit 3.3 to 3.7 MB; both fit.
- On the **2 MB variant** 16-bit PCM does not fit and neither does 1.7 to 1.9 MB of BFP8 with room for the effects and anything else the
  engine puts there; about 30 s of BFP8 (1.0 MB) does. The bank has to stay BFP8 after unpacking, which is why codec 2 can write BFP8.
- The figures are for MP3 on **every** sample at 16.7 dB SNR, which has not been listened to. Per sample, 68 of the 152 samples are shorter
  than an MP3 frame and gain nothing from MP3 as streams of their own (42 228 bytes at 64 kbit/s against 42 278 for BFP8 + xz); the one
  stream is what takes the padding away. A bank with other material gains what that material packs to. With the choice of samples above (25 dB) the bank is 308 204 bytes, about 25 600 bytes a
  second of audio, so about 18 s fit: 1.5 times today's bank.
- Without an SDRAM none of this works for the whole bank: the unpacked BFP8 of `1mgm.sf2` (0.39 MB) would take about three quarters of the 511 KB of SRAM
  that the engine, Bluetooth and USB share (the build needs at least 48 KB of heap), and the 0.53 MB of the 16 s do not fit in it at all.

**How it would be tested.** Nothing below has been run; it has to pass on the PC and in the emulator before anything goes to the FM-1.

On the PC and in fm1-emu:

1. *Round trip* (a new `test_pack.js`, as `test_upload.js` does it: the real unpacker C code on WebAssembly, the converter's writer in node). Pack
   `1mgm.sf2` with each codec, unpack, and compare with the bank `make_bank.js` writes now. Codecs 0, 1 and 3 must be byte for byte the
   same (codec 3 after the BFP8 expansion, against the 16-bit bank); with only codecs 0 and 1 the unpacked bank is today's bank but for the
   header. Codec 2 has no exact answer, so it is checked as numbers: SNR per sample against the source (not below what
   `measure_zone_codecs.py` gives for that sample), the length and the delay (no shift of the start), and at every loop the step
   from the loop end to the loop start no larger than the largest step inside the loop.
2. *The engine does not change*: `test_formats.js`, `test_sim.js` (pitch, loop, release), `test_multitimbral.js`, `test_zone.js` and
   `test_layers.js` on the unpacked bank must pass as they do on today's. The unpacker adds no state the engine reads.
3. *Listening*: for the samples that pass 1, render the same notes from the BFP8 bank and the packed one (as the A/B files made for the
   MP3 decision: original, BFP8, MP3 128, MP3 64, with the loop repeated), long notes at several pitches and the drums. Decide per sample
   which go through MP3, and whether the gap is needed. This is the step that cannot be a number.
4. *fm1-emu*: the packed bank in the user flash (`make_flash_overlay.js`), boot, and count the unpack time in emulated cycles at 240 and
   320 MHz; the CPU load with 16, 24 and 32 voices from the unpacked bank against the BFP8 bank in flash (`load24_all.script` and the
   others in `test_emu/`). fm1-emu has to give the unpacker somewhere to write at the SDRAM's address; if it does not model that, it
   needs a RAM region there, and then it says nothing about the SDRAM's speed.

On the FM-1, in this order, and stop at the first step that fails:

5. *The SDRAM alone*, before any bank: which chip is it (the marking BA or BB, `AC7911B`), is the controller initialised by the
   build, does a pattern written over the whole 2 or 8 MB read back, and how long does a block copy take. Without this nothing further
   is worth doing.
6. *A lossless bank* (codecs 0 and 1) unpacked into the SDRAM, the CRC32 of the unpacked image against the one the PC works out, and
   then the `FM1_TEST.md` checklist as it is for a bank in flash.
7. *Measure on the panel*: the time from power on until the keys play, and the audio interrupt's CPU load (the program page's
   `CPU a% MAX p%`) with 16 and 32 voices, against the same bank in flash. The cache is 24 KB for data, so many voices reading the SDRAM is
   the thing to watch.
8. *Codec 2 samples*, the ones that passed the listening test, then loops and the pan and effects as in the checklist. The effects
   moving into the SDRAM (longer delays) is a separate step after this one.

The firmware is flashed in step 5 and after, and `FIRMWARE.md` has not been run on the device even once, so the stock firmware
(`sdk/firmware/FM-1_015.fwsc`, as `FIRMWARE.md` says) and M-VAVE's recovery have to be at hand first. The bank itself goes in over SysEx
(`sf2_upload.c`) and does not touch the firmware.

Order of work if it is built: first bit 4 with codecs 0 and 1 only (a lossless -27 %; the unpacker is an LZMA2 decoder, perhaps 10 KB of code, a guess not measured),
then codecs 3 and 2 per sample for the ones that pass a listening test.

## Proposal: more of the SoundFont (not implemented)

With the room that packed sample data gives (see the proposal above), the parts of a SoundFont that the converter leaves out
("What is left out", below) are worth taking in. None of it is built. `1mgm.sf2` uses none of it (measured with the converter: 0 of
its 129 presets have velocity ranges, all 153 samples are mono, it has one percussion kit and no preset is dropped), so it cannot be
tested on that file; the fonts that need it have to be counted first (below).

Space is one of three limits. The others are the **CPU** (`RUNNING.md`: 32 voices from a BFP8 bank run at 50 to 55 %, 69 % as the
notes start, in fm1-emu at 240 MHz; SEA6 is 84 to 98 %, 111 % as they start) and the **seconds of audio**: the flash holds about 52 to 57 s as MP3 at
64 kbit/s, and every velocity layer or stereo channel takes its share of them.

| | What is missing | CPU | Samples |
| --- | --- | --- | --- |
| Several percussion kits | the player only | none | more (drums are short) |
| Velocity layers | 2 bytes a zone and a velocity test at note start | none (one layer sounds at a time) | 2 to 4 times for an instrument that has them |
| Stereo | the converter only, as two zones | 2 voices a note | 2 times |
| Modulators | a modulator table and its evaluation | small, at block rate | none |

**1. Several percussion kits.** The format already has them: a kit is a preset of bank 128, the bank map index is `128 + program`, and
`fm1b.h` lets a kit play on any channel by program change. The converter writes every preset of bank 128. What is missing is the player:
`fm1b.c` takes the first percussion preset the bank has (`drum_preset`) and channel 10 and the SCL drum mode always play that one. Make
a program change on channel 10 choose the kit, and give the panel a way to choose one (the drum mode's page). A bank with kits that
share samples costs only the zones.

**2. Velocity layers.** A zone gets a velocity range. The zone levels record (8 bytes, its size is in the header, offset 52) grows to 12:

| Offset | Type | Field |
| --- | --- | --- |
| 8 | u8 | lowest velocity, 0 to 127 |
| 9 | u8 | highest velocity, 0 to 127 |
| 10 | u16 | reserved, 0 |

A bank with the 8-byte record plays every zone at every velocity, as now. A note then starts only the zones whose range holds its
velocity: the key map's zone and every layer entry of the preset that covers the note, each tested (the loop in `zone_start` in
`fm1b.c`), so one layer sounds at a time as in a SoundFont, with no crossfade between layers. The layer entries already list every zone that
overlaps a lower one on a key, so the velocity layers are found without a new table. The engine's limit of four zones a note counts the zones that
start, not the entries, so the entry table can list more than four zones on a key when velocity layers split them. The converter
drops its filter that keeps only "the layers that answer to velocity 100" and writes the ranges. Tests: `test_layers.js` and
`test_zone.js` get banks with ranges (a note at velocity 40 and one at 100 start different zones; a range that holds neither starts none).

**3. Stereo.** A stereo pair is two samples in the file (sample type 4 left, 2 right, linked by the sample link field); the converter
takes the left one now. First step, with no change to the engine: write the pair as two zones that cover the same keys and velocities,
with the zone pan at -500 (left) and +500 (right), which the pan code already plays, stacked by layer entries. A note then takes two
voices and twice the CPU, so the 32 voices are 16 notes; measure that against the CPU figures above before choosing it for a font.
Both channels are stored, so twice the samples (as separate mono MP3 streams there is no gain from the channels being alike). Only if
that CPU is too much is a stereo voice worth building: one voice that reads two samples with one envelope, filter and LFO.

**4. Modulators.** The converter reads none of the SoundFont's modulators (the instrument and preset `imod` and `pmod` lists). The
player has a fixed set: velocity to level (squared), CC 7, 10 and 11, the sends CC 91 and 93, the wheel's vibrato, pitch bend. Do not
design the rest before counting which modulators the fonts really use; a table of (source, destination, amount) per zone, run once a
block of 128 frames, covering the destinations the engine already has (pitch, level, filter cutoff, pan, LFO depth), is the likely shape.

**Counting first.** A script that reads a SoundFont and says, in the converter's terms: the seconds of audio at the bank's rate
(samples at 44.1 kHz, 22 kHz or 8-bit are not the same size on disk as in flash), presets by MIDI bank and how many the bank map drops (a
GS font has variation banks; the bank map has only 0 and 128), percussion kits, presets and zones with velocity ranges and the layers
they make, stereo pairs, and the count of instrument and preset modulators. It is `measure_font_features.py`
(`python3 -I measure_font_features.py font.sf2`). The numbers decide the order of 1 to 4 for a given font; without them it is
1, 2, 3, 4: kits and velocity layers cost no CPU, stereo costs voices, modulators cost the most work for the least known gain.

Measured so far (the converter's rules, 32 kHz):

| | `1mgm.sf2` | `4gmgsmt.sf2` ("Four MB General MIDI bank with GS and MT32 variation tones", EMU8000, per its header) |
| --- | --- | --- |
| File | 1 090 280 bytes | 4 174 814 bytes |
| Samples | 153, all mono, 44.1 kHz, 11.7 s | 488, all mono, 44.1 kHz, 44.9 s |
| Presets | 129 (bank 0: 128, kit: 1), none dropped | 329: bank 0 128, banks 1 to 9 and 16 63, bank 127 128, kits 10 (programs 0, 8, 16, 24, 25, 32, 40, 48, 56, 127); 191 dropped by the bank map |
| Velocity layers, stereo pairs, modulators | none, none, none | none, none, none (the `imod` and `pmod` lists hold only their terminal record) |
| Samples used only by the dropped presets | 0 | 5 (under 0.1 s): the variation banks reuse the sample pool, so they cost zones, not samples |
| Bank today, BFP8 at 32 kHz | 464 636 bytes | 1 748 904 bytes: 246 292 of tables, 1 502 612 of sample data (3.7 times the flash) |
| BFP8 + xz, samples as one stream | 330 356 | 1 223 662 |
| MP3 64 kbit/s, one stream, no gap / 256-frame gap | 93 933 / 103 725 (8 030 and 8 870 B/s) | 359 181 / 390 285 (8 010 and 8 700 B/s), SNR 17.5 / 17.9 dB |
| MP3 96 and 128 kbit/s, no gap | | 538 749 and 718 317 |
| Zone tables xz packed | 8 194 | 38 604 |
| Packed bank, MP3 64 kbit/s (tables + samples) | about 102 000 to 112 000 | about 398 000 (no gap) to 429 000 (gap): 85 to 92 % of the 466 944 bytes |

So `4gmgsmt.sf2` fits the flash at 64 kbit/s, and only at 64 kbit/s: 96 kbit/s is 539 KB for the samples alone. It matches what
the capacity table above predicted for a font of 45 s (360 to 397 KB). BFP8 packed with xz does not come near (1.2 MB). It has
none of the velocity layers, stereo or modulators of 2 to 4 (so nothing in those would change this font), but its 10 kits and its
variation banks are what 1 and the bank map are for: the kits are already written to the bank and only the player picks the first one,
and the 191 presets the bank map drops would cost zones, not samples.

**A waste in today's converter** that this font shows: the zone table holds the zones of every preset of the file, also of the dropped
ones. `4gmgsmt.sf2` has 3 560 zones in the bank and the 138 written presets use 1 882 of them; the other 1 678 (47 %, about 115 KB of the 246 KB
of tables, an estimate from the bytes a zone takes) are never reached. `1mgm.sf2` has no dropped presets, so it does not show it.

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
offsets. The first three and the percussion kits are proposed in "Proposal: more of the SoundFont" above.

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
