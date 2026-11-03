// Every sample format the engine reads (SEA5, SEA6, BFP8, 8-, 12- and 16-bit PCM) must play exactly what a 16-bit bank holding the
// same decoded samples plays: the real fm1b.c on WebAssembly, bit for bit, on notes long enough to cross chunks and loops.
// The reference decoders are index.html's (seaDecode, the PCM it keeps), so this also checks the engine's own SEA decoder.
//   node test_formats.js [1mgm.sf2]
const fs = require('fs'), path = require('path'), { execFileSync } = require('child_process');
const root = __dirname;
const sdk = path.join(root, 'fm1_open/build/tools/wasi-sdk-27.0-x86_64-windows');
const wasmPath = path.join(root, 'test_mt/mt_test.wasm');
const sources = [path.join(root, 'test_mt/wrap.c'), path.join(root, 'fm1_open/app/fm1b.c'), path.join(root, 'fm1_open/web/synth/sea_decode.c')];
const headers = ['fm1b.h', 'fm1b_config.h', 'fm1b_dqt.inc', 'fm1b_dqt6.inc'].map(h => path.join(root, 'fm1_open/app', h));
if (!fs.existsSync(wasmPath) || [...sources, ...headers].some(s => fs.statSync(s).mtimeMs > fs.statSync(wasmPath).mtimeMs))
  execFileSync(path.join(sdk, 'bin/clang.exe'), ['--sysroot=' + path.join(sdk, 'share/wasi-sysroot'), '-O1', '-DFM1_HOST_TEST', '-mexec-model=reactor', '-Wl,--no-entry',
    '-Wl,--export-dynamic', ...['fm1_open/app', 'fm1_open/app/synth', 'fm1_open/sdk/include'].map(d => '-I' + path.join(root, d)), ...sources, '-o', wasmPath], { stdio: 'inherit' });
const mod = new WebAssembly.Module(fs.readFileSync(wasmPath));

const html = fs.readFileSync(path.join(root, 'index.html'), 'utf8');
eval(html.match(/<script id="core">([\s\S]*?)<\/script>/)[1] + '\nglobalThis.api = { parseSf2, buildBank, writeFm1, seaDecode };');
const A = globalThis.api;
const b = fs.readFileSync(process.argv[2] || path.join(root, '1mgm.sf2'));
const sf = A.parseSf2(b.buffer.slice(b.byteOffset, b.byteOffset + b.length));
const find = (bank, p) => sf.presets.findIndex(x => x.bank === bank && x.preset === p);
const PROGRAMS = [0, 24, 40, 56, 73, 80];
const pick = PROGRAMS.map(p => find(0, p)).concat(find(128, 0));

function player(bin) {
  const w = new WebAssembly.Instance(mod, { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }) }).exports;
  if (w._initialize) w._initialize();
  new Uint8Array(w.memory.buffer, w.mt_bank(), bin.length).set(bin);
  if (w.mt_bind(bin.length) !== 0) throw new Error('bank rejected');
  return (program, notes, blocks) => {
    if (w.mt_bind(bin.length) !== 0 || w.mt_program(0, program) !== 0) throw new Error('no program ' + program);
    w.mt_block();
    for (const n of notes) w.mt_note(0, n, 100);
    const all = new Int32Array(blocks * 128);
    for (let k = 0; k < blocks; ++k) {
      if (k === blocks >> 1) for (const n of notes) w.mt_note(0, n, 0);     // the release half
      w.mt_block();
      all.set(new Int32Array(w.memory.buffer, w.mt_out(), 128), k * 128);
    }
    return all;
  };
}
let failed = 0, checks = 0;
function check(ok, what) { ++checks; if (!ok) { ++failed; console.log('FAIL', what); } else console.log('ok  ', what); }

for (const [name, opt, decoded] of [
  ['SEA5', { sea: true, seaBits: 5 }, s => A.seaDecode(s.enc, s.pcm.length, 5)],
  ['SEA6', { sea: true, seaBits: 6 }, s => A.seaDecode(s.enc, s.pcm.length, 6)],
  ['BFP8', { bfp: true }, s => s.pcm],
  ['12-bit', { bits: 12 }, s => s.pcm],
  ['8-bit', { bits: 8 }, s => Int16Array.from(s.pcm, x => x * 256)],
  ['16-bit', { bits: 16 }, s => s.pcm]]) {
  const bank = A.buildBank(sf, pick, { rate: 32000, ...opt }), bin = A.writeFm1(bank);
  for (const s of bank.samples) { s.pcm = Int16Array.from(decoded(s)); s.enc = null; s.bytes = s.pcm.byteLength; }
  Object.assign(bank, { sea: false, seaBits: 0, bfp: false, bits: 16 });
  const play = player(bin), twin = player(A.writeFm1(bank));
  let same = true, energy = 0, where = '';
  for (const p of PROGRAMS)
    for (const notes of [[36], [60], [84], [48, 55, 64, 71]]) {
      const a = play(p, notes, 700), t = twin(p, notes, 700);   // about 2 s: chunk changes, loop jumps, the release
      for (let i = 0; i < a.length; ++i) energy += Math.abs(a[i]);
      if (same && a.some((v, i) => v !== t[i])) { same = false; where = `program ${p}, notes ${notes}`; }
    }
  check(energy > 0 && same, `${name}: the engine plays exactly the decoded samples (${PROGRAMS.length} programs x 4 chords)${where ? ' - differs at ' + where : ''}`);
}
console.log(failed ? `${failed} of ${checks} checks FAILED` : `${checks}/${checks} checks passed`);
process.exitCode = failed ? 1 : 0;
