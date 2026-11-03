// The zone values the converter takes from the SoundFont besides pan: the level (initial attenuation, generator 48), the exclusive
// class (57: a closed hi-hat cuts the open one) and the scale tuning (56: cents a key). First the converter against the SoundFont
// itself, then the real fm1b.c on WebAssembly with banks where one zone's values are changed.
//   node test_zone.js [1mgm.sf2]
const fs = require('fs'), path = require('path'), { execFileSync } = require('child_process');
const root = __dirname;
const sdk = path.join(root, 'fm1_open/build/tools/wasi-sdk-27.0-x86_64-windows');
const wasmPath = path.join(root, 'test_mt/mt_test.wasm');
const sources = [path.join(root, 'test_mt/wrap.c'), path.join(root, 'fm1_open/app/fm1b.c'), path.join(root, 'fm1_open/web/synth/sea_decode.c')];
const headers = ['fm1b.h', 'fm1b_config.h', 'fm1b_dqt.inc', 'fm1b_dqt6.inc'].map(h => path.join(root, 'fm1_open/app', h));
if (!fs.existsSync(wasmPath) || [...sources, ...headers].some(s => fs.statSync(s).mtimeMs > fs.statSync(wasmPath).mtimeMs))
  execFileSync(path.join(sdk, 'bin/clang.exe'), ['--sysroot=' + path.join(sdk, 'share/wasi-sysroot'), '-O1', '-DFM1_HOST_TEST', '-mexec-model=reactor', '-Wl,--no-entry',
    '-Wl,--export-dynamic', ...['fm1_open/app', 'fm1_open/app/synth', 'fm1_open/sdk/include'].map(d => '-I' + path.join(root, d)), ...sources, '-o', wasmPath], { stdio: 'inherit' });
const w = new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(wasmPath)), { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }) }).exports;
if (w._initialize) w._initialize();

const html = fs.readFileSync(path.join(root, 'index.html'), 'utf8');
eval(html.match(/<script id="core">([\s\S]*?)<\/script>/)[1] + '\nglobalThis.api = { parseSf2, presetZones, buildBank, writeFm1, writeSf2 };');
const A = globalThis.api;
const b = fs.readFileSync(process.argv[2] || path.join(root, '1mgm.sf2'));
const sf = A.parseSf2(b.buffer.slice(b.byteOffset, b.byteOffset + b.length));
let failed = 0, checks = 0;
function check(ok, what) { ++checks; if (!ok) { ++failed; console.log('FAIL', what); } else console.log('ok  ', what); }

// ---- 1. The converter ----
const bank = A.buildBank(sf, -2, { rate: 32000, bits: 16 });
const bin = A.writeFm1(bank), dv = new DataView(bin.buffer);
const nz = bank.zones.length, offZone = dv.getUint32(32, true), offExtra = dv.getUint32(48, true);
check(dv.getUint32(16, true) === 52 && offExtra === offZone + 24 * nz && dv.getUint32(44, true) === offExtra + 4 * nz,
  'the header is 52 bytes; the zone levels follow the zones, 4 bytes each, and the envelopes follow them');
