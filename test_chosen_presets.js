// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
// The converter's "Chosen instruments": a bank of the ticked presets only, each at its own program number, and one empty preset
// (no zones) that the font's other programs point at, so a program change to one of them makes the channel silent instead of
// leaving it as it was. Checked in the bank's tables and in the SF2 player's voice engine (fm1_open/app/fm1b.c on WebAssembly,
// the harness of test_multitimbral.js).
//   node test_chosen_presets.js [font.sf2]      (default soundfonts/1mgm.sf2)
const fs = require('fs'), path = require('path'), { execFileSync } = require('child_process');
const root = __dirname;
const html = fs.readFileSync(path.join(root, 'index.html'), 'utf8');
eval(html.match(/<script id="core">([\s\S]*?)<\/script>/)[1] + '\nglobalThis.api = { parseSf2, buildBank, writeFm1 };');
const A = globalThis.api;
let failed = 0;
const check = (ok, what) => { if (!ok) ++failed; console.log(ok ? 'ok  ' : 'FAIL', what); };

const fontFile = process.argv[2] || path.join(root, 'soundfonts/1mgm.sf2'), b = fs.readFileSync(fontFile);
const sf = A.parseSf2(b.buffer.slice(b.byteOffset, b.byteOffset + b.length));
const slotOf = p => p.bank === 0 ? p.preset : p.bank === 128 ? 128 + p.preset : -1;      // as the page has it
const at = s => sf.presets.findIndex(p => slotOf(p) === s);
const want = [0, 40, 128].filter(s => at(s) >= 0), ticked = want.map(at).sort((x, y) => x - y);
check(want.length === 3 && at(1) >= 0, 'the font has program 0, 1, 40 and kit 0');

// what the page does for "Chosen instruments"
const bank = A.buildBank(sf, ticked, { rate: 32000, bfp: true });
const kept = new Set(bank.presets.map(slotOf));
bank.emptySlots = [...new Set(sf.presets.map(slotOf).filter(s => s >= 0 && !kept.has(s)))];
const bin = A.writeFm1(bank), all = A.writeFm1(A.buildBank(sf, -2, { rate: 32000, bfp: true }));
const dv = new DataView(bin.buffer, bin.byteOffset, bin.length), u16 = o => dv.getUint16(o, true), u32 = o => dv.getUint32(o, true);
const map = u32(20), pre = u32(24), np = u16(12), slot = s => u16(map + s * 2);
const name = p => String.fromCharCode(...bin.subarray(pre + p * 24 + 4, pre + p * 24 + 24)).replace(/\0.*$/s, '').trim();
const count = p => u16(pre + p * 24 + 2);
console.log(`${path.basename(fontFile)}: ${bin.length} bytes with ${want.length} instruments, ${all.length} with all ${sf.presets.length}`);

check(np === want.length + 1, `${np} presets: the ${want.length} ticked and one empty`);
check(want.every(s => slot(s) < np && count(slot(s)) > 0 && name(slot(s)) === sf.presets[at(s)].name.trim()), 'each ticked instrument is at its own program number, with its name and zones');
const empty = np - 1;
check(count(empty) === 0 && name(empty) === '(empty)' && bin.subarray(u32(28) + empty * 128, u32(28) + empty * 128 + 128).every(k => k === 0xff), 'the last preset is "(empty)": no zones, every key unmapped');
const fontSlots = new Set(sf.presets.map(slotOf).filter(s => s >= 0));
check([...fontSlots].filter(s => !want.includes(s)).every(s => slot(s) === empty), `the font's ${fontSlots.size - want.length} other programs and kits point at the empty preset`);
check([...Array(256).keys()].filter(s => !fontSlots.has(s)).every(s => slot(s) === 0xffff), 'programs the font does not have stay unmapped, as before');
check(bin.length < all.length / 2, 'the bank holds only the ticked instruments\' sounds');

// ---- the voice engine
const sdk = path.join(root, 'fm1_open/build/tools/wasi-sdk-27.0-x86_64-windows');
const wasmPath = path.join(root, 'test_mt/mt_test.wasm');
const sources = [path.join(root, 'test_mt/wrap.c'), path.join(root, 'fm1_open/app/fm1b.c'), path.join(root, 'fm1_open/web/synth/sea_decode.c')];
const headers = ['fm1b.h', 'fm1b_config.h', 'fm1b_dqt.inc', 'fm1b_dqt6.inc'].map(h => path.join(root, 'fm1_open/app', h));
if (!fs.existsSync(wasmPath) || [...sources, ...headers].some(s => fs.statSync(s).mtimeMs > fs.statSync(wasmPath).mtimeMs))
  execFileSync(path.join(sdk, 'bin/clang.exe'), ['--sysroot=' + path.join(sdk, 'share/wasi-sysroot'), '-O1', '-DFM1_HOST_TEST', '-mexec-model=reactor', '-Wl,--no-entry',
    '-Wl,--export-dynamic', ...['fm1_open/app', 'fm1_open/app/synth', 'fm1_open/sdk/include'].map(d => '-I' + path.join(root, d)), ...sources, '-o', wasmPath], { stdio: 'inherit' });
const w = new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(wasmPath)), { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }) }).exports;
if (w._initialize) w._initialize();
new Uint8Array(w.memory.buffer, w.mt_bank(), bin.length).set(bin);
check(w.mt_bind(bin.length) === 0, 'the SF2 player takes the bank');
const level = blocks => { let e = 0; for (let i = 0; i < blocks; ++i) { w.mt_block(); for (const v of new Int32Array(w.memory.buffer, w.mt_out(), 128)) e += Math.abs(v); } return e; };
// a note on channel 1 after a program change (and the note off, and time for the release)
const play = (program, kit = false) => {
  w.mt_panic(); level(4);
  if (kit) { w.mt_channel_program(9, 128, program); level(1); w.mt_drum(9, 36, 100); const e = level(40); w.mt_drum(9, 36, 0); level(200); return e; }
  w.mt_channel_program(1, 0, program); level(1); w.mt_note(1, 60, 100); const e = level(40); w.mt_note(1, 60, 0); level(200); return e;
};
const piano = play(0), silent = play(1), after = play(40);
check(piano > 0 && after > 0, `program 0 and 40 sound (${piano}, ${after})`);
check(silent === 0, `program 1, not ticked, is silent after program 0 (${silent}); before this the channel kept program 0`);
check(play(0) === piano, 'program 0 again sounds as it did');
check(play(0, true) > 0, 'kit 0 on the percussion channel sounds');
// the panel (SELECT) steps over the programs left out, and shows the number of the program it is on
check(w.mt_program(0, 40) === 0 && w.mt_status(3) === 40, 'the panel on program 40 shows 40');
check(w.mt_program(0, 1) === -1 && w.mt_status(3) === 40 && (w.mt_panel_note(60, 100), level(40)) > 0, 'the panel cannot select program 1: it stays on 40 and plays it');
// a bank without program 0: the panel starts on the first program that has sounds, not on the empty one
const later = A.buildBank(sf, [at(40), at(128)].sort((x, y) => x - y), { rate: 32000, bfp: true });
later.emptySlots = [...new Set(sf.presets.map(slotOf).filter(s => s >= 0 && s !== 40 && s !== 128))];
const bin2 = A.writeFm1(later);
new Uint8Array(w.memory.buffer, w.mt_bank(), bin2.length).set(bin2);
check(w.mt_bind(bin2.length) === 0 && w.mt_status(3) === 40, `without program 0 the panel starts on program 40 (got ${w.mt_status(3)})`);
console.log(failed ? `${failed} FAILED` : 'all passed'); process.exit(failed ? 1 : 0);
