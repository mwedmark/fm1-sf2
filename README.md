# fm1-sf2

Turns a SoundFont (`.sf2`) into a sound bank for the M-VAVE FM-1, and plays it in the FM-1 simulator. Everything runs in the browser.

- `index.html`: the converter. Drop an `.sf2`, pick the encoding (SEA5 or raw PCM) and the sample rate, download a simplified `.sf2` or an FM1B `.bin`, and play the result on a keyboard in the simulator. Serve it (`python -m http.server`) rather than opening the file.
- `FORMAT.md`: the FM1B bank format, made to be used in place by the FM-1's CPU. Samples are raw PCM or SEA5, the FM-1's own codec.
- `fm1_open/`: a copy of [m-vave/fm1_open](https://github.com/m-vave/fm1_open) (upstream commit `4c7496d`, GPLv3 with the FM1 SDK linking exception, see `fm1_open/LICENSE.md`) with a melodic sampler added: it plays an FM1B bank on track 1 (`web/synth/sampledrum.c`, `web/engine/sound.c`, `web/engine/Simulator.c`, `third_party/synth/voices.h`).
- `simulator_wasm.js`: the simulator built from `fm1_open`, as base64 for `index.html`. Rebuild it with `fm1_open/tools/Build-Web.ps1 -LocalPreview` (downloads the WASI SDK on first use), then `node make_wasm_js.js`.
- `test_core.js <sf2> [outdir]`: converter checks (every note of every preset resolves, SEA5 round trip). `test_sim.js <simulator.wasm> <bank.bin>`: plays a bank through the simulator and measures pitch, loop and release.
- `convert.py`: the original command-line converter, no longer maintained.

`.sf2` and `.bin` files are not committed.
