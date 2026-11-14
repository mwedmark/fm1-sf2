# Build, test, run: the short version

Everything needed to get the SF2 player going again. Facts marked **(verified)** were run on this machine on 2026-11-01;
**(not verified)** means it comes from older notes or is a best guess. Details of the firmware are in `FIRMWARE.md`.

## 0. What is where

| What | Where |
|---|---|
| Project | `D:\git\fm1-sf2` (not `D:\git\fm-sf2`, that folder is empty) |
| JieLi device toolchain 2.5.3 | **`D:\JL\pi32`** (not `C:\JL`). Needs `bin\clang.exe` |
| WASI SDK 27 (for the WebAssembly tests) | `fm1_open\build\tools\wasi-sdk-27.0-x86_64-windows` (downloaded by `Build-Web.ps1`) |
| Node, Python 3.13 | on `PATH` (Python only for `http.server`) |
| ffmpeg with libmp3lame | on `PATH`; only for the live cache (`pack_bank.js`, `test_pack_bank.js`, `test_sound_cache.js`, `test_packed_engine.js`, `measure_zone_codecs.py`). Install: `winget install --id Gyan.FFmpeg`, open a new PowerShell and check `ffmpeg -version` and `ffmpeg -hide_banner -encoders \| Select-String libmp3lame` (a minimal build without libmp3lame cannot encode the MP3 stream). Without winget: the "release essentials" zip from gyan.dev, and `$env:PATH += ';D:\tools\ffmpeg\bin'` for the session |
| Docker | **not needed**. The firmware builds natively (verified). The Docker images on the machine are for other projects |
| Recovery firmware | `fm1_open\sdk\firmware\FM-1_015.fwsc`, keep it at hand before flashing anything |

## 1. Build the firmware (verified)

**With a name** (2026-11-05), for the panel's info page (REC, section 5):

```powershell
cd D:\git\fm1-sf2
.\build_fw.ps1 -Name sf2player_32v_info                       # dist\sf2player_32v_info.fwsc and dist\sf2player_32v_info.txt
.\build_fw.ps1 -Name sf2player_32v_info -Bank dist\gm_32k.fm1b  # the .txt then has that bank's and the sound set's CRC32
```

It writes the name and time to `fm1_open\app\build_info.h` (git ignores it), runs the plain build below, removes the header again,
copies the package to `dist\<name>.fwsc` and writes what `node fm1_info.js` prints next to it. A plain build shows
`FM1_OFFICIAL_OPEN.fwsc` and the compiler's clock on the info page.

**Defaults since 2026-11-14:** `build_fw.ps1`, `build_firmware.ps1` and `build_all.ps1` build the 44.1 kHz player with both cores and four-point interpolation, for
`dist\gm_44k_fill.fm1b` (sections 2f, 2h, 2i). `-Rate 32000` is the old 32 kHz build (linear, one core, `dist\gm_32k_packed.fm1b`; the figures below are for it),
`-Linear` turns four-point off, `-DualCore off` is one core. The GitHub workflow builds with `sdk\Build.ps1` and is unchanged (32 kHz).

