// The device renders the player at 32 kHz and resamples to 44.1 kHz (fm1_open/app/sf2out.c); the browser renders at 44.1 kHz.
// Both builds of the whole output stage (fm1b + sf2fx + sf2out) on WebAssembly must agree on what is heard: pitch, level,
// brightness, envelope times, vibrato rate and the reverb tail.
//   node test_rate.js [bank.fm1b]       (default: dist/gm_32k.fm1b)
const fs = require('fs'), path = require('path'), { execFileSync } = require('child_process');
const root = __dirname;
const sdk = path.join(root, 'fm1_open/build/tools/wasi-sdk-27.0-x86_64-windows');
const app = path.join(root, 'fm1_open/app');
const sources = [path.join(root, 'test_rate/wrap.c'), ...['fm1b.c', 'sf2fx.c', 'sf2out.c'].map(f => path.join(app, f))];
const inputs = [...sources, ...['fm1b.h', 'sf2fx.h', 'sf2out.h', 'fm1b_dqt.inc', 'fm1b_dqt6.inc', 'resample_table.inc'].map(f => path.join(app, f))];
function build(rate) {
  const wasm = path.join(root, `test_rate/rate_${rate}.wasm`);
  if (!fs.existsSync(wasm) || inputs.some(s => fs.statSync(s).mtimeMs > fs.statSync(wasm).mtimeMs))
    execFileSync(path.join(sdk, 'bin/clang.exe'), ['--sysroot=' + path.join(sdk, 'share/wasi-sysroot'), '-O2', '-DFM1_HOST_TEST', `-DFM1B_RATE=${rate}U`,
      '-mexec-model=reactor', '-Wl,--no-entry', '-Wl,--export-dynamic', '-Wall', '-Werror',
      ...['fm1_open/app', 'fm1_open/app/synth', 'fm1_open/sdk/include'].map(d => '-I' + path.join(root, d)), ...sources, '-o', wasm], { stdio: 'inherit' });
  const w = new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(wasm)), { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }) }).exports;
  if (w._initialize) w._initialize();
  return w;
}
const bank = fs.readFileSync(process.argv[2] || path.join(root, 'dist/gm_32k.fm1b'));
const engines = [44100, 32000].map(rate => {
  const w = build(rate);
  if (w.rt_rate() !== rate) throw new Error('rate');
  new Uint8Array(w.memory.buffer, w.rt_bank(), bank.length).set(bank);
  return w;
});
// Play: program, notes on channel 1, controllers first, held for `on` seconds then released, `total` seconds in all. Mono (left).
function play(w, { program = 73, notes = [69], on = 1.5, total = 3, fx = [0, 0], controls = [], velocity = 100 }) {
  if (w.rt_bind(bank.length) !== 0 || w.rt_program(program) !== 0) throw new Error('bank');
  w.rt_fx(fx[0], fx[1]);
  for (const [c, v] of controls) w.rt_control(0, c, v);
  for (const n of notes) w.rt_note(0, n, velocity);
  const blocks = Math.round(total * 44100 / 128), off = Math.round(on * 44100 / 128), out = new Float64Array(blocks * 128);
  for (let b = 0; b < blocks; ++b) {
    if (b === off) for (const n of notes) w.rt_note(0, n, 0);
    w.rt_block();
    const o = new Int32Array(w.memory.buffer, w.rt_out(), 256);
    for (let i = 0; i < 128; ++i) { out[b * 128 + i] = o[2 * i] / 8388608; if (Math.abs(o[2 * i]) >= 8388607) ++clipped; }
  }
  return out;
}
function period(a, from, n = 4096) {
  const r = [];
  for (let l = 20; l < 1200; ++l) { let s = 0; for (let i = 0; i < n; ++i) s += a[from + i] * a[from + i + l]; r[l] = s; }
  const top = Math.max(...r.slice(20));
  let lag = 0;
  for (let l = 21; l < 1199; ++l) if (r[l] > 0.9 * top && r[l] >= r[l - 1] && r[l] >= r[l + 1]) { lag = l; break; }
  const y0 = r[lag - 1], y1 = r[lag], y2 = r[lag + 1];
  return lag + 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2);
}
let clipped = 0;                                          // samples at full scale, since start
const rms = (a, from, to) => { let s = 0; for (let i = from; i < to; ++i) s += a[i] * a[i]; return Math.sqrt(s / (to - from)); };
const db = x => 20 * Math.log10(x);
function centroid(a, from, n = 2048) {                   // spectral centroid in Hz: Hann window, DFT bins of 43 Hz up to 22 kHz
  const x = Array.from({ length: n }, (_, i) => a[from + i] * (0.5 - 0.5 * Math.cos(2 * Math.PI * i / n)));
  let num = 0, den = 0;
  for (let k = 1; k < n / 2; ++k) {
    let re = 0, im = 0;
    for (let i = 0; i < n; ++i) { const t = 2 * Math.PI * k * i / n; re += x[i] * Math.cos(t); im -= x[i] * Math.sin(t); }
    const p = re * re + im * im, f = k * 44100 / n; num += f * p; den += p;
  }
  return num / den;
}
// When the level, after `from`, first falls `below` dB under ref.
function fall(a, from, ref, below) {
  for (let i = from; i + 441 < a.length; i += 441) if (db(rms(a, i, i + 441) / ref) < -below) return (i - from) / 44100;
  return Infinity;
}
let failed = 0, checks = 0;
function check(ok, what) { ++checks; if (!ok) { ++failed; console.log('FAIL', what); } else console.log('ok  ', what); }
const both = opt => engines.map(w => play(w, opt));

