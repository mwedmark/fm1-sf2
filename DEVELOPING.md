# Developing fm1-sf2

The user manual is [README.md](README.md). This file lists what is in the repository for working on it.
Start with [RUNNING.md](RUNNING.md): how to build the firmware, run the tests, serve the pages and use the emulator and the device,
with the paths on the build machine.

## Build helpers

```powershell
.\build_firmware.ps1                     # dist\sf2player_<date>.fwsc, live cache with a 72 KB pool (-NoLiveCache: the plain firmware)
.\build_install.ps1                      # packs the last build and a bank into dist\install\ for install.html
.\build_all.ps1 -Name sf2player_x -Serve # both, then starts the server and opens install.html
.\start_web.ps1                          # serves index.html, install.html and song.html on http://localhost:8765
.\start_emu.ps1                          # the newest build in fm1-emu on http://127.0.0.1:8781 (RUNNING.md section 4)
```

The GitHub workflow (`.github/workflows/firmware.yml`) runs only when started by hand (Actions > firmware > Run workflow). It builds
both firmwares and, on `main`, publishes the emulator site with the MIDI player and the install page (`install.html` with the plain
firmware and `banks/default.fm1b`, packed by `make_install.js --fwsc --identity --out`).

## What is where

- `index.html`: the converter (hosted as `converter.html`). Drop an `.sf2`, pick the encoding (SEA5, SEA6, BFP8, raw PCM, or MP3 at 64/128/256 kbit/s packed by `pack_bank.js` for the live cache) and the sample rate, download a simplified `.sf2` or an FM1B `.bin`, and play the result on a keyboard in the simulator. Serve it (`python -m http.server`) rather than opening the file.
- `install.html`: firmware and bank to an FM-1 over USB-MIDI; from other firmware after a confirmation (through V15 when it is not V15). `make_install.js` (or `build_install.ps1`) fills `dist/install/`.
- `song.html`: a MIDI file into the RAM of an FM-1 that runs the plain firmware (`fm1_song.js` makes the song).
- `FORMAT.md`: the FM1B bank format, made to be used in place by the FM-1's CPU. Samples are raw PCM, SEA5/SEA6 (the FM-1's own codec) or BFP8.
- `fm1_open/`: a copy of [m-vave/fm1_open](https://github.com/m-vave/fm1_open) (upstream commit `4c7496d`, GPLv3 with the FM1 SDK linking exception, see `fm1_open/LICENSE.md`) with the SF2 player added. The player is `app/fm1b.c`, the panel `app/sf2player.c`, the bank upload `app/sf2_upload.c`, MIDI input `app/transfer.c`; `web/synth/sampledrum.c`, `web/engine/sound.c` and `web/engine/Simulator.c` only feed the browser simulator.
- `mp3_decoder_wasm.js`: the firmware's MP3 decoder (minimp3, `mp3_decoder_web.c`) as base64 for `index.html`'s MP3 banks; `node make_mp3_decoder.js` rebuilds it (the wasi-sdk under `fm1_open/build/tools`, or `FM1_CLANG`). `third_party/lamejs/`: the MP3 encoder the page uses (lamejs 1.2.1, LGPL-3.0, unchanged).
- `simulator_wasm.js`: the simulator built from `fm1_open`, as base64 for `index.html`. Rebuild it with `fm1_open/tools/Build-Web.ps1 -LocalPreview` (downloads the WASI SDK on first use), then `node make_wasm_js.js`.
- `FIRMWARE.md`: what it takes to run the player on a real FM-1 (the hook in the audio interrupt, the image size, safe mode).
- `HANDOVER_LIVE_CACHE.md`: the live cache (MP3-packed banks decoded into RAM) and where it stands.
- `FM1_TEST.md`: the checklist for the real FM-1 (Swedish).
- `convert.py`: the original command-line converter, no longer maintained.

## Tests and tools

- `test_core.js <sf2> [outdir]`: converter checks (every note of every preset resolves, SEA5 round trip).
- `test_sim.js <simulator.wasm> <bank.bin>`: plays a bank through the simulator and measures pitch, loop and release.
- `test_sea_equiv.js <sf2> <simulator.wasm>`: every program on five notes, SEA5 against the same samples as plain PCM, sample for sample.
- `test_formats.js [sf2]`: the firmware engine plays SEA5, SEA6, 8-, 12- and 16-bit banks exactly as their decoded samples.
- `make_dqt.js`: writes the engine's SEA6 quantizer table from `index.html`.
- `test_rate.js [bank]`: the firmware's output stage at 32 kHz with its resampler against 44.1 kHz. `make_resampler.js`: writes that resampling filter (`fm1_open/app/resample_table.inc`).
- `make_bank.js <format> [rate]`: the whole GM bank in SEA5, SEA6, BFP8 or PCM, for `start_emu.ps1 -Bank`.
- `pack_bank.js in out [--kbit 64]`: a BFP8 bank into the live cache's packed bank (one MP3 stream, a pack table, a lossless tier; ffmpeg). The converter page loads the same file and passes lamejs and `mp3_decoder_wasm.js` in place of ffmpeg. `test_pack_bank.js`: unpacks it again with the C decoder (`--packed=<file>` checks a bank the page made). `test_packed_engine.js`: the engine (`fm1b.c` with `FM1B_PACKED`) playing a packed bank, bit for bit against the plain engine on the unpacked bank. `test_sound_cache.js`: the decoder and the cache (`fm1_open/live_cache`).
- `test_chosen_presets.js [sf2]`: the converter's *Chosen instruments*: the ticked presets at their program numbers, the others on the empty preset, and silent in the engine (`fm1b.c`, the harness of `test_multitimbral.js`).
- `rescue_tools/test_rescue_chip.py`: the USB rescue on a pretend FM-1 with the first units' flash (`0x856014`), the newer units' (`0xB36014`) and an unknown one, which it must refuse.
- `test_upload.js`: the firmware's side of the bank upload on WebAssembly. `make_flash_overlay.js`: a flash image for fm1-emu.
- `make_midi.js`: the emulator scripts as MIDI files (`test_emu/midi/`).
- `measure_zone_codecs.py [sf2]`: what packing the sample data (xz, MP3) would save, sample by sample (python3, numpy, ffmpeg).
- `measure_font_features.py <sf2>`: what a SoundFont holds in the converter's terms (presets by bank, seconds of audio, velocity layers, stereo, kits, modulators; python3 only).
- `measure_working_set.js [sf2 ...]`: the sample data the programs that sound at once need, against the flash and the SRAM.
- `measure_sample_rates.py <sf2 ...>`: what a rate of its own for each sound would save (python3, numpy).
- `measure_live_cache.js <sf2> <mid ...>`: plays MIDI files against a bank and counts what a cache of decoded sounds in SRAM would miss.
- `map_sizes.js sdk.map`: what takes the room in the linked firmware (the biggest sections and objects of the link map, and the live cache's share).
- `fm1_info.js <sdk.elf> <bank>`: the CRC32s the panel's info page shows.

`.sf2` and `.bin` files are not committed (except the SoundFonts in `soundfonts/`).

The screenshots in `docs/img/` were taken in fm1-emu with the plain firmware and `dist/gm_32k_bfp8.fm1b`: the LCD from `window.__lcd`
(`start_emu.ps1` adds it), the panel from the emulator's canvas.
