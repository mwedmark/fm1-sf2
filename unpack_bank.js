// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
// Unpacks a packed (MP3) FM1B bank into a plain BFP8 bank with the live cache's own C decoder on WebAssembly, the way test_packed_engine.js does for
// its plain engine. The result holds the sounds exactly as the live-cache firmware decodes them, so any tool that reads a plain bank (compare_fluid.js,
// fm1-emu's plain firmware) can play what the packed bank sounds like.
//   node unpack_bank.js packed.fm1b out.fm1b      (a clang for wasm32: the WASI SDK of fm1_open/build/tools, or FM1_CLANG)
const fs = require('fs'), path = require('path'), { execFileSync } = require('child_process');
const root = __dirname, [inFile, outFile] = process.argv.slice(2);
if (!inFile || !outFile) { console.error('usage: node unpack_bank.js packed.fm1b out.fm1b'); process.exit(1); }
const wasiClang = path.join(root, 'fm1_open/build/tools/wasi-sdk-27.0-x86_64-windows/bin/clang.exe');
const clang = process.env.FM1_CLANG || (fs.existsSync(wasiClang) ? wasiClang : 'clang');
const lc = path.join(root, 'fm1_open/live_cache'), mp3 = path.join(root, 'fm1_open/third_party/mp3'), dir = path.join(root, 'test_sound_cache');
const rate = fs.readFileSync(inFile).readUInt32LE(8);                                  // 32000, or 44100 for the experiment build (RUNNING.md, 2e): the decoder is built for one rate
const wasmPath = path.join(dir, rate === 44100 ? 'harness_44k.wasm' : 'harness.wasm');
const sources = [path.join(dir, 'harness.c'), path.join(lc, 'mp3_sound.c'), path.join(lc, 'sound_cache.c'), path.join(dir, 'libc_min.c')];
if (!fs.existsSync(wasmPath) || sources.concat(path.join(lc, 'mp3_sound.h'), path.join(lc, 'sound_cache.h')).some(s => fs.statSync(s).mtimeMs > fs.statSync(wasmPath).mtimeMs))
  execFileSync(clang, ['--target=wasm32', '-nostdlib', '-O2', '-Wall', '-Wextra', '-Wno-unused-function', '-ffreestanding', '-fno-builtin', ...(rate === 44100 ? ['-DFM1B_RATE=44100U'] : []), '-I' + path.join(dir, 'stubs'), '-I' + lc, '-I' + mp3,
    '-Wl,--no-entry', '-Wl,-z,stack-size=262144', '-Wl,--initial-memory=8388608', ...sources, '-o', wasmPath], { stdio: 'inherit' });
const m = new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(wasmPath)), {}).exports;
const P = new Uint8Array(fs.readFileSync(inFile)), dv = new DataView(P.buffer, P.byteOffset), u32 = o => dv.getUint32(o, true);
const offPack = u32(80), nPack = u32(84), offSmp = u32(36), nz = dv.getUint16(14, true), offZone = u32(32);
if (!offPack || !nPack) throw new Error('not a packed bank');
const frames = new Map(), loops = new Map();
for (let i = 0; i < nz; i++) { const z = offZone + i * 24; frames.set(u32(z), u32(z + 4)); if (P[z + 17] & 1) loops.set(u32(z), [u32(z + 8), u32(z + 12)]); }
let streamEnd = 0; for (let i = 0; i < nPack; i++) { const e = offPack + i * 24; if (P[e + 16] === 2) streamEnd = Math.max(streamEnd, u32(e + 8) + u32(e + 12)); }
new Uint8Array(m.memory.buffer).set(P.subarray(offSmp, offSmp + streamEnd), m.stream_ptr()); m.set_stream_bytes(streamEnd);
let total = 0; for (let i = 0; i < nPack; i++) total = Math.max(total, u32(offPack + i * 24) + u32(offPack + i * 24 + 4));
const U = new Uint8Array(total); U.set(P.subarray(0, offSmp));
const uv = new DataView(U.buffer); uv.setUint16(4, 1, true); uv.setUint16(6, 8, true); uv.setUint32(40, U.length, true); uv.setUint32(80, 0, true); uv.setUint32(84, 0, true); uv.setUint32(88, 0, true);
let mp3n = 0, lossless = 0;
for (let i = 0; i < nPack; i++) {
  const e = offPack + i * 24, uOff = u32(e), uBytes = u32(e + 4), codec = P[e + 16], n = frames.get(uOff);
  if (codec === 0) { U.set(P.subarray(offSmp + u32(e + 8), offSmp + u32(e + 8) + uBytes), uOff); ++lossless; continue; }
  new Uint32Array(m.memory.buffer, m.index_ptr(), 4).set([u32(e + 8), dv.getUint16(e + 18, true), n, 0]);
  const xf = dv.getUint16(e + 20, true), lp = loops.get(uOff);
  new Uint32Array(m.memory.buffer, m.loop_ptr(), 3).set(xf && lp ? [lp[0], lp[1], xf] : [0, 0, 0]);
  if (m.decode_sound(0, 1000) !== uBytes / 33) throw new Error('decode failed at ' + uOff);
  if (xf && lp) m.crossfade(m.out_ptr(), n, lp[0], lp[1], xf);
  U.set(new Uint8Array(m.memory.buffer, m.out_ptr(), uBytes), uOff); ++mp3n;
}
fs.writeFileSync(outFile, U);
console.log(`${outFile}: ${U.length} bytes, ${mp3n} MP3 sounds decoded, ${lossless} lossless`);