// The bank's zones in the order buildBank adds them: every preset's zones from presetZones, as the SoundFont has them.
const source = sf.presets.flatMap((p, i) => A.presetZones(sf, i));
check(source.length === nz, `one bank zone per SoundFont zone (${nz})`);
const extra = i => ({ atten: bin[offExtra + i * 4], excl: bin[offExtra + i * 4 + 1], scale: dv.getUint16(offExtra + i * 4 + 2, true) });
let levelOk = 0, exclOk = 0, scaleOk = 0, attenuated = 0, classes = 0, scaled = 0;
source.forEach((s, i) => {
  const x = extra(i);
  levelOk += x.atten === Math.min(255, Math.round(s.atten * 0.4 / 5));
  exclOk += x.excl === s.excl;
  scaleOk += x.scale === s.scale;
  attenuated += x.atten > 0; classes += x.excl > 0; scaled += x.scale !== 100;
});
check(levelOk === nz && attenuated > 200, `every zone's level is the SoundFont's attenuation x 0.4 in 0.5 dB steps (${attenuated} zones below full level)`);
check(exclOk === nz && classes > 0, `every zone keeps its exclusive class (${classes} zones in a class)`);
check(scaleOk === nz && scaled > 0, `every zone keeps its scale tuning (${scaled} zones not at 100 cents a key)`);
// The pitch the player computes, (note - root) * scale + fine, against the SoundFont's (note - root key) * scale + tuning.
let pitchBad = 0, pitchChecked = 0, worst = 0;
source.forEach((s, i) => {
  const z = bank.zones[i];
  for (let n = z.lo; n <= z.hi; ++n) {
    const want = (n - s.root) * s.scale + s.cents, got = (n - z.root) * z.scale + z.fine;
    if (z.root === 0 || z.root === 127) continue;    // a root clamped at the ends of the keyboard
    ++pitchChecked; worst = Math.max(worst, Math.abs(want - got));
    if (Math.abs(want - got) > 0.5) ++pitchBad;
  }
});
check(pitchBad === 0, `the root and fine tune give every key the SoundFont's pitch, at 100 cents a key and the others (${pitchChecked} keys, worst ${worst.toFixed(2)} cent)`);
// The hi-hats of the GM kit share a class: closed (42), pedal (44) and open (46).
const kit = bank.presets.find(p => p.bank === 128 && p.preset === 0);
const kitZone = n => bank.zones.slice(kit.first, kit.first + kit.count).findIndex(z => z.lo <= n && n <= z.hi) + kit.first;
const hatClasses = [42, 44, 46].map(n => extra(kitZone(n)).excl);
check(hatClasses[0] > 0 && hatClasses.every(c => c === hatClasses[0]), `the kit's hi-hats 42, 44 and 46 share exclusive class ${hatClasses[0]}`);
// The simplified SF2 the converter writes keeps the values: converted again, it is the same bank.
const sf2 = A.writeSf2(bank, 'zone test'), back = A.parseSf2(sf2.buffer.slice(sf2.byteOffset, sf2.byteOffset + sf2.byteLength));
const again = A.writeFm1(A.buildBank(back, -2, { rate: 32000, bits: 16 }));
const zonesOf = x => { const d = new DataView(x.buffer, x.byteOffset); const o = d.getUint32(32, true), e = d.getUint32(48, true), n = d.getUint16(14, true);
  return Array.from({ length: n }, (_, i) => [x[o + i * 24 + 16], d.getInt16(o + i * 24 + 20, true), x[e + i * 4], x[e + i * 4 + 1], d.getUint16(e + i * 4 + 2, true)].join()); };
const z1 = zonesOf(bin), z2 = zonesOf(again);
check(z1.length === z2.length && z1.every((v, i) => v === z2[i]), 'the SF2 the converter writes keeps root, fine tune, level, class and scale tuning');

// ---- 2. The player ----
const BLOCK = 128;
function load(image) {
  new Uint8Array(w.memory.buffer, w.mt_bank(), image.length).set(image);
  if (w.mt_bind(image.length) !== 0) throw new Error('bank rejected');
  w.mt_block();
}
function render(blocks) {
  const all = new Int32Array(blocks * BLOCK);
  for (let k = 0; k < blocks; ++k) {
    w.mt_block();
    all.set(new Int32Array(w.memory.buffer, w.mt_out(), BLOCK), k * BLOCK);
  }
  return all;
}
const sumAbs = a => a.reduce((s, v) => s + Math.abs(v), 0);
const same = (a, x) => a.length === x.length && a.every((v, i) => v === x[i]);
// A copy of the bank with zone i's level, class and scale tuning set.
function patched(image, changes) {
  const c = image.slice(), d = new DataView(c.buffer), e = d.getUint32(48, true);
  for (const [i, v] of changes) {
    if (v.atten !== undefined) c[e + i * 4] = v.atten;
    if (v.excl !== undefined) c[e + i * 4 + 1] = v.excl;
    if (v.scale !== undefined) d.setUint16(e + i * 4 + 2, v.scale, true);
  }
  return c;
}
const allZones = (v) => Array.from({ length: nz }, (_, i) => [i, v]);
const neutral = patched(bin, allZones({ atten: 0, excl: 0, scale: 100 }));
const old = bin.slice(); new DataView(old.buffer).setUint32(48, 0, true);    // the offset 0: a bank without the table
function play(image, program, notes, blocks = 300) {
  load(image);
  if (w.mt_program(0, program) !== 0) throw new Error('no program ' + program);
  for (const n of notes) w.mt_note(0, n, 100);
  return render(blocks);
}
const melodic = p => bank.presets.find(x => x.bank === 0 && x.preset === p);
const zoneOf = (p, n) => bank.zones.slice(p.first, p.first + p.count).findIndex(z => z.lo <= n && n <= z.hi) + p.first;

