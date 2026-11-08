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
node test_sdram.js            # the SDRAM test of the info page (sdram_test.c) against a model of the memory with faults; any clang for wasm32
python3 test_sdram_config.py  # make_sdram_config.py: boot-config.bin read as its entries, the copy with the SDRAM on differs in two keys only (python3, no packages)
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
node make_install.js [bank.fm1b]     # dist/install/: the last build's package, the bank (default dist/gm_32k.fm1b), manifest.json
```

Open <http://localhost:8765/install.html> (the same server as section 3) in Chrome or Edge and click **Installera**. It finds the FM-1
with the SDK's own browser updater (`fm1_open/sdk/WebUpdate.js`, the protocol M-VAVE's workbench uses: identity query, capability with the
build id), installs the firmware through the vendor loader unless the FM-1 already reports this build, waits for it to come back and checks
the build id it reports, then sends the bank with the player's SysEx upload (as `index.html` does, on the port the updater found and
checked). Either step can be left out. It only updates over the SF2 player's own firmware (`FM-1_8000014`): an FM-1 on the stock V15 or
another firmware is told to use the workbench the first time. `make_install.js` must run after the build it packs: the build id it
writes is `build/dx7/build-identity.json`'s, and the device's must match it after the update. Checked in the browser pane against a fake
FM-1 (Web MIDI replaced): the same build skips the firmware and the whole bank arrives (1 040 frames, CRC right), bank only, the stock
V15 refused, no FM-1 found. On the FM-1 (2026-11-03, already on this build): the bank was written and plays. The firmware write itself is the SDK's code and has
not been run from this page yet.

**From another computer on the same network (2026-11-05).** The server (`start_web.ps1`) answers on every address and prints them
(this machine: `http://192.168.50.145:8765/install.html`; Windows Firewall already lets Python in). Chrome gives Web MIDI only to
`localhost` or HTTPS, so on the other computer, once: `chrome://flags`, *Insecure origins treated as secure*, add
`http://192.168.50.145:8765`, Relaunch. Without the flag the page says it has no Web MIDI. The packages can also be fetched from
`http://192.168.50.145:8765/dist/` and flashed with M-VAVE's workbench, which needs no flag.

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
   Add `&mhz=240` for the device's clock (the page default is 100).
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
| CC91 / CC93 | Reverb and chorus send **per channel**, 30 and 20 until set. The panel's KNOB3/KNOB4 master levels (127 by default) scale them |
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

What a channel gets of an effect is master x send. The defaults (master 127, sends 30 and 20) sound bit for bit as the single global
level of 30 and 20 did before (`test_fx.js` checks it). On the channel page the panel's keys play the selected channel.

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
