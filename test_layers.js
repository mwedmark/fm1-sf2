// Stacked layers (FORMAT.md, "Layers"): every zone of a preset that covers a key sounds, not only the lowest. The converter
// against 1mgm.sf2 (the key map and the layer entries name every zone that covers each key), then the real fm1b.c on
// WebAssembly at 32 kHz with the BFP8 bank: a layered note is the exact sum of its zones alone, note off ends them all, the
// voice limit and the exclusive classes count layers right, and a bank from before the layers (68-byte header) plays one zone.
//   node test_layers.js [1mgm.sf2]
const fs = require('fs'), path = require('path'), { execFileSync } = require('child_process');
const root = __dirname;
const sdk = path.join(root, 'fm1_open/build/tools/wasi-sdk-27.0-x86_64-windows');
const sources = [path.join(root, 'test_mt/wrap.c'), path.join(root, 'fm1_open/app/fm1b.c'), path.join(root, 'fm1_open/web/synth/sea_decode.c')];
const headers = ['fm1b.h', 'fm1b_config.h', 'fm1b_dqt.inc', 'fm1b_dqt6.inc'].map(h => path.join(root, 'fm1_open/app', h));
function engine(rate) {
  const wasmPath = path.join(root, `test_mt/layers_${rate}.wasm`);
  if (!fs.existsSync(wasmPath) || [...sources, ...headers].some(s => fs.statSync(s).mtimeMs > fs.statSync(wasmPath).mtimeMs))
    execFileSync(path.join(sdk, 'bin/clang.exe'), ['--sysroot=' + path.join(sdk, 'share/wasi-sysroot'), '-O1', '-DFM1_HOST_TEST', `-DFM1B_RATE=${rate}U`,
      '-mexec-model=reactor', '-Wl,--no-entry', '-Wl,--export-dynamic',
      ...['fm1_open/app', 'fm1_open/app/synth', 'fm1_open/sdk/include'].map(d => '-I' + path.join(root, d)), ...sources, '-o', wasmPath], { stdio: 'inherit' });
  const w = new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(wasmPath)), { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }) }).exports;
  if (w._initialize) w._initialize();
  return w;
}
const html = fs.readFileSync(path.join(root, 'index.html'), 'utf8');
eval(html.match(/<script id="core">([\s\S]*?)<\/script>/)[1] + '\nglobalThis.api = { parseSf2, buildBank, writeFm1, FM1_SAMPLE_BANK };');
const A = globalThis.api;
let failed = 0, checks = 0;
function check(ok, what) { ++checks; if (!ok) { ++failed; console.log('FAIL', what); } else console.log('ok  ', what); }

// ---- 1. The converter ----
const b = fs.readFileSync(process.argv[2] || path.join(root, '1mgm.sf2'));
const sf = A.parseSf2(b.buffer.slice(b.byteOffset, b.byteOffset + b.length));
const bank = A.buildBank(sf, -2, { rate: 32000, bfp: true, bits: 16 });
const bin = A.writeFm1(bank), dv = new DataView(bin.buffer);
const np = dv.getUint16(12, true), offPre = dv.getUint32(24, true), offKey = dv.getUint32(28, true), offLayer = dv.getUint32(68, true), nLayer = dv.getUint32(72, true);
const presetOf = pi => ({ first: dv.getUint16(offPre + pi * 24, true), count: dv.getUint16(offPre + pi * 24 + 2, true) });
const entriesOf = pi => { const e = dv.getUint16(offLayer + pi * 4, true), n = dv.getUint16(offLayer + pi * 4 + 2, true);
  return Array.from({ length: n }, (_, i) => { const o = offLayer + 4 * np + 4 * (e + i); return { lo: bin[o], hi: bin[o + 1], k: bin[o + 2] }; }); };
check(dv.getUint32(16, true) === 76 && offLayer % 4 === 0 && offLayer === dv.getUint32(56, true) + 40 * dv.getUint32(60, true) &&
      dv.getUint32(36, true) === offLayer + 4 * np + 4 * nLayer,
  `the header is 76 bytes; offset 68 points at the layers (4 bytes a preset, ${nLayer} entries of 4), after the modulation records`);