// A bank without the table (offset 0, or a 48-byte header from before it) plays as one whose zones are all at the defaults.
check(same(play(old, 0, [48, 60, 67]), play(neutral, 0, [48, 60, 67])), 'a bank without zone levels plays as one at full level, no class, 100 cents a key');
const older = path.join(root, 'dist/gm_32k_nopan.fm1b');
if (fs.existsSync(older)) {
  const o = fs.readFileSync(older);
  check(o.readUInt32LE(16) === 48, 'dist/gm_32k_nopan.fm1b is a bank from before the zone levels (48-byte header)');
  load(o); w.mt_program(0, 0); w.mt_note(0, 60, 100);
  check(sumAbs(render(100)) > 0, 'and it still binds and plays');
}

// Level: 12 steps are 6 dB, 2^(-1) to within 0.5 %; 255 is 127.5 dB down.
const FLUTE = melodic(73), fz = zoneOf(FLUTE, 72);
const full = sumAbs(play(patched(neutral, [[fz, { atten: 0 }]]), 73, [72]));
const half = sumAbs(play(patched(neutral, [[fz, { atten: 12 }]]), 73, [72]));
const ratio = half / full, want6 = Math.pow(10, -6 / 20);
check(full > 0 && Math.abs(ratio / want6 - 1) < 0.005, `a zone 6 dB down plays at ${ratio.toFixed(4)} of full level (${want6.toFixed(4)} wanted)`);
const quiet = sumAbs(play(patched(neutral, [[fz, { atten: 255 }]]), 73, [72]));
check(quiet < full * 1e-5, 'a zone 127.5 dB down is silent');
check(same(play(patched(neutral, [[fz, { atten: 12 }]]), 73, [60]), play(neutral, 73, [60])) || zoneOf(FLUTE, 60) === fz,
  'the level is the zone\'s own: another zone of the preset is not changed');
// The real bank: the zones the SoundFont attenuates sound quieter by exactly their level.
let realChecked = 0, realBad = 0;
for (const p of [0, 24, 40, 56, 73, 80].map(melodic)) {
  for (const n of [48, 60, 72]) {
    const i = zoneOf(p, n), a = extra(i).atten;
    if (!a) continue;
    const r = sumAbs(play(bin, p.preset, [n], 150)) / sumAbs(play(patched(bin, [[i, { atten: 0 }]]), p.preset, [n], 150));
    ++realChecked;
    if (Math.abs(r / Math.pow(10, -a / 40) - 1) > 0.01) realBad++, console.log('   program', p.preset, 'note', n, 'level', a, 'ratio', r);
  }
}
check(realChecked > 0 && realBad === 0, `the SoundFont's own levels: ${realChecked} attenuated notes of 6 programs play at their level`);

