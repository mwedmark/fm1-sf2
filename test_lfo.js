// The zone's LFOs (vibrato LFO to pitch, mod LFO to pitch and volume) and the key number's scaling of the volume envelope's hold
// and decay (FORMAT.md, "Modulation"). First the converter against 1mgm.sf2, then the real fm1b.c on WebAssembly, at 44.1 kHz
// (the tests' rate) and 32 kHz (the device's), with a bank of one looped sine whose modulation record is set per check.
//   node test_lfo.js [1mgm.sf2]
const fs = require('fs'), path = require('path'), { execFileSync } = require('child_process');
const root = __dirname;
const sdk = path.join(root, 'fm1_open/build/tools/wasi-sdk-27.0-x86_64-windows');
const sources = [path.join(root, 'test_mt/wrap.c'), path.join(root, 'fm1_open/app/fm1b.c'), path.join(root, 'fm1_open/web/synth/sea_decode.c')];
const headers = ['fm1b.h', 'fm1b_config.h', 'fm1b_dqt.inc', 'fm1b_dqt6.inc'].map(h => path.join(root, 'fm1_open/app', h));
function engine(rate) {
  const wasmPath = path.join(root, `test_mt/lfo_${rate}.wasm`);
  if (!fs.existsSync(wasmPath) || [...sources, ...headers].some(s => fs.statSync(s).mtimeMs > fs.statSync(wasmPath).mtimeMs))
    execFileSync(path.join(sdk, 'bin/clang.exe'), ['--sysroot=' + path.join(sdk, 'share/wasi-sysroot'), '-O1', '-DFM1_HOST_TEST', `-DFM1B_RATE=${rate}U`,
      '-mexec-model=reactor', '-Wl,--no-entry', '-Wl,--export-dynamic',
      ...['fm1_open/app', 'fm1_open/app/synth', 'fm1_open/sdk/include'].map(d => '-I' + path.join(root, d)), ...sources, '-o', wasmPath], { stdio: 'inherit' });
  const w = new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(wasmPath)), { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }) }).exports;
  if (w._initialize) w._initialize();
  return w;
}

const html = fs.readFileSync(path.join(root, 'index.html'), 'utf8');
eval(html.match(/<script id="core">([\s\S]*?)<\/script>/)[1] +
  '\nglobalThis.api = { parseSf2, presetZones, buildBank, writeFm1, envRecord, modRecord, MOD_FIELDS };');
const A = globalThis.api;
let failed = 0, checks = 0;
function check(ok, what) { ++checks; if (!ok) { ++failed; console.log('FAIL', what); } else console.log('ok  ', what); }

// ---- 1. The converter ----
const b = fs.readFileSync(process.argv[2] || path.join(root, '1mgm.sf2'));
const sf = A.parseSf2(b.buffer.slice(b.byteOffset, b.byteOffset + b.length));
const bank = A.buildBank(sf, -2, { rate: 32000, bfp: true, bits: 16 });
const bin = A.writeFm1(bank), dv = new DataView(bin.buffer);
const offExtra = dv.getUint32(48, true), S = dv.getUint32(52, true), offMod = dv.getUint32(56, true), nMod = dv.getUint32(60, true), M = dv.getUint32(64, true);
const source = sf.presets.flatMap((p, i) => A.presetZones(sf, i));
const recordOf = i => { const n = dv.getUint16(offExtra + i * S + 6, true); if (!n) return null;
  return Object.fromEntries(A.MOD_FIELDS.map((f, j) => [f, dv.getInt16(offMod + (n - 1) * M + j * 2, true)])); };
let modOk = 0, withMod = 0, vib = 0, trem = 0, keyed = 0;
source.forEach((s, i) => {
  const r = recordOf(i);
  modOk += JSON.stringify(r) === JSON.stringify(s.mod);
  if (r) { ++withMod; vib += !!(r.lfoPitch || r.vibPitch); trem += !!r.lfoVolume; keyed += !!(r.keyHold || r.keyDecay); }
});
check(offMod && offMod % 4 === 0 && dv.getUint32(16, true) === 76 && M === 40 && (dv.getUint32(68, true) || dv.getUint32(36, true)) === offMod + M * nMod,
  `header offset 56 points at ${nMod} modulation records of ${M} bytes (offset 64), right before the layers and the sample data`);
check(modOk === source.length && vib > 0 && trem > 0 && keyed > 0,
  `every zone points at its own LFOs and key scaling (${withMod} zones: ${vib} with vibrato, ${trem} with tremolo, ${keyed} with key scaling)`);
