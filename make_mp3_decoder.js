// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
// Builds mp3_decoder_web.c (the firmware's minimp3) to WebAssembly and writes mp3_decoder_wasm.js, which the converter page loads:
//   node make_mp3_decoder.js            (a clang for wasm32: FM1_CLANG, else the wasi-sdk under fm1_open/build/tools, else clang)
// The file is kept in the repository, as simulator_wasm.js is, so the page and Pages need no compiler.
// MP3_DECODER.decode(Uint8Array) gives an Int16Array, as `ffmpeg -f mp3 -i - -f s16le -` does for a mono stream.
const fs = require('fs'), path = require('path'), os = require('os'), { execFileSync } = require('child_process');
const root = __dirname;
const wasiClang = path.join(root, 'fm1_open/build/tools/wasi-sdk-27.0-x86_64-windows/bin/clang.exe');
const clang = process.env.FM1_CLANG || (fs.existsSync(wasiClang) ? wasiClang : 'clang');
const wasm = path.join(os.tmpdir(), 'fm1_mp3_decoder.wasm');
execFileSync(clang, ['--target=wasm32', '-nostdlib', '-O2', '-Wall', '-Wextra', '-Wno-unused-function', '-ffreestanding', '-fno-builtin',
  '-I' + path.join(root, 'test_sound_cache/stubs'), '-I' + path.join(root, 'fm1_open/third_party/mp3'),
  '-Wl,--no-entry', '-Wl,--export=__heap_base', '-Wl,-z,stack-size=65536',
  path.join(root, 'mp3_decoder_web.c'), path.join(root, 'test_sound_cache/libc_min.c'), '-o', wasm], { stdio: 'inherit' });
const b64 = fs.readFileSync(wasm).toString('base64');
fs.writeFileSync(path.join(root, 'mp3_decoder_wasm.js'), `// The firmware's MP3 decoder (minimp3, CC0; mp3_decoder_web.c, GPLv3 with the FM1 SDK linking exception) for the converter page.
// Made by make_mp3_decoder.js; do not edit.
const MP3_DECODER = (() => {
  const raw = Uint8Array.from(atob('${b64}'), c => c.charCodeAt(0));
  let w = null;
  return {
    decode(mp3) {
      if (!w) w = new WebAssembly.Instance(new WebAssembly.Module(raw), {}).exports;
      const base = (w.__heap_base.value + 15) & ~15, max = Math.ceil(mp3.length / 24) * 1152 + 4608, need = base + mp3.length + 16 + max * 2;
      if (w.memory.buffer.byteLength < need) w.memory.grow(Math.ceil((need - w.memory.buffer.byteLength) / 65536));
      const inAt = base, outAt = (base + mp3.length + 15) & ~15;
      new Uint8Array(w.memory.buffer).set(mp3, inAt);
      const n = w.decode(inAt, mp3.length, outAt, max);
      return new Int16Array(w.memory.buffer.slice(outAt, outAt + n * 2));
    },
  };
})();
if (typeof module !== 'undefined') module.exports = MP3_DECODER;
`);
console.log('mp3_decoder_wasm.js:', b64.length, 'base64 characters');
