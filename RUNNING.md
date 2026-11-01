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

App image history: 401 020 bytes before multitimbral, 403 260 after (+2 kB). If a change makes the build stop with
"Application does not fit the FM1 layout.", the image has outgrown its room (see `FIRMWARE.md`).

`dist\*.fwsc` are older packages from earlier builds (not committed). Copy a new one there if you want to keep it.

## 2. Tests that need no device (verified, all pass)

Each one compiles the real C source to WebAssembly with the WASI SDK on the first run (and again when a source is newer).

```bash
cd D:/git/fm1-sf2
node test_multitimbral.js     # 35 checks: channels, program change, volume, CC120/121/123, channel 10. Uses dist/gm_32k.fm1b
node test_fx.js               # reverb and chorus (sf2fx.c)
node test_upload.js           # the SysEx bank upload (sf2_upload.c)
```

`test_multitimbral.js` runs the real `fm1b.c` with 16 voices (the browser build), the device has 8. It does **not** cover
`transfer.c` (MIDI routing: bank select, channel 10 skipping program change, the CC10 rule) or the `MULTI` panel line.
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

## 4. fm1-emu, the CPU emulator (not verified in the latest session)

<https://joakimeriksson.github.io/fm1-emu/> runs `.fwsc` packages unchanged (**Open .fwsc...**). The firmware package has
no bank, so it shows `NO BANK` until one is in the user flash.

1. Make the flash sector file for the bank (verified to run):
   ```bash
   node make_flash_overlay.js dist/gm_32k.fm1b gm.flash
   ```
2. The page cannot fetch `gm.flash` from another origin, so it needs a **local copy of the site's files** next to the package
   (`index.html`, `main.js`, `worklet.js`, `persistence.js`, `fwsc.js`, `tryout.js`, `firmwares.json`, `fm1_web.wasm`). No such copy
   was found on this machine on 2026-11-01. Download it from the site again, put `gm.flash` and `FM1_OFFICIAL_OPEN.fwsc` in the same
   folder, and serve that folder (`python -m http.server <port>`).
3. `tryout.js` takes `&flash=PATH` (sector records: a little-endian u32 flash offset and 4096 bytes). The exact query format was not
   written down: read `tryout.js` in the local copy and put the query format here.
4. Known: the emulator's DIN MIDI input stops delivering, so a MIDI test of program change per channel may not work in it;
   panel keys and the `MULTI` line are the safer checks. Measured load at 240 MHz with 8 voices held: about 56 % with both effects.

## 5. On the real FM-1

- Flash the package with the M-VAVE workbench (<https://open.m-vave.com/fm1-workbench>). Back up first and keep the device powered.
- Send a bank over USB MIDI from the converter page ("Skicka till FM-1"). The panel shows `UPLOAD n Rx` and then the program.
- Panel: encoder 1 program, OCT-/OCT+ octave, SCL drums, KNOB1 reverb, KNOB2 chorus, KNOB3 pan. Pan (KNOB3 and CC10) was reported working after it was added.

## 6. Playing it from Windows

- **USB or DIN keyboard:** Windows shows it as a MIDI port, nothing else needed.
- **A DAW or MIDI file into the FM-1:** create a virtual port with **loopMIDI**, send to it, and route it to the FM-1's USB MIDI port.
- **BLE MIDI** (reported working, the method used was not written down): pair the device in Windows, then either let the FM-1 take BLE MIDI
  directly or bridge with **MIDIberry** (BLE in, loopMIDI port out). Windows MIDI Services is also installed on this machine.
- **Recording the audio:** Audacity with the *Windows WASAPI* host and the output device marked *(loopback)*, or OBS "Audio Output Capture".

## 7. What the firmware does with MIDI (multitimbral)

| Message | Effect |
|---|---|
| Note on/off, channels 1-9, 11-16 | Plays the channel's preset. A channel that never had a program change plays the panel's preset (encoder 1) |
| Note on/off, channel 10 | The percussion kit (the first kit in the bank), same as the SCL button |
| Program change | Per channel. Bank select first (CC0/CC32): CC0=120, or CC0=1 and CC32=0, is the percussion bank, anything else melodic. A preset the bank lacks is ignored. Ignored on channel 10 |
| CC7 volume, CC11 expression | Per channel, General MIDI curve, live on sounding notes. 127/127 is unity |
| CC120 / CC121 / CC123 | All sound off / reset volume and expression / all notes off, **for that channel only** |
| CC91 / CC93 | Reverb and chorus send, global |
| CC10 pan | **Global** pan. Ignored as soon as any channel has its own program (otherwise channels would overwrite each other) |

Panel: the line `MULTI n CH   LAST CH.. PRG...` appears once a program change has been received.

Limits: 8 voices in total on the device (oldest is stolen first), no pitch bend, no sustain pedal, no per-channel pan.

## 8. Open items

- Run the multitimbral build on the device or in fm1-emu, with program changes on two channels, and note the result here.
- Measure the audio interrupt load with the multitimbral build (it adds one multiply per voice per 32 frames).
- Per-channel pan needs a stereo `fm1b_render` and a changed `sf2fx_mix`.
- Fill in section 4 once the emulator has been started again.
