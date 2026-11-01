# From the simulator to a firmware

Status: the player (`fm1_open/app/fm1b.c`) is tested in the browser simulator, and it compiles with the device toolchain. The stock
firmware builds here with JieLi 2.5.3 (`pi32v2` clang 4.0.1) into `fm1_open/build/dx7/sdk.elf`, 453 576 bytes, "SRAM/BLE/USB checks passed". The player is
compiled in but not called, and nothing has been flashed or run on a device.

## The toolchain on this machine

`D:\JL\pi32` (152 MB). JieLi's installer (`2.5.3.exe`, 52 860 672 bytes, signed by ZhuHai JieLi Technology) refuses to run without CodeBlocks in the
registry, so only the `pi32` folder was unpacked from it with `innoextract` (in an `ubuntu:22.04` container). `bin/clang.exe` has the SHA-256 the SDK expects
(`d4bc97cf...3bce`), so the build's manifest check passes. Build:

```powershell
cd fm1_open
./sdk/Build.ps1 -FirmwareOnly -Toolchain 'D:\JL\pi32'
```

## The SF2-player firmware (branch `sf2-player`)

The device is meant to be only an SF2 player, so the branch removes the stock application (panel UI, projects, presets, factory voices, sample
transfer, MIDI router: `screen.c`, `project.c`, `presets.c`, `fm_factory.c`, `fm_library.c`, `samples.c`, `midi_router.c`) and keeps what the library
and the build need. `libfm1_engine.a` (engine, Bluetooth, USB) stays; it calls 20 `groove_*` hooks the stock code provided, which
`app/sf2player.c` now stubs. The 27 keys play the bank (F3..G5, first encoder = program), MIDI input from DIN, USB and BLE plays it too
(`app/transfer.c`: `fm1_app_midi_received`), and `board/audio.c` mixes the player into the engine's output. `app/keep_linked.c`
references every library global, because `sdk/Build.ps1` insists that the engine's SRAM code is linked and the linker otherwise drops it.

Result with the stock `Build.ps1` and `manifest.json` untouched: app image **394 972 bytes** (stock 453 576), "SRAM/BLE/USB checks passed", and
`build/dx7/generated/FM1_OFFICIAL_OPEN.fwsc` is written. That leaves about 59 KB for a bank in the image. The user flash region and the
layout are unchanged. Not flashed and not run on a device. Whether the web workbench accepts a custom `.fwsc`, and what the screen,
keys, LEDs and volume knob do under this firmware, is unknown. Flashing something untested to the only device can leave it needing a recovery with M-VAVE's
tools; keep the original firmware (`sdk/firmware/FM-1_015.fwsc`) at hand.

### Run in the browser emulator (fm1-emu)

[fm1-emu](https://joakimeriksson.github.io/fm1-emu/) emulates the FM-1's CPU and runs `.fwsc` packages unchanged; **Open .fwsc...** takes ours.
Its page cannot read files from a local server across origins, so a local copy of the site's files (`index.html`, `main.js`, `worklet.js`,
`persistence.js`, `fwsc.js`, `tryout.js`, `firmwares.json`, `fm1_web.wasm`) was served next to the package for the tests. Measured at 120 MHz with the
status line's `load`: the stock library engine alone cost 26 % when idle (36 % total), so `board/audio.c` skips `fm1_engine_render()` for the player
(`FM1B_SKIP_ENGINE`); then idle is 10-14 % and a voice costs about 12 % (Harmonica, 22 kHz bank) or 4 % (piano, 12 kHz bank). Keys down raise the load and
releasing them lowers it again, so the key -> note queue -> SEA5 decode -> mix path runs. Audio was not listened to and the screen only shows a dark background.

`dist/sf2player_gm19.fwsc` (not committed) is the firmware with `make_bank_c.js 1mgm.sf2 0,4,11,16,19,24,25,27,32,33,40,48,56,60,65,71,73,80,115 12000`:
19 GM melodic programs at 12 kHz SEA5, 56 460 bytes, app image 452 764 of 453 921 allowed. The first encoder steps through the programs the bank has.
The whole `1mgm.sf2` (343 732 bytes at 32 kHz) does not fit: the package carries the image, not the user flash.

### What the player does with a SoundFont zone

