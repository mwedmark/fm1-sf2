// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
// The decoder and the cache of the live cache design (FORMAT.md, "Calculation: a live cache of decoded sounds"): fm1_open/live_cache/mp3_sound.c (minimp3 into BFP8) and
// sound_cache.c, the real C code on WebAssembly, freestanding. The sounds of a font (the 128 melodic presets and kit 0, at 32 kHz) are encoded by ffmpeg as one
// constant-bit-rate mono MP3 stream at 64 kbit/s and indexed as FORMAT.md's pack entries say; then
//   1. a sound decoded in slices of 1, 3 and 1000 MP3 frames is the same bytes, and sounds like what ffmpeg decodes of the same stream;
//   2. the BFP8 packer gives the converter's bytes (packBfp8 in index.html);
//   3. the cache: hits, the order sounds go in when it is full, pins, no room, no overlap, progress, sounds that play in place, a sound too big for the pool;
//   4. a song played through it: every sound a note finds is bit for bit what a decode of that sound gives, however often it has been dropped and decoded again.
// The stream is coded with the bit reservoir off (ffmpeg -reservoir 0) and a sound starts one MP3 frame before the frame it begins in: with the reservoir on, 4 frames
// are needed, and with 2, a quarter of the sounds decode to silence (measured; the reason this file says so).
//   node test_sound_cache.js [font.sf2] [song.mid]      (default 1mgm.sf2 and MIDIs/ABBA_-_Dancing_Queen.mid; ffmpeg and a clang for wasm32 are needed)
const fs = require('fs'), path = require('path'), { execFileSync } = require('child_process');
const root = __dirname;
const fontFile = process.argv[2] || path.join(root, '1mgm.sf2'), songFile = process.argv[3] || path.join(root, 'MIDIs/ABBA_-_Dancing_Queen.mid');
const wasiClang = path.join(root, 'fm1_open/build/tools/wasi-sdk-27.0-x86_64-windows/bin/clang.exe');
const clang = process.env.FM1_CLANG || (fs.existsSync(wasiClang) ? wasiClang : 'clang');
const app = path.join(root, 'fm1_open/live_cache'), dir = path.join(root, 'test_sound_cache');
const sources = [path.join(dir, 'harness.c'), path.join(app, 'mp3_sound.c'), path.join(app, 'sound_cache.c'), path.join(dir, 'libc_min.c'),
  path.join(app, 'mp3_sound.h'), path.join(app, 'sound_cache.h'), path.join(root, 'fm1_open/third_party/mp3/minimp3.h')];
const wasmPath = path.join(dir, 'harness.wasm');
if (!fs.existsSync(wasmPath) || sources.some(s => fs.statSync(s).mtimeMs > fs.statSync(wasmPath).mtimeMs))
  execFileSync(clang, ['--target=wasm32', '-nostdlib', '-O2', '-Wall', '-Wextra', '-Wno-unused-function', '-ffreestanding', '-fno-builtin', '-I' + path.join(dir, 'stubs'), '-I' + app,
    '-I' + path.join(root, 'fm1_open/third_party/mp3'), '-Wl,--no-entry', '-Wl,-z,stack-size=262144', '-Wl,--initial-memory=8388608', ...sources.slice(0, 4), '-o', wasmPath], { stdio: 'inherit' });
const w = new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(wasmPath)), {}).exports;
const mem = () => new Uint8Array(w.memory.buffer);
let failed = 0, checks = 0;
function check(ok, what) { ++checks; if (!ok) { ++failed; console.log('FAIL', what); } else console.log('ok  ', what); }

