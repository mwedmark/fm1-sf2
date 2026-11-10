// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
// pack_bank.js (the converter's writer of the live cache's packed bank) against the C decoder of fm1_open/live_cache on WebAssembly:
// a bank is made as BFP8 (index.html's converter), packed, and every sound is unpacked again from the packed file alone, as the engine would:
// the header and pack table are read independently of pack_bank.js, a codec 2 entry is decoded from the stream by the C code (and its loop
// crossfaded), a codec 0 entry is copied. Then the unpacked sound is compared with the BFP8 sound the bank held.
//   node test_pack_bank.js [font.sf2]      (default 1mgm.sf2; ffmpeg with libmp3lame and a clang for wasm32 are needed, as for test_sound_cache.js)
//   node test_pack_bank.js font.sf2 --packed=bank.fm1b    checks a bank the converter page packed from that font (any bit rate) instead
const fs = require('fs'), path = require('path'), { execFileSync } = require('child_process');
const root = __dirname, fontFile = process.argv.slice(2).find(a => !a.startsWith('--')) || path.join(root, '1mgm.sf2');
const packedFile = (process.argv.find(a => a.startsWith('--packed=')) || '').slice(9);
const { pack, unpackBfp8 } = require('./pack_bank.js');
const wasiClang = path.join(root, 'fm1_open/build/tools/wasi-sdk-27.0-x86_64-windows/bin/clang.exe');
const clang = process.env.FM1_CLANG || (fs.existsSync(wasiClang) ? wasiClang : 'clang');
const app = path.join(root, 'fm1_open/live_cache'), dir = path.join(root, 'test_sound_cache'), wasmPath = path.join(dir, 'harness.wasm');
const sources = [path.join(dir, 'harness.c'), path.join(app, 'mp3_sound.c'), path.join(app, 'sound_cache.c'), path.join(dir, 'libc_min.c')];
if (!fs.existsSync(wasmPath) || sources.concat(path.join(app, 'mp3_sound.h'), path.join(app, 'sound_cache.h')).some(s => fs.statSync(s).mtimeMs > fs.statSync(wasmPath).mtimeMs))
  execFileSync(clang, ['--target=wasm32', '-nostdlib', '-O2', '-Wall', '-Wextra', '-Wno-unused-function', '-ffreestanding', '-fno-builtin', '-I' + path.join(dir, 'stubs'), '-I' + app,
    '-I' + path.join(root, 'fm1_open/third_party/mp3'), '-Wl,--no-entry', '-Wl,-z,stack-size=262144', '-Wl,--initial-memory=8388608', ...sources, '-o', wasmPath], { stdio: 'inherit' });
const w = new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(wasmPath)), {}).exports;
let failed = 0;
const check = (ok, what) => { if (!ok) ++failed; console.log(ok ? 'ok  ' : 'FAIL', what); };

const html = fs.readFileSync(path.join(root, 'index.html'), 'utf8');
eval(html.match(/<script id="core">([\s\S]*?)<\/script>/)[1] + '\nglobalThis.api = { parseSf2, buildBank, writeFm1, bankInfo };');
const A = globalThis.api, b = fs.readFileSync(fontFile);
const sf = A.parseSf2(b.buffer.slice(b.byteOffset, b.byteOffset + b.length));
// a bank from the page: the same BFP8 bank, with the name and date its info block has (at 92 in the packed file, 32 and 20 bytes)
const given = packedFile ? new Uint8Array(fs.readFileSync(packedFile)) : null, text = (a, n) => String.fromCharCode(...given.subarray(a, a + n)).replace(/\0.*$/s, '');
const src = A.writeFm1(A.buildBank(sf, -2, { rate: 32000, bfp: true }), given ? { name: text(92, 32), date: text(124, 20) } : A.bankInfo('test.fm1b'));
const t0 = Date.now(), r = given ? { bytes: given, sounds: '?', lossless: '?' } : pack(new Uint8Array(src)), P = r.bytes, dv = new DataView(P.buffer, P.byteOffset), sdv = new DataView(src.buffer, src.byteOffset), u32 = o => dv.getUint32(o, true);
console.log(`${path.basename(fontFile)}: ${src.length} bytes as BFP8, ${P.length} packed (${((Date.now() - t0) / 1000).toFixed(0)} s); ${r.sounds} sounds, ${r.sounds - r.lossless} MP3, ${r.lossless} lossless`);

// ---- the header, read on its own
const oldHdr = sdv.getUint32(16, true) >= 80 && sdv.getUint32(76, true) ? 80 : sdv.getUint32(16, true), shift = 92 - oldHdr;
check(String.fromCharCode(...P.subarray(0, 4)) === 'FM1B' && dv.getUint16(4, true) === 2 && dv.getUint16(6, true) === 0x18 && u32(40) === P.length && u32(8) === 32000,
  'FM1B, version 2, flags BFP8 + packed (0x18), the file size and the rate');
const offPack = u32(80), nPack = u32(84), entry = u32(88), offSmp = u32(36);
check(entry === 24 && offPack + nPack * entry === P.length && offSmp === sdv.getUint32(36, true) + shift && offPack > offSmp, `pack table of ${nPack} entries of ${entry} bytes ends the file, the sample data starts at ${offSmp}`);
check([16, 20, 24, 28, 32].every(f => u32(f) === sdv.getUint32(f, true) + shift) && src.subarray(oldHdr, sdv.getUint32(36, true)).every((v, i) => {
  const a = oldHdr + i, zone = sdv.getUint32(32, true), nz = sdv.getUint16(14, true); const inZoneOffset = a >= zone && a < zone + nz * 24 && (a - zone) % 24 < 4;
  return inZoneOffset || P[92 + i] === v; }), 'the tables between the header and the samples are the bank\'s own bytes, the zones\' sample offsets moved by the header\'s growth');