**One firmware, with the live cache** (the default since 2026-11-11; `build_fw.ps1`, `build_firmware.ps1` and `sdk\Build.ps1` build it without a switch, the
GitHub workflow builds only it, and `install.html` installs it for BFP8 and MP3 banks alike; `-LiveCache:$false` on `build_fw.ps1` or `sdk\Build.ps1`, or
`-NoLiveCache` on `build_firmware.ps1`, builds the firmware without it, which has every bank format and the MIDI file player). With all the formats
and the cache, the heap was 3.7 KB short (their voice loops run from SRAM); the MIDI file player does not fit the image (2.4 KB). Pool 84 KB:
image 453 756 bytes with a name from `build_fw.ps1` (165 to spare), 453 852 from a plain `sdk\Build.ps1` (69: the compiler's date string), heap 111 660.
It plays banks packed as MP3 by `pack_bank.js` (FORMAT.md, "Calculation: a live cache of decoded sounds").
The image is at the layout's limit, so this build also plays BFP8 banks only (`FM1B_BFP8_ONLY`: one voice loop in place of ten; the packed bank is BFP8 after decoding; SEA, 8-, 12- and 16-bit banks are refused), and leaves out the MIDI file player (`FM1_MIDIPLAY=0`; the code stays and the plain build has it: with it the image was 2 235 bytes over, and a song takes its RAM from the heap the cache leaves short). (The SDRAM test of the info page is gone from every build since 2026-11-08: on the FM-1 it restarted the device, there is no SDRAM.)
It adds `live_cache\mp3_sound.c` and `sound_cache.c` and minimp3, defines `FM1B_PACKED=1`, and puts a static pool of `-CacheKB` KB (default 84, 16 to 400; 90 and more did not boot) in the SRAM, which the link takes from the
runtime heap; `-CacheFrames` (default 2) is the MP3 frames (1152 samples) decoded in one control cycle. The build prints the heap that is left (`runtime_heap_bytes` in `firmware-report.json`), and stops under 49 152
bytes, which is how the free SRAM is found: raise `-CacheKB` until it stops. Without the switch the firmware is what it was. What to look at in the first build, none of it known:
the compiler's warnings (`-Wframe-larger-than`: minimp3's decode takes about 16 KB of stack, and the control task that calls `fm1b_cache_service` must have it), the image size against the 30 KB or so spare
(`Application does not fit the FM1 layout.`), and whether minimp3's floats link (on WebAssembly it needs only `memcpy`, `memset`, `memmove`, `memcmp`). Then the bank: `node pack_bank.js in.fm1b out.fm1b`, to the
flash as any bank (the file's size is in its header, so the existing flash path binds it); a packed bank the player binds without the switch is refused, so nothing plays rather than the wrong thing.

**Plain:**

```powershell
cd D:\git\fm1-sf2\fm1_open
$env:FM1_PI32_TOOLCHAIN = 'D:\JL\pi32'
./sdk/Build.ps1 -FirmwareOnly -Toolchain 'D:\JL\pi32'
```

Success looks like this, and the five `keep_linked.c` warnings about `malloc`/`calloc`/`realloc`/`sprintf`/`strdup` are old and harmless:

```
FM1: 403260 bytes app image; SRAM/BLE/USB checks passed
Offline package fits 1 MiB: image 421888, VM 81920, user 536576 bytes
```

Outputs, all under `fm1_open\build\dx7\`:

- `generated\FM1_OFFICIAL_OPEN.fwsc` is **the package**: for fm1-emu (section 4) and the M-VAVE workbench.
- `sdk.elf` is the linked image. `firmware-report.json` has the sizes.

App image history: 401 020 bytes before multitimbral, 403 260 after (+2 kB), 407 068 with pitch bend, modulation wheel, sustain pedal
and the channel page (+3.8 kB), 407 452 with program choice per channel on the panel,
408 412 with effect sends per channel, 408 668 with 12-bit samples, 413 404 with the faster voice loop and the engine's own
SEA decoder (SEA5 and SEA6), 412 540 with 16 voices, 419 740 with the player at 32 kHz (6 KB of it the resampling filter), 420 604 with 24 voices and the CPU guard, 421 212 with BFP8. If a change makes the build stop with
"Application does not fit the FM1 layout.", the image has outgrown its room (see `FIRMWARE.md`).

`dist\*.fwsc` are older packages from earlier builds (not committed). Copy a new one there if you want to keep it.

**Before flashing any build** (2026-11-08): start it in fm1-emu (section 4) and check that the screen draws and a note sounds:
`.\start_emu.ps1 -Tap -NoBrowser -Bank <bank>`, open the address it prints, and check that the page says `Running sf2player.fwsc?v=` with the new
build's number. A build with too small a runtime heap shows a blank screen and about 320 % load there, and on the FM-1 it does not start at all. The build
stops under 111 468 bytes of heap. If a bank makes the player crash: safe mode, OCT- and OCT+ held at power-on (FIRMWARE.md).

## 2. Tests that need no device (verified, all pass)

Each one compiles the real C source to WebAssembly with the WASI SDK on the first run (and again when a source is newer).

```bash
cd D:/git/fm1-sf2
node test_multitimbral.js     # 93 checks: channels, program change, volume, CC120/121/123, channel 10, pitch bend and its
                              # RPN range, modulation wheel, sustain pedal, what the channel page reads, the effect sends, pan,
                              # mute and solo. Uses dist/gm_32k.fm1b
node test_fx.js               # reverb and chorus (sf2fx.c), master levels times sends
node test_upload.js           # the SysEx bank upload (sf2_upload.c)
node test_formats.js          # SEA5, SEA6, BFP8, 8-, 12- and 16-bit banks play exactly their decoded samples (needs 1mgm.sf2)
node test_rate.js             # the output stage at 32 kHz + resampler against 44.1 kHz: pitch, level, brightness, release, vibrato, reverb;
                              # the velocity curve and the output limiter
node test_zone.js             # zone level, exclusive class (hi-hat choke), scale tuning: converter against 1mgm.sf2, then fm1b.c
node test_lfo.js              # the zone's LFOs (vibrato, tremolo, delay) and key to hold/decay, at 44.1 and 32 kHz, on a sine bank
node test_filter.js           # the low-pass filter and the modulation envelope against the same filter in floating point; 1mgm.sf2
node test_layers.js           # stacked layers: every zone that covers a key sounds; 1mgm.sf2 at 32 kHz, BFP8
node test_dropped_presets.js  # the zones and sounds of presets the bank map drops are not written (a bank made by hand, no font)
python3 test_sdram_config.py  # make_sdram_config.py: boot-config.bin read as its entries, the copy with the SDRAM on differs in two keys only (python3, no packages)
node pack_bank.js in.fm1b out.fm1b          # the live cache's packed bank from a BFP8 bank (ffmpeg); node test_pack_bank.js [font] unpacks it again with the C decoder
node test_packed_engine.js [font]            # the engine (fm1b.c, FM1B_PACKED) playing a packed bank against the plain engine, bit for bit; ffmpeg and any clang for wasm32 (1 min with the 4 MB font)
node test_sound_cache.js [font] [mid]  # the live cache's decoder and cache (fm1_open/live_cache): minimp3 into BFP8, the pool, a song played through it; ffmpeg and any clang for wasm32
```

`test_multitimbral.js` runs the real `fm1b.c` with 16 voices, as the device has. It does **not** cover
`transfer.c` (MIDI routing: bank select, channel 10 skipping program change, the controllers per channel) or the panel (the `MULTI` line, the channel page).
Those are only compiled, so test them in the emulator or on the device.

### 2b. Against FluidSynth, the reference (2026-11-04)

`compare_fluid.js` plays the same MIDI on FluidSynth with `1mgm.sf2` and on the player's whole output stage on WebAssembly
(`test_fluid/wrap.c`: fm1b + sf2fx + sf2out at 32 kHz, resampled to 44.1 kHz, MIDI routed as `transfer.c` does) with `dist/gm_32k.fm1b`,
reverb and chorus off, one gain for the whole render. FluidSynth 2.6.1 is the Windows zip from github.com/FluidSynth/fluidsynth/releases
unpacked in `tools/fluidsynth/` (git ignores it). WAVs to listen to, level matched, go to `test_fluid/out/`.

```powershell
node compare_fluid.js notes                         # velocity sweep, 11 programs: level and brightness per velocity
node compare_fluid.js spectra                       # all 128 programs at notes 48, 60, 72: FM-1 - FluidSynth by octave band
node compare_fluid.js midi "MIDIs\TOTO.Africa K.mid" --seconds=60
```

Found, 2026-11-04:
- **Velocity:** level per velocity matches within 0.1 dB in every program. FluidSynth does **not** filter by velocity: it adds the
  SF2 default modulator (velocity to cutoff, -2400 cents) but `fluid_mod_get_value` returns 0 for it ("to stop forcing velocity based
  filtering"), so the player, which has none, already plays as FluidSynth does.
- **Treble:** the player is darker. `spectra`, mean over programs (linear interpolation): -1.2 dB at 1 kHz, -1.9 at 2k, -3.0 at 4k, -3.3 at 8k, -5.5 at 16k. MIDI files, 60 s:
  Africa -4.3 / -7.1 dB at 4k / 8k (spectral distance 4.4 dB), Dancing Queen -1.2 / -4.2, Bach BWV 846 -1.9 / -2.8, Star Wars -1.9 / -5.9.
  Level over time correlates 0.88-0.99, within 1-1.4 dB on average.
- **Where it comes from** (`--interp`, `--polyphony`, `--rate`, banks from `make_bank.js` into `test_fluid/out/`): with a 44.1 kHz
  PCM16 bank, the player at 44.1 kHz and FluidSynth linear (`--interp=1`), every band is within 0.2 dB: the engine is right.
  BFP8 against PCM16 at 32 kHz: no difference. FluidSynth at 32 voices: no difference. The resampler to 44.1 kHz: no difference.
  What is left is the 32 kHz bank under the player's linear interpolation, whose gain at a frequency f is about cos(pi f / 32000)
  (-0.7 dB at 4 kHz, -3 dB at 8 kHz) where FluidSynth's default 4-point interpolation is near flat; part of the tilt from 1 kHz is not explained yet.
- The converter's `resample()` (box average down, linear up) is flat within 0.7 dB to 8 kHz but adds noise: aliases at -21 dB of a
  4 kHz tone, -15 dB at 8 kHz. A Kaiser-windowed sinc (tried, kept in `git stash`, "sinc resample") brings them to about -95 dB, the same
  bank size; against FluidSynth it measures darker (the aliases had filled the treble), so it waits for the interpolation.
- **The sinc again, with the 4-point interpolation (2026-11-04).** Same bank size (BFP8 464 580 bytes); the converter takes 21 s for
  1mgm.sf2 in Node against 1.5 s. `spectra` mean, before / sinc: lowest band +1.8 / -0.5, 1k -1.0 / -1.2, 2k -1.3 / -1.8, 4k -2.2 / -3.5,
  8k -3.8 / -6.1, 16k -10.2 / -12.0 dB. Africa 4k / 8k -2.9 / -6.2 against -4.3 / -9.2 (spectral distance 4.7 / 5.4 dB), Dancing Queen
  8k -3.1 / -7.5 (5.7 / 7.0 dB). So the sinc is cleaner below 1 kHz and further from FluidSynth above it. By ear (the user, same day)
  the old resampler sounds better: the sinc is dropped, and the old `resample()` stays.
  **Why:** 1mgm.sf2 plays its short samples far below their pitch (Tinker Bell's agogo, root 107, on keys 0-127; E Piano 1's
  coldglass7wave, root 65, from key 0). A note n semitones under the root takes its treble from the sample's content up to
  16 kHz * 2^(n/12), and the 32 kHz bank has nothing above 16 kHz: what FluidSynth plays from 16-22 kHz of the 44.1 kHz sample is gone,
  and the old resampler's aliases stood in for it. Every sample of 1mgm.sf2 is played at least 5.5 semitones under its pitch somewhere,
  so storing those at 44.1 kHz (possible without the firmware: root + 6, fine - 45 cents) would cost 38 % more flash, with 0.5 % left.
  The rest of the treble gap is the bank rate, not the engine (with a 44.1 kHz PCM16 bank every band is within 0.2 dB, above).
  WAVs to compare by ear: `test_fluid/out/listen/` (Africa, Dancing Queen and the `spectra` sweep: `_1_before`, `_2_sinc`, `_3_fluidsynth`).
- Per program level: most within 1.5 dB; louder are 73 flute (+4.4), 10 music box (+7.7), 120 guitar fret noise (+8.4), 123 bird (+8.0);
  quieter 124 telephone (-7.9). Not looked into.
- **4-point interpolation (`FM1B_INTERP4`, 2026-11-04; off again since 2026-11-05, see below; `--interp4` in `compare_fluid.js` builds it).** Catmull-Rom
  as FluidSynth: four frames, weights from a 256-row Q14 table (2 KB of RAM, made at the first `fm1b_bind`). For BFP8, when the four
  frames are in one group with no loop end among them (29 of 32), the shift is read once and put on after the weighted sum (bit exact
  with the general way). Sound: `spectra` mean 1k -1.0, 2k -1.3, 4k -2.2, 8k -3.8, 16k -10.2 dB (linear: -1.2, -1.9, -3.0, -3.3, -5.5;
  the energy linear had over 8 kHz is mostly its own aliases); Africa 2k / 4k / 8k -0.4 / -2.9 / -6.2 against -1.4 / -4.3 / -7.1.
  All the WebAssembly tests pass. **Cost**, fm1-emu at 240 MHz, BFP8, the firmware's busy share of emulated time (`1 - idleShare`, read
  from `window.__perf`, a line added at the end of the local `emu/main.js`; steady to 0.1 %, unlike the page's `load`):

  | | idle | `load32_plain` (32 flutes) | `load32_filter` (32, filtered) |
  | --- | --- | --- | --- |
  | linear | 4.4 % | 31.5 % (page load 41 %) | 48.9 % (62 %) |
  | 4-point, four `sample_at` | | 48.4 % (63 %) | |
  | 4-point, BFP8 one-group path | | 41.8 % (68 %) | 59.0 % (88 %) |

  So about +10 points at 32 voices, a voice some 38 % dearer (0.85 % of the CPU against 1.17 %). Image 434 492 bytes (+1.8 KB).
  The page's `load` (host time) rises more than the busy share; whether the FM-1's own guard (80 %) is reached with 32 filtered
  voices must be seen on the device.
  **Off again (2026-11-05).** On the FM-1, *Never Gonna Give You Up* dropped notes with it that the user did not hear with the
  linear build (`dist/sf2player_32v_linear.fwsc`, everything else the same); by ear fewer notes drop with linear, so linear is the
  default again. The song needs more than 32 voices either way: on the PC (`compare_fluid.js midi`) 3 451 voices are cut for the
  limit with both, and in fm1-emu the CPU guard never cut in (80 s). So on the device the dearer voices likely brought the guard on.
  The panel's `DROPPED` line (section 5) now shows which it is.

### 2c. Plain BFP8 and MP3 64 kbit/s against FluidSynth (2026-11-12, issue 26)

Both banks come from `soundfonts/1mgm.sf2` at 32 kHz and go through `compare_fluid.js` against FluidSynth 2.6.1 (default 4-point interpolation, reverb and chorus off).
The engine is the current `fm1b.c` (32 voices, layers, filter, LFOs, pan), built on WebAssembly with the WASI SDK. **Nothing here was listened to**; these are figures.

- **BFP8:** `node make_bank.js bfp8 32000 soundfonts/1mgm.sf2 test_fluid/cmp/bfp8.fm1b` (464 636 bytes).
- **MP3 64:** `node pack_bank.js test_fluid/cmp/bfp8.fm1b test_fluid/cmp/mp3_64_packed.fm1b --kbit 64` is the bank the live-cache firmware gets (241 520 bytes: 120 sounds as MP3,
  45 kept lossless, mean 17.1 dB against the source). `node unpack_bank.js` (new) decodes it with the live cache's own C decoder into a plain BFP8 bank, and `compare_fluid.js` plays that
  with the plain engine. `test_packed_engine.js` shows the packed engine gives the same samples, one block later, so this is what the live-cache firmware plays. It is the shipped
  mix (some sounds lossless), not MP3 on every sound.
- Runs, per bank: `notes`, `spectra` (all 128 programs at notes 48, 60, 72, velocity 100) and `midi --seconds=60` on six songs. `test_fluid/render_snr.js`, `spectra_diff.js`,
  `program_distance.js` and `song_snr.js` compare the two banks' results; the WAVs and texts are in `test_fluid/cmp/` (git ignores it).

**Result: MP3 64 sits within a few tenths of a dB of BFP8 against FluidSynth. It is worse for a handful of programs.**

| | BFP8 | MP3 64 |
| --- | --- | --- |
| `notes`: mean level error against FluidSynth over velocity | 0.03 dB | 0.02 dB |
| `spectra`: mean RMS distance to FluidSynth over the octave bands, 128 programs | 3.14 dB | 3.27 dB |
| `spectra`: mean FM-1 minus FluidSynth, 63 / 250 / 1k / 4k / 8k / 16k Hz | +1.9 / -0.1 / -1.2 / -3.0 / -3.3 / -5.5 dB | +2.5 / -0.1 / -1.1 / -2.7 / -3.1 / -5.3 dB |

Both lose treble against FluidSynth in the same way, and it comes from the 32 kHz player, not from the codec (see #4); the codec adds about 0.1 dB to it.

**The MP3 render against the BFP8 render** (same notes, same engine, gain undone), signal to difference over the 3 x 1 s notes of each program:
mean 21.7 dB, median 20.4 dB, worst 2.8 dB, best 46.6 dB. 63 programs are under 20 dB, 50 between 20 and 25, 3 between 25 and 40, and 12 above 40 (probably programs whose sounds
are all lossless). This is a waveform measure, close to the 17 dB per sample of the pack, and says little about what is heard (an MP3 hides its noise under the signal).

**Programs where MP3 64 is farther from FluidSynth than BFP8** (RMS over the bands; 23 programs by 0.5 dB or more, 9 by 1 dB or more, 2 by 2 dB or more; 8 are closer by 0.5 dB or more):

| Program | BFP8 | MP3 64 |
| --- | --- | --- |
| 10 Music Box | 5.8 | 9.0 |
| 29 Overdriven Guitar | 1.4 | 3.6 |
| 9 Glockenspiel | 2.4 | 4.2 |
| 80 Square Lead | 3.3 | 4.9 |
| 65 Alto Sax | 2.4 | 4.0 |
| 19 Church Organ | 0.8 | 2.3 |
| 12 Marimba | 2.8 | 4.2 |
| 69 English Horn | 2.0 | 3.2 |
| 23 Tango Accordion | 3.5 | 4.6 |

Lowest signal to difference: 31 Guitar Harmonics (2.8 dB), 9 Glockenspiel (10.0), 14 Tubular Bells (11.8), 27 Clean Guitar (12.1), 16 Drawbar Organ (12.4), 19 Church Organ (12.5),
111 Shanai (12.8), 12 Marimba (13.2), 109 Bagpipe (13.3). Program 31 is odd: its spectrum is close to the BFP8's (bands within 3 dB) but the waveform correlates only 0.67, with the MP3
render about 670 samples (15 ms) early; the other programs checked lie within 1 sample of the BFP8's. Not investigated; it may be a sound whose start or loop the MP3 path moves.

**Whole songs, 60 s, `midi`** (spectral distance to FluidSynth in dB, mean level error per 100 ms, MP3 render against BFP8 render):

| Song | Distance BFP8 | Distance MP3 | Level error BFP8 / MP3 | SNR MP3 vs BFP8 |
| --- | --- | --- | --- | --- |
| TOTO Africa | 4.43 | 4.41 | 1.07 / 1.02 dB | 22.1 dB |
| ABBA Dancing Queen | 4.79 | 4.73 | 0.96 / 0.94 dB | 19.2 dB |
| Star Wars theme | 3.94 | 3.94 | 0.72 / 0.67 dB | 19.2 dB |
| Eine kleine Nachtmusik | 3.90 | 4.19 | 0.58 / 0.62 dB | 16.9 dB |
| Bach 846 | 1.96 | 2.09 | 0.72 / 0.72 dB | 22.6 dB |
| Into the Groove | 4.99 | 5.20 | 0.85 / 0.85 dB | 17.7 dB |

The octave bands of every song differ by at most 0.5 dB between the banks. The strings (Eine kleine) and the synth and drum song (Into the Groove) move most, 0.2-0.3 dB. The limit of 32 voices cuts
the same notes with both banks (for example 509 in Africa, 1 140 in Dancing Queen), so part of every song's distance to FluidSynth (256 voices) is the limit, the same for both.

**Reading it.** On figures MP3 64 is as good as BFP8 for the whole font and for songs, and 241 520 bytes against 464 636 is a bank half the size. The risk is in the 9 to 20 programs above:
mallets and bells (Music Box, Glockenspiel, Marimba, Tubular Bells), organs, saxes, an overdriven guitar and the square lead. Those are the ones to listen to first.

**To listen to** (level matched to FluidSynth by each run, so the three differ by 0.3-0.5 dB in gain at most):
- `test_fluid/cmp/out_bfp8/spectra_fluid.wav`, `.../spectra_fm1.wav` (BFP8) and `test_fluid/cmp/out_mp3/spectra_fm1.wav` (MP3 64): every program, three notes each, 4.85 s a program from 0.1 s.
- The songs: `test_fluid/cmp/out_bfp8/<song>_fluid.wav`, `<song>_fm1.wav` and `test_fluid/cmp/out_mp3/<song>_fm1.wav`.

**Not done:** the listening itself (so no word on what MP3 64 sounds like beyond the figures), a bank with MP3 on every sound (the pack keeps 45 sounds lossless), the tiers' thresholds (FORMAT.md),
and program 31's offset.

### 2d. Would 44.1 kHz help? MP3 at 32 and 44.1 kHz, and where the treble is (2026-11-12)

The bank and the player work at 32 kHz, so everything above 16 kHz is gone before any codec. Two measurements on the samples of `soundfonts/1mgm.sf2` (44.1 kHz source) and a first
listening. Nothing was built for the device, and the CPU and RAM figures below are estimates (measured in the emulator since: 2e).

**MP3 at 32 against 44.1 kHz** (`node test_fluid/rate_mp3.js`; ffmpeg/libmp3lame, mono; all 152 samples joined, 26.9 s; the 32 kHz rows are resampled first, as the converter does;
decoded and compared with the 44.1 kHz source):

| Rate, bit rate | SNR against the source | SNR under 15.5 kHz | Energy above 16 kHz against the source |
| --- | --- | --- | --- |
| 32 kHz, 64 kbit/s (today) | 9.2 dB | 15.4 dB | -32 dB (gone) |
| 32 kHz, 128 kbit/s | 9.7 dB | 18.1 dB | -32 dB (gone) |
| 44.1 kHz, 64 kbit/s | 9.9 dB | 16.5 dB | -8.4 dB |
| 44.1 kHz, 128 kbit/s | 14.8 dB | 22.1 dB | -1.6 dB |
| 44.1 kHz, 192 kbit/s | 16.0 dB | 29.4 dB | -1.4 dB |

The MP3 codec does not throw the treble away by itself: at 44.1 kHz and 128 kbit/s nearly all of it is kept, at 64 kbit/s part of it. At 32 kHz it is lost in the resampling, and a
higher bit rate only helps below 16 kHz. In this join 6 % of the energy is above 16 kHz. The stream's bytes per second of audio are the same at both rates (8 KB at 64 kbit/s, 16 KB at 128).

**Where the treble is** (`node test_fluid/hf_share.js`; the share of each sample's energy above 16 kHz; 139 samples of 256 frames or more):

| A sample is kept at 44.1 kHz when its share above 16 kHz is at least | Samples | Of the frames | BFP8 sample data |
| --- | --- | --- | --- |
| (none: all at 32 kHz, today) | 0 | 0 % | 376 KB |
| 1 % (-20 dB) | 62 | 45 % | 439 KB |
| 0.1 % (-30 dB) | 85 | 65 % | 468 KB |
| 0.01 % (-40 dB) | 113 | 84 % | 495 KB |
| (all at 44.1 kHz) | 139 | 100 % | 518 KB |

Most above 16 kHz: guiro 98.9 %, bell tree 93.9 %, a vibraphone loop 84.6 %, clave and cabasa 75.2 %, open hi-hat 74.1 %, crash 66.8 %, ride 60.2 %. A plain BFP8 bank at 44.1 kHz does not
fit the 456 KB of flash, and a rate of its own for each sound saves less than hoped: with the -20 dB rule the data grows 1.17 times instead of 1.38.

**Listening, 2026-11-12** (one listener, not blind; closed, pedal and open hi-hat, crash and ride, each as FluidSynth, plain BFP8, MP3 64; `test_fluid/cmp/listen/drums_ABC.flac`, made with
`compare_fluid.js spectra --programs=0 --notes=60 --drums=42,44,46,49,51`): BFP8 and MP3 64 sound almost identical; FluidSynth differs clearly from both, most on the hi-hats, least on the ride.
So the codec is not what is heard; the 32 kHz limit is.

**What 44.1 kHz would cost (estimates, not measured).**
- CPU: mixing and decoding grow with the rate, 1.38 times; the resampler to 44.1 kHz goes away (the move to 32 kHz took 16 SEA6 voices from 49-51 % to 44-48 %). A rough guess for 32 BFP8 voices is 55-62 %
  against 42-48 % today; the CPU guard acts at 80 %. The MP3 decoder's share comes on top.
- RAM: the live cache's pool (84 KB, already the limit for a whole song: HANDOVER_LIVE_CACHE.md) would hold 1.38 times less, as about 61 KB does today.
- Code: `FM1B_RATE`, minimp3's `MINIMP3_FM1_MONO_32K`, `pack_bank.js` (frame bytes from 32 000) and the converter's rate all assume 32 kHz.

**Ways to find the RAM, none tried:** SRAM the SDK holds for things this firmware may not use (effects 49 KB, Bluetooth audio 16 KB, the 4 KB `groove_project` scratch; who uses them is not checked);
a smaller code in the cache (SEA5 or SEA6, 24 or 12 % smaller, about one point of CPU a voice); a rate of its own for each sound; voices that do not pin a whole sound (decoding ahead of the
voice, a bigger change). To measure the CPU for real: a firmware with `FM1B_RATE=44100`, minimp3 and the packer at 44.1 kHz, a 44.1 kHz bank, and the load scripts in fm1-emu (section 7b).

### 2e. A 44.1 kHz build in fm1-emu: the CPU holds, the pool does not (2026-11-12)

The estimates of 2d, measured. Emulator only: nothing was put on the FM-1 and nothing was listened to, and fm1-emu decodes MP3 faster than the device does.

**How.** A file `fm1_open\app\fm1b_rate.h` with `#define FM1B_RATE 44100U` turns the engine, the live cache and minimp3 to 44.1 kHz (`fm1b.h` and `mp3_sound.h` include it when it is there; minimp3 then
takes the 44.1 kHz band table). The file is not in the repository, and without it the build is the 32 kHz one, byte for byte the same size. `pack_bank.js` and `unpack_bank.js` read the rate from the bank.

