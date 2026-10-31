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