const program = (p, n) => { const pr = bank.presets.find(x => x.bank === 0 && x.preset === p);
  return recordOf(bank.zones.slice(pr.first, pr.first + pr.count).findIndex(z => z.lo <= n && n <= z.hi) + pr.first); };
const violin = program(40, 67), accordion = program(21, 50), piano = program(0, 60);
check(violin && violin.lfoPitch === 19 && violin.lfoDelay === -2332 && accordion && accordion.lfoVolume === 16 && piano && piano.keyDecay === 33,
  'the violin has 19 cents of vibrato after 0.26 s, the accordion 1.6 dB of tremolo, piano 1 a decay 33 timecents shorter per key');

// ---- 2. The player: one looped sine, 64 samples a period at the bank's rate ----
const tc = s => Math.round(1200 * Math.log2(s));               // seconds, or a ratio, to timecents
function sineBank(rate, mod, env) {
  const pcm = new Int16Array(64 * 100);
  for (let i = 0; i < pcm.length; ++i) pcm[i] = Math.round(16000 * Math.sin(2 * Math.PI * i / 64));
  const sample = { pcm, enc: null, bytes: pcm.byteLength };
  const zone = { lo: 0, hi: 127, root: 60, fine: 0, pan: 0, atten: 0, excl: 0, scale: 100, reverb: 0, chorus: 0, mod: mod ? A.modRecord(mod) : null,
    loop: true, loopStart: 0, loopEnd: pcm.length, sample,
    env: A.envRecord(Object.assign({ attack: -12000, hold: -12000, decay: -12000, sustain: 0, release: -12000 }, env), false) };
  return A.writeFm1({ zones: [zone], samples: [sample], presets: [{ name: 'sine', bank: 0, preset: 0, first: 0, count: 1 }], rate, bits: 16 });
}
const BLOCK = 128;
for (const rate of [44100, 32000]) {
  const w = engine(rate), tag = `${rate / 1000} kHz:`;
  function play(image, note, seconds) {
    new Uint8Array(w.memory.buffer, w.mt_bank(), image.length).set(image);
    if (w.mt_bind(image.length) !== 0) throw new Error('bank rejected');
    w.mt_program(0, 0);
    w.mt_note(0, note, 127);
    const blocks = Math.ceil(seconds * rate / BLOCK), all = new Float64Array(blocks * BLOCK);
    for (let k = 0; k < blocks; ++k) { w.mt_block(); all.set(new Int32Array(w.memory.buffer, w.mt_out(), BLOCK), k * BLOCK); }
    w.mt_panic();
    return all;
  }
  // per window of about 10 ms, a whole number of the sine's periods: the pitch in cents from the rising zero crossings, the level in dB
  const W = 64 * Math.round(rate / 100 / 64);
  function track(a) {
    const up = [];
    for (let i = 1; i < a.length; ++i) if (a[i - 1] < 0 && a[i] >= 0) up.push(i - 1 + a[i - 1] / (a[i - 1] - a[i]));
    const out = [];
    for (let s = 0, k = 0; s + W <= a.length; s += W) {
      while (k < up.length && up[k] < s) ++k;
      let e = k; while (e < up.length && up[e] < s + W) ++e;
      let sq = 0; for (let i = s; i < s + W; ++i) sq += a[i] * a[i];
      const hz = e - k > 2 ? (e - k - 1) / (up[e - 1] - up[k]) * rate : 0;
      out.push({ t: (s + W / 2) / rate, cents: hz ? 1200 * Math.log2(hz / (rate / 64)) : NaN, db: 10 * Math.log10(sq / W + 1e-9) });
    }
    return out;
  }
  const from = (tr, t0, t1) => tr.filter(x => x.t >= t0 && x.t < t1);
  const max = (xs, f) => Math.max(...xs.map(f)), min = (xs, f) => Math.min(...xs.map(f));
  // the rate of an oscillating value: the mean time between its rising crossings of its mean
  function cycleHz(xs, f) {
    const m = xs.reduce((s, x) => s + f(x), 0) / xs.length, at = [];
    for (let i = 1; i < xs.length; ++i) if (f(xs[i - 1]) < m && f(xs[i]) >= m) at.push(xs[i].t);
    return at.length > 2 ? (at.length - 1) / (at[at.length - 1] - at[0]) : 0;
  }

  const plain = track(play(sineBank(rate, null), 60, 1));
  const plainDb = plain[50].db;
  check(max(from(plain, 0.05, 1), x => Math.abs(x.cents)) < 0.5, `${tag} without a record the sine holds its pitch`);

  // a triangle at 2 Hz: a 10 ms window takes about 1 % off its peaks
  const vibrato = from(track(play(sineBank(rate, { vibPitch: 100, vibFreq: tc(2 / 8.176) }), 60, 3.05)), 0.05, 3);
  const hi = max(vibrato, x => x.cents), lo = min(vibrato, x => x.cents), hz = cycleHz(vibrato, x => x.cents);
  check(Math.abs(hi - 100) < 3 && Math.abs(lo + 100) < 3 && Math.abs(hz - 2) < 0.04,
    `${tag} vibrato LFO, 100 cents at 2 Hz: ${lo.toFixed(1)} to +${hi.toFixed(1)} cents at ${hz.toFixed(3)} Hz`);

  const delayed = track(play(sineBank(rate, { lfoPitch: 100, lfoFreq: tc(5 / 8.176), lfoDelay: tc(0.5) }), 60, 1.2));
  const before = max(from(delayed, 0.05, 0.49), x => Math.abs(x.cents)), after = max(from(delayed, 0.6, 1.2), x => x.cents);
  check(before < 0.5 && after > 80, `${tag} mod LFO to pitch after a 0.5 s delay: ${before.toFixed(2)} cents before, up to ${after.toFixed(1)} after`);

  const tremolo = from(track(play(sineBank(rate, { lfoVolume: 60, lfoFreq: tc(2 / 8.176) }), 60, 3.05)), 0.05, 3);
  const up = max(tremolo, x => x.db) - plainDb, down = min(tremolo, x => x.db) - plainDb, th = cycleHz(tremolo, x => x.db);
  const steady = max(tremolo, x => Math.abs(x.cents));
  check(Math.abs(up - 6) < 0.25 && Math.abs(down + 6) < 0.25 && Math.abs(th - 2) < 0.04 && steady < 0.5,
    `${tag} mod LFO to volume, 6 dB at 2 Hz: ${down.toFixed(2)} to +${up.toFixed(2)} dB at ${th.toFixed(3)} Hz, pitch still (${steady.toFixed(2)} cents)`);

  // key scaling: the time from the peak to 20 dB down (decay), and to 1 dB down (hold, then a fast decay)
  const fall = (tr, d) => { const p = tr[1].db; const i = tr.findIndex((x, k) => k > 1 && x.db < p - d);
    return i < 1 ? NaN : tr[i - 1].t + (tr[i - 1].db - (p - d)) / (tr[i - 1].db - tr[i].db) * (tr[i].t - tr[i - 1].t); };
  const decayEnv = { decay: tc(1), sustain: 1000 };
  // the decay's time: 1 over its fall in dB a second, between 50 and 150 ms
  const decayAt = (keyDecay, note) => { const tr = track(play(sineBank(rate, keyDecay ? { keyDecay } : null, decayEnv), note, 0.2));
    const a = tr.find(x => x.t >= 0.05), z = tr.find(x => x.t >= 0.15); return (z.t - a.t) / (a.db - z.db); };
  const d60 = decayAt(100, 60), r72 = decayAt(100, 72) / d60, r48 = decayAt(100, 48) / d60, flat = decayAt(0, 72) / decayAt(0, 60);
  check(Math.abs(r72 - 0.5) < 0.005 && Math.abs(r48 - 2) < 0.02 && Math.abs(flat - 1) < 0.005 && Math.abs(100 * d60 - 1) < 0.02,
    `${tag} key to decay, 100 timecents a key: 1 s for 100 dB at the middle key (${(100 * d60).toFixed(3)} s), an octave up ${r72.toFixed(3)} times as long, an octave down ${r48.toFixed(3)} (none: ${flat.toFixed(3)})`);
  const holdEnv = { hold: tc(0.2), decay: tc(0.05), sustain: 1000 };
  const holdAt = note => fall(track(play(sineBank(rate, { keyHold: 100 }, holdEnv), note, 1)), 1);
  const h72 = holdAt(72), h60 = holdAt(60), h48 = holdAt(48);
  check(Math.abs((h60 - h72) - 0.1) < 0.012 && Math.abs((h48 - h60) - 0.2) < 0.012,
    `${tag} key to hold, 100 timecents a key: 0.2 s at the middle key, ${(h72 - h60 + 0.2).toFixed(3)} s an octave up, ${(h48 - h60 + 0.2).toFixed(3)} s an octave down`);
}

console.log(failed ? `${failed} of ${checks} checks FAILED` : `${checks}/${checks} checks passed`);
process.exit(failed ? 1 : 0);