// ---- the font's sounds, as the converter makes them
const html = fs.readFileSync(path.join(root, 'index.html'), 'utf8');
const A = new Function(html.match(/<script id="core">([\s\S]*?)<\/script>/)[1] + '\nreturn { parseSf2, buildBank };')();
const b = fs.readFileSync(fontFile), sf = A.parseSf2(b.buffer.slice(b.byteOffset, b.byteOffset + b.length));
const picked = sf.presets.map((p, i) => (p.bank === 0 || (p.bank === 128 && p.preset === 0)) ? i : -1).filter(i => i >= 0);
const bank = A.buildBank(sf, picked, { rate: 32000, bits: 16 });
const melodic = new Map(), kit = [], soundId = new Map(), sounds = [];
for (const p of bank.presets) { const zones = bank.zones.slice(p.first, p.first + p.count); if (p.bank === 0) melodic.set(p.preset, zones); else kit.push(...zones); }
for (const z of bank.zones) if (!soundId.has(z.sample)) { soundId.set(z.sample, sounds.length); sounds.push({ pcm: Int16Array.from(z.sample.pcm), loop: z.loop, loopStart: z.loopStart, loopEnd: z.loopEnd }); }
const offsets = []; let total = 0; for (const s of sounds) { offsets.push(total); total += s.pcm.length; }
const all = new Int16Array(total); sounds.forEach((s, i) => all.set(s.pcm, offsets[i]));
const run = (args, input) => execFileSync('ffmpeg', ['-v', 'error', ...args], { input, maxBuffer: 1 << 28 });
const stream = run(['-f', 's16le', '-ar', '32000', '-ac', '1', '-i', '-', '-c:a', 'libmp3lame', '-b:a', '64k', '-reservoir', '0', '-write_xing', '0', '-id3v2_version', '0', '-f', 'mp3', '-'], Buffer.from(all.buffer));
const ffmpegPcm = (() => { const d = run(['-f', 'mp3', '-i', '-', '-ar', '32000', '-f', 's16le', '-'], stream); return new Int16Array(d.buffer.slice(d.byteOffset, d.byteOffset + d.length)); })();
if (stream.length % 288 || stream[0] !== 0xff) throw new Error('the stream must be whole 288-byte frames from the first byte (ffmpeg writes an ID3 tag unless -id3v2_version 0)');
const FRAME_BYTES = 288, FRAME = 1152, LEAD = 1, XFADE = 256;
console.log(`${path.basename(fontFile)}: ${sounds.length} sounds, ${(total / 32000).toFixed(1)} s, MP3 stream ${stream.length} bytes (${(stream.length / (total / 32000)).toFixed(0)} per second)`);
mem().set(stream, w.stream_ptr()); w.set_stream_bytes(stream.length);
const idx = () => new Uint32Array(w.memory.buffer, w.index_ptr(), 4 * 1024);
const loopTable = () => new Uint32Array(w.memory.buffer, w.loop_ptr(), 3 * 1024);
function setIndex(delay) {
  sounds.forEach((s, i) => loopTable().set(s.loop && s.loopStart >= 64 ? [s.loopStart, s.loopEnd, XFADE] : [0, 0, 0], i * 3));
  sounds.forEach((s, i) => { const t0 = offsets[i] + delay, f0 = Math.max(0, Math.floor(t0 / FRAME) - LEAD);
    idx().set([f0 * FRAME_BYTES, t0 - f0 * FRAME, s.pcm.length, 0], i * 4); });
}
// the codec's delay: decode the stream's start with nothing thrown away and line it up with the source
idx().set([0, 0, 12000, 0], 0); w.decode_sound(0, 1000);
const first = new Int8Array(w.memory.buffer, w.out_ptr(), 33 * 100);
const bfp = (bytes, groups) => { const o = new Float64Array(groups * 32); for (let g = 0; g < groups; g++) { const sh = bytes[g * 33]; for (let i = 0; i < 32; i++) o[g * 32 + i] = (bytes[g * 33 + 1 + i] << 24 >> 24) * (1 << sh); } return o; };
const dec0 = bfp(new Uint8Array(w.memory.buffer, w.out_ptr(), 33 * 375), 375);
let delay = 0, best = -1e30;
for (let lag = 0; lag < 2400; lag++) { let c = 0; for (let i = 0; i < 6000; i++) c += dec0[lag + i] * all[i]; if (c > best) { best = c; delay = lag; } }
console.log(`codec delay found: ${delay} samples`);
setIndex(delay);

