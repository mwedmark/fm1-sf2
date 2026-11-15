// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
// A program change on channel 10 chooses the percussion kit: the kit at that program, else the nearest one below it (General MIDI:
// program 1 is the Standard kit at 0, 9 the Room kit at 8). The real fm1b.c on WebAssembly, with a bank of several kits made
// from a SoundFont that has them (default soundfonts/Roland SC-55 Presets.sf2; the repository's 1mgm.sf2 has one kit).
//   node test_drum_kits.js [kits.sf2]
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

const sfPath = process.argv[2] || path.join(root, 'soundfonts/Roland SC-55 Presets.sf2');
if (!fs.existsSync(sfPath)) { console.log('skipped: no ' + sfPath); process.exit(0); }
const html = fs.readFileSync(path.join(root, 'index.html'), 'utf8');
eval(html.match(/<script id="core">([\s\S]*?)<\/script>/)[1] + '\nglobalThis.api = { parseSf2, buildBank, writeFm1, bankInfo };');
const A = globalThis.api, raw = fs.readFileSync(sfPath);
const sf = A.parseSf2(raw.buffer.slice(raw.byteOffset, raw.byteOffset + raw.length));
const kits = sf.presets.map((p, i) => [p, i]).filter(([p]) => p.bank === 128).map(([p, i]) => [p.preset, i]);
const piano = sf.presets.findIndex(p => p.bank === 0 && p.preset === 0);
const have = kits.map(k => k[0]);
if (have.length < 3 || piano < 0) throw new Error('the font needs three kits and a piano');
const bank = A.writeFm1(A.buildBank(sf, [piano, ...kits.map(k => k[1])], { rate: 22050, bfp: true }), A.bankInfo('kits'));
if (bank.length > w.mt_bank_capacity()) throw new Error('the test bank is too big: ' + bank.length);
new Uint8Array(w.memory.buffer, w.mt_bank(), bank.length).set(bank);

const BLOCK = 128, LONG = 200;
let failed = 0;
const check = (ok, what) => { if (!ok) ++failed; console.log(ok ? 'ok  ' : 'FAIL', what); };
function fresh() { if (w.mt_bind(bank.length) !== 0) throw new Error('bank rejected'); w.mt_program(0, 0); w.mt_block(); }
function render(blocks) {
  const all = new Int32Array(blocks * BLOCK);
  for (let b = 0; b < blocks; ++b) { w.mt_block(); all.set(new Int32Array(w.memory.buffer, w.mt_out(), BLOCK), b * BLOCK); }
  return all;
}
const same = (a, b) => a.every((v, i) => v === b[i]);
const sumAbs = a => a.reduce((s, v) => s + Math.abs(v), 0);
// Five drums played at once on channel 10 after a program change `program` (null: none), melodic bank 0 / kit bank 128 as the MIDI says.
const KEYS = [36, 38, 42, 46, 49];                  // kick, snare, hi-hats, crash: kits that share one drum still differ in another
function hit(program) {
  fresh();
  if (program !== null) { w.mt_channel_program(9, 128, program); w.mt_block(); }
  for (const k of KEYS) w.mt_drum(9, k, 100);
  return render(LONG);
}
const first = have[0], base = hit(null);
// Kits that sound alike are common (the Room kit of the SC-55 font has the Standard kit's drums): pick kits that differ.
const sounding = have.map(k => [k, hit(k)]);
const second = (sounding.find(([k, r]) => k > first && !same(r, base) && !have.includes(k + 1)) || [])[0];
const third = (sounding.find(([k, r]) => k > second && !same(r, base) && !same(r, hit(second))) || [])[0];
if (second === undefined || third === undefined) throw new Error('the font needs three kits that sound different');
check(sumAbs(base) > 0, 'channel 10 plays a kit without any program change');
check(same(hit(first), base), `program ${first} (the first kit) is what plays with no program change`);
const k2 = hit(second), k3 = hit(third);
check(sumAbs(k2) > 0 && !same(k2, base), `program ${second} plays another kit than ${first}`);
check(!same(k3, base) && !same(k3, k2), `program ${third} plays a third kit`);
check(same(hit(second + 1), k2), `program ${second + 1} (no kit there) plays the kit at ${second}, the nearest below`);
check(third - 1 === second || same(hit(third - 1), hit(have.filter(k => k <= third - 1).pop())), `program ${third - 1} falls back below ${third}`);
check(same(hit(127), hit(have[have.length - 1])), 'program 127 plays the highest kit the bank has');
fresh(); w.mt_channel_program(9, 128, third); w.mt_channel_program(9, 128, first); w.mt_block(); for (const k of KEYS) w.mt_drum(9, k, 100);
check(same(render(LONG), base), 'a later program change goes back to the first kit');
fresh(); w.mt_channel_program(9, 0, second); w.mt_block(); for (const k of KEYS) w.mt_drum(9, k, 100);
check(same(render(LONG), k2), 'the bank select does not matter on channel 10: bank 0 picks the kit too');
// Another channel's program change does not touch the kit, and the panel's drum mode plays the chosen kit.
fresh(); w.mt_channel_program(0, 0, 0); w.mt_block(); for (const k of KEYS) w.mt_drum(9, k, 100);
check(same(render(LONG), base), 'a program change on channel 1 leaves the kit');
fresh(); w.mt_channel_program(9, 128, second); w.mt_block(); for (const k of KEYS) w.mt_panel_drum(k, 100);
check(same(render(LONG), k2), 'the panel\'s drum mode plays the kit chosen on channel 10');
console.log(failed ? failed + ' failed' : 'all passed'); process.exit(failed ? 1 : 0);