// Exclusive class: the open hi-hat is cut by the closed one, on the same channel, quickly.
const DRUMS = 9, OPEN = 46, CLOSED = 42;
function hats(image, closedChannel = DRUMS, gap = 20) {
  load(image);
  w.mt_drum(DRUMS, OPEN, 100);
  render(gap);
  const before = w.mt_voices();
  w.mt_drum(closedChannel, CLOSED, 100);
  render(4);                                         // 512 frames, 12 ms: the cut takes about 5 ms
  return [before, w.mt_voices()];
}
const [openAlone, withClass] = hats(bin);
check(openAlone === 1 && withClass === 1, `the closed hi-hat cuts the open one (voices ${openAlone} then ${withClass})`);
const noClass = patched(bin, [[kitZone(OPEN), { excl: 0 }], [kitZone(CLOSED), { excl: 0 }]]);
check(hats(noClass)[1] === 2, 'without the class both hi-hats sound');
check(hats(bin, 3)[1] === 2, 'a hi-hat on another channel does not cut it');
const otherClass = patched(bin, [[kitZone(OPEN), { excl: 9 }]]);
check(hats(otherClass)[1] === 2, 'a note of another class does not cut it');
load(bin);
w.mt_drum(DRUMS, OPEN, 100); render(20); w.mt_drum(DRUMS, OPEN, 100); render(4);
check(w.mt_voices() === 1, 'a second open hi-hat cuts the first');
// The cut is a fast fade, not a click: the open hat (the mix minus the closed hat alone) starts where it was and is gone in 3 blocks.
load(bin);
w.mt_drum(DRUMS, OPEN, 100); render(20);
const openOnly = render(8);
load(bin);
render(20); w.mt_drum(DRUMS, CLOSED, 100);
const closedOnly = render(8);
load(bin);
w.mt_drum(DRUMS, OPEN, 100); render(20); w.mt_drum(DRUMS, CLOSED, 100);
const faded = render(8).map((v, i) => v - closedOnly[i]);
const blockAbs = (a, k) => sumAbs(a.slice(k * BLOCK, (k + 1) * BLOCK));
const startsAt = Math.abs(faded[0] - openOnly[0]) <= 0.05 * Math.abs(openOnly[0]) + 1;   // no jump: it ramps from where it was
check(startsAt && blockAbs(faded, 0) < blockAbs(openOnly, 0) && blockAbs(faded, 1) < 0.01 * blockAbs(openOnly, 1) && sumAbs(faded.slice(2 * BLOCK)) === 0,
  `the open hi-hat ramps down from where it was and is gone after 2 blocks (6 ms): ${[0, 1, 2].map(k => (blockAbs(faded, k) / blockAbs(openOnly, k)).toFixed(3)).join(', ')} of its level`);

// Scale tuning: notes in one zone step by scale cents. A held flute note's period gives its pitch.
function period(a, from = 20000, n = 4096) {
  const x = Array.from(a.subarray(from, from + n + 1200));
  const r = [];
  for (let l = 40; l < 1200; ++l) { let s = 0; for (let i = 0; i < n; ++i) s += x[i] * x[i + l]; r[l] = s; }
  const top = Math.max(...r.slice(40));
  let lag = 0;
  for (let l = 41; l < 1199; ++l) if (r[l] > 0.9 * top && r[l] >= r[l - 1] && r[l] >= r[l + 1]) { lag = l; break; }
  const y0 = r[lag - 1], y1 = r[lag], y2 = r[lag + 1];
  return lag + 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2);
}
const cents = (pa, pb) => 1200 * Math.log2(pa / pb);   // how much higher b sounds than a, from their periods
const z60 = zoneOf(FLUTE, 60), keys = [];
for (let n = 0; n < 128; ++n) if (zoneOf(FLUTE, n) === z60) keys.push(n);
const lo = keys.includes(60) ? 60 : keys[0], hi = keys.filter(n => n > lo && n <= lo + 4).pop();   // up to 4 keys apart in one zone
for (const scale of [100, 50, 0, 200]) {
  const image = patched(neutral, [[z60, { scale }]]);
  const step = cents(period(play(image, 73, [lo], 400)), period(play(image, 73, [hi], 400)));
  // the period measure is good to a few cents; a key is 50 cents or more here
  check(Math.abs(step - (hi - lo) * scale) < 8, `scale tuning ${scale}: keys ${lo} and ${hi} are ${step.toFixed(1)} cents apart (${(hi - lo) * scale} wanted)`);
}
const atRoot = n => period(play(patched(neutral, [[z60, { scale: 50 }]]), 73, [n], 400));
const root60 = bank.zones[z60].root;
if (keys.includes(root60))
  check(Math.abs(cents(atRoot(root60), period(play(neutral, 73, [root60], 400)))) < 1, 'at the root key the scale tuning changes nothing');

console.log(failed ? `${failed} of ${checks} checks FAILED` : `${checks}/${checks} checks passed`);
process.exitCode = failed ? 1 : 0;