const packRef = g => { let sh = 0; const fits = s => g.every(v => { const m = Math.round(v / (1 << s)); return m >= -128 && m <= 127; }); while (sh < 8 && !fits(sh)) sh++;
  return [sh, ...g.map(v => Math.max(-128, Math.min(127, Math.round(v / (1 << sh)))) & 255)]; };
const snr = (a, c) => { let s = 0, e = 0; for (let i = 0; i < a.length; i++) { s += a[i] * a[i]; e += (a[i] - c[i]) ** 2; } return 10 * Math.log10(s / Math.max(e, 1e-9)); };
const decoded = (sound, step) => { const g = w.decode_sound(sound, step); return g < 0 ? null : Uint8Array.from(new Uint8Array(w.memory.buffer, w.out_ptr(), g * 33)); };
const decodedFinal = sound => { const d = decoded(sound, 1000); if (!d) return d; const [ls, le, xf] = loopTable().subarray(sound * 3, sound * 3 + 3);
  if (xf) { new Uint8Array(w.memory.buffer, w.out_ptr(), d.length).set(d); w.crossfade(w.out_ptr(), sounds[sound].pcm.length, ls, le, xf); return Uint8Array.from(new Uint8Array(w.memory.buffer, w.out_ptr(), d.length)); } return d; };
const expectBytes = sound => (sounds[sound].pcm.length + 31 >> 5) * 33;

// ---- 1. the decoder
const spread = [0, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144].filter(i => i < sounds.length);
let worst = 1e9, sum = 0, same = true, bytesRight = true;
for (const s of spread) {
  const a = decoded(s, 1000), c = decoded(s, 1), d = decoded(s, 3);
  if (!a || !c || !d) { same = false; continue; }
  bytesRight = bytesRight && a.length === expectBytes(s);
  same = same && Buffer.compare(Buffer.from(a), Buffer.from(c)) === 0 && Buffer.compare(Buffer.from(a), Buffer.from(d)) === 0;
  const ref = ffmpegPcm.subarray(offsets[s] + delay, offsets[s] + delay + sounds[s].pcm.length), mine = bfp(a, a.length / 33).subarray(0, ref.length);
  const q = snr(ref, mine); worst = Math.min(worst, q); sum += q;
}
check(bytesRight && same, `a sound decoded 1, 3 or 1000 MP3 frames at a time is the same bytes, and (length + 31) / 32 * 33 bytes (${spread.length} sounds)`);
check(worst > 33 && sum / spread.length > 40, `against ffmpeg's decode of the same stream: worst ${worst.toFixed(1)} dB, mean ${(sum / spread.length).toFixed(1)} dB (BFP8's own noise is about 44 dB)`);
const eager = decoded(0, 1000), minimal = (() => { w.decode_sound(0, 1); return w.last_frames_decoded(); })();
check(eager !== null && spread.every(s => { const a = decoded(s, 1000); return a && a.some(v => v) ; }), 'with the reservoir off and one lead-in frame every sound decodes, none to silence');

// ---- 2. the packer
let rng = 12345; const rnd = () => (rng = (rng * 1664525 + 1013904223) >>> 0) / 4294967296;
let packOk = true;
for (let t = 0; t < 3000; t++) {
  const amp = [1, 20, 120, 255, 1000, 6000, 32767][Math.floor(rnd() * 7)], g = Array.from({ length: 32 }, () => Math.round((rnd() * 2 - 1) * amp));
  if (t % 50 === 0) g[Math.floor(rnd() * 32)] = rnd() < .5 ? -32768 : 32767;
  new Int16Array(w.memory.buffer, w.out_ptr(), 32).set(g); w.pack_group(w.out_ptr(), w.out_ptr() + 128);
  const got = Array.from(new Uint8Array(w.memory.buffer, w.out_ptr() + 128, 33));
  if (JSON.stringify(got) !== JSON.stringify(packRef(g))) { packOk = false; console.log('  group', g.join(','), 'got', got.join(','), 'want', packRef(g).join(',')); break; }
}
check(packOk, 'the BFP8 packer gives the converter\'s bytes for 3000 random groups (every amplitude up to the extremes)');