// ---- every sound, unpacked from the packed file only
const zoneAt = (i, f) => dv.getUint32(u32(32) + i * 24 + f, true), nz = dv.getUint16(14, true);
const loops = new Map();                                   // unpacked offset -> [loop start, loop end] of a zone using it
for (let i = 0; i < nz; i++) if (P[u32(32) + i * 24 + 17] & 1) loops.set(zoneAt(i, 0), [zoneAt(i, 8), zoneAt(i, 12)]);
const frames = new Map(); for (let i = 0; i < nz; i++) frames.set(zoneAt(i, 0), zoneAt(i, 4));
const stream = P.subarray(offSmp, offPack), mem = () => new Uint8Array(w.memory.buffer);
check(stream.length <= 1 << 21, 'the sample data fits the harness\'s stream buffer');
// The decoder is given the stream up to the end of the last MP3 entry, not the sample data to the end of the file: minimp3 finds a frame only when the
// next frames match it or the buffer ends, and BFP8 bytes after the stream make the last 10 frames "not a frame" (found by this test).
let streamEnd = 0; for (let i = 0; i < nPack; i++) { const e = offPack + i * entry; if (P[e + 16] === 2) streamEnd = Math.max(streamEnd, u32(e + 8) + u32(e + 12)); }
mem().set(P.subarray(offSmp, offSmp + streamEnd), w.stream_ptr()); w.set_stream_bytes(streamEnd);
let prevEnd = 0, ordered = true, covered = true, sameLossless = true, nMp3 = 0, nLossless = 0, snrSum = 0, snrWorst = 1e9, loopOk = true, xfaded = 0;
const snr = (a, c) => { let s = 0, e = 0; for (let i = 0; i < a.length; i++) { s += a[i] * a[i]; e += (a[i] - c[i]) ** 2; } return 10 * Math.log10(Math.max(s, 1) / Math.max(e, 1e-9)); };
const idx = () => new Uint32Array(w.memory.buffer, w.index_ptr(), 4 * 1024);
for (let i = 0; i < nPack; i++) {
  const e = offPack + i * entry, uOff = u32(e), uBytes = u32(e + 4), pOff = u32(e + 8), pBytes = u32(e + 12), codec = P[e + 16], delay = dv.getUint16(e + 18, true), xf = dv.getUint16(e + 20, true);
  ordered = ordered && uOff >= prevEnd && uOff % 4 === 0; prevEnd = uOff + uBytes;
  const n = frames.get(uOff); if (n === undefined || (n + 31 >> 5) * 33 !== uBytes) { covered = false; continue; }
  const want = src.subarray(uOff - shift, uOff - shift + uBytes);                       // the bank's own BFP8 for the sound
  let got;
  if (codec === 0) { got = P.subarray(offSmp + pOff, offSmp + pOff + pBytes); sameLossless = sameLossless && pBytes === uBytes && Buffer.compare(Buffer.from(got), Buffer.from(want)) === 0; ++nLossless; continue; }
  if (codec !== 2) { covered = false; continue; }
  idx().set([pOff, delay, n, 0], 0);
  const lp = loops.get(uOff); new Uint32Array(w.memory.buffer, w.loop_ptr(), 3).set(xf && lp ? [lp[0], lp[1], xf] : [0, 0, 0]);
  const g = w.decode_sound(0, 1000);
  if (g !== uBytes / 33) { covered = false; console.log("  entry", i, "n", n, "pOff", pOff, "pBytes", pBytes, "delay", delay, "groups", g, "want", uBytes / 33); continue; }
  got = Uint8Array.from(new Uint8Array(w.memory.buffer, w.out_ptr(), uBytes));
  if (xf && lp) { new Uint8Array(w.memory.buffer, w.out_ptr(), uBytes).set(got); w.crossfade(w.out_ptr(), n, lp[0], lp[1], xf); got = Uint8Array.from(new Uint8Array(w.memory.buffer, w.out_ptr(), uBytes)); ++xfaded; }
  const a = unpackBfp8(want, 0, n), c = unpackBfp8(got, 0, n), q = snr(a, c); snrSum += q; snrWorst = Math.min(snrWorst, q); ++nMp3;
  if (pBytes > 0 && pOff + pBytes > offPack - offSmp) loopOk = false;
}
check(ordered && covered, `${nPack} entries in order of their offsets, each a sound a zone uses, with its exact BFP8 size, and each MP3 sound decodes to its size`);
check(sameLossless, `${nLossless} lossless entries are the bank's bytes exactly`);
check(nMp3 > 0 && snrSum / nMp3 > 10 && snrWorst > 0, `${nMp3} MP3 sounds against the BFP8 of the bank: mean ${(snrSum / Math.max(nMp3, 1)).toFixed(1)} dB, worst ${snrWorst.toFixed(1)} dB (${xfaded} with the loop crossfade)`);
check(loopOk, 'no MP3 entry reaches past the stream');
const budget = 466944;
console.log(`file ${P.length} bytes against ${budget} (${(P.length / budget * 100).toFixed(0)} %)`);
console.log(failed ? `${failed} FAILED` : 'all passed'); process.exit(failed ? 1 : 0);