```
node make_bank.js bfp8 44100 soundfonts/1mgm.sf2 test_fluid/cmp/bfp8_44k.fm1b
node pack_bank.js test_fluid/cmp/bfp8_44k.fm1b test_fluid/cmp/mp3_64_44k_packed.fm1b --kbit 64
.\build_fw.ps1 -Name rate44k -CacheKB 81        (with fm1b_rate.h in place)
.\start_emu.ps1 -Tap -Script astley60_rec.script -NoBrowser -Bank test_fluid\cmp\mp3_64_44k_packed.fm1b
```

With the 84 KB pool the 44.1 kHz build stops on the heap check (109 516 bytes against the 111 468 needed: the rate takes 2 144 bytes more), so its pool is 81 KB. That makes the comparison
a little unfair to 44.1 kHz.

**Result** (fm1-emu at 240 MHz, MP3 64 kbit/s, the first 60 s of "Never gonna give you up", figures from the info page and the emulator's load each second; each build run twice with the same figures):

| | 32 kHz, 84 KB pool | 44.1 kHz, 81 KB pool |
| --- | --- | --- |
| Notes that came late (`L`) | 2 | 50 |
| Times with no room in the pool (`F`) | 162 | 1 593 |
| Hits / decodes / dropped sounds (`H` `M` `E`) | 1697 / 90 / 74 | 1261 / 207 / 197 |
| MP3 frames decoded | 508 | 1 159 |
| Time for one MP3 frame | 2 185 us | 2 659 us |
| Longest control cycle | 43 ms | 46 ms |
| Load while the song plays, mean / peak | 44.0 % / 49.9 % | 53.1 % / 64.3 % |
| Load at rest | about 23 % | about 22 % |
| Image (limit 453 921) | 453 724 bytes | 446 684 bytes (no resampler) |
| Packed bank | 241 520 bytes (120 MP3, 45 lossless) | 317 788 bytes (96 MP3, 69 lossless) |

The CPU is not the problem: 53 % in the mean is under the guess of 2d and far from the guard's 80 %. The pool is: every sound is 1.38 times bigger, and 25 times more notes come late. So
44.1 kHz is of no use before the pool grows, and the next step is the RAM (the list at the end of 2d), not more work on the rate.

### 2f. 44.1 kHz with the flash filled with plain BFP8: no late notes (2026-11-12)

The pool was the limit in 2e. A sound stored as BFP8 plays in place from the flash and takes no room in the pool, and the packed 44.1 kHz bank of 2e left 219 KB of the 536 576 bytes
of flash unused: the packer keeps a sound as BFP8 only when MP3 is badly wrong for it. `pack_bank.js --fill 1` goes on after that and stores more sounds as BFP8, worst MP3 first, until the
flash is full. Without the switch a bank packs to the same bytes as before (checked on the 32 kHz bank). Emulator only: not on the FM-1, and nothing was listened to.

```
node pack_bank.js test_fluid/cmp/bfp8_44k.fm1b test_fluid/cmp/mp3_64_44k_fill.fm1b --kbit 64 --fill 1
.\build_fw.ps1 -Name rate44k -CacheKB 81        (with fm1b_rate.h in place, as in 2e; since 2026-11-13: .\build_fw.ps1 -Name sf2player_44k -Rate 44100)
.\start_emu.ps1 -Tap -Script astley60_rec.script -NoBrowser -Bank test_fluid\cmp\mp3_64_44k_fill.fm1b
```

**Result** (as 2e: fm1-emu at 240 MHz, MP3 64 kbit/s, the first 60 s of "Never gonna give you up"; the first two columns are 2e's; the filled bank's counts are from one run, its load from a second):

| | 32 kHz, 84 KB pool | 44.1 kHz, 81 KB pool | 44.1 kHz, 81 KB pool, `--fill 1` |
| --- | --- | --- | --- |
| Sounds as MP3 / as BFP8 in place | 120 / 45 | 96 / 69 | 16 / 149 |
| Packed bank | 241 520 bytes | 317 788 bytes | 533 268 bytes |
| Notes that came late (`L`) | 2 | 50 | 0 |
| Times with no room in the pool (`F`) | 162 | 1 593 | 0 |
| Hits / decodes / dropped sounds (`H` `M` `E`) | 1697 / 90 / 74 | 1261 / 207 / 197 | 303 / 5 / 0 |
| MP3 frames decoded | 508 | 1 159 | 67 |
| Time for one MP3 frame | 2 185 us | 2 659 us | 3 358 us |
| Longest control cycle | 43 ms | 46 ms | 60 ms |
| Load while the song plays, mean / peak | 44.0 % / 49.9 % | 53.1 % / 64.3 % | 56.8 % / 64.9 % |

With 16 sounds left as MP3 the pool is no longer a limit in this song: five decodes and nothing dropped. The bank fills 99 % of the flash, so this needs the 68 KB the bank got at the
start of the user flash; without them about 70 KB more of the sounds would be MP3. Unpacked with the firmware's decoder (`unpack_bank.js`), 84 % of the sample bytes are the source bank's.

**Not known.** Why one MP3 frame takes longer (3.4 ms) and the longest control cycle is 60 ms. How another song does: which 16 sounds are MP3 follows from the MP3's quality only, not from
how often a sound is played, and this song uses few of them. `test_pack_bank.js --packed` compares with a 32 kHz bank, so `--fill` has no test of its own on a 44.1 kHz bank.

**On the FM-1, 2026-11-13.** This firmware and bank, installed with `install.html` (`dist\gm_44k_fill.fm1b` as the bank of `make_install.js`), play on the device: the
first bank over 466 944 bytes on it, and the first at the start of the user flash. Listened to: sounds right. A second install skips both.

**The whole of "Never gonna give you up" on the FM-1** (2026-11-13, this firmware `sf2player_44k_20261113.fwsc`, the live figures over MIDI with `stats.html` as in 3d,
239 s of the song from the first voice to the last; against the same song on the 32 kHz firmware and packed bank of 2026-11-11, `FM1_TEST.md` H):

| | 32 kHz, 84 KB pool, packed bank (2026-11-11) | 44.1 kHz, 81 KB pool, `--fill 1` bank |
| --- | --- | --- |
| Notes that came late | 25 (19 with releasing voices cut) | 0 |
| Times with no room in the pool | 373 | 0 |
| Decodes / dropped sounds | 426 / 407 | 3 / 0 |
| CPU, mean of the seconds | 63 % | 49 % |
| CPU, highest peak | 83 % | 117 % |
| Voices the CPU guard took (`DROPPED CPU`) | 3 | 257 |
| Held notes dropped (`DROPPED HELD`) | 502 | 1 551 |
| Releases cut (`DROPPED TAIL`) | 5 131 | 4 447 |
| Longest control cycle | 128 ms | 93 ms |

The cache is no longer the limit on the device either: three decodes in the whole song. The CPU is: from 26 s on the peak goes over the guard's 80 % in nearly every
second (up to 117 %), the guard lowers the voice limit and fades voices out, releasing ones first, which is why `HELD` is three times what it was. The lower mean is most
likely the guard at work, not a cheaper player. Nothing wrong was heard. fm1-emu at 240 MHz showed 56 % with peaks of 66 % for the first minute, so it reads the device's load
low here. One MP3 frame took 7.1 ms on the device (22 frames). Next: the voices on the second core (branch `sf2-dualcore`), or fewer voices at 44.1 kHz.

**The build switch** (2026-11-13): `-Rate 44100` on `build_fw.ps1`, `build_firmware.ps1` and `build_all.ps1` writes `fm1_open\app\fm1b_rate.h` for the one build and removes it
after (git ignores it), and takes an 81 KB pool unless `-CacheKB` says otherwise. `build_firmware.ps1` and `build_all.ps1` then use `dist\gm_44k_fill.fm1b`, which is made by hand:

```
node make_bank.js bfp8 44100 <font.sf2> dist\gm_44k.fm1b
node pack_bank.js dist\gm_44k.fm1b dist\gm_44k_fill.fm1b --kbit 64 --fill 1
.\build_all.ps1 -Rate 44100 -Serve            firmware, dist\install\ and install.html
```

Built so, the image is the same size as above (446 716 bytes with a 27-character name, 81 KB pool, heap 112 652), and in fm1-emu it plays the 60 s of the test song with the
same counts: `CACHE H303 M5 E0 F0 L0 P0 W0`, load about 56 % (peak 66 %) against 44 % (peak 50 %) for today's 32 kHz firmware and packed bank (2 late notes, 162 times with no room).
4-point interpolation at 44.1 kHz (`FM1B_INTERP4 1`) cost about 18 points more (mean 75 %, peak 86 %, over the CPU guard's 80 %) and 2.5 KB of heap (pool 78 KB): not with one core.

### 2g. What the flash reads cost the voices on the FM-1 (2026-11-13)

For the second core (branch `sf2-dualcore`, `PLAN_DUALCORE_CLOCK.md`): if only CPU0 reads the flash, can CPU1 still take the heavy part? That depends on how much of a
voice's time is reading its samples from the flash. Measured on the device with a probe build:

```
.\build_fw.ps1 -Name sf2player_44k_probe -Rate 44100 -FlashProbe -CacheKB 79 -Bank dist\gm_44k_fill.fm1b
node make_install.js dist\gm_44k_fill.fm1b       (then install.html: the bank is skipped)
```

`-FlashProbe` (`FM1B_FLASH_PROBE 1` in `fm1b_rate.h`) puts 64 BFP8 groups (2 112 bytes, a quiet triangle) in SRAM, and ARP on the info page switches every voice between
its own samples in the flash and that buffer: the voice's position is masked to the buffer's 2 048 frames, with the mask all ones in the flash mode, so both run the same
instructions and only the memory differs. The info page's last line says `ARP SAMPLES: FLASH` or `RAM`, and the live figures over MIDI (3d) carry it as a 24th value
(version 4; `stats.html` reads it as `probe`). The pool is 79 KB to make room for the buffer. The default build is unchanged (453 756 bytes).

The whole of "Never gonna give you up" played twice from Windows, once in each mode (`dist\stats_log.jsonl`, the seconds with voices sounding):

| | Flash | SRAM |
| --- | --- | --- |
| Seconds | 215 | 214 |
| CPU, mean / highest peak | 53.7 % / 115 % | 55.0 % / 117 % |
| Voices, mean | 18.6 | 18.9 |
| Voices the CPU guard took | 240 | 238 |
| CPU against voices (least squares) | 23.4 % + 1.63 % a voice | 20.5 % + 1.83 % a voice |

CPU at the same number of voices, mean of the seconds with that many:

| Voices | 16 | 17 | 18 | 19 | 20 | 21 | 22 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Flash | 49.0 % | 51.0 % | 54.2 % | 55.0 % | 56.0 % | 58.3 % | 57.9 % |
| SRAM | 49.2 % | 51.8 % | 54.5 % | 55.8 % | 57.2 % | 59.1 % | 61.3 % |

The two are the same within the noise (SRAM if anything a little dearer): reading the samples from the flash costs the voices nothing that shows, most likely
because the chip's flash cache catches it. The time is the voice's arithmetic: interpolation, envelope, filter, mixing. So a split where CPU0 copies each voice's
samples for the block into SRAM and CPU1 does the rest from SRAM gives CPU1 nearly all of the work without CPU1 ever reading the flash; the cost is the SRAM for those
samples (8-16 KB at 44.1 kHz and 32 voices), which the heap does not have to spare today. Not measured: the BFP8 unpacking (one shift a sample, in both modes); the SRAM
buffer is the best case for the cache (2 KB), and it was not cheaper. fm1-emu showed about 47 % (SRAM) against 55 % (flash) for the first minute, so it models the flash
slower than the device is here.

### 2h. Both cores: the voices shared with the SDK's CPU1 (2026-11-14)

**What it is.** The SDK starts CPU1 itself at power-on and leaves it in a worker loop (`PLAN_DUALCORE_CLOCK.md` on branch `sf2-dualcore`, section 8: how that was found in `sdk.elf`, and why our own
start tests of 2026-11-05 and 2026-11-13 hung the unit). The player borrows it through the SDK's own hand-over (`cpu_enter_sfc_critical(func, arg)`, which its flash driver uses). In each audio
block `fm1b_render_sends` lists the voices that run and gives CPU1 a share of their cost (50 % at start; a filtered voice counts 3, another 2); CPU1 renders its voices into buffers of its own
while CPU0 renders the rest, and CPU0 adds the buffers (`fm1b.c`, `app\cpu1_split.c`). Our code never starts, holds or restarts CPU1.

**Build.** `.\build_fw.ps1 -Name sf2player_44k -Rate 44100` has it on (`-DualCore auto`); `-DualCore off` is one core. At 32 kHz it is off: the image has 165 bytes to spare and the code takes about
2.8 KB. The 44.1 kHz build: image 449 692 bytes (4 229 to spare), pool 77 KB, taken from what the heap allows (build_fw.ps1 lowers it and links again if the heap comes out short, unless `-CacheKB`
is given); `-Interp4` is four-point interpolation, `-SplitCheck` a verification build. The two SDK functions are local symbols of its library: the build links, reads their addresses off the link with
`llvm-nm` and links again until the image has the addresses it was built with (three links). `build_firmware.ps1` and `build_all.ps1` pass the switches on. The default 32 kHz build is unchanged (453 756 bytes).

**Safety.** CPU1 not up (the SDK's flag is not set), a flash write going on (`fm1b_split_hold` in `sf2_upload.c`) or the SDK having CPU1 parked: the block is rendered on CPU0 alone, as on one core.
CPU1 not done in 5 ms: its share is left out of that block and the split is switched off. The info page (REC) has the line `SPLIT 50% V51% L0 H1 W27 J559` (CPU1's share; its share of the voices
so far; blocks it was late with; per block in us the handshake, CPU0's wait and CPU1's job), ARP switches it on and off, SEQ sets the share (30 to 70 %); CC 102 on MIDI channel 16 sets it too (0: off).
The live figures over MIDI (`stats.html`, version 4) carry the share.

**Exact.** `node test_split.js` (host): with and without the split the output stage gives the same int32 output block for block, at shares 0 to 100 %, at 32 and 44.1 kHz, with linear and four-point
interpolation, and the verification build agrees with itself. On the FM-1, `-SplitCheck` (CPU0 renders CPU1's voices again from copies of their state and compares the buffers and the voices' state;
info page `CHK`, live figures version 5): "Dancing Queen", 60 s, share 50 %: **18 116 blocks compared, none differed**. Not heard: the assistant cannot listen.

**On the FM-1: six songs, one core against both** (`dualcore.html`: plays MIDI files to the FM-1 once for each share and sums up the live figures; 44.1 kHz firmware `-Rate 44100`, the `--fill 1` bank, linear
interpolation; the first 75 s of each song; the seconds with voices playing):

| Song | CPU mean, one core → both | Highest peak | Seconds at 80 % or more | Voices, mean | Taken by the CPU guard |
| --- | --- | --- | --- | --- | --- |
| ABBA, Dancing Queen | 54.4 → **43.7 %** | 107 → **72 %** | 42 → **0** | 18.7 → 27.9 | 83 → **0** |
| Star Wars theme | 39.8 → **24.0 %** | 80 → **51 %** | 2 → **0** | 18.2 → 18.1 | 0 → 0 |
| TOTO, Africa | 59.3 → **41.9 %** | 88 → **58 %** | 5 → **0** | 23.6 → 28.6 | 9 → **0** |
| Backstreet Boys, I Want It That Way | 55.5 → **35.0 %** | 87 → **60 %** | 13 → **0** | 23.1 → 23.5 | 13 → **0** |
| Beethoven, Hammerklavier 4 | 58.8 → **36.0 %** | 81 → **52 %** | 3 → **0** | 25.1 → 25.0 | 2 → **0** |
| Under the Sea | 60.2 → **41.2 %** | 88 → **59 %** | 25 → **0** | 26.2 → 29.9 | 25 → **0** |
| **All six** | | 107 → **72 %** | 90 of 442 → **0 of 447** | | 132 → **0** |

No late notes and no lack of room in the pool in any run. With both cores no second of any song reaches the CPU guard's 80 %, and the guard takes no voice; on one core it takes 132 and cuts
the voices down (Dancing Queen: 18.7 voices on average on one core, 27.9 on both). Together with the whole of "Never gonna give you up" (`PLAN_DUALCORE_CLOCK.md`, section 8: guard 205 → 0).

**Four-point interpolation, on the FM-1** (`-Interp4`, pool 74 KB; the same page, the first 75 s of three songs, CPU1's share 0 = one core, 50 % and 70 %). Four-point interpolation (FluidSynth's
default, which removes the aliasing of the linear one: RUNNING.md 2g, issue 5) cost about 18 points of CPU on one core in fm1-emu and was left off for that; with the voices on both cores it fits:

| Song | Share | CPU mean | Highest peak | Seconds at 80 % or more | Voices, mean | Taken by the CPU guard |
| --- | --- | --- | --- | --- | --- | --- |
| Dancing Queen | one core | 50.8 % | 113 % | 46 | 14.5 | 118 |
| | **50 %** | 51.7 % | **78 %** | **0** | 28.2 | **0** |
| | 70 % | 56.1 % | 98 % | 41 | 22.4 | 61 |
| Africa | one core | 57.6 % | 92 % | 7 | 17.5 | 14 |
| | **50 %** | 48.9 % | **66 %** | **0** | 27.8 | **0** |
| | 70 % | 61.7 % | 81 % | 5 | 27.2 | 3 |
| Under the Sea | one core | 58.1 % | 99 % | 28 | 18.8 | 51 |
| | **50 %** | 49.0 % | **69 %** | **0** | 29.0 | **0** |
| | 70 % | 63.0 % | 87 % | 6 | 28.6 | 10 |

With four-point interpolation and 50 % to CPU1 no second of the three songs reaches 80 % and the guard takes nothing; the same three songs with linear interpolation (above) read 72, 58 and 59 % at
their highest, so four-point costs about 7 to 10 points of CPU on both cores. **50 % is the right share:** at 70 % CPU1 is the one that is late and the CPU0 waits for it (the peaks and the guard
come back); on one core four-point interpolation does not fit (guard 118, 14, 51). The host test is exact with four-point interpolation too (`node test_split.js --interp4=1`). Not heard.
(A first run of these three songs was spoiled when the page was in the background: Chrome throttled its timers and the songs went out in lumps; `dualcore.html` now gives the messages
timestamps of their own.)

**Not known.** How it sounds: nothing here was listened to, neither the split nor four-point interpolation. Only six songs for the linear build and three for four-point, 75 s each. The share is fixed
at 50 % (SEQ and CC 102 change it by hand); a share chosen from the measured cost of each core's last block would follow a song whose voices are heavier on one side (not built). A flash write by
the SDK's own code (not ours) while CPU1 renders is not guarded against; the SDK's flash driver then waits for CPU1.

### 2i. Recording the FM-1's USB audio and comparing it with the host render

The unit's final mix goes out over USB audio (`audio_output.c`), so a PC can record what it really plays. `dualcore.html?go=1&seconds=40&shares=0,50&songs=...`
plays the songs (results carry `startEpoch`), `ffmpeg -f dshow -audio_buffer_size 50 -i audio="Microphone (USB-Audio)"` records, `node render_host.js song.mid host.wav --seconds=40`
renders the same messages through the player's own code on the host (no CPU limit, reverb/chorus 30), and
`node analyze_usb.js rec.wav host.wav --start=<seconds from recording start to song start> --length=40` finds the lag (envelope, then samples), matches the level by RMS
and reports per 93 ms frame the octave-band distance, frames where the unit is 6/12 dB quieter than the host (missing notes), clicks and zero runs.
Sample-by-sample correlation is low by nature (reverb/chorus, timing of MIDI over USB); the figures are similarity to the host render, not quality.

44.1 kHz, linear, 40 s (30 s for Under the Sea), one core -> both cores: frames 6 dB quieter than host / mean band distance: Dancing Queen 3 (0.8 %) / 2.6 dB -> 0 / 2.0 dB;
Africa 0 / 1.95 -> 0 / 2.0; Under the Sea 2 / 3.5 -> 0 / 3.1. No clicks or zero runs on the unit in any run. Four-point (sf2player_44k_i4, compared with the host's four-point render), one core -> both cores: Dancing Queen 8 frames 6 dB quiet / 3.1 dB, CPU guard took 58 voices -> 0 frames / 1.8 dB (guard 3);
Africa 0 / 1.9 -> 0 / 2.0. No clicks or zero runs. So four-point on one core drops notes in the densest passage (more than linear), and with the split it is as clean as linear.
Not known: how it sounds (listen to the six files of the Dancing Queen excerpt: FluidSynth, host, linear one/two cores, four-point one/two cores).

## 3. The converter and browser simulator (verified)

```powershell
cd D:\git\fm1-sf2
python -m http.server 8765
```

Open <http://localhost:8765/index.html> ("SF2 till FM-1"): drop an `.sf2`, convert to an FM1B bank, play it in the simulator,
or send it to a connected FM-1 ("Skicka till FM-1", Web MIDI over USB). Serve it, do not open the file directly.
A server on port 8765 was already running when this was written, so check before starting another.
`.\start_web.ps1` does both: starts the server on 8765 unless one already listens there, and opens `index.html` and `install.html` (section 3b).

Rebuild the simulator after changing `fm1b.c` or `fm1_open\web`:

```powershell
cd D:\git\fm1-sf2\fm1_open
.\tools\Build-Web.ps1 -LocalPreview
cd ..
node make_wasm_js.js
```

The simulator plays on one sampler track through the panel slot only. **MIDI channels and program change are firmware only**
(`transfer.c`), so multitimbral cannot be heard in the simulator.

### 3b. The install page: firmware and bank in one go (2026-11-03, the bank step run on the FM-1)

```powershell
cd D:\git\fm1-sf2
cd fm1_open; .\sdk\Build.ps1 -FirmwareOnly -Toolchain 'D:\JL\pi32'; cd ..   # the build of section 1
node make_install.js [bank.fm1b]     # dist/install/: the last build's package, the bank (default dist/gm_32k.fm1b), V15, manifest.json
```

Open <http://localhost:8765/install.html> (the same server as section 3) in Chrome or Edge and click **Installera**. It finds the FM-1
with the SDK's own browser updater (`fm1_open/sdk/WebUpdate.js`, the protocol M-VAVE's workbench uses: identity query, capability with the
build id), installs the firmware through the vendor loader unless the FM-1 already reports this build, waits for it to come back and checks
the build id it reports, then sends the bank with the player's SysEx upload (as `index.html` does, on the port the updater found and
checked). Either step can be left out. An FM-1 on the stock V15 or another firmware (the SDK says "migration") gets nothing written; the page
shows its identity and a confirmation, and only then connects again with the SDK's `force` (the workbench's "Force flash": firmware without
the capability reply goes the "legacy" route through the vendor loader). The force is only used for the identity the user confirmed. From
any other firmware than V15 (and other than a vendor loader) the page first installs V15 (`dist/install/stock.fwsc`, from
`fm1_open/sdk/firmware/FM-1_015.fwsc`, the SDK's `kind: 'stock'` with its voice restore), then the SF2 player from V15: Felucca's own loader
(`ota-FM-1_900`) takes only a `flash.bin` of exactly 0x93000 bytes (its `ldr_core.c`, error -14), ours is 0x70000 (`package-report.json`
`FLASH_BIN_SIZE`). Checked against a fake Felucca the same way (2026-11-10): FM-1_918, and an FM-1 left in Felucca's loader. On the FM-1 the
first try hung after the restart into Felucca's loader ("USB-Midi", its input listed twice): the SDK pairs only a port name it finds
once, and with `force` it also asks every other port (a loopMIDI echo answers with a malformed identity, which the SDK reports
instead of "not found"). The page now leaves out the ports that did not answer while the FM-1 did, takes one port of a name at a
time (another on each search), and logs each search. The bank step first sends cmd 9 (size and CRC32, `sf2_upload.c`) and skips the
upload when the FM-1 plays that bank (`test_upload.js` checks cmd 9). *Leaving FM1-SF2* installs `stock.fwsc` with `force` whatever runs
(from the SF2 player the tagged route, from V15 only the voice restore); the SDK writes V15's factory voices after it. Checked against
a fake FM-1 (2026-11-10): the same bank skipped, another bank sent, leaving from the SF2 player to V15 with its four voice banks. `make_install.js` must run after the build it packs: the build id it
writes is `build/dx7/build-identity.json`'s, and the device's must match it after the update. Checked in the browser pane against a fake
FM-1 (Web MIDI replaced): the same build skips the firmware and the whole bank arrives (1 040 frames, CRC right), bank only, the stock
V15 refused, no FM-1 found; and (2026-11-10) "FM-1_918" with no capability reply: the confirmation, then legacy route, loader,
firmware, the SF2 player back with the right build, then the bank. On the FM-1 (2026-11-03, already on this build): the bank was written and plays. The firmware write itself is the SDK's code and has
not been run from this page yet.

**MP3 banks and the live-cache firmware (2026-11-10).** The converter page packs MP3 banks itself (64, 128 or 256 kbit/s): `pack_bank.js`
runs in the page with lamejs as the encoder (`third_party/lamejs`, bit reservoir off, no tag) and the firmware's minimp3 as the decoder
(`mp3_decoder_wasm.js`). That decoder gives ffmpeg's samples within 1 LSB and the same length, and `pack_bank.js` with it makes the same
file byte for byte as with ffmpeg (64 and 128 kbit/s, ffmpeg's encoder). The banks the page made from `1mgm.sf2` pass
`node test_pack_bank.js soundfonts/1mgm.sf2 --packed=<bank>` at all three rates (64: 245 040 bytes, 119 MP3 sounds, mean 15.8 dB;
128: 275 988, 153, 21.4 dB; 256: 459 672, 155, 28.4 dB, against the C decoder with the loop crossfade). The hosted install page now has
both builds (`manifest.live`) and a menu for them; an MP3 bank picks the live cache, and a non-BFP8 bank is refused for it. The two builds
used to have the same build id (it hashed only `app/`), so the page could not tell them apart: `Build.ps1` now also hashes the `-D`
switches and, for `-LiveCache`, the `live_cache` sources, which gives every build a new id. Checked in the browser pane against a fake FM-1
(the choice, `?firmware=live`, the bank checks, the live package through the confirmation); the switch from the plain to the live-cache
build on a running SF2 player is not covered by the fake, and nothing of this has run on the FM-1.

**From another computer on the same network (2026-11-05).** `.\start_web.ps1 -Lan` also serves this folder over HTTPS on port 8766
and prints the addresses (this machine: `https://192.168.50.145:8766/install.html`, and `song.html` the same way; Windows Firewall
already lets Python in). Chrome gives Web MIDI only to `localhost` or HTTPS, so on the other computer accept the self-signed
certificate warning once (*Advanced*, *Continue to ...*); no flag is needed (the Chrome flag *Insecure origins treated as secure*
did not take in our test). The certificate is `test_emu\lan_cert.pem` (made by `test_emu\lan_cert.ps1`, also used by
`start_emu.ps1 -Lan`, whose HTTPS port is 8782). The packages can also be fetched from `http://192.168.50.145:8765/dist/` and
flashed with M-VAVE's workbench, which needs no certificate.

### 3c. The MIDI file player: a song in the FM-1's RAM (branch sf2-midiplay, 2026-11-05, run in fm1-emu)

```powershell
.\build_fw.ps1 -Name sf2player_midiplay        # dist\sf2player_midiplay.fwsc; node make_install.js packs it for install.html
node fm1_song.js <file.mid> [out.fm1s]          # what the page does: events, length, the song's size
```

Open <http://localhost:8765/song.html>, drop a `.mid` file and click **Send to FM-1**; then PLAY on the FM-1 (or **Play** on the
page) plays it from the start, PLAY again stops it (pedal up and all notes off on every channel). `fm1_song.js` merges the tracks and
works out the tempo: the song is the channel messages with their times in ms (aftertouch, meta events and SysEx left out), 4 bytes an
event, about the MIDI file's size (the test files: 5-80 KB). It goes over the bank's SysEx upload, commands 4 to 8 (`sf2_upload.c`),
into a block `malloc()` gives (`song.c`, at most `FM1_MIDIPLAY_MAX_BYTES`, 128 KB); it is gone when the FM-1 is switched off.
The control task sends the events that are due through `fm1_app_midi_received()`, like MIDI in, so the channel page, mute and solo
work as for MIDI from a computer; before the first event every channel is reset (controllers, bank, pan, sends, the panel's preset).
It never fills the player's 32-place queue (`fm1b_queue_room()`): what does not fit goes a cycle later. The program page shows
`PLAY <name> m:ss/m:ss` (green) or `SONG <name> m:ss  PLAY STARTS`; REC's info page also shows `FREE RAM n KB FOR A SONG`, the
largest block `malloc()` gives (the emulator always says 512, its ceiling: the device's own number is the one to note).

All of it is behind `FM1_MIDIPLAY` in `fm1b_config.h`: 0 leaves out the code (2.4 KB: 436 700 bytes against 439 132) and the PLAY
button does nothing. The song's RAM is only taken when a song is uploaded.

In fm1-emu (`test_emu\song_test.js`, MIDI into the DIN jack, which takes about 3 KB/s: 35 s for a 60 KB song; over USB it should be
much faster): `R.ASTLEY.Never gonna give you up K.mid` (63 209 bytes, 15 771 events) arrived whole (`UPLOAD 154 R0`), played in time
(20 s of the page's clock were 20 s of the song), with 16-30 voices and `DROPPED HELD 46` after 31 s; PLAY started and stopped it, the
info page showed the song. Not yet run on the FM-1.

### 3d. Live figures over MIDI (2026-11-11, the live-cache build)

Built with `FM1_STATS_MIDI` (`sf2player.c`; on when `FM1B_PACKED` is, i.e. in `-LiveCache` builds; `-DFM1_STATS_MIDI=0` leaves it out, 288 bytes), the
player sends its figures once a second as one SysEx to the USB MIDI port: `F0 7D 46 4D 53 01`, 20 values of 32 bits (5 bytes of 7 bits each, lowest first),
`F7` (version 3, 2026-11-11: 23 values, 122 bytes; version 2 had the first 22, version 1 the first 20). Totals since start: ms, the cache's hits, misses, dropped sounds, no room, MP3 frames decoded, requests, notes late, waiting, pins; ms in
`fm1b_cache_service`, control cycles and their ms, the longest cycle since the last message; CPU % average and peak; voices; DROPPED held, tail, CPU; the longest control step and the longest screen update since the last message; releasing voices cut so that a waiting note's sound got room.
`stats.html` (Chrome or Edge, Web MIDI with SysEx) shows them live, per second and since a moment you pick, and posts each message to `/stats-log`
of the server that serves the page: `test_emu\cors_server.py` (the `-Lan` server of `start_web.ps1`) appends it to `dist\stats_log.jsonl`, so the
figures can be read on this computer while the FM-1 is on another. `python -m http.server` (the plain 8765 server) takes no posts: the page then
says so and only shows them. From another computer: `.\start_web.ps1 -Lan`, then `https://<this computer>:8766/stats.html`.
Not on the FM-1 yet; fm1-emu does not pass the firmware's MIDI output on, so there it only shows that the build boots and plays.

## 4. fm1-emu, the CPU emulator (run 2026-11-01 with the multitimbral build)

**Quick start** (verified): build the firmware (section 1), then

```powershell
cd D:\git\fm1-sf2
.\start_emu.ps1            # multitimbral check: serves emu\ on 127.0.0.1:8781 and opens it
.\start_emu.ps1 -Script '' # no script, play it yourself with the keys or a MIDI keyboard
.\start_emu.ps1 -Tap       # the page that records the emulator's audio into window.__cap (see step 6)
.\start_emu.ps1 -Update    # first fetch the emulator's newest files from its site (the old ones go to emu\previous\)
```

`start_emu.ps1` keeps the emulator's site files in `emu\` (git ignores it), copies the newest `FM1_OFFICIAL_OPEN.fwsc` and `test_emu\*` into it,
makes `gm.flash` from `dist\gm_32k.fm1b` when needed, points the `sf2` entry of `firmwares.json` at its port, starts `test_emu\cors_server.py`
hidden if nothing listens on the port, and opens the URL. The first run copies the emulator's files from the old scratchpad folder below;
if that is gone it stops and lists the files to download from the site. Stop the server by its port, so that other Python servers survive:
`Get-NetTCPConnection -State Listen -LocalPort 8781 | ForEach-Object { Stop-Process -Id $_.OwningProcess }`.
It also makes three local changes to `emu\main.js` (once, and again after `-Update`): a browser that has run `sf2` before kept
that package in its storage and booted it again after every new build until "Check for firmware updates" was pressed (seen
2026-11-05 in Chrome: REC did nothing, the page ran a build from before the CPU display); now a kept copy is used only while its
`?v=` address is the current one. `window.__lcd` holds the latest screen, so a test can read it while the page is hidden, and
`window.__perf` the load figures. When the site's `firmwares.json` has no `sf2` entry (after `-Update`) the script adds one.
`-Update` was first run on 2026-11-05: a new `fm1_web.wasm` (3 358 200 bytes, was 2 817 402 from 2026-10-31), `worklet.js`,
`fwsc.js`, `main.js` and catalog; the info page, the multitimbral check (`mt.script`) and the CRC32s worked unchanged in it.
The rest of this section is how it works, and what the script does by hand.

fm1-emu (<https://joakimeriksson.github.io/fm1-emu/>, MIT) runs `.fwsc` packages unchanged. The firmware package has no bank, so it
shows `NO BANK` until a bank is laid in the user flash. The online page cannot fetch our bank (other origin), so the emulator's site files
run from a **local copy served with CORS headers**. That copy already exists, in an old scratchpad under Temp:

```
C:\Users\Magnus\AppData\Local\Temp\claude\D--git-pico8-hws\df678bca-34d6-48e4-84f5-0df8efb5cf71\scratchpad\
    cors_server.py        the server: python cors_server.py <port> <folder>   (binds 127.0.0.1, adds CORS headers)
    emu\                  the emulator's files: index.html main.js worklet.js persistence.js fwsc.js tryout.js style.css
                          firmwares.json fm1_web.wasm, plus ours: sf2player.fwsc, gm.flash, *.script
```

**It is in Temp and can disappear.** If it is gone, download those site files again into a new folder, copy `cors_server.py` (14 lines, below),
and redo the two registrations in steps 2-3.

1. **Server** (it was running on port 8780 when this was written; check `Get-NetTCPConnection -State Listen -LocalPort 8780`):
   ```powershell
   cd <scratchpad folder above>
   python cors_server.py 8780 emu
   ```
2. **Firmware** is registered in `emu\firmwares.json` as `"id": "sf2"`, `"package": "http://127.0.0.1:8780/sf2player.fwsc"`.
   To test a new build, overwrite `emu\sf2player.fwsc` with `fm1_open\build\dx7\generated\FM1_OFFICIAL_OPEN.fwsc`.
   (On 2026-11-01 the file there was byte-identical to `dist\sf2player_flash.fwsc`, the build before multitimbral.)
3. **Bank**: `emu\gm.flash` is `dist\gm_32k.fm1b` laid at the user flash. Since 2026-11-02 that is the SEA6 bank (1mgm.sf2, 32 kHz,
   397 532 bytes, the converter's default); the SEA5 one it replaced is `dist\gm_32k_sea5.fm1b`. Make the flash file again with:
   ```bash
   node make_flash_overlay.js dist/gm_32k.fm1b gm.flash
   ```
4. **Open** `http://127.0.0.1:8780/?fw=sf2&flash=gm.flash` (use `127.0.0.1`, not `localhost`: `flash=` and `script=` are only fetched from the page's own
   origin, and the package URL says `127.0.0.1`). A try-out is its own session: nothing is loaded from or saved to the browser's saved flash.
   `&mhz=320` is the device's clock (measured 2026-11-05; start_emu.ps1 uses it, and the page default is 320 too; the emulator's own default was 100).
5. **Scripted input**: `&script=NAME.script` plays an input script on the emulated clock, in the syntax `ms:mHEX` (MIDI bytes into the DIN jack),
   `ms:id` / `ms:id^` (key or button down / up), `ms:eN:clicks` (encoder), `ms:pot:N`, separated by commas. `emu\drum.script` is `4000:m99267f`
   (note on, channel 10, note 38). `test_emu\mt.script` is the multitimbral check: program change 73 (`C1 49`, flute) on channel 2 at 3.0 s,
   then note C4 on channels 1 and 2 at 3.1 s, off at 6.0 s: `3000:mC149,3100:m903C64,3100:m913C64,6000:m803C00,6000:m813C00`.
   The scripts are kept in `test_emu\` in this repo; copy them to `emu\`.
6. **The screen stays dark** (the emulator does not draw the firmware's own LCD), so the `MULTI` line cannot be seen. Measure the audio instead:
   copy `test_emu\tap.js` to `emu\`, make `emu\index_tap.html` from `emu\index.html` by adding `<script src="tap.js"></script>` right after `<head>`,
   and open `index_tap.html?fw=sf2&flash=gm.flash&script=mt.script&mhz=240`. The script must be in the page before the emulator's own
   (the audio node is made at the first boot; a reboot from the clock selector reuses it). `window.__cap` then holds `{t, pk, rms, cen}` every 100 ms
   (peak, RMS, spectral centroid in Hz) of what the emulator plays. Audio starts by itself, no click needed.

   Result with the multitimbral build, mean of 3.5-5.5 s (the notes sound from 3.0 s and are gone 0.5 s after 6.0 s):

   | Script | RMS | Centroid |
   |---|---|---|
   | `mt_ch1` channel 1 only (piano, the panel's program) | 0.0096 | 348 Hz |
   | `mt_nopc` both channels, **no** program change | 0.0193 (= 2 x channel 1) | 348 Hz |
   | `mt_ch2` channel 2 only, after program change 73 | 0.0953 | 275 Hz |
   | `mt` channel 1 and 2 together | 0.0976 | 280 Hz |

   Without a program change both channels play the panel's piano (twice the level, same spectrum); after one, channel 2 plays a different,
   sustained instrument. The mix of both is about the level of the louder one, as it should be. This does not separate the piano inside the mix
   (it decays quickly); the WebAssembly test proves that part bit for bit.

`cors_server.py`, in case it has to be recreated:

```python
import http.server, sys, os
class H(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Private-Network', 'true')
        self.send_header('Cache-Control', 'no-store')   # a new firmware build or bank is fetched, never a cached one
        super().end_headers()
    def do_OPTIONS(self):
        self.send_response(204); self.end_headers()
os.chdir(sys.argv[2])
http.server.ThreadingHTTPServer(('127.0.0.1', int(sys.argv[1])), H).serve_forever()
```

Two other servers were running at the same time: `http.server 8765` in `D:\git\fm1-sf2` (the converter, section 3) and `http.server 8790` (the converter
again, bound to 127.0.0.1).

Known from older notes: the emulator's DIN MIDI input can stop delivering after a while. The scripted program change and notes of `mt.script`
did arrive in every run on 2026-11-01 (they are played shortly after boot). Measured load at 240 MHz with 8 voices held: about 56 % with both effects.

## 5. On the real FM-1

- Flash the package with the M-VAVE workbench (<https://open.m-vave.com/fm1-workbench>). Back up first and keep the device powered.
- Send a bank over USB MIDI from the converter page ("Skicka till FM-1"). The panel shows `UPLOAD n Rx` and then the program.
- Panel: SELECT program, OCT-/OCT+ octave, SCL drums, HOME channel page, MASTER main volume, KNOB2 pan, KNOB3 reverb, KNOB4 chorus;
  on the channel page ALGORITHM channel volume, ARP mute, SEQ solo (see section 7).
- The program page's `DROPPED HELD h TAIL t CPU c` line counts what the 32-voice limit took since start or SAVE (SAVE zeroes it):
  `HELD` notes still sounding (key or pedal down) that were cut for a new one, the ones heard as dropped; `TAIL` notes already
  releasing that were cut short; `CPU` voices the CPU guard took. Orange while `HELD` or `CPU` is not 0. In fm1-emu the first 80 s of
  *Never Gonna Give You Up* gave `HELD 379 TAIL 1021 CPU 0` (2026-11-05). `midi_script.js` turns a `.mid` into an fm1-emu script.
- **Voice stealing (2026-11-05).** When all voices are taken the engine used to take the oldest, even with its key down. Now it takes
  (`voice_to_take` in `fm1b.c`) one already fading for room, else the oldest releasing note, else one only the sustain pedal holds,
  else the oldest. The whole song on the PC (`compare_fluid.js midi`, which now prints HELD and TAIL): `HELD` 1 141 -> 490, `TAIL`
  3 235 -> 5 271; no CPU cost. fm1-emu, the first 80 s (`dist/sf2player_32v_steal.fwsc`): `HELD 181 TAIL 1727 CPU 0` against
  `HELD 379 TAIL 1021 CPU 0`. On the FM-1 the device reads 60-70 % CPU with peaks to 99 % at 32 voices, so more voices would only
  bring the guard on. Listen: `test_fluid/out/listen/never_steal_1_before.wav`, `_2_after`, `_3_fluidsynth`.
- REC shows the info page, REC again goes back (HOME goes to the program or channel page). Firmware: its file name, build time,
  CRC32 and size; bank: its file name and build time (from its info block, FORMAT.md; an older bank has none), CRC32 and size;
  and the `SOUNDSET CHECKSUM`, one CRC32 over the firmware image followed by the bank file. The keys and knobs keep working as on
  the page below. `node fm1_info.js [sdk.elf] [bank.fm1b]` prints the same three numbers on the PC.
  The firmware CRC32 is over the application image the packager gets (`.text .data .ram0_data .cache_ram_data`, 437 692 bytes
  on 2026-11-05), read from flash where the code runs: the `.fwsc` file is ciphered, so 7-Zip's CRC32 of it is a different number.
  The bank CRC32 is the file's own (as 7-Zip shows it). The page works them out when REC opens it (about 0.9 MB of reading).
  Checked in fm1-emu on 2026-11-05: all three as `fm1_info.js` gives them, for a bank with an info block and an older one.
  Pan (then on KNOB3, and CC10) was reported working after it was added.

## 6. Playing it from Windows

- **USB or DIN keyboard:** Windows shows it as a MIDI port, nothing else needed.
- **A DAW or MIDI file into the FM-1:** create a virtual port with **loopMIDI**, send to it, and route it to the FM-1's USB MIDI port.
- **BLE MIDI** (reported working, the method used was not written down): pair the device in Windows, then either let the FM-1 take BLE MIDI
  directly or bridge with **MIDIberry** (BLE in, loopMIDI port out). Windows MIDI Services is also installed on this machine.
- **Recording the audio:** Audacity with the *Windows WASAPI* host and the output device marked *(loopback)*, or OBS "Audio Output Capture".

## 7. What the firmware does with MIDI (multitimbral)

| Message | Effect |
|---|---|
| Note on/off, channels 1-9, 11-16 | Plays the channel's preset. A channel that never had a program change plays the panel's preset (SELECT) |
| Note on/off, channel 10 | The percussion kit (the first kit in the bank), same as the SCL button |
| Program change | Per channel. Bank select first (CC0/CC32): CC0=120, or CC0=1 and CC32=0, is the percussion bank, anything else melodic. A preset the bank lacks is ignored. Ignored on channel 10 |
| CC7 volume, CC11 expression | Per channel, General MIDI curve, live on sounding notes. 127/127 is unity |
| Pitch bend | Per channel, on sounding notes too. Range 2 semitones until RPN 0 sets it (CC101=0, CC100=0, CC6 semitones up to 24, CC38 cents) |
| CC1 modulation wheel | Per channel: vibrato, 5.5 Hz, up to +-50 cents at 127 |
| CC64 sustain pedal | Per channel: a note released while it is down sounds on until the pedal goes up |
| CC120 / CC121 / CC123 | All sound off / reset volume, expression, pan, bend, wheel and pedal / all notes off, **for that channel only** |
| CC91 / CC93 | Reverb and chorus send **per channel**, 30 and 20 until set. The panel's KNOB3/KNOB4 master levels (30 for reverb and 30 for chorus by default, since 2026-11-09; they were 127) scale them |
| CC10 pan | **Per channel**, live on sounding notes. Added to each zone's own pan from the SoundFont (generator 17), as an SF2 player does: at 64 (the default) the SoundFont's pan is heard as it is (the drum kit is spread), 0 moves the channel all the way left, 127 all the way right |

Panel: the line `MULTI n CH   LAST CH.. PRG...` appears once a program change has been received. **HOME** switches to the channel page:
one row per MIDI channel with its program and name (grey: it follows the panel's program; white: a program of its own; orange: the kit on
channel 10; green: it plays now; the name is cut to 7 characters), level (volume x expression), pan (`L64`..` C `..`R64`), bend in semitones,
modulation wheel, and a flag in the last column: `M` muted, `S` soloed, else `P` while the pedal is down. A silent channel (muted, or
another one soloed) is dark grey, dark green while its notes arrive; the title says `SOLO` (yellow) while a channel is soloed. HOME again goes back.
The knobs are the same on both pages, only what they act on changes:

| Knob | Program page (the start) | Channel page (HOME) |
|---|---|---|
| SELECT | The panel's program | The selected channel's program; below the first, the channel follows the panel again (channel 10 stays the kit) |
| KNOB1 | - | Moves the cursor (the highlighted row) |
| MASTER | Output level | Output level (the same: MASTER is always the synth's main volume) |
| ALGORITHM | - | The selected channel's volume (CC7), 4 steps a click |
| KNOB2 | Pan of the whole mix (a balance) | The selected channel's pan (CC10) |
| KNOB3 | Master reverb, 0..127 | The selected channel's reverb send (CC91) |
| KNOB4 | Master chorus, 0..127 | The selected channel's chorus send (CC93) |

Two buttons act on the selected channel, on the channel page only, while it plays:

| Button | Channel page |
|---|---|
| ARP (button 10) | Mute / unmute the selected channel. The ARP LED is lit while the selected channel is muted |
| SEQ (button 11) | Solo the selected channel (all other channels silent); again on the same channel: no solo; on another channel: the solo moves there. The SEQ LED is lit while the selected channel is the soloed one |

A silent channel (muted, or not the soloed one; a muted channel stays silent when soloed) fades out in 20 ms (`fm1b.c`
`channel_gate`, one step a block, each a 32-frame ramp of the voices: no click). Its voices then stop, so a muted channel costs no CPU
and frees its voices for the others, and its new notes are not started (the row still flashes when they arrive). Unmuted, it fades in
and its next notes sound; notes held across the mute do not come back. The panel's own keys on the program page are never silenced.
Mute and solo are kept until a new bank is bound, not saved. Until 2026-11-04 MASTER set the selected channel's volume on the channel
page (from 2026-11-02); the user wanted MASTER as the main volume everywhere, so the channel's volume moved to ALGORITHM (encoder 1,
the knob the stock firmware uses for the DX7 algorithm).

Checked (2026-11-04): `test_multitimbral.js` (19 mute/solo checks: the fade is monotone, still sounding after 10 ms and silent after
20 ms, the voices stop, a muted channel leaves the others bit exact, unmute and no solo are bit exact again). fm1-emu at 240 MHz,
BFP8, `test_emu/mute.script` (flute on channels 1 and 2, C4 and G4; HOME; ARP; ARP and C4 again; SEQ; SEQ and G4 again; ALGORITHM 16
clicks down; MASTER to 300), RMS from the tap: both 0.067, channel 1 muted 0.041, unmuted 0.068, channel 1 soloed 0.053, no solo
0.068, ALGORITHM -64 on channel 1 0.043, MASTER down 0.014. The emulator draws no LCD, so the screen and the LEDs are for the device
(FM1_TEST.md C8-C13). Image 435 068 bytes (+576).

What a channel gets of an effect is master x send. Until 2026-11-09 the defaults (master 127, sends 30 and 20) sounded bit for bit as the single global
level of 30 and 20 did before; the master levels now default to 30 and 30, so a channel gets about a quarter of that (`test_fx.js` checks it). On the channel page the panel's keys play the selected channel.

Cost of the sends: while all sounding channels have the same sends the effects are fed from the dry mix as before; only when they differ
does each voice also go to a reverb and a chorus bus. In fm1-emu at 240 MHz, 8 flute voices on two channels: same sends
(`test_emu/load8_same.script`) 53-55 %, different sends (`load8_sends.script`) 55-57 %.

Checked in fm1-emu: `test_emu/knob73.script` (73 clicks on SELECT for channel 2, then a MIDI note on it) and `knobkey.script` (the same, then
a panel key) both sound as program change 73, the flute (RMS 0.095, centroid 275 Hz). The emulator loses clicks when many come in one
script event (`e0:73`), so the scripts send one click every 30 ms. Script encoders: `e0` SELECT, `e2`..`e5` KNOB1..KNOB4.

Limits: 32 voices in total on the device (stolen: a releasing note first, then one the pedal holds, then the oldest; since 2026-11-05; 8, then 16, then 24 until 2026-11-02), fewer while the CPU guard holds
the limit down: the number after the slash is then under 32 and the line is orange.

**CPU on the panel (2026-11-04).** The program page's `VOICES n/limit CPU a% MAX p%` and the channel page's bottom line
`ARP MUTE SEQ SOLO  CPU a% MAX p%` show the audio interrupt's time as a share of a block (2.9 ms), the same figure the CPU guard reads
(`sf2out_cpu`): `a` the average over about 0.1 s, `p` the highest block, held for a second. Both lines turn orange while the guard
holds the voice limit under 32. The control task (screen, buttons, MIDI) runs in what is left, so this is not the whole CPU.

Why the limit can stay at e.g. 17 (seen on the FM-1 on 2026-11-04): the guard (`sf2out.c`, `sf2out_load`) looks at every block. One
block over 80 % sets the limit to the voices sounding then minus one (two over 95 %), and the oldest voices fade out; that is the
"many sounds muted". Note starts cost the most (a new voice decodes its first groups, a filter is set up), so a chord or a drum hit on
top of many voices can give one such block even when the average is far lower. The limit then rises only by one voice per 0.25 s, and
only while every block stays under 60 %: busy music between 60 and 80 % keeps it where it fell. The new display shows whether that
is what happens (MAX over 80 % while the average is lower). Not changed yet.

**Pan (2026-11-02).** Each voice's pan is its zone's (SF2 generator 17, stored in the zone record, FORMAT.md) plus its channel's CC10,
with the same balance law as KNOB2: the near side keeps its level, the far side is sqrt(1 - |p| / 64). The engine still mixes one mono
signal, which also feeds reverb and chorus (so the sends do not depend on the pan); a panned voice adds its far side's share (one more
multiply) to a cut buffer for that side, and the mix takes it away from that channel. A centred voice costs nothing new and the output
is bit exact as before (`test_multitimbral.js`, 11 pan checks). In fm1-emu at 240 MHz, 24 voices, the same session: `load24_all`
(nothing panned) 74-79 %, `load24_pan` (every voice panned) 75-78 % with SEA6; 46-50 % and 49-53 % with BFP8. That session ran higher
than the earlier tables (load24_all 57-62 % then), so compare within it: pan costs about 0-3 points. Firmware image 422 332 bytes.

**32 voices (2026-11-02).** `FM1B_VOICES` is 32, for the BFP8 bank. fm1-emu, 240 MHz, silent (`&mute=1`: tap.js keeps the
emulator from the speakers), one session, which again read about 7 points over the BFP8 table above:

| | load24_all (24-voice build) | load32_all (32-voice build) |
| --- | --- | --- |
| BFP8 | 45-49 % (66 % as the notes start) | 50-55 % (69 %) |
| SEA6 | - | 84-98 % (111 %): the guard has to cut |

So the 32-voice firmware is for BFP8 banks: `dist\sf2player_32v_32k.fwsc`. For SEA6 the 24-voice one is kept,
`dist\sf2player_24v_32k.fwsc` (built with `FM1B_VOICES 24U`). Image 422 588 bytes; the SRAM check passes.

**Zone level, exclusive class, scale tuning (2026-11-03).** The converter keeps three more SF2 generators per zone, in a table of 4 bytes
a zone after the zone table (FORMAT.md, "Zone levels"; the header is 52 bytes, and banks with the old 48-byte header still play, at full
level). Level: initial attenuation times 0.4 (EMU and FluidSynth), 482 of 1mgm.sf2's 569 zones are 0.5-15 dB down; one multiply per note.
Exclusive class: a note fades the voices of its class on the same channel and preset out in about 5 ms (the kit's hi-hats 42, 44, 46 are
class 1). Scale tuning: 42 zones play 50 cents a key. `test_zone.js`, 26 checks. The banks grow by 4 bytes a zone (BFP8 456 936 bytes,
97.9 %). fm1-emu, 240 MHz, silent, 32-voice build, SEA6, one session: `hats.script` (open hat, closed hat 0.5 s later): with the old
bank (`gm_32k_nopan`) the open hat rings 0.7 s past the closed one, with the new one the sound ends 0.3 s after it. `load24_all`: load
66-70 % new against 68-73 % old, so the cost is not measurable; RMS 0.24 against 0.51, as that program's zones are about 6 dB down in
the SoundFont: the mix is quieter and dense chords saturate less, turn MASTER up to match. Images: 32 voices 423 740 bytes, 24 voices
423 612.

**Velocity curve, output limiter (2026-11-03, branch `sf2-velocity-limiter`).** A voice's gain is velocity squared (fm1b.c), the
SoundFont default curve (velocity to attenuation, concave, 960 cB at 0.4): velocity 64 is 12 dB under 127, 32 is 24 dB under (it was 6
and 12, so soft playing was too loud). The output stage (sf2out.c, before the effects) gives the voices' mix 6 dB of makeup gain, taking
back what the zone levels took, and holds its peaks at -3 dB of full scale: the gain drops at once on a peak and comes back with a time
constant of 128 ms, one gain for the dry mix, the effect buses and the pan's sides alike. `test_rate.js`, 6 more checks (37): the
velocity steps, one note never reaches the limiter, 16 loud notes do and nothing clips (peak -2.9 dB), the gain comes all the way back.
fm1-emu, 240 MHz, silent, 32-voice build, SEA6, `load24_all` (velocity 100): load 63-69 % (66-70 % without the limiter), RMS up to
0.39 (0.24 without, 0.51 before the zone levels), peak 0.88: nothing clips. Images: 32 voices 423 900 bytes, 24 voices 423 804.

**Zone sends; BFP8 the standard (2026-11-03, branch `sf2-zone-sends`).** The converter keeps each zone's reverb and chorus send (SF2
generators 16 and 15) in the zone levels, which grow to 8 bytes a zone (header 56 bytes, FORMAT.md); the player adds them to the
channel's CC 91 and CC 93, at most 127. While all sounding voices end up with the same sends nothing changes in the cost; voices that
differ use the effect buses, as channels with different sends always did. In 1mgm.sf2 every zone has a reverb send of 10.9 % (+14, so
the default reverb goes from 30 to 44), and 95 zones of 24 programs have a chorus send of 25-50 % (+32 to +63): E Piano 1, Vibraphone,
Marimba, Organ 3, Clean Gt., the synth leads and pads 82-103. `test_zone.js`, 32 checks: the converter keeps the sends, a bank with the
52-byte header still plays without them, the sum and its limit, two voices with different sends on the buses. `test_multitimbral.js`
sets the zone sends to 0, as it tests the channels'. **BFP8 is the standard from now on:** the converter starts on BFP8 and
`dist/gm_32k.fm1b` is the BFP8 bank (459 216 bytes, 98.3 %), so the tests, `start_emu.ps1` and the FM-1 test use it; SEA6 is still
built (`node make_bank.js sea6`) and plays. fm1-emu, 240 MHz, silent, 32-voice build, BFP8: `load32_all` load 49 % held, 63 % as the
notes start, against 47-48 % and 64 % with the zone sends set to 0 (`dist/gm_32k_nosend.fm1b`); `load32_plain` 43-44 % either way.
Images: 32 voices 424 092 bytes, 24 voices 424 028.

**LFOs and key scaling (2026-11-03, branch `sf2-lfo`).** The converter keeps each zone's vibrato LFO (to pitch), mod LFO (to pitch and
to volume) with their delays and rates, and how the key number scales the volume envelope's hold and decay (SF2 generators 5, 6, 13,
21-24, 39, 40), in modulation records the zones share (20 bytes each, header 64 bytes, FORMAT.md "Modulation"): 33 records for 206 zones
of 1mgm.sf2, 668 bytes more (459 884, 98.5 %). The player runs both LFOs as triangles once per envelope step (0.72 ms): their pitch adds
to the bend and the wheel's vibrato, the mod LFO's volume multiplies the level. Hold and decay scale per note when it starts (integer
log2 and 2^-x, so the decay keeps its exact shape). In 1mgm.sf2: strings and brass get 9-19 cents of vibrato after 0.26 s, the
accordion, harmonica and vibraphone a tremolo, pianos 1-4 a decay 33 timecents shorter per key (an octave up decays 0.8 times as long).
Not done: the mod envelope and the filter, and stacked layers (Organ 3 and the accordion have a vibrato layer over a tremolo layer: only
the first plays, as before). `test_lfo.js`, 15 checks at 44.1 and 32 kHz on a looped sine: vibrato 100 cents at 2 Hz, the mod LFO after a
0.5 s delay, a 6 dB tremolo, key to decay and hold; `test_zone.js` 33: a bank with the 56-byte header plays without them, the SF2 the
converter writes keeps them. fm1-emu, 240 MHz, silent, 32-voice build, BFP8: `load32_lfo` (32 notes on violin and Organ 3, every voice
with an LFO) 42-45 % held, 57 % as the notes start, against 39-42 % and 55 % with the old bank (`dist/gm_32k_nolfo.fm1b`); `load32_all`
40-44 % against 42-46 % (no measurable difference; this run is a few % under the last section's). `make_sine_bank.js` writes
`dist/sine_lfo.fm1b` and `test_emu/sine.script` plays it: the tap's new `hz` field shows the firmware's vibrato (472-522 Hz around 500),
the tremolo (about +-5 dB at 1 Hz) and the delay. Images: 32 voices 427 004 bytes, 24 voices 427 100.

**Low-pass filter, modulation envelope (2026-11-03, branch `sf2-filter`).** The converter keeps each zone's filter cutoff and resonance
(SF2 generators 8, 9), the modulation envelope (25-30) and what it moves (cutoff 11, pitch 7), and the mod LFO on the cutoff (10); the
modulation records grow to 40 bytes (header 68 bytes, offset 64 their size; FORMAT.md). A filter whose cutoff can never come under
13500 cents (pianos 2 and 3: 14400 with an envelope that only opens it) is left out. 1mgm.sf2: 185 zones of 52 programs are filtered,
piano 1, violin, the basses, brass, organs 2 and 3, the synths and pads among them; 101 records, 463 268 bytes (99.2 % of the sample
flash, 3.6 KB left). The player runs RBJ's two-pole low-pass as FluidSynth does (Q from the resonance less 3.01 dB, gain 1 / sqrt(Q)) on
each filtered voice's block before it is mixed; the coefficients are computed again only when the cutoff moves, once a block, and glide
across it. Integer only: sin to x^9, 1 / a0 by a 32-bit division and a Newton step (a 64-bit division is a library call on the device).
`test_filter.js`, 22 checks at 44.1 and 32 kHz: the response at five octaves for 1, 8 and 20 kHz and resonance 0 to 19 dB within 0.01
dB of the same filter in floating point, a 100 Hz cutoff with no limit cycles after the release, the envelope on the cutoff and on the
pitch, the LFO on the cutoff, a bank with the 64-byte header (LFOs, no filter); on 1mgm.sf2 the filter darkens Synth Bass 1 and Warm
Pad and no filtered program's peak rises over 2 dB. Two overflows found and fixed on the way: the cutoff's angle past pi / 2, and a
coefficient's change from near 2 to near -2 in one block.
**Cost** (fm1-emu, 240 MHz, silent, 32-voice build, BFP8, the mean of the held notes from 6 s on, one session; the emulator differs by
up to 5 % between sessions): `load32_lfo` (violin and Organ 3, all filtered) 76.6 %, peak 87 %, against 50.4 % with the filters off in
the same firmware (`dist/gm_32k_nofilter.fm1b`) and 47.6 % with the `sf2-lfo` firmware; `load32_filter` (Saw Wave, Warm Pad, Organ 2,
Synth Bass 1) 73-78 %, peak 86 %, against 45-49 % without; `load32_all` (flute, not filtered) 51.2 % against 49.1 % on `sf2-lfo`. So a
filtered voice costs about 0.8 % of the CPU more, some 50 % over the voice itself; 32 filtered voices come near the CPU guard (80 %),
which then lowers the voice limit. A first version cost twice that (96-110 %): coefficients every block with 64-bit divisions, and a
second pass to mix. `make_sine_bank.js` program 4 is a 1 kHz filter the envelope opens and closes: in fm1-emu a 2 kHz tone goes from
-22 to -14 dB in 0.45 s and down to -26 dB, its pitch still. `test_emu/measure_load.ps1` serves one setup (script, bank, firmware) for
such a measurement. Images: 32 voices 431 516 bytes, 24 voices 431 292.

**The filter in 32 bits (2026-11-03, branch `sf2-filter`).** Two simplifications were tried, to be kept only if they cannot be heard.
First where the cost is: with the filter's arithmetic replaced by a copy the load is 54.8 % against 76.9 % (`load32_lfo`), so the
arithmetic is almost all of it; with the same three products in 32 bits (sound not kept), 59.3 %. The emulator counts the 64-bit
multiplies dear; whether the FM-1 does is not known. So, kept: a cutoff that holds still between 1/50 and 1/4 of the rate (640 Hz to
8 kHz at 32 kHz) runs in 32 bits: coefficients in Q13 (b0 with up to 16 bits more), the signal shifted so that the voice's loudest
possible frame (from its level) has 13 bits, as many for a soft note as for a loud one, 12 dB of room for the resonance, and the
output's rounding carried to the next frame. Under 1/50 the poles are too near 1 for Q13, over 1/4 the carried rounding (moved up in
frequency) meets the resonance near the top and ticked; there, and while the cutoff moves, it runs in 64 bits as before. The block
that glides to a still cutoff glides to the 32-bit coefficients themselves, so the change of way has no step. The 64-bit way now also
carries its rounding: it used to round down, which at 100 Hz added a DC of about 1 % of the signal. Tried and dropped: leaving out an
open filter (at the top, resonance at most 3 dB) for its gain alone: it saved nothing measurable (62.3 % against 61.3 %, `load32_filter`)
and closing it again ticked. `test_filter.js`, 26 checks: the responses within 0.2 dB of floating point (0.01 dB in 64 bits); sample by
sample against floating point 43-66 dB at 44.1 kHz and 52-65 dB at 32 kHz (the Q13 coefficients: under 1 %, about 0.1 dB), 80 dB in 64
bits; the noise of a note 30 dB down, scaled up, 55-67 dB at 44.1 kHz and 56-72 dB at 32 kHz under the signal (the unfiltered voice's
own: 94 dB; BFP8's 8-bit mantissas are some 45-50 dB), a sweep from 200 Hz to the top and back with no tick. **Cost**, fm1-emu, one
session, the mean from 6 s:

| | filters off | 64 bits (before) | 32 bits where it can (now) |
| --- | --- | --- | --- |
| `load32_filter` | 48.6 % (peak 63) | 73.8 % (84) | 62.6 % (76) |
| `load32_lfo` | about 50 % | 80.1 % (89) | 64.1 % (77) |

So 32 filtered voices cost some 14 % more than none, against 25-30 % before: about 0.45 % a filtered voice. Images: 32 voices 432 252
bytes, 24 voices 431 804.

**The 32-bit filter buzzed in the device build (2026-11-03, found in fm1-emu).** Warm Pad (a still 1.1 kHz cutoff, so in 32 bits)
came out brighter with its filter than without (the tap's centroid 622 Hz against 432), while the same firmware with the 32-bit way
turned off made it darker (385). With `make_sine_bank.js` programs 5 and 6 (a still 2 kHz low-pass, with and without resonance) and
`test_emu/sine_filter.script` a 500 Hz sine came out with its zero crossings at 690-940 a second, at full and at soft velocity; in
WebAssembly it is clean. The cause, in the disassembly (`llvm-objdump -D` of `build/dx7/sdk.elf` from `filter_run`): the compiler
paired a shift by a register with a load or store that uses the shift's result register (`r7 = r1 >>> r0 # r2 = [r7+40]`,
`r1 = r14 << r0 # [r2+56] = r1`). The pair means "both read the old registers" (the SDK's hand-written `dx7_seed_q4_block_64_asm`
relies on that), but fm1-emu runs this one as if the shift came first, so the filter's history was read from and saved to the wrong
place at every block. Whether the FM-1 itself does the same is not known. An empty `asm volatile` between the loads and the shifts
(and the shifts and the stores) in `filter_loop32` keeps them apart; no shift by a register is paired that way anywhere in the
firmware now (checked over the whole disassembly). After it: the sine 500 Hz in all five windows, peaks as with the 64-bit filter
(0.371 against 0.373); Warm Pad 377 Hz (64 bits 385, unfiltered 432), Synth Bass 1 69 (unfiltered 107). `test_emu/filter_listen.script`
plays Synth Bass 1, Saw Wave, Warm Pad, Violin and Piano 2.5 s each; `pad_long.script` Warm Pad 10 s and Synth Bass 1 8 s. Saw Wave
reads brighter with its filter in fm1-emu and in WebAssembly alike: its cutoff ends at 4.4 kHz with 8 dB of resonance, which lifts the
harmonics there, as in FluidSynth. Cost, one session (slower than the one above): `load32_filter` 69.8 % (peak 83) against 83.0 % (96)
with the 64-bit filter only and 53.2 % (70) with the filters off; `load32_lfo` 64.5 % (81). Images: 32 voices 432 252 bytes, 24 voices
431 836.

**Stacked layers (2026-11-03, branch `sf2-layers`).** In 62 of 1mgm.sf2's 129 presets two zones cover the same keys (Honky-Tonk's two
detuned pianos, 5th Saw Wave, Bass & Lead, the choirs, organs, most pads and synths); the player used to play only the lowest. All
the zones were already in the bank, so the samples cost nothing: a layer table (FORMAT.md, "Layers"; header 76 bytes) names, per
preset, the zones stacked on the key map's, 197 entries, 1 312 bytes. The bank is now 464 580 bytes, 99.5 % of the sample flash
(2.4 KB left). A note starts every zone that covers its key, one voice each: exclusive classes are applied for all of them before
any starts (so one layer never cuts the other), and the voice limit makes room for all of them at once. `test_layers.js`, 9 checks:
every key of every preset starts exactly the zones that cover it (16 512 keys, 7 297 stacked); 174 notes of the layered GM programs
start one voice per zone and are, sample for sample, the sum of each zone played alone; note off ends both; the limit; two layers in
one class sound together; a 68-byte header plays one zone as before. The tests that measure one zone at a time (`test_zone.js`) run
with the layers off (offset 68 = 0); `test_rate.js`'s voice-limit checks use the recorder (74, one zone a key) instead of the flute,
which now has two. In fm1-emu (`test_emu/layers.script`: Honky-Tonk, 5th Saw Wave, Bass & Lead, Choir Aahs, Piano 1 at C4, RMS
and centroid against `dist/gm_32k_nolayers.fm1b`): Honky-Tonk 0.030 against 0.0125 (+7.7 dB, the second piano), 5th Saw Wave 0.196
against 0.152 (+2.2 dB, centroid 367 against 318 Hz), Choir 0.223 against 0.143 (+3.9 dB, 550 against 372 Hz), Bass & Lead about the
same, the piano unchanged. A layered program is louder because its layers add up, as in FluidSynth. Cost, one session: `load32_all`
(flute, now two voices a note, so 32 notes are capped at 32 voices) 50.8 % (peak 72) against 47.6 % (58); `load32_filter` 67.7 % (92)
against 65.5 % (86). Twice the voices a note means half the notes in those programs, and more voices fading at once as notes start,
so the peaks rise; the CPU guard takes over over 80 %. Image: 32 voices 432 700 bytes.

Cost: a channel that never gets bend or the wheel plays exactly as before (the tests check it bit for bit). A bend recomputes the step of
that channel's voices once; the wheel does it once per 32 frames per voice while it is up. In fm1-emu at 240 MHz, 8 voices held on a flute
(`test_emu/load8.script`): load 53 %; the same with the wheel at 127, the bend full up and the pedal down (`load8_bend.script`): 55-59 %.
Bending up costs most, as the samples are read faster.

## 7b. Sample formats compared (measured 2026-11-02)

1mgm.sf2 at 32 kHz, built with `index.html`'s core in node. Noise is against the 16-bit samples, per 20 ms block, by the block's level.
Load is fm1-emu at 240 MHz, 8 held voices, a bank of piano, flute and the standard kit in each format (idle about 11 %).

| Format | Whole GM bank | Of 466 944 | Noise > -20 dBFS | -20..-40 | -40..-60 | Load, flute high | Load, piano low |
| --- | --- | --- | --- | --- | --- | --- | --- |
| SEA5 | 343 732 | 74 % | 32 dB | 35 dB | 28 dB | 50 % | 46 % |
| 8-bit | 439 248 | 94 % | 41 dB | 27 dB | 8 dB | 42 % | 38 % |
| 12-bit | 640 240 | 137 % | 65 dB | 51 dB | 29 dB | 44 % | 41 % |
| 16-bit | 841 084 | 180 % | | | | 43 % | 40 % |

SEA5 decoding costs about 1 percentage point per voice. fm1-emu likely does not model flash wait states, so 16-bit (2.8 times the
bytes of SEA5) may cost more on the device than here.

Lower bank rates (whole GM bank, of 466 944): 22 050 Hz SEA5 54 %, 8-bit 67 %, 12-bit 96 %, 16-bit 125 %; 16 000 Hz SEA5 44 %,
8-bit 52 %, 12-bit 73 %, 16-bit 95 %. The engine always renders at 44.1 kHz, so a lower bank rate only saves SEA5 decoding:
8 voices SEA5 50 % at 32 kHz, 47 % at 22 and 16 kHz; 8-bit 42 % and 41 %. Reverb and chorus cost about 7 points
(8 voices with sends 0: SEA5 44 %, 8-bit 35 %).

16 voices (`FM1B_VOICES 16U`, SRAM check passes), flute, notes 30 ms apart (fm1-emu drops MIDI when 16 notes come at once):
SEA5 32 kHz 77-81 %, SEA5 22 kHz 71-76 %, 8-bit 32 kHz 57-62 %, 12-bit 22 kHz 58-63 %.

**After the voice loop rewrite (413 404 bytes).** The loop runs in stretches up to the next envelope step with the voice in registers,
one loop per format, Q4 interpolation and one 32 x 32 multiply per frame (output within about 100 dB of the old loop's). SEA is decoded
by the engine's own decoder (any code size with a table; the SDK's reads one bit at a time): codes from a bit buffer, the LMS sum exactly
in 32 bits. Flute, notes 30 ms apart (`test_emu/load8s.script`, `load16s.script`):

| | 8 voices before | 8 voices after | 16 voices before | 16 voices after |
| --- | --- | --- | --- | --- |
| SEA5 32 kHz | 50 % | 33-35 % | 77-81 % | 52-57 % |
| SEA6 32 kHz | | | | 52-59 % |
| 8-bit 32 kHz | 42 % | 24-25 % | 57-62 % | 32-37 % |

SEA6 costs the same as SEA5. With the SDK's decoder instead of the engine's, 8 voices SEA5 was 35-37 %. SEA6 against SEA5 (whole GM
bank, 32 kHz): 397 532 bytes (85 %), noise 37 / 41 / 33 dB by level against 32 / 35 / 28 dB.

The firmware now has 16 voices and `dist\gm_32k.fm1b` is the SEA6 bank. Whole GM bank in fm1-emu, 16 notes held: 51-53 %;
`mt.script` (program 73 on channel 2) still gives the flute, RMS 0.100, centroid 282 Hz.
Everything at once (`test_emu/load16_all.script`: 16 voices on two channels with different sends, so both effect buses run,
modulation wheel at 127 and pitch bend up on both): 49-59 % (the higher figures while the notes start), against 49-51 % for the
same notes plain (`load16_plain.script`).

**The player at 32 kHz (2026-11-02).** The device renders voices and effects at `FM1B_RATE` 32 kHz (`fm1b.h`; the browser and the
tests stay at 44.1 kHz) and `sf2out.c` resamples to 44.1 kHz: 92 or 93 player frames a block, a 12-tap Kaiser-windowed sinc at 256
phases (`resample_table.inc`, from `make_resampler.js`: flat to 10 kHz, -1.1 dB at 12 kHz, images at -20 dB or lower), skipped while
everything it reads is silent. The effects' delays and chorus rate scale with the rate and the reverb's damping is retuned; an envelope
step is 23 frames (0.719 ms against 0.726 ms: envelopes run 1 % fast). `test_rate.js`: pitch within 1 cent, level within 0.01 dB,
release, vibrato rate and reverb tail as at 44.1 kHz. fm1-emu, 16 voices, SEA6 GM bank:

| | 44.1 kHz | 32 kHz |
| --- | --- | --- |
| idle | about 10 % | about 10 % |
| `load16_plain.script` | 49-51 % | 44-48 % |
| `load16_all.script` | 49-59 % | 45-56 % |

About 5 points, not the 10 the rate alone suggests: SEA decoding is per bank sample, which does not change, and the filter costs a few.
`mt.script` gives the flute as before (RMS 0.098, centroid 283 Hz).

More voices at 32 kHz (test builds with `FM1B_VOICES` 24 and 32, both pass the SRAM check; flute on 3 or 4 channels, notes 30 ms apart;
`_all` adds different sends on every other channel, the wheel at 127 and bend up):

| Voices | `load<N>_plain.script` | `load<N>_all.script` | while the notes start |
| --- | --- | --- | --- |
| 16 | 44-48 % | 45-56 % | up to 56 % |
| 24 | 59-62 % | 57-61 % | up to 76 % |
| 32 | 70-74 % | 74-78 % | up to 88 % |

**24 voices and the CPU guard.** `FM1B_VOICES` was 24 (32 since 2026-11-02, below). After each block the audio interrupt passes its own time to `sf2out_load()`
(`sf2out.c`): over 80 % of the block the voice limit (`fm1b_voice_limit()`) drops to one under the voices sounding (two over 95 %),
and the oldest fade out in about 5 ms; it then waits 8 blocks, and never goes under 4. After 0.25 s under 60 % the limit rises by
one, up to 24. `test_rate.js` checks the limit and the guard (31 checks). In fm1-emu with `load24_all.script`: 240 MHz 62-63 %, the
guard never acts; at 160 and 120 MHz it cuts voices and the load settles at 57-61 % and 40-45 % (the RMS falls with the voices).
fm1-emu's load and the firmware's timer may not scale alike with `&mhz=`, so the thresholds are only checked on WebAssembly; the
first block of a sudden overload is not caught (the guard reacts after it).

**BFP8 beside SEA6, for evaluation (2026-11-02).** The engine reads every format from the bank's header, so one firmware plays
either; only the bank changes. `node make_bank.js <sea5|sea6|bfp8|pcm8|pcm12|pcm16> [rate]` builds the whole GM bank as
`dist\gm_32k_<format>.fm1b`, and `.\start_emu.ps1 -Tap -Bank dist\gm_32k_bfp8.fm1b` plays it in fm1-emu (each bank gets its own
`.flash` file). The standard bank `dist\gm_32k.fm1b` is still SEA6. Noise by level against 16-bit, sizes of the whole GM bank:

| | > -20 dBFS | -20..-40 | -40..-60 | Whole GM bank |
| --- | --- | --- | --- | --- |
| SEA6 | 37 dB | 40 dB | 32 dB | 397 532 (85 %) |
| BFP8 | 44 dB | 43 dB | 42 dB | 454 656 (97 %, 12 KB to spare) |
| mu-law 8-bit (not built) | 38 dB | 37 dB | 32 dB | about 439 000 (94 %) |
| BFP7, groups of 16 (not built) | 38 dB | 38 dB | 37 dB | about 401 000 (86 %) |

fm1-emu, 240 MHz (32-voice figures from a test build with `FM1B_VOICES 32U`):

| | SEA6 | BFP8 |
| --- | --- | --- |
| 24 voices, `load24_plain` | 57-59 % | 35-41 % |
| 24 voices, `load24_all` | 57-62 % (79 % as the notes start) | 38-42 % (53 %) |
| 32 voices, `load32_plain` | 70-74 % | 41-45 % (55 %) |
| 32 voices, `load32_all` | 71-78 % (88 %) | 42-48 % (63 %) |

`mt.script` with the BFP8 bank: the flute, RMS 0.099, centroid 284 Hz.

**Emulator round before the device (2026-11-02, 24 voices, 240 MHz), SEA6 and BFP8 banks.** Nothing clipped past full scale, nothing
non-finite in any run.

| Script | What it checks | SEA6 | BFP8 |
| --- | --- | --- | --- |
| `programs.script` | programs 0, 24, 40, 56, 73, 80, 118 one after another | all sound, each its own centroid (94-1328 Hz) | the same centroids |
| `drums.script` | kick, snare, hats, crash on channel 10 | all five heard, quiet after | the same |
| `release.script` | 24 notes, then note off | silent within 1 s, load back to 13 % | silent, 10-11 % |
| `steal40.script` | 40 notes into 24 voices | 60-66 % (83 % as they start), steady sound | 37-41 % (46 %) |
| `load24_all.script` | wheel, bend, both effect buses | 57-62 % | 39-43 % (54 %) |
| `mt.script` | program change on channel 2 | the flute, 0.099 / 283 Hz | 0.099 / 284 Hz |

Dense chords (24 voices) reach the output's full level (peak 0.875 in the tap): loud enough to clip; a matter of level, not CPU.
In `programs.script` the BFP8 windows read 2-4 dB lower than SEA6 in fm1-emu, but the engine on WebAssembly gives both banks the
same level within 0.4 dB per program: the short windows right at the note starts are timing-sensitive in the emulator.

The checklist for the device is `FM1_TEST.md`; `node make_midi.js` turns these scripts into `test_emu\midi\*.mid` to play from
Windows (each file ends by resetting the controllers and sends and turning all notes off).

What SEA matches in plain PCM (every SF2 sample at 32 kHz, noise against 16-bit, per 20 ms block by level):

| Coding | > -20 dBFS | -20..-40 | -40..-60 | Bytes per sample |
| --- | --- | --- | --- | --- |
| SEA5 | 33 dB | 35 dB | 27 dB | 0.71 |
| SEA6 | 37 dB | 40 dB | 32 dB | 0.84 |
| 8-bit | 40 dB | 27 dB | 6 dB | 1 |
| 9-bit | 46 dB | 33 dB | 11 dB | |
| 10-bit | 52 dB | 40 dB | 17 dB | |
| 11-bit | 58 dB | 46 dB | 22 dB | |
| 12-bit | 64 dB | 52 dB | 28 dB | 1.5 |

So SEA6 is about 7.5-bit PCM on loud blocks, 10-bit at -20..-40 dBFS and better than 12-bit below that, where noise is heard.
A lower bank rate does not add noise; it drops the treble. Energy above 90 % of the new Nyquist frequency, against the block:
22 050 Hz 18 / 17 / 7 dB, 16 000 Hz 15 / 14 / 6 dB, 11 025 Hz 11 / 11 / 6 dB (by the same levels; the quiet blocks are mostly
broadband tails). Scripts: the session's `noise.js` and `band.js` were not kept; `compare.js`-style code over `index.html`'s core.

## 8. Open items

- Run the multitimbral build on the real FM-1 with program changes on two channels (it has run in fm1-emu, section 4), and note the result here.
- Check the channel page and bend, wheel and pedal from a real keyboard on the device.
- Check the `MULTI` panel line, the channel page and the bank-select rule (CC0/CC32) on a screen: the emulator draws no LCD.
- Check pan per channel (CC10, KNOB2 on the channel page), the kit's spread and MASTER as channel volume on the device (FM1_TEST.md).
- Fill in section 4 once the emulator has been started again.