// ---- 3. the cache
const sizeOf = s => (expectBytes(s) + 3) & ~3;
const byBytes = [...sounds.keys()].sort((x, y) => sizeOf(x) - sizeOf(y));
const mid = Math.floor(byBytes.length * .5), [sc, sa, sb] = [byBytes[mid - 1], byBytes[mid], byBytes[mid + 1]];   // sc is not bigger than sa, sb is not smaller
const room = sizeOf(sa) + sizeOf(sb) + 16;
w.cache_init(room);
const pool = () => new Uint8Array(w.memory.buffer, w.pool_ptr(), room);
let ea = w.cache_request(sa);
check(ea >= 0 && w.cache_entry_pins(ea) === 1 && w.cache_entry_state(ea) === 1 && w.cache_pending() === 1, 'a request starts a decode, pins the sound once, and the sound is being decoded');
let prev = 0, grew = true, firstStep = true;
while (!w.cache_ready(ea)) { w.cache_step(1); const g = w.cache_groups(ea); if (g < prev) grew = false; prev = g; if (w.cache_pending() === 0 && !w.cache_ready(ea)) break; }
check(grew && w.cache_groups(ea) === sounds[sa].pcm.length + 31 >> 5, `the groups ready only grow while it is decoded, and are all of them at the end (${w.cache_groups(ea)})`);
check(Buffer.compare(Buffer.from(pool().subarray(w.cache_entry_offset(ea), w.cache_entry_offset(ea) + expectBytes(sa))), Buffer.from(decodedFinal(sa))) === 0, 'the sound in the pool is what a decode of it gives (the loop crossfade included)');
const again = w.cache_request(sa);
check(again === ea && w.cache_entry_pins(ea) === 2 && w.cache_stat(0) === 1, 'asking again is a hit: the same entry, pinned twice');
w.cache_unpin(ea); w.cache_unpin(ea);
let eb = w.cache_request(sb); w.cache_step(100000);
check(w.cache_ready(eb) && w.cache_entry_offset(eb) !== w.cache_entry_offset(ea), 'a second sound goes in the room that is left');
w.cache_unpin(eb);
let ec = w.cache_request(sc);                              // sa is the least recently used
check(ec >= 0 && w.cache_entry_state(ea) === 0 && w.cache_entry_state(eb) !== 0 && w.cache_stat(2) >= 1, 'a third sound that does not fit drops the least recently used one that is not pinned (a, not b)');
w.cache_step(100000);
const pinB = eb;                                           // pin b and c: nothing can be dropped for a
w.cache_request(sb); const noRoomBefore = w.cache_stat(3);
const eA = w.cache_request(sa);
check(eA === -1 && w.cache_stat(3) === noRoomBefore + 1, 'with everything pinned a request gets no room (-1) and nothing is dropped');
check(w.cache_entry_state(pinB) === 2 && w.cache_entry_state(ec) === 2, 'the pinned sounds are still there');
w.cache_unpin(ec); w.cache_unpin(pinB); w.cache_unpin(pinB);
const eA2 = w.cache_request(sa); w.cache_step(100000);
check(eA2 >= 0 && w.cache_ready(eA2) && Buffer.compare(Buffer.from(pool().subarray(w.cache_entry_offset(eA2), w.cache_entry_offset(eA2) + expectBytes(sa))), Buffer.from(decodedFinal(sa))) === 0,
  'unpinned, it goes in, and the sound that has been dropped and decoded again is the same bytes');
