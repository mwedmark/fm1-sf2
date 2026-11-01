// Runs the SF2 player's reverb and chorus (fm1_open/app/sf2fx.c) on WebAssembly and checks what they do to a signal.
//   node test_fx.js
const fs = require('fs'), path = require('path'), { execFileSync } = require('child_process');
const root = __dirname;
const sdk = path.join(root, 'fm1_open/build/tools/wasi-sdk-27.0-x86_64-windows');
const wasmPath = path.join(root, 'test_fx/fx_test.wasm');
const sources = [path.join(root, 'test_fx/wrap.c'), path.join(root, 'fm1_open/app/sf2fx.c')];
if (!fs.existsSync(wasmPath) || sources.some(s => fs.statSync(s).mtimeMs > fs.statSync(wasmPath).mtimeMs))
  execFileSync(path.join(sdk, 'bin/clang.exe'), ['--sysroot=' + path.join(sdk, 'share/wasi-sysroot'), '-O1', '-DFM1_HOST_TEST', '-mexec-model=reactor', '-Wl,--no-entry',
    '-Wl,--export-dynamic', ...['fm1_open/app', 'fm1_open/app/synth', 'fm1_open/sdk/include'].map(d => '-I' + path.join(root, d)), ...sources, '-o', wasmPath], { stdio: 'inherit' });
const w = new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(wasmPath)), { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }) }).exports;
if (w._initialize) w._initialize();
const heap = () => new Int32Array(w.memory.buffer);
const dryAt = w.fx_dry() >> 2, outAt = w.fx_out() >> 2;
let failed = 0;
const check = (name, ok, extra = '') => { console.log((ok ? 'ok   ' : 'FAIL ') + name + (extra ? '  ' + extra : '')); if (!ok) failed++; };
const db = x => (20 * Math.log10(Math.max(x, 1e-9))).toFixed(1) + ' dB';

// run `seconds` of a generator through the effects; returns {left, right, dry}
function run(seconds, gen, reverb, chorus) {
  w.fx_set(reverb, chorus);
  const blocks = Math.ceil(seconds * 44100 / 128), L = new Float64Array(blocks * 128), R = new Float64Array(blocks * 128), D = new Float64Array(blocks * 128);
  for (let b = 0; b < blocks; b++) {
    const h = heap();
    for (let i = 0; i < 128; i++) h[dryAt + i] = D[b * 128 + i] = gen(b * 128 + i);
    w.fx_block();
    const o = heap();
    for (let i = 0; i < 128; i++) { L[b * 128 + i] = o[outAt + 2 * i]; R[b * 128 + i] = o[outAt + 2 * i + 1]; }
  }
  return { L, R, D };
}
const rms = (a, from = 0, to = a.length) => { let s = 0; for (let i = from; i < to; i++) s += a[i] * a[i]; return Math.sqrt(s / Math.max(1, to - from)) / 8388608; };
const tone = (hz, amp, until) => n => n < until * 44100 ? Math.round(amp * 8388607 * Math.sin(2 * Math.PI * hz * n / 44100)) : 0;
const noise = (amp, until) => { let s = 12345; return n => { s = (s * 1103515245 + 12345) >>> 0; return n < until * 44100 ? Math.round(amp * 8388607 * ((s >>> 8) / 8388608 - 1)) : 0; }; };

let r = run(1, tone(440, 0.5, 1), 0, 0);
check('both effects off: the output is the dry signal on both channels', r.L.every((v, i) => v === r.D[i]) && r.R.every((v, i) => v === r.D[i]));

r = run(4, tone(440, 0.5, 0.5), 127, 0);
const dryRms = rms(r.D, 0, 22050);
const tail = [0.55, 1.0, 1.6, 2.4].map(t => rms(r.L, Math.round(t * 44100), Math.round((t + 0.2) * 44100)));
check('reverb at maximum: just after the note the tail is within 20 dB of the note', tail[0] > dryRms * 0.1 && tail[0] < dryRms * 1.5, `${db(tail[0])} against the note ${db(dryRms)}`);
check('reverb: the tail falls steadily', tail[0] > tail[1] && tail[1] > tail[2] && tail[2] > tail[3] && tail[3] < dryRms * 0.01, tail.map(db).join(' '));
check('reverb: left and right differ (stereo)', (() => { let d = 0; for (let i = 22050; i < 44100; i++) d += Math.abs(r.L[i] - r.R[i]); return d > 1e6; })());

r = run(2, tone(440, 0.5, 2), 0, 127);
check('chorus: stays within a few dB of the dry level', rms(r.L, 10000) / rms(r.D, 10000) < 2.2, `${db(rms(r.L, 10000))} vs ${db(rms(r.D, 10000))}`);
check('chorus: left and right differ', (() => { let d = 0; for (let i = 20000; i < 44100; i++) d += Math.abs(r.L[i] - r.R[i]); return d > 1e6; })());

r = run(8, noise(0.9, 4), 127, 127);
let peak = 0; for (let i = 0; i < r.L.length; i++) peak = Math.max(peak, Math.abs(r.L[i]), Math.abs(r.R[i]));
check('loud noise with both effects at maximum: nothing wraps (saturates at 24 bits)', peak <= 8388608, `peak ${peak}`);
const late = rms(r.L, 7 * 44100);
check('  and it dies out after the input stops', late < 1e-3, `last second ${db(late)}`);

// idle: after about 3 s of silence the effects cost nothing and the return is exactly nothing
r = run(8, tone(440, 0.5, 0.3), 127, 127);
check('after silence the output is exactly silent again', r.L.slice(-44100).every(v => v === 0) && r.R.slice(-44100).every(v => v === 0));
r = run(3, tone(440, 0.5, 1), 127, 127);
check('a new note after the idle state sounds like the first (the tails were cleared)', rms(r.L, 0, 44100) > 0.1, db(rms(r.L, 0, 44100)));

console.log(failed ? failed + ' FAILED' : 'ALL OK');
process.exit(failed ? 1 : 0);