// What the player starts for a note: the key map's zone, and every entry that covers the note and is not that zone.
let keysChecked = 0, keysBad = 0, stacked = 0, layeredPresets = 0;
for (let pi = 0; pi < np; ++pi) {
  const p = presetOf(pi), entries = entriesOf(pi), zones = bank.zones.slice(p.first, p.first + p.count);
  layeredPresets += entries.length > 0;
  for (let n = 0; n < 128; ++n) {
    const want = zones.map((z, k) => z.lo <= n && n <= z.hi ? k : -1).filter(k => k >= 0);
    const key = bin[offKey + pi * 128 + n];
    const got = key === 0xff ? [] : [key, ...entries.filter(e => e.lo <= n && n <= e.hi && e.k !== key).map(e => e.k)];
    ++keysChecked;
    stacked += got.length > 1;
    if (want.join() !== got.join()) { if (keysBad++ < 3) console.log('   preset', pi, 'note', n, 'want', want, 'got', got); }
  }
}
check(keysBad === 0 && stacked > 0, `every key starts every zone that covers it, the lowest first (${keysChecked} keys, ${stacked} stacked, in ${layeredPresets} presets)`);
check(bin.length <= A.FM1_SAMPLE_BANK, `the bank still fits the FM-1's sample flash: ${bin.length} of ${A.FM1_SAMPLE_BANK} bytes`);

// ---- 2. The player ----
const w = engine(32000), BLOCK = 128;
function load(image) {
  new Uint8Array(w.memory.buffer, w.mt_bank(), image.length).set(image);
  if (w.mt_bind(image.length) !== 0) throw new Error('bank rejected');
  w.mt_limit(w.mt_voice_max());
}
function render(blocks) {
  const all = new Int32Array(blocks * BLOCK);
  for (let k = 0; k < blocks; ++k) { w.mt_block(); all.set(new Int32Array(w.memory.buffer, w.mt_out(), BLOCK), k * BLOCK); }
  return all;
}
function play(image, program, notes, blocks = 150) {
  load(image);
  if (w.mt_program(0, program) !== 0) throw new Error('no program ' + program);
  for (const n of notes) w.mt_note(0, n, 100);
  return render(blocks);
}
const same = (a, x) => a.length === x.length && a.every((v, i) => v === x[i]);
const melodic = p => bank.presets.findIndex(x => x.bank === 0 && x.preset === p);
// The bank with the layers off (offset 68 = 0) and, for one preset and note, the key map pointing at zone k.
const single = (pi, n, k) => { const c = bin.slice(); new DataView(c.buffer).setUint32(68, 0, true); if (pi >= 0) c[offKey + pi * 128 + n] = k; return c; };
const covering = (pi, n) => { const p = presetOf(pi); return bank.zones.slice(p.first, p.first + p.count).map((z, k) => z.lo <= n && n <= z.hi ? k : -1).filter(k => k >= 0); };

// Every layered GM program at three keys: as many voices as zones, and the sound the sum of each zone alone.
let notesChecked = 0, voicesBad = 0, sumBad = 0;
for (let prog = 0; prog < 128; ++prog) {
  const pi = melodic(prog);
  if (pi < 0 || !entriesOf(pi).length) continue;
  for (const n of [48, 60, 72]) {
    const ks = covering(pi, n);
    if (ks.length < 2) continue;
    // voices after two blocks, against each zone alone after two blocks (a one-shot of a few dozen samples, as Glockenspiel's
    // strike, has ended by then)
    const voicesAfter = image => { load(image); w.mt_program(0, prog); w.mt_note(0, n, 100); render(2); return w.mt_voices(); };
    const voices = voicesAfter(bin), want = ks.reduce((s, k) => s + voicesAfter(single(pi, n, k)), 0);
    const both = play(bin, prog, [n]);
    const parts = ks.map(k => play(single(pi, n, k), prog, [n]));
    const sum = both.map((_, i) => parts.reduce((s, a) => s + a[i], 0));
    ++notesChecked;
    if (voices !== want || voices < 1) { if (voicesBad++ < 3) console.log('   program', prog, 'note', n, 'voices', voices, 'zones', ks.length, 'alone', want); }
    if (!same(both, sum)) { if (sumBad++ < 3) console.log('   program', prog, 'note', n, 'is not the sum of its zones'); }
  }
}
check(notesChecked > 50 && voicesBad === 0, `a layered note starts one voice per zone (${notesChecked} notes of the layered GM programs)`);
check(notesChecked > 50 && sumBad === 0, 'and it is, sample for sample, the sum of each of its zones played alone');