w.cache_unpin(eA2);
check(w.cache_request(byBytes[byBytes.length - 1]) === -1 || sizeOf(byBytes[byBytes.length - 1]) <= room, 'a sound bigger than the whole pool gets no room');
// the loop crossfade: the C code against the same blend done on the decoded samples
const looped = [...sounds.keys()].filter(i => loopTable()[i * 3 + 2] && sounds[i].loopEnd - sounds[i].loopStart > 600).slice(0, 12);
let xfOk = looped.length > 0, jump = [];
for (const s of looped) {
  const raw = decoded(s, 1000), [ls, le, xf] = loopTable().subarray(s * 3, s * 3 + 3), pcm = bfp(raw, raw.length / 33), want = Float64Array.from(pcm);
  for (let i = 0; i < xf; i++) want[le - xf + i] = Math.floor((pcm[le - xf + i] * (xf - i) + pcm[ls - xf + i] * (i + 1) + Math.floor((xf + 1) / 2)) / (xf + 1));
  const mine = decodedFinal(s), got = bfp(mine, mine.length / 33);
  let maxErr = 0; for (let i = 0; i < le; i++) maxErr = Math.max(maxErr, Math.abs(got[i] - want[i]));
  const untouched = Buffer.compare(Buffer.from(mine.subarray(0, ((le - xf) >> 5) * 33)), Buffer.from(raw.subarray(0, ((le - xf) >> 5) * 33))) === 0;
  // the step from the last sample of the loop to the first of it, against the steps inside the loop
  const stepIn = (a, i) => Math.abs(a[i + 1] - a[i]); let big = 0; for (let i = ls; i < le - 1; i += 1) big = Math.max(big, stepIn(got, i));
  jump.push([Math.abs(got[ls] - got[le - 1]), big, Math.abs(pcm[ls] - pcm[le - 1])]);
  if (!untouched || maxErr > 2 ** 9) xfOk = false;      // a regrouped BFP8 group may differ by its own step (up to 256 for a loud group)
}
check(xfOk, `the loop crossfade: ${looped.length} looped sounds, nothing before the blended groups is touched, the blended samples are the formula's (to one BFP8 step of the regrouping)`);
check(jump.every(([after, inside]) => after <= Math.max(inside, 8) * 1.01 + 8), 'after the crossfade the step from the loop end to the loop start is no larger than the largest step inside the loop (' + jump.filter(([a, i, b]) => a < b).length + ' of ' + jump.length + ' closed better than the decoder left them)');
// groups held back: a looped sound is not ready in its last groups before it is done
w.cache_init(1 << 20);
// a sound is only visible in pieces before it is done if its last MP3 frame holds few of its samples: find one that ends 1 to 200 samples into a frame
const ls0 = [...sounds.keys()].find(i => loopTable()[i * 3 + 2] && sounds[i].pcm.length > 3000 && (offsets[i] + delay + sounds[i].pcm.length) % FRAME >= 1 && (offsets[i] + delay + sounds[i].pcm.length) % FRAME <= 200);
if (ls0 === undefined) console.log('  (no looped sound in this font ends just inside an MP3 frame: the held-back groups are not tried)');
else {
  const el = w.cache_request(ls0), hold = Math.floor((loopTable()[ls0 * 3 + 2] + 31) / 32) + 1, total = sounds[ls0].pcm.length + 31 >> 5; let early = true, sawHeld = false;
  while (!w.cache_ready(el)) { w.cache_step(1); const g = w.cache_groups(el); if (!w.cache_ready(el)) { if (g > total - hold) early = false; if (g > 0) sawHeld = true; } }
  check(early && sawHeld && w.cache_groups(el) === total, `a sound that ends ${(offsets[ls0] + delay + sounds[ls0].pcm.length) % FRAME} samples into its last MP3 frame: the last ${hold} groups, which the loop crossfade changes, are not ready before it is done`);
  w.cache_unpin(el);
}

