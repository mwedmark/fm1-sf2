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

> **Update 2026-11-08: this proposal needs SDRAM, and the FM-1 most likely has none.** Public teardowns of FM-1 boards read the SoC's
> marking as `...-11B8` on three units (`C156211-11B8`, `C188612-11B8`, `C188760-11B8`; see "What is known about the SDRAM so far"). By the
> datasheet's type code (the last character is the memory in the package: `8` is 8 Mbit of flash, `A` and `B` are the 2 and 8 MB SDRAM) that is an
> **AC7911B8**: the 1 MB flash the FM-1 boots from is inside the package, and there is no SDRAM. The AC7911BA and BB are other chips. Nobody
> has read the marking on the unit this repository is tried on, and one source disagrees, so it is not settled; but until a unit with SDRAM is
> found, read the rest of this section as a proposal for AC7911BA/BB hardware. On the FM-1 the room is the 466 944 bytes of flash and about
> 514 KB of SRAM, and "unpack once into SDRAM" has nowhere to go.

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

**What is known about the SDRAM so far.**

- The AC7911B datasheet ("Package Type Specification", page 15 of V1.1): the last character of the type is the memory in the package, one
  kind per type: 2 to 7 flash (`8` is 8 Mbit, 1 MB), A 1M×16 SDRAM (2 MB), B 4M×16 SDRAM (8 MB), C and D PSRAM, 0 none.