// Honky-Tonk (program 3): two zones at 60, against one before.
const HONKY = 3, hp = melodic(HONKY);
load(single(-1)); w.mt_program(0, HONKY); w.mt_note(0, 60, 100); render(2);
const voicesBefore = w.mt_voices();
load(bin); w.mt_program(0, HONKY); w.mt_note(0, 60, 100); render(2);
const voicesNow = w.mt_voices();
w.mt_note(0, 60, 0); render(400);
check(voicesBefore === 1 && voicesNow === 2 && w.mt_voices() === 0,
  `Honky-Tonk at C4: ${voicesBefore} voice without the layers, ${voicesNow} with them, and note off ends both (${w.mt_voices()} left after 1.6 s)`);
// The voice limit counts layers: at 5 voices three Honky-Tonk notes keep at most 5 sounding, the newest note whole.
load(bin); w.mt_limit(5); w.mt_program(0, HONKY);
for (const n of [48, 55, 60]) { w.mt_note(0, n, 100); render(2); }
render(4);
check(w.mt_voices() <= 5 && w.mt_info(0, 6) <= 5, `under a limit of 5 voices three layered notes sound with ${w.mt_voices()} voices`);
// A layer never cuts its own note: both zones of Honky-Tonk at 60 put in one exclusive class still sound together.
{
  const c = bin.slice(), d = new DataView(c.buffer), extra = d.getUint32(48, true), S = d.getUint32(52, true), p = presetOf(hp);
  for (const k of covering(hp, 60)) c[extra + (p.first + k) * S + 1] = 5;
  load(c); w.mt_program(0, HONKY); w.mt_note(0, 60, 100); render(4);
  const together = w.mt_voices();
  w.mt_note(0, 60, 100); render(4);                    // the same note again: the class's old voices fade, the two new ones sound
  check(together === 2 && w.mt_voices() === 2, `both layers in one exclusive class sound together (${together} voices), and a new note of the class cuts the old pair (${w.mt_voices()} left)`);
}
// A bank from before the layers (68-byte header, no table): binds and plays only the key map's zone.
{
  const H = 76, h = 8, smp = dv.getUint32(36, true), cut = nLayer ? smp - offLayer : 0;
  const at = x => x < offLayer || !offLayer ? x - h : x - h - cut;
  const o = new Uint8Array(bin.length - h - cut), od = new DataView(o.buffer);
  o.set(bin.subarray(0, 68)); o.set(bin.subarray(H, offLayer), 68); o.set(bin.subarray(smp), at(smp));
  [16, 20, 24, 28, 32, 36, 40, 44, 48, 56].forEach(f => { const v = dv.getUint32(f, true); od.setUint32(f, v ? at(v) : 0, true); });
  const zones = dv.getUint32(32, true), nz = dv.getUint16(14, true);
  for (let i = 0; i < nz; ++i) od.setUint32(at(zones) + i * 24, at(dv.getUint32(zones + i * 24, true)), true);
  check(od.getUint32(16, true) === 68 && same(play(o, HONKY, [48, 60, 72]), play(single(-1), HONKY, [48, 60, 72])),
    'a bank with the 68-byte header (from before the layers) plays one zone a key, as it did');
}

console.log(failed ? `${failed} of ${checks} checks FAILED` : `${checks}/${checks} checks passed`);
process.exit(failed ? 1 : 0);