w.cache_init(room);
// the sounds that play in place
const dsound = byBytes[0]; idx()[dsound * 4 + 3] = 64;
const dres = w.cache_request(dsound);
check(dres === -2 && w.cache_pending() === 0, 'a sound stored as BFP8 plays in place: the cache pins nothing and decodes nothing');
idx()[dsound * 4 + 3] = 0;
// a long random sequence: no overlap, inside the pool, every ready sound right
w.cache_init(1 << 18);
let integrity = true, overlap = false, pins = [], steps = 0;
for (let n = 0; n < 4000; n++) {
  while (pins.length && rnd() < .5) w.cache_unpin(pins.shift());
  const s = Math.floor(rnd() * sounds.length), e = w.cache_request(s);
  if (e >= 0) pins.push(e);
  if (rnd() < .7) w.cache_step(1 + Math.floor(rnd() * 20));
  if (n % 40 === 0) {
    const live = []; for (let i = 0; i < w.cache_entries(); i++) if (w.cache_entry_state(i)) live.push([w.cache_entry_offset(i), w.cache_entry_bytes(i), i]);
    live.sort((x, y) => x[0] - y[0]);
    for (let i = 0; i < live.length; i++) { if (live[i][0] + live[i][1] > (1 << 18) || (i && live[i - 1][0] + live[i - 1][1] > live[i][0])) overlap = true; }
  }
}
w.cache_step(1000000);
for (let i = 0; i < w.cache_entries(); i++) if (w.cache_entry_state(i) === 2) {
  const s = w.cache_entry_sound(i), have = new Uint8Array(w.memory.buffer, w.pool_ptr() + w.cache_entry_offset(i), expectBytes(s)), want = decodedFinal(s);
  if (Buffer.compare(Buffer.from(have), Buffer.from(want)) !== 0) integrity = false;
}
check(!overlap && integrity, `4000 random requests with random pins and slices: no two sounds overlap or leave the pool, and every sound left in it is right (${w.cache_stat(1)} decodes, ${w.cache_stat(2)} dropped, ${w.cache_stat(3)} without room)`);