- **Envelope per zone** (attack, hold, decay, sustain, release from the SoundFont's volume envelope generators, FORMAT.md "Envelopes"). Decay and release are
  exponential, a step is 32 frames; the voice ends when it has fallen below -96 dB. Percussion zones ignore note off.
- **Exact loops.** A piano's loop is 17-18 frames after resampling, and rounding that to a whole number of frames detunes the loop by several percent, which was
  heard as the pitch changing at the end of the sample. The converter now repeats a loop k times so that k loops are a whole number of frames within 0.25 %.
- **Tuning**: whole semitones move the root key, the rest (-50..50 cents) is a fine tune the player applies to the step.
- **The panel** shows the program, the zone the latest note hit (key range, root key, fine tune, sample length, loop, rate), the envelope and the voice's level.

### The bank in the user flash (`dist/sf2player_flash.fwsc`)

The package carries the firmware image only, so a bank too big for the image lives in the user flash, the region the stock firmware used for samples
(`FM1B_FLASH_OFFSET` 69 632, up to 466 944 bytes, `fm1b_config.h`). `fm1b_bank.c` looks for an FM1B bank there at start (`fm1_flash_map()`, magic, size from
the header) and plays it in place; without one it uses a bank compiled into the image if the build has one (`fm1b_bank_data.inc`), else the panel says NO BANK.
The flash build has no built-in bank (image 398 140 bytes, 56 KB room to spare). The whole 1mgm.sf2 at 32 kHz SEA5 (335.7 KB) plays from it in fm1-emu:
`node make_flash_overlay.js bank.fm1b gm.flash` makes the sector file for `&flash=` (the user partition starts at 0x7C000 of the flash image).

**Upload over MIDI SysEx** (`fm1_open/app/sf2_upload.c`, host side in `index.html`, button "Skicka till FM-1", Web MIDI): `F0 7D 'S' 'F' 02 cmd ... F7`,
numbers as 5 x 7 bits. `begin` (1, size) stops the bank and erases the flash it needs, `data` (2, offset + up to 448 bytes packed 7 to 8) is written where it says, `end`
(3, CRC-32) binds the bank when the checksum matches. The firmware answers each frame with `cmd | 0x40` and a status (0 done, 1 malformed, 2 outside the flash,
3 flash error, 4 CRC differs, 5 no begin first); the host sends the next frame only on the answer and sends a lost one again (writing the same bytes twice is
harmless). The SDK says no flash access from the SysEx callback, so the callback copies the frame and the control task (`groove_screen_control`, every cycle) does the work.
`node test_upload.js` runs the real `sf2_upload.c` on WebAssembly against a flash model that only lets bits be cleared: the whole bank in frames of 448, 112, 7 and 1 bytes,
and the refusals (data before begin, bad CRC, outside the bank, a flash error). **Not run on a device**, and fm1-emu cannot test it end to end: its DIN input stops delivering
frames after about 20 of them (the same with the flash calls left out), and the firmware's answers go nowhere. Over USB with the host waiting for the answers the pacing is the host's.

### Reverb and chorus (`fm1_open/app/sf2fx.c`)

The library's own reverb and delay are internal to its engine (local symbols, state for its tracks), so the player has its own: a Freeverb-style reverb (four combs,
two allpasses a side, mono in and stereo out) and a two-tap chorus (16 ms +- 6.8 ms, 0.8 Hz, opposite on the two channels). About 16 KB of SRAM, 16-bit delay lines, run from the
audio interrupt after the voices (`board/audio.c`); after 3 s of silence they cost nothing. Levels 0..127 (0 = off), default reverb 30 and chorus 20: MIDI CC 91 (reverb) and
CC 93 (chorus), and on the panel KNOB1 (reverb) and KNOB2 (chorus), 4 steps a click; the panel shows them. **Pan** of the dry sound: KNOB3 (4 steps a click) or MIDI CC 10 on any channel but 10, 0 = left, 64 = centre (the default), 127 = right. It is a balance, not a power pan: the side turned away from is faded out by sqrt(2 (1 - x)) and the other keeps its level, so the centre is bit exact the unpanned sound; reverb and chorus stay stereo. The gains are computed on the control side (integer square root), the audio interrupt only multiplies. It is global, not per program or channel. **Drums**: MIDI channel 10 plays the percussion kit (bank 128, the first kit the bank has) and SCL switches the 27 keys between the selected program and the kit (the panel then says DRUMS and the kit's name); the kit sounds beside the program, as the zones of a kit have the fixed flag and ignore note off. OCT- and OCT+ (buttons 0 and 1) move the keys by octaves, -2..2; a key released stops the note it started. `node test_fx.js` runs it on WebAssembly (off is bit exact dry, the tail
falls and dies out, no wrap at full level with noise, silence after the idle time, a new note after it). Measured in fm1-emu at 240 MHz with 8 voices held: 56 % with both effects
off, 62 % with both at 127 (idle 11 %). Not heard on a device.

## The hook is written, but the stock image is full