for (const [program, note] of [[73, 69], [0, 60], [0, 84], [40, 55]]) {
  const [a, b] = both({ program, notes: [note] });
  const pa = period(a, 30000), pb = period(b, 30000), cents = 1200 * Math.log2(pa / pb);
  const la = rms(a, 22050, 44100), lb = rms(b, 22050, 44100);
  const ca = centroid(a, 30000), cb = centroid(b, 30000);
  check(Math.abs(cents) < 2, `program ${program} note ${note}: pitch at 32 kHz within 2 cents of 44.1 kHz (${cents.toFixed(2)})`);
  check(Math.abs(db(lb / la)) < 0.5, `program ${program} note ${note}: level within 0.5 dB (${db(lb / la).toFixed(2)} dB)`);
  check(Math.abs(cb / ca - 1) < 0.05, `program ${program} note ${note}: spectral centroid within 5 % (${ca.toFixed(0)} / ${cb.toFixed(0)} Hz)`);
  const off = Math.round(1.5 * 44100 / 128) * 128, ta = fall(a, off, la, 40), tb = fall(b, off, lb, 40);
  check(isFinite(ta) && Math.abs(tb / ta - 1) < 0.05, `program ${program} note ${note}: release to -40 dB within 5 % (${(ta * 1000).toFixed(0)} / ${(tb * 1000).toFixed(0)} ms)`);
}
{ // vibrato: the period over time, the rate of its swing
  const swing = a => { const p = []; for (let i = 10000; i + 6000 < 66150; i += 441) p.push(period(a, i, 1024));
    const m = p.reduce((s, v) => s + v, 0) / p.length; let x = 0; for (let i = 1; i < p.length; ++i) if ((p[i - 1] - m) * (p[i] - m) < 0) ++x;
    return x / 2 / ((p.length - 1) * 0.01); };
  const [a, b] = both({ notes: [69], controls: [[1, 127]], on: 2, total: 2 });
  const ra = swing(a), rb = swing(b);
  check(Math.abs(rb / ra - 1) < 0.1 && ra > 4 && ra < 7, `vibrato rate within 10 % (${ra.toFixed(2)} / ${rb.toFixed(2)} Hz)`);
}
{ // reverb and chorus: the tail after the note has gone, against the note
  const [a, b] = both({ notes: [69], fx: [127, 127], on: 1, total: 3 });
  const off = Math.round(44100 / 128) * 128, ra = rms(a, off + 8820, off + 44100) / rms(a, 22050, off), rb = rms(b, off + 8820, off + 44100) / rms(b, 22050, off);
  check(Math.abs(db(rb / ra)) < 1.5, `reverb tail 0.2-1 s after the note within 1.5 dB (${db(ra).toFixed(1)} / ${db(rb).toFixed(1)} dB under the note)`);
  const la = rms(a, 22050, off), lb = rms(b, 22050, off);
  check(Math.abs(db(lb / la)) < 0.5, `level with reverb and chorus within 0.5 dB (${db(lb / la).toFixed(2)} dB)`);
}
// The velocity curve (fm1b.c: velocity squared, 40 log10(127 / v) dB) and the output limiter (sf2out.c: 6 dB makeup, peaks held at
// -3 dB), on the 32 kHz build as the device runs it.
{
  const w = engines[1], soft = { program: 0, notes: [60], on: 1, total: 1.5 };
  const level = v => { const a = play(w, { ...soft, velocity: v }); return rms(a, 4410, 22050); };
  const start = w.rt_limited(), l127 = level(127), l64 = level(64), l32 = level(32);
  check(Math.abs(db(l64 / l127) + 11.95) < 0.3, `velocity 64 is 12 dB under 127 (${db(l64 / l127).toFixed(2)} dB)`);
  check(Math.abs(db(l32 / l127) + 23.9) < 0.3, `velocity 32 is 24 dB under 127 (${db(l32 / l127).toFixed(2)} dB)`);
  check(w.rt_limited() === start, `one note does not reach the limiter (${w.rt_limited() - start} frames held)`);
  const chord = [36, 40, 43, 48, 52, 55, 60, 64, 67, 72, 76, 79, 84, 88, 91, 96];
  const before = clipped, a = play(w, { program: 0, notes: chord, velocity: 127, on: 1, total: 3 });
  let peak = 0; for (let i = 0; i < a.length; ++i) peak = Math.max(peak, Math.abs(a[i]));
  check(w.rt_limited() > start, `16 loud notes reach it (${w.rt_limited() - start} frames held)`);
  check(clipped === before && db(peak) < -2.5, `and stay under full scale: peak ${db(peak).toFixed(2)} dB, ${clipped - before} samples clipped`);
  const after = level(64);
  check(Math.abs(db(after / l64)) < 0.1, `the gain comes back after the chord (${db(after / l64).toFixed(3)} dB)`);
}
// The voice limit and the CPU guard (sf2out_load), on the 32 kHz build as the device runs it.
{
  const w = engines[1], blocks = n => { for (let i = 0; i < n; ++i) w.rt_block(); };
  // Recorder (74): one zone a key, so one voice a note (the flute, 73, has two layers)
  if (w.rt_bind(bank.length) !== 0 || w.rt_program(74) !== 0) throw new Error('bank');
  w.rt_fx(0, 0);
  const notes = [48, 52, 55, 60, 64, 67, 72, 76, 79, 84];
  for (const n of notes) { w.rt_note(0, n, 100); blocks(2); }
  check(w.rt_sounding() === 10, `ten notes held sound on ten voices (${w.rt_sounding()})`);
  w.rt_limit(6);
  check(w.rt_sounding() === 6, `a limit of 6 leaves 6 sounding at once (${w.rt_sounding()})`);
  blocks(10);                                             // 29 ms: the four oldest have faded out
  check(w.rt_status(0) === 6 && w.rt_status(1) === 6 && w.rt_status(2) === 4, `29 ms later 6 voices are left, 4 cut (${w.rt_status(0)}, ${w.rt_status(2)})`);
  let peak = 0; blocks(1); for (const v of new Int32Array(w.memory.buffer, w.rt_out(), 256)) peak = Math.max(peak, Math.abs(v));
  check(peak > 0, 'the six newest still sound');
  w.rt_note(0, 88, 100); blocks(10);
  check(w.rt_status(0) === 6 && w.rt_sounding() === 6, `a new note under the limit fades the oldest out: still 6 (${w.rt_status(0)})`);
  w.rt_limit(w.rt_voice_max());
  w.rt_note(0, 91, 100); blocks(2);
  check(w.rt_sounding() === 7, `the limit back up: a new note adds a voice (${w.rt_sounding()})`);
  // the guard: over 80 % it takes voices away, never below 4; a quarter second under 60 % gives one back
  for (let i = 0; i < 9; ++i) { w.rt_load(85, 100); blocks(1); }
  check(w.rt_status(1) === 6 && w.rt_sounding() === 6, `85 % load: the limit drops to one under the voices sounding (${w.rt_status(1)}, ${w.rt_sounding()})`);
  for (let i = 0; i < 9; ++i) { w.rt_load(99, 100); blocks(1); }
  check(w.rt_status(1) === 4, `99 % load: two at once (${w.rt_status(1)})`);
  for (let i = 0; i < 40; ++i) { w.rt_load(99, 100); blocks(1); }
  check(w.rt_status(1) === 4 && w.rt_sounding() === 4, `never under 4 voices (${w.rt_status(1)})`);
  for (let i = 0; i < 85; ++i) { w.rt_load(50, 100); blocks(1); }
  const before = w.rt_status(1); w.rt_load(50, 100);
  check(before === 4 && w.rt_status(1) === 5, `86 blocks (0.25 s) under 60 %: one voice back (${before} -> ${w.rt_status(1)})`);
  for (let i = 0; i < 86 * 30; ++i) w.rt_load(70, 100);
  check(w.rt_status(1) === 5, 'between 60 and 80 % the limit stays');
  for (let i = 0; i < 86 * w.rt_voice_max(); ++i) w.rt_load(10, 100);        // one voice back every 0.25 s from 5: voice_max steps are enough (30 were for 32 voices)
  check(w.rt_status(1) === w.rt_voice_max(), `a calm interrupt gets every voice back (${w.rt_status(1)})`);
}
console.log(failed ? `${failed} of ${checks} checks FAILED` : `${checks}/${checks} checks passed`);
process.exitCode = failed ? 1 : 0;
