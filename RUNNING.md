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
node test_multitimbral.js     # 63 checks: channels, program change, volume, CC120/121/123, channel 10, pitch bend and its
                              # RPN range, modulation wheel, sustain pedal, what the channel page reads, the effect sends. Uses dist/gm_32k.fm1b
node test_fx.js               # reverb and chorus (sf2fx.c), master levels times sends
node test_upload.js           # the SysEx bank upload (sf2_upload.c)
node test_formats.js          # SEA5, SEA6, BFP8, 8-, 12- and 16-bit banks play exactly their decoded samples (needs 1mgm.sf2)
node test_rate.js             # the output stage at 32 kHz + resampler against 44.1 kHz: pitch, level, brightness, release, vibrato, reverb
```

`test_multitimbral.js` runs the real `fm1b.c` with 16 voices, as the device has. It does **not** cover
`transfer.c` (MIDI routing: bank select, channel 10 skipping program change, the CC10 rule) or the panel (the `MULTI` line, the channel page).
Those are only compiled, so test them in the emulator or on the device.

## 3. The converter and browser simulator (verified)

```powershell
cd D:\git\fm1-sf2
python -m http.server 8765
```

Open <http://localhost:8765/index.html> ("SF2 till FM-1"): drop an `.sf2`, convert to an FM1B bank, play it in the simulator,
or send it to a connected FM-1 ("Skicka till FM-1", Web MIDI over USB). Serve it, do not open the file directly.
A server on port 8765 was already running when this was written, so check before starting another.

Rebuild the simulator after changing `fm1b.c` or `fm1_open\web`:

```powershell
cd D:\git\fm1-sf2\fm1_open
.\tools\Build-Web.ps1 -LocalPreview
cd ..
node make_wasm_js.js
```

The simulator plays on one sampler track through the panel slot only. **MIDI channels and program change are firmware only**
(`transfer.c`), so multitimbral cannot be heard in the simulator.

## 4. fm1-emu, the CPU emulator (run 2026-11-01 with the multitimbral build)

**Quick start** (verified): build the firmware (section 1), then

```powershell
cd D:\git\fm1-sf2
.\start_emu.ps1            # multitimbral check: serves emu\ on 127.0.0.1:8781 and opens it
.\start_emu.ps1 -Script '' # no script, play it yourself with the keys or a MIDI keyboard
.\start_emu.ps1 -Tap       # the page that records the emulator's audio into window.__cap (see step 6)
```

`start_emu.ps1` keeps the emulator's site files in `emu\` (git ignores it), copies the newest `FM1_OFFICIAL_OPEN.fwsc` and `test_emu\*` into it,
makes `gm.flash` from `dist\gm_32k.fm1b` when needed, points the `sf2` entry of `firmwares.json` at its port, starts `test_emu\cors_server.py`
hidden if nothing listens on the port, and opens the URL. The first run copies the emulator's files from the old scratchpad folder below;
if that is gone it stops and lists the files to download from the site. Stop the server by its port, so that other Python servers survive:
`Get-NetTCPConnection -State Listen -LocalPort 8781 | ForEach-Object { Stop-Process -Id $_.OwningProcess }`.
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
- Panel: SELECT program, OCT-/OCT+ octave, SCL drums, HOME channel page, KNOB2 pan, KNOB3 reverb, KNOB4 chorus (see section 7).
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
| CC120 / CC121 / CC123 | All sound off / reset volume, expression, bend, wheel and pedal / all notes off, **for that channel only** |
| CC91 / CC93 | Reverb and chorus send **per channel**, 30 and 20 until set. The panel's KNOB3/KNOB4 master levels (127 by default) scale them |
| CC10 pan | **Global** pan. Ignored as soon as any channel has its own program (otherwise channels would overwrite each other) |

Panel: the line `MULTI n CH   LAST CH.. PRG...` appears once a program change has been received. **HOME** switches to the channel page:
one row per MIDI channel with its program and name (grey: it follows the panel's program; white: a program of its own; orange: the kit on
channel 10; green: it plays now), level (volume x expression), bend in semitones, modulation wheel and `P` while the pedal is down. HOME again goes back.
The knobs are the same on both pages, only what they act on changes:

| Knob | Program page (the start) | Channel page (HOME) |
|---|---|---|
| SELECT | The panel's program | The selected channel's program; below the first, the channel follows the panel again (channel 10 stays the kit) |
| KNOB1 | - | Moves the cursor (the highlighted row) |
| KNOB2 | Pan, global | - |
| KNOB3 | Master reverb, 0..127 | The selected channel's reverb send (CC91) |
| KNOB4 | Master chorus, 0..127 | The selected channel's chorus send (CC93) |

What a channel gets of an effect is master x send. The defaults (master 127, sends 30 and 20) sound bit for bit as the single global
level of 30 and 20 did before (`test_fx.js` checks it). On the channel page the panel's keys play the selected channel.

Cost of the sends: while all sounding channels have the same sends the effects are fed from the dry mix as before; only when they differ
does each voice also go to a reverb and a chorus bus. In fm1-emu at 240 MHz, 8 flute voices on two channels: same sends
(`test_emu/load8_same.script`) 53-55 %, different sends (`load8_sends.script`) 55-57 %.

Checked in fm1-emu: `test_emu/knob73.script` (73 clicks on SELECT for channel 2, then a MIDI note on it) and `knobkey.script` (the same, then
a panel key) both sound as program change 73, the flute (RMS 0.095, centroid 275 Hz). The emulator loses clicks when many come in one
script event (`e0:73`), so the scripts send one click every 30 ms. Script encoders: `e0` SELECT, `e2`..`e5` KNOB1..KNOB4.

Limits: 24 voices in total on the device (oldest is stolen first; 8, then 16, until 2026-11-02), fewer while the CPU guard holds
the limit down (the panel then shows `VOICES n/limit CPU`), no per-channel pan.

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

**24 voices and the CPU guard.** `FM1B_VOICES` is 24. After each block the audio interrupt passes its own time to `sf2out_load()`
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
- Per-channel pan needs a stereo `fm1b_render` and a changed `sf2fx_mix`.
- Fill in section 4 once the emulator has been started again.