The hook (`board/audio.c`), the key route (`groove_midi_local` in `midi_router.c`: a key on a track whose source is the sampler plays the bank),
the built-in bank (`fm1b_bank.c`, from `make_bank_c.js`) and the note queue between the panel and the audio interrupt are all in the tree,
behind `FM1B_IN_FIRMWARE` in `fm1_open/app/fm1b_config.h`, which is 0. With 1 the code compiles and links ("SRAM/BLE/USB checks passed") but
packaging stops with "Application does not fit the FM1 layout.": `sdk/Package.cs` allows an application of
`0xff000 - 536576 - 4096 - 32768 - 16384 - 288 - 383 - 64 = 453 921` bytes and the stock one is 453 576, so 345 bytes are free. The player is about
3.6 KB (460 968 bytes with a 3.7 KB bank), and the one-preset bank for the first test is another few KB. To get there something has to shrink
by about 3.5 KB (a UI or factory-voice feature, or a smaller flash area for the user data, which changes the layout), or M-VAVE has to make room.
The bank itself belongs in the user sample region, not in the image, once an upload path exists.

## What the device gives us

- The instrument is `libfm1_engine.a`, a binary with a pinned hash (`sdk/synth.json`, checked by `sdk/Build.ps1`). Its sampler is the
  8-pad `sampledrum_*` code, which cannot play a keyboard. We do not change it, we add beside it.
- The audio interrupt (`app/board/audio.c`, `fm1_audio_irq`) calls `fm1_engine_render(rendered, 128)` and then
  `fm1_output_mix(rendered)`, whose header says "APP mixes external audio". `rendered` is 128 stereo frames of Q23 `int32_t`.
  That is the hook: add the melodic player's output to `rendered` between the two calls.
- `fm1_sea_decode_groups` (the SEA5 decoder the player uses) is in the library, so the player decodes with the device's own code.
- The sample flash region is `GROOVE_SAMPLE_OFFSET` (69 632) for `GROOVE_SAMPLE_BYTES` (466 944), mapped with `fm1_flash_map()`
  (`app/instrument.h`, `app/samples.c`). A bank of up to that size fits (a 316 604-byte SEA5 bank of all 129 presets does).

## The change in `fm1_audio_irq`

```c
static int32_t melodic[FM1_BLOCK_FRAMES];                    /* with the other buffers */
...
if (fm1_engine_render(rendered, FM1_BLOCK_FRAMES) != 0) { ++statistics.overruns; return; }
if (fm1b_ready()) {
    memset(melodic, 0, sizeof melodic);
    fm1b_render(&melodic, melodic, FM1_BLOCK_FRAMES);       /* owner = any fixed pointer, here the buffer */
    for (unsigned i = 0; i < FM1_BLOCK_FRAMES; ++i) {
        int64_t l = (int64_t)rendered[2 * i] + melodic[i], r = (int64_t)rendered[2 * i + 1] + melodic[i];
        rendered[2 * i] = clamp24(l); rendered[2 * i + 1] = clamp24(r);   /* 24-bit saturation, as fm1_output_mix does */
    }
}
fm1_output_mix(rendered);
```

`fm1b.c` has no globals besides its own and uses `FM1_DSP_CODE` so it lands in the same RAM section as the engine's code.
Use the same note owner for `fm1b_note()` as for `fm1b_render()`.

## What is still missing

1. **Flashing and a device to test on.** `-FirmwareOnly` stops at `sdk.elf`; the packaged firmware that the web workbench flashes comes from
   the full `build.bat --package` (needs `FM1_SOURCE_URL`, see `fm1_open/README.md`). Not tried.
2. **Notes into the player.** The keyboard and MIDI input go into the engine inside the library. `fm1b_note()` must be called with the same
   notes, from wherever the app sees them (the track/key code in `app/`; `midi_router.c` only handles MIDI *output*). Not investigated yet.
3. **The bank in flash.** `app/samples.c` and `app/project.c` both treat the sample region as the 8-slot SEA bank
   (magic `0x53314d46`, 32 000 Hz). An FM1B bank needs its own magic check (`FM1B`) and an upload path (`app/transfer.c`, the workbench).
4. **The quantizer table.** `fm1b_bind()` takes the 512-entry SEA5 table. The firmware needs its own copy (the same numbers as
   `fm1_open/web/synth/sample_metadata.inc`, `sdk/Samples.js`).
5. **CPU budget.** Unknown until measured. The audio interrupt has `statistics.peak_ticks` against `block_ticks` (2.9 ms per block).
   Each SEA5 voice costs one decoded sample per output sample at the original pitch, more when played higher, plus a jump
   back to the chunk start on every loop wrap (up to 320 samples). Start with `FM1B_VOICES` at 4 or 8 and raise it while the peak stays low.
6. **Program selection and the level control** on the panel (`fm1b_program()`, the `level` argument).