- **What the FM-1's chip is.** Four public documentation projects, with photos of at least three units: the SoC is marked `JL C156211-11B8`
  (aroum's unit), `C188612-11B8` (the owner's unit in [ip2k/lunar-modulator](https://github.com/ip2k/lunar-modulator), `docs/01-hardware.md`)
  and `C188760-11B8` ([DimmKirr/modd](https://github.com/DimmKirr/modd), `docs/hardware/board/README.md`), QFN-48. Read with the datasheet's code,
  `11B8` is an AC7911B**8**: 8 Mbit (1 MB) of flash in the package, which is exactly the Puya flash (JEDEC `0x856014`) the FM-1 boots from.
  [TEG-Lizard-Man/cyma-1](https://github.com/TEG-Lizard-Man/cyma-1) (`notes/FM-1-hardware-atlas.md`) says the same: "The chip is an AC7911B8 ...
  The flash is inside the package", "The B8 has no SDRAM", and no separate flash chip is found on the board (the SOIC-8 `U12` is the charger).
  ip2k/lunar-modulator: "the FM-1 has none", and it checked that an AC7911B8 is 1 MB of in-package flash and no SDRAM against datasheet V1.4 and
  the SDK's selection table. Against it: modd lists the marking `11B8` and calls the part an AC7911BA with 2 MB SDRAM, which does not follow from
  its own marking; [aroum/fm1-custom-fw](https://github.com/aroum/fm1-custom-fw) (`REVERSE_ENGINEERING.md`), where "AC7911BA or BB with 2 or 8 MB SDRAM"
  seems to come from, reads the marking as `11A-B` (the others read the same photo as `11B8`) and gives both the SDRAM and the 1 MB in-package
  flash to one chip, which a single type does not have. A type has one memory, so a chip with the FM-1's in-package flash is not also an SDRAM
  variant. This rests on other people's photos and on reading the marking as the datasheet's code; the marking of this repository's own unit has
  not been read.
- The boot configuration, in M-VAVE's `FM-1_015.fwsc` and in this repo's `sdk/tools/pack/boot-config.bin` (`Package.cs` takes it as 721
  bytes and works the checksums out), has `ENABLE_SDRAM` and `SDRAM_SIZE` at 0. The boot loader (the SPL, `uboot.boot`) has the names of
  these keys among its strings, so it is what reads them, and with `ENABLE_SDRAM` set it sets the SDRAM up before the application starts
  (inferred, not traced). M-VAVE's firmware does not use the memory; that says nothing about whether the chip has it. JieLi's own generator of this file for the AC791N (`cpu/wl82/tools/isd_config_rule.c` in the AC79 SDK) writes the same
  file for a build without SDRAM (`CONFIG_NO_SDRAM_ENABLE`: `ENABLE_SDRAM=0`, `SDRAM_SIZE=0M`) and for a build with it, and the two differ
  in these keys only:

  | Key | M-VAVE's FM-1 (and `boot-config.bin`) | With SDRAM, system clock 320 MHz (JieLi's rule) |
  | --- | --- | --- |
  | `ENABLE_SDRAM` | 0 | **1** |
  | `SDRAM_SIZE` | 0 | **2M** (AC7911BA, 1M×16) or **8M** (AC7911BB, 4M×16) |
  | `SDRAM_MODE` / `SDRAM_COLUM` | 0 / 0 (SDR SDRAM) | the same |
  | `SDRAM_REFRESH_TIME` / `_CYCLES` | 32 / 4096 | the same |
  | `SDRAM_PLL3_EN`, `_NOUSB_EN` | 0, 0 | the same |
  | `SDRAM_CL`, `SDRAM_DQ_DLY_TRM` | 2, 4 | the same: CL 2 is a 192 MHz SDRAM clock at 320 MHz |
  | `SDRAM_WLCNT`, `_D_DLY`, `_RLCNT`, `_Q_DLY`, `_PHASE` | 3, 1, 1, 1, 3 | the same (`_D_DLY` 1 is "test the SDRAM" in the SDK's comment) |
  | `SDRAM_TRFC`, `TRP`, `TRCD`, `TRRD`, `TWTR`, `TRTW`, `TWR`, `TRC` | 60, 18, 18, 12, 20, 60, 60, 55 | the same |
  | `SDRAM_DQS_DLY_TRM`, `UDQS`, `LDQS`, `DQ_DLY_INNER` | 0, 10, 10, 0 | the same |

  M-VAVE's FM-1 is, in other words, JieLi's standard AC791N configuration with the SDRAM switched off, which is what a build for a chip
  without SDRAM gets too: it fits a chip without SDRAM as well as a choice not to use one. Things around it that the SDK shows: the
  SDRAM window starts at `0x4000000` (`cpu/wl82/sdk_ld_sdram.c`: `sdram ORIGIN = 0x4000000, LENGTH = SDRAM_SIZE`, heap end at
  `0x4000000 + SDRAM_SIZE - 32`; the 0x120 of `0x4000120` is the stock build's overlay window); the entry stays `0x2000120` with the code in
  the flash and the SDRAM for data (`CONFIG_SFC_ENABLE`); and the clock keys only apply at a system clock of 320 MHz or at most 240 MHz,
  while `boot-config.bin` has 320 MHz, M-VAVE's package 360 MHz, where the SDK says the SDRAM clock becomes `SYS_CLK * 1.5 / 2`, 270 MHz.
  Not known: how the tool stores `2M` (the numbers in the file are 32-bit values, `24MHz` is 24 000 000 and `4K` is 4096, so 2 097 152 is
  the guess), and what the boot loader does when its SDRAM test fails (stop, or go on without). A wrong boot configuration may stop the
  FM-1 booting; [AL-255's reverse engineering of the FM-1](https://github.com/AL-255/FM-1-RE/blob/main/TODO_aug2.md) finds no proven
  recovery from the ROM and says not to flash non-stock images to a device that cannot be recovered some other way. Sources: JieLi's AC79
  SDK as mirrored in [0x0fe/7911b](https://github.com/0x0fe/7911b) (`cpu/wl82/tools/isd_config_rule.c`, the README: 578 KB of SRAM and 2 or 8 MB of
  SDRAM in some packages) and [jeffreywugz/fw-AC79_AIoT_SDK](https://github.com/jeffreywugz/fw-AC79_AIoT_SDK) (`cpu/wl82/tools/isd_config.ini`,
  `cpu/wl82/sdk_ld_sdram.c`). `boot-config.bin` is not changed.
- Step 5 is built as the info page's SDRAM test (REC, then SAVE twice; `fm1_open/app/sdram_test.c`, `FM1_TEST.md` F). It checks the data
  bus, the address bus (which also gives the size), every cell with a value of its own and with its inverse, and the inverse again after
  3 s, and shows the verdict and the details. `test_sdram.js` runs that code against a model with faults (stuck and shorted data and
  address lines, nothing there, a bus that holds the last value, a stuck cell, no refresh): 21 checks. It has not been run on a device.
  The window it tests, `0x04000000` and up to 8 MB, is where JieLi's SDK puts the SDRAM. With the SDRAM off in the boot configuration it
  is expected to fail, or to restart the FM-1.
- **Is there a flag that says whether this unit has the SDRAM?** None is documented. The datasheet calls the memory letter a different
  chip ("different packages or bindings"), which suggests bonding bits that software can read, but no document says which register or
  which bit. Three read-only registers are worth writing down for each unit, and the info page shows them before any test (the
  `ID ... SDR ... PSR ...` line): `JL_INTEST->CHIP_ID` at `0x10200` (M-VAVE's firmware checks it for `0x6F00`, as a silicon check), and
  the first register of the SDRAM controller at `0x40400` and of the PSRAM controller at `0x40500`, whose bit 11 and bit 0 M-VAVE's
  start-up code reads as "chip straps" every time it starts ([AL-255/FM-1-RE](https://github.com/AL-255/FM-1-RE), `docs/io/01-boot.md`
  and `03-audio-dac.md`), so reading them is nothing the stock firmware does not do. The boot loader also leaves a hardware block at
  `0x01C7FD50` with a chip-id byte and two "sizes" words that are not traced. With one unit the values cannot be read as "has" or "has
  not": it takes a BA, a BB and a chip without SDRAM side by side. The marking on the chip is the first check, and the only sure one.
- `make_sdram_config.py` (`--show`, `--size 2M|8M`) reads `boot-config.bin` as the 42 entries it is (they use the 721 bytes to the last one)
  and writes a copy with `ENABLE_SDRAM` 1 and `SDRAM_SIZE` 2M or 8M, 2 to 4 bytes different; nothing in the build uses the copy and the
  script never writes the original. `--encoding` picks how `2M` is stored (bytes, the guess; decimal; megabytes). `test_sdram_config.py`: 18
  checks. M-VAVE's file also has two keys that are neither in JieLi's ini file nor in the table above, `SDRAM_TEST_SIZE` (4096) and
  `SDRAM_CONFIG_VAL` (0); the boot loader has both among its strings and what they do is not known; the copy leaves them as they are.

The firmware is flashed in step 5 and after, and `FIRMWARE.md` has not been run on the device even once, so the stock firmware
(`sdk/firmware/FM-1_015.fwsc`, as `FIRMWARE.md` says) and M-VAVE's recovery have to be at hand first. The bank itself goes in over SysEx
(`sf2_upload.c`) and does not touch the firmware.

**Mixed tiers.** The format needs nothing new to mix quality in one bank: every pack entry has its own codec, and an MP3 stream's bitrate is
in the stream, not in the entry. Three tiers, three entries:

| Tier | Entry | Quality (SNR against the source on `1mgm.sf2`) |
| --- | --- | --- |
| lossless: BFP8 + xz | codec 1 in a BFP8 bank, codec 3 in a 16-bit bank | 43.8 dB |
| MP3 128 kbit/s | codec 2 | 21.7 dB (whole set) |
| MP3 64 kbit/s | codec 2 | 16.7 dB (whole set) |

The zone table addresses the unpacked data, so the converter is free to lay the samples out sorted by tier: each tier is then one contiguous
range and one stream, with no padding per sample (the 8 030 B/s of one stream against 14 200 for a stream per sample). A bank of the three
tiers has three entries; a bank with two has two. The loop crossfade of codec 2 applies to both MP3 tiers. In a BFP8 bank the MP3 tiers
are decoded and put into BFP8 groups as codec 2 says.

*Choosing the tier of a sample* is the converter's job, by one of:

1. a list by hand, sample by sample (the result of listening);
2. a threshold: the smallest tier whose per-sample SNR against the source reaches it (`measure_zone_codecs.py` prints this table);
3. a flash budget: begin with every sample lossless and move samples to a cheaper tier, the ones that save the most bytes for each dB lost
   first, until the bank fits. A list by hand overrides both.

What the threshold gives on `1mgm.sf2` (152 samples, 32 kHz, `measure_zone_codecs.py`; zone tables 8 194 bytes xz packed are in every row;
MP3 tiers are counted as one stream each, 16 000 and 8 000 bytes a second of audio, BFP8 + xz per sample, so BFP8 + xz alone is 346 123):

| Smallest tier whose SNR reaches | BFP8 | MP3 128 | MP3 64 | Bank | Against BFP8 + xz | BFP8 and MP3 64 only |
| --- | --- | --- | --- | --- | --- | --- |
| 25 dB | 91 | 53 | 8 | 282 628 | -18.3 % | 341 115 |
| 20 dB | 35 | 55 | 62 | 192 999 | -44.2 % | 228 094 |
| 17 dB | 25 | 38 | 89 | 173 898 | -49.8 % | 202 709 |
| 15 dB | 18 | 24 | 110 | 159 298 | -54.0 % | 184 178 |

The 128 kbit/s tier is worth having: at 20 dB it saves 15 % over BFP8 + xz and MP3 64 only (192 999 against 228 094), because a sample that does not
reach the threshold at 64 kbit/s often does at 128. These are estimates, not three streams written and measured: the SNR of a sample is
that of its own MP3 file, and in a long stream the neighbours may change it a little; the sizes of the MP3 tiers are the nominal rates. And
SNR is a measure of the waveform, not of what is heard (an MP3 hides its noise under the signal), so the thresholds are not a quality
statement: which tier a sample can take is for the listening test (step 3 above), and the 20 dB row only says where the bytes would go.
A tier per sample on `4gmgsmt.sf2` has not been worked out; its one-stream numbers are in the table of fonts above.

Tests that follow from it: the round trip per tier (codecs 1 and 3 exact; MP3 with each sample's SNR not below the value the converter
chose it by), and the A/B listening files with a column for each tier.

Order of work if it is built: first bit 4 with codecs 0 and 1 only (a lossless -27 %; the unpacker is an LZMA2 decoder, perhaps 10 KB of code, a guess not measured),
then codecs 3 and 2 per sample for the ones that pass a listening test.

## Calculation: a sample cache in the FM-1's SRAM (not built)

Without SDRAM the room is the 466 944 bytes of flash and the SRAM, 578 KB with 64 KB of it the caches, so about 514 KB usable. How much of that this
firmware leaves free is not known here: the stock firmware's static RAM is about 135 KB (AL-255/FM-1-RE), this one's is in the build's
`firmware-report.json` (`runtime_heap_bytes`, which `sdk/Build.ps1` insists is at least 48 KB), and a cache of 150 to 250 KB is the plausible range. The
question: could the flash hold the bank packed (MP3, xz) and the player decode what sounds into SRAM? Worked out with `measure_working_set.js` and
`measure_sample_rates.py`, 32 kHz, the converter's own banks.

**What the SRAM would have to hold** (the sounds of the programs that sound at once; a preset's sounds counted once; KB):

| | `1mgm.sf2` | `4gmgsmt.sf2` |
| --- | --- | --- |
| one melodic preset, median / largest (PCM16) | 17 / 155 (Piano 1) | 18 / 361 (Piano 3) |
| percussion kit 0 (PCM16 / BFP8) | 216 / 111 | 360 / 186 |
| 4 random programs and a kit: median, 99 % (BFP8) | 136, 215 | 250, 493 |
| 15 random programs and a kit: median, 99 % (BFP8) | 190, 273 | 440, 692 |
| a typical band, 10 programs and kit 0 (BFP8 / PCM16) | 246 / 476 | 555 / 1 077 |
| the whole bank in BFP8 (all melodic, kit 0, everything) | 333, 111, 405 | 1 155, 186, 1 457 |

(The flash holds 456 KB.) For `1mgm.sf2` the whole bank in BFP8 is 405 KB: it already fits the flash and plays in place, so a cache buys nothing. For
the 4 MB font a typical band needs 555 KB of BFP8, more than the SRAM has at all, and even 4 programs and a kit need 493 KB one time in a hundred; the kit alone
is 186 KB. Keeping what sounds resident does not fit.

**Decoding while it plays instead** (no resident set, each voice decodes from the flash). Measured in fm1-emu at 240 MHz (`RUNNING.md` 7b): BFP8 and 8-bit
need no decoding; SEA5 and SEA6 cost about one point of CPU for each voice (16 voices 52 to 59 % against 32 to 37 %; 24 voices 57 to 59 % against 35 to 41 %), for a
bank 24 % (SEA5) or 12 % (SEA6) smaller. 32 voices of BFP8 already run at 50 to 55 % and the CPU guard acts at 80 %, so about 25 points are left. MP3 per voice is an
estimate, not a measurement: assumed 5 to 12 million cycles for each second of mono audio from a fixed-point layer-III decoder (from memory of such decoders' speeds,
not looked up here and not run on a pi32v2), which is 2 to 5 points per voice at 240 MHz, so 5 to 12 voices of the 32. xz would need the stream in blocks and costs more
than SEA per voice.

**A rate of its own for each sound** (no decoding, but the engine has no rate per zone: a field per zone and a step multiplier would be needed). The lowest of 8, 11.025, 16,
22.05, 24 and 32 kHz that keeps 99.9 % of a sound's energy (99.99 %) saves, in BFP8: `1mgm.sf2` 11 % (4 %), `4gmgsmt.sf2` 19 % (9 %).

**What follows.**

- Keeping the programs that sound resident does not bring a 4 MB font to the FM-1: the working set of whole programs does not fit the SRAM, and decoding per voice does not fit
  the CPU. A cache of **single sounds**, filled by the notes that are played, is another matter: see the next section.
- Without it, 456 KB of flash is the budget, in a format that plays in place. The 4 MB font's melodic part and kit 0 are 1 300 KB of BFP8: 35 % of it fits, which is what `1mgm.sf2`
  (405 KB) already is, a font of that class thinned to fit.
- What is left then, none of it three times as much: SEA5 or SEA6 (12 to 24 % more, one point per voice, so the 24-voice firmware), a rate per sound (4 to 19 %), thinning zones and
  presets by hand, and not storing the loops repeated (29 KB of the 1mgm bank).
- The proposal above (packed in the flash, unpacked into SDRAM) stays valid for an AC7911BA or BB board, which has the memory.
- Not measured: how much SRAM is free, and the speed of any decoder on the FM-1.

## Calculation: a live cache of decoded sounds (not built)

The idea: the flash holds the whole bank as MP3 at 64 kbit/s, in one stream with an index of where each sound starts; a note whose sound is not in a cache in the SRAM has it decoded
into BFP8 (33 bytes for 32 frames, played in place by the engine as it is), and the least recently used sounds that no voice is playing go when the cache is full. Not whole programs
resident, as above, but only what is played. `measure_live_cache.js` plays standard MIDI files against the converter's bank (the key maps and layers of the preset each channel has,
the kit on channel 10, bank select ignored) and counts what the song does to a cache of a given size. The 15 files in `MIDIs/` (pop and band arrangements and piano music), the
4 MB font, `node measure_live_cache.js 4gmgsmt.sf2 MIDIs/*.mid`:

- A song touches few sounds: 8 to 57, 150 to 338 KB of BFP8 over the whole song (the 128 melodic presets and 10 kits of the font have 483 sounds, 1 457 KB).
- Over the 15 files (every sound a note needs, chords counted once for each zone):

| Cache (KB of BFP8) | Sounds found in it | Misses (decodes) | Sounds that could not be loaded | Decoded audio, average, % of a core (K 5 to 12) | Worst second, audio decoded | Most misses in 50 ms |
| --- | --- | --- | --- | --- | --- | --- |
| 96 | 87.7 % | 5 886 | 3 563 | 0.3 to 0.8 | 5.2 s | 15 |
| 128 | 94.0 % | 3 147 | 1 432 | 0.2 to 0.5 | 5.8 s | 15 |
| 192 | 98.6 % | 996 | 67 (two songs: 21 and 46) | 0.1 | 6.2 s | 15 |
| 256 | 99.4 % | 432 | **0** | 0.0 to 0.1 | 6.2 s | 15 |

  "Could not be loaded" is a sound that is needed while the cache is full of sounds that are playing; that note would be silent or late. K is the decoder's cost in million cycles for each
  second of mono audio on a 240 MHz core, an **assumption** (5 and 12 are tried; none measured on a pi32v2). The worst second is the song's start, when everything is cold: 6.2 s of
  audio to decode is 13 to 31 % of a core, and 15 cold sounds at once would wait, one after the other, about 45 to 110 ms for the last (4 frames of each: 3 of lead-in for the
  bit reservoir and the first; also an assumption), where a sound already in the cache costs nothing.
- Flash: the 4 MB font's 138 presets are 136 KB of tables and about 354 KB of MP3 (8 009 bytes for each second of audio, measured on one stream of all its sounds) and an index of 4 bytes a
  sound: 492 KB, 36 KB too much. The 128 melodic presets and kit 0 are 112 KB and about 315 KB: 429 KB, which fits (27 KB over); the melodic presets alone 390 KB. That is 1 419 KB of BFP8, 3.1
  times what the flash holds now (`measure_working_set.js`, 464 636 bytes for `1mgm.sf2`).

What this does not show, and what it needs:

- **The SRAM.** 192 to 256 KB is at the top of the plausible 150 to 250 KB that the firmware leaves, and a cache of sounds of every length needs an allocator that does not fragment; with
  10 to 25 % lost to that, 256 KB would act like 190 to 230. Below 192 KB, songs start to lose notes. The real figure is in the build's `firmware-report.json`.
- **The decoder.** An MP3 layer-III decoder that runs on the pi32v2 in the control task (the audio interrupt only reads the cache), 15 to 25 KB of code (a guess; the image has roughly 30 KB
  to spare), at a speed nobody has measured here. Decoding must stay ahead of a voice's read position (20 to 48 times real time at K 5 to 12, against a voice that reads at the pitch's speed, up to
  2 or 4 times); a note may start as soon as the lead-in is done.
- **The sound.** MP3 at 64 kbit/s is 16.7 dB against the source over the whole font (BFP8 is 43.8), and then goes into BFP8, whose noise is far below the MP3's. **Listened to, 2026-11-08,
  one listener:** the piano (a note, a chord, an arpeggio over the keyboard, bass and treble notes; the 4 MB font's Piano 1 against `1mgm.sf2`'s, rendered by a stand-in for the player with
  the sounds' pitch, loops and envelopes) as MP3 64 kbit/s decoded to BFP8 "sounds almost like" the same piano not compressed. The piano is an easy case: its ten sounds are 21.3 dB at 64 kbit/s
  (24.8 at 128), 49 KB against 188 KB, and 2.3 times the audio of `1mgm.sf2`'s piano in 82 KB. Not yet listened to: the rest. In the 16 s excerpts of three songs the weakest sounds are the
  noise-like ones, hi-hats, cabasa and ride cymbal, 4 to 5 dB at 64 kbit/s and 7 to 9 at 128 (strings about 14), and a few such sounds are what a lossless tier in "Mixed tiers" is for: the 4 MB
  font's kit hi-hats and cymbals stay BFP8 and cost a small part of the flash. The loops need the crossfade of the packed-data proposal (used in the renderings).
- **The engine.** A zone points into the flash today; it would point at a cache block, or at nothing yet. A voice pins its sound; a note whose sound cannot be loaded needs a decision (late or
  silent). The random access into one MP3 stream (a frame index and 2 or 3 frames of lead-in) is another reason the sounds are not stored as a stream each, which costs 77 % more on `1mgm.sf2`.
- **The songs.** 15 files, GM arrangements played from bank 0 and kit 0: a performer changing programs by hand, or a song with many different programs and kits, has a larger working set.
  The simulation counts every sound a note needs, with no look-ahead (a decoder could fetch the next likely zones when a program is chosen).

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
| Bank, BFP8 at 32 kHz | 464 636 bytes | 1 640 200 bytes: 139 412 of tables, 1 500 788 of sample data (3.5 times the flash). Before the converter stopped writing the zones of dropped presets (below) it was 1 748 904: 246 292 of tables |
| BFP8 + xz, samples as one stream | 330 356 | 1 223 662 |
| MP3 64 kbit/s, one stream, no gap / 256-frame gap | 93 933 / 103 725 (8 030 and 8 870 B/s) | 359 181 / 390 285 (8 010 and 8 700 B/s), SNR 17.5 / 17.9 dB |
| MP3 96 and 128 kbit/s, no gap | | 538 749 and 718 317 |
| Zone tables xz packed | 8 194 | 24 504 (38 604 before that) |
| Packed bank, MP3 64 kbit/s (tables + samples) | about 102 000 to 112 000 | about 384 000 (no gap) to 415 000 (gap): 82 to 89 % of the 466 944 bytes |

So `4gmgsmt.sf2` fits the flash at 64 kbit/s, and only at 64 kbit/s: 96 kbit/s is 539 KB for the samples alone. It matches what
the capacity table above predicted for a font of 45 s (360 to 397 KB). BFP8 packed with xz does not come near (1.2 MB). It has
none of the velocity layers, stereo or modulators of 2 to 4 (so nothing in those would change this font), but its 10 kits and its
variation banks are what 1 and the bank map are for: the kits are already written to the bank and only the player picks the first one,
and the 191 presets the bank map drops would cost zones, not samples.

**A waste in the converter, fixed.** `writeFm1` used to write the zone table of every preset of the file, also of the dropped ones, and
the sounds only they use. `4gmgsmt.sf2` had 3 560 zones in the bank where the 138 written presets use 1 882 (47 %), and 1 824 bytes of
sounds that nothing reached. It now writes only what a written preset reaches: 106 880 bytes fewer tables (246 292 to 139 412), 108 704 bytes
fewer in all (1 748 904 to 1 640 200, 6.2 %). The bank is the same to the player: `1mgm.sf2`, which has no dropped presets, gives the same bytes
as before in every format and mode, and for `4gmgsmt.sf2` every key, layer, zone field, envelope, modulation record and sample byte of the
138 bank-map slots is the same. `test_dropped_presets.js` keeps it so (no font needed). Zones that belong to no preset (a bank of loose
samples) are still written.

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
