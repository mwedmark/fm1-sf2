// The voices shared between two cores (FM1B_SPLIT, fm1b.c) give the same output as one core, sample for sample.
// Builds the player's output stage (fm1b, sf2fx, sf2out) for WebAssembly twice, without and with FM1B_SPLIT (the "other core" runs its
// share at once, test_fluid/split_host.c), plays the same random notes on 16 channels with different programs, sends, pans and filters
// through both, and compares the int32 output of every block. Also at other shares of the cost (0, 25, 50, 75, 100 %).
//   node test_split.js [bank.fm1b] [--rate=44100] [--seconds=30]
const fs = require('fs'), path = require('path'), { execFileSync } = require('child_process');
const root = __dirname;
const args = process.argv.slice(2), opt = {};
for (const a of args) { const m = a.match(/^--([^=]+)=(.*)$/); if (m) opt[m[1]] = m[2]; }
const bankPath = path.resolve(root, args.find(a => !a.startsWith('--')) || 'banks/default.fm1b');
const rate = +(opt.rate || 32000), seconds = +(opt.seconds || 30);
const sdk = path.join(root, 'fm1_open/build/tools/wasi-sdk-27.0-x86_64-windows'), app = path.join(root, 'fm1_open/app');
const sources = [path.join(root, 'test_fluid/wrap.c'), path.join(root, 'test_fluid/split_host.c'), ...['fm1b.c', 'sf2fx.c', 'sf2out.c'].map(f => path.join(app, f))];
const outDir = path.join(root, 'test_fluid/out');
fs.mkdirSync(outDir, { recursive: true });

function build(split) {
  const wasm = path.join(outDir, `split_${rate}_${split}.wasm`);
  execFileSync(path.join(sdk, 'bin/clang.exe'), ['--sysroot=' + path.join(sdk, 'share/wasi-sysroot'), '-O2', '-DFM1_HOST_TEST', `-DFM1B_RATE=${rate}U`,
    '-DFM1B_INTERP4=0', `-DFM1B_SPLIT=${split}`, '-mexec-model=reactor', '-Wl,--no-entry', '-Wl,--export-dynamic', '-Wall', '-Werror',
    ...['fm1_open/app', 'fm1_open/app/synth', 'fm1_open/sdk/include'].map(d => '-I' + path.join(root, d)), ...sources, '-o', wasm], { stdio: 'inherit' });
  const module = new WebAssembly.Module(fs.readFileSync(wasm));
  return () => {                                       // a fresh instance for each run: no voice left from the one before
    const w = new WebAssembly.Instance(module, { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }) }).exports;
    if (w._initialize) w._initialize();
    return w;
  };
}

// the same events for both: a fixed random sequence
function events() {
  let s = 12345;
  const rnd = n => { s = (s * 1103515245 + 12345) >>> 0; return (s >>> 8) % n; };
  const ev = [], blocks = Math.ceil(seconds * 44100 / 128);
  for (let ch = 0; ch < 16; ++ch) {
    ev.push({ b: 0, f: 'program', a: [ch, 0, ch === 9 ? 0 : (ch * 8 + 1) % 128] });
    ev.push({ b: 0, f: 'control', a: [ch, 91, (ch * 17) % 128] });
    ev.push({ b: 0, f: 'control', a: [ch, 93, (ch * 29) % 128] });
    ev.push({ b: 0, f: 'control', a: [ch, 10, (ch * 37) % 128] });
    ev.push({ b: 0, f: 'control', a: [ch, 74, 20 + (ch * 7) % 100] });
  }
  for (let b = 1; b < blocks; ++b) {
    if (rnd(3)) continue;
    const ch = rnd(16), note = 36 + rnd(48);
    if (ch === 9) ev.push({ b, f: 'drum', a: [9, 35 + rnd(46), 40 + rnd(87)] });
    else ev.push({ b, f: 'note', a: [ch, note, 40 + rnd(87)] }, { b: b + 20 + rnd(400), f: 'note', a: [ch, note, 0] });
    if (!rnd(40)) ev.push({ b, f: 'control', a: [rnd(16), 1, rnd(128)] });
  }
  return ev.sort((x, y) => x.b - y.b);
}

function play(make, share) {
  const w = make();
  const bank = fs.readFileSync(bankPath);
  new Uint8Array(w.memory.buffer, w.cf_bank(), bank.length).set(bank);
  if (w.cf_bind(bank.length) !== 0 || w.cf_program(0) !== 0) throw new Error('bank rejected: ' + bankPath);
  w.cf_fx(64, 64);
  if (share !== undefined) w.cf_split_share(share);
  const ev = events(), blocks = Math.ceil(seconds * 44100 / 128), out = new Int32Array(blocks * 256);
  let e = 0, peak = 0;
  for (let b = 0; b < blocks; ++b) {
    while (e < ev.length && ev[e].b <= b) {
      const { f, a } = ev[e++];
      if (f === 'program') w.cf_channel_program(...a); else if (f === 'control') w.cf_control(...a);
      else if (f === 'drum') w.cf_drum(...a); else w.cf_note(...a);
    }
    w.cf_block();
    out.set(new Int32Array(w.memory.buffer, w.cf_out(), 256), b * 256);
    peak = Math.max(peak, w.cf_sounding());
  }
  return { out, peak, split: share === undefined ? null : [w.cf_split_blocks(), w.cf_split_other(), w.cf_split_all()] };
}

const one = play(build(0));
const two = build(1);
let failed = 0;
console.log(`${path.relative(root, bankPath)} at ${rate} Hz, ${seconds} s, up to ${one.peak} voices at once`);
for (const share of [0, 25, 50, 75, 100]) {
  const r = play(two, share);
  let diff = -1;
  for (let i = 0; i < one.out.length; ++i) if (one.out[i] !== r.out[i]) { diff = i; break; }
  const [blocks, other, all] = r.split;
  const ok = diff < 0;
  if (!ok) failed++;
  console.log(`${ok ? 'ok  ' : 'FAIL'} share ${share} %: ${blocks} blocks split, the other core ${all ? (100 * other / all).toFixed(1) : 0} % of the voices` +
    (ok ? ', the output equals one core\'s' : `, first difference at block ${Math.floor(diff / 256)} sample ${diff % 256}: ${one.out[diff]} against ${r.out[diff]}`));
}
console.log(failed ? `${failed} FAILED` : 'all passed');
process.exit(failed ? 1 : 0);