// ---- 4. a song through it
function parseMidi(file) {
  const d = fs.readFileSync(file), ntrk = d.readUInt16BE(10), division = d.readUInt16BE(12); let pos = 8 + d.readUInt32BE(4); const events = [], tempos = [];
  for (let t = 0; t < ntrk && pos + 8 <= d.length; t++) { const len = d.readUInt32BE(pos + 4); let i = pos + 8; const end = i + len; pos = end; let tick = 0, status = 0;
    const vlq = () => { let v = 0, c; do { c = d[i++]; v = (v << 7) | (c & 0x7f); } while (c & 0x80); return v; };
    while (i < end) { tick += vlq(); let s = d[i]; if (s & 0x80) { status = s; ++i; } else s = status;
      if (s === 0xff) { const type = d[i++], n = vlq(); if (type === 0x51 && n === 3) tempos.push({ tick, us: (d[i] << 16) | (d[i + 1] << 8) | d[i + 2] }); i += n; }
      else if (s === 0xf0 || s === 0xf7) i += vlq();
      else { const hi = s & 0xf0, ch = s & 15, a = d[i++], bb = hi === 0xc0 || hi === 0xd0 ? 0 : d[i++]; if (hi === 0x90 || hi === 0x80 || hi === 0xc0) events.push({ tick, hi: hi === 0x90 && bb === 0 ? 0x80 : hi, ch, a, b: bb }); } } }
  tempos.sort((x, y) => x.tick - y.tick); if (!tempos.length || tempos[0].tick > 0) tempos.unshift({ tick: 0, us: 500000 });
  const sec = tick => { let s = 0, last = tempos[0]; for (const t of tempos) { if (t.tick >= tick) break; s += (t.tick - last.tick) * last.us / 1e6 / division; last = t; } return s + (tick - last.tick) * last.us / 1e6 / division; };
  events.sort((x, y) => x.tick - y.tick); const prog = new Array(16).fill(0), open = new Map(), notes = [];
  for (const e of events) { const t = sec(e.tick);
    if (e.hi === 0xc0) prog[e.ch] = e.a;
    else if (e.hi === 0x90) { const k = e.ch * 128 + e.a; if (!open.has(k)) open.set(k, []); open.get(k).push({ t0: t, ch: e.ch, note: e.a, program: prog[e.ch] }); }
    else { const q = open.get(e.ch * 128 + e.a); if (q && q.length) { const n = q.shift(); n.t1 = t; notes.push(n); } } }
  const lastT = events.length ? sec(events[events.length - 1].tick) : 0; for (const q of open.values()) for (const n of q) { n.t1 = Math.min(lastT, n.t0 + 4); notes.push(n); }
  return notes.sort((x, y) => x.t0 - y.t0);
}
const notes = parseMidi(songFile);
let bestT = 8, bestN = -1; const lastNote = Math.max(...notes.map(n => n.t0));
for (let t = 8; t < lastNote - 30; t += 1) { const c = notes.filter(n => n.t0 >= t && n.t0 < t + 30).length; if (c > bestN) { bestN = c; bestT = t; } }
const window = notes.filter(n => n.t0 >= bestT && n.t0 < bestT + 30);
const reference = new Map();
const refOf = s => { if (!reference.has(s)) reference.set(s, decodedFinal(s)); return reference.get(s); };
const DECODE_SPEED = 30 * 32000 / FRAME;                     // MP3 frames a second: 30 times real time, an assumption
console.log(`\n${path.basename(songFile)}: ${window.length} notes in the 30 s from ${bestT.toFixed(0)} s; decoding at ${(DECODE_SPEED / (32000 / FRAME)).toFixed(0)} times real time (assumed)`);
for (const kb of [128, 192, 256]) {
  w.cache_init(kb * 1024);
  let pinned = [], hits = 0, found = 0, wrong = 0, lost = 0, late = 0, lastT = bestT, credit = 0, maxPending = 0, checked = 0;
  for (const n of window) {
    credit += (n.t0 - lastT) * DECODE_SPEED; lastT = n.t0;
    const run = Math.floor(credit); credit -= run; if (run) w.cache_step(run);
    pinned = pinned.filter(p => p.until >= n.t0 || (w.cache_unpin(p.entry), false));
    const zones = n.ch === 9 ? kit : melodic.get(n.program) || [];
    for (const z of zones) {
      if (n.note < z.lo || n.note > z.hi) continue;
      const s = soundId.get(z.sample), e = w.cache_request(s);
      if (e < 0) { ++lost; continue; }
      const seconds = sounds[s].pcm.length / 32000;
      pinned.push({ entry: e, until: n.t0 + (n.ch === 9 || (!z.loop && seconds < n.t1 - n.t0 + .4) ? seconds : n.t1 - n.t0 + .4) });
      if (w.cache_ready(e)) { ++found; ++checked; const got = Buffer.from(new Uint8Array(w.memory.buffer, w.cache_data(e), expectBytes(s))); if (Buffer.compare(got, Buffer.from(refOf(s))) !== 0) ++wrong; }
      else if (w.cache_groups(e) < 4 * FRAME / 32) ++late;
    }
    maxPending = Math.max(maxPending, w.cache_pending());
  }
  w.cache_step(1 << 30);
  console.log(`  pool ${String(kb).padStart(3)} KB: ${w.cache_stat(0)} hits, ${w.cache_stat(1)} decodes, ${w.cache_stat(2)} dropped, ${lost} sounds without room, ${late} found with under 4 MP3 frames decoded, most waiting at once ${maxPending}, ${checked} ready sounds checked`);
  check(wrong === 0, `pool ${kb} KB: every sound that was ready when a note found it is bit for bit a decode of that sound (${checked} checked, ${w.cache_stat(2)} dropped and decoded again during the song)`);
}
console.log(failed ? `${failed} of ${checks} checks FAILED` : `all ${checks} checks passed`);
process.exit(failed ? 1 : 0);
