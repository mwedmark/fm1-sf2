// The zone's low-pass filter (SF2 generators 8 and 9), the modulation envelope that moves its cutoff and the pitch (25-30, 11, 7)
// and the mod LFO on the cutoff (10): FORMAT.md, "Modulation". The real fm1b.c on WebAssembly at 44.1 kHz (the tests' rate) and
// 32 kHz (the device's), with a bank of one looped sine, against the same filter computed in floating point; then 1mgm.sf2.
//   node test_filter.js [1mgm.sf2]
const fs = require('fs'), path = require('path'), { execFileSync } = require('child_process');
const root = __dirname;
const sdk = path.join(root, 'fm1_open/build/tools/wasi-sdk-27.0-x86_64-windows');
const sources = [path.join(root, 'test_mt/wrap.c'), path.join(root, 'fm1_open/app/fm1b.c'), path.join(root, 'fm1_open/web/synth/sea_decode.c')];
const headers = ['fm1b.h', 'fm1b_config.h', 'fm1b_dqt.inc', 'fm1b_dqt6.inc'].map(h => path.join(root, 'fm1_open/app', h));
function engine(rate) {
  const wasmPath = path.join(root, `test_mt/filter_${rate}.wasm`);
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
  '\nglobalThis.api = { parseSf2, presetZones, buildBank, writeFm1, envRecord, modRecord };');
const A = globalThis.api;
let failed = 0, checks = 0;
function check(ok, what) { ++checks; if (!ok) { ++failed; console.log('FAIL', what); } else console.log('ok  ', what); }

const tc = s => Math.round(1200 * Math.log2(s));                  // seconds, or a ratio, to timecents
const hzCents = hz => Math.round(1200 * Math.log2(hz / 8.176));    // a frequency as SF2 absolute cents
// The filter in floating point, as FORMAT.md describes it: its gain in dB at frequency f.
function responseDb(fcCents, q, f, rate) {
  const f0 = Math.min(8.176 * Math.pow(2, Math.max(1500, Math.min(13500, fcCents)) / 1200), 0.45 * rate);
  const w0 = 2 * Math.PI * f0 / rate, ql = Math.pow(10, (q / 10 - 3.01) / 20), gain = 1 / Math.sqrt(ql), alpha = Math.sin(w0) / (2 * ql);
  const a0 = 1 + alpha, b0 = (1 - Math.cos(w0)) / 2 / a0 * gain, a1 = -2 * Math.cos(w0) / a0, a2 = (1 - alpha) / a0;
  const w = 2 * Math.PI * f / rate, re = (z, c0, c1, c2) => [c0 + c1 * Math.cos(w) + c2 * Math.cos(2 * w), -c1 * Math.sin(w) - c2 * Math.sin(2 * w)];
  const [nr, ni] = re(0, b0, 2 * b0, b0), [dr, di] = re(0, 1, a1, a2);
  return 10 * Math.log10((nr * nr + ni * ni) / (dr * dr + di * di));
}
function sineBank(rate, mod, env) {
  const pcm = new Int16Array(64 * 100);
  for (let i = 0; i < pcm.length; ++i) pcm[i] = Math.round(12000 * Math.sin(2 * Math.PI * i / 64));
  const sample = { pcm, enc: null, bytes: pcm.byteLength };
  const zone = { lo: 0, hi: 127, root: 60, fine: 0, pan: 0, atten: 0, excl: 0, scale: 100, reverb: 0, chorus: 0, mod: mod ? A.modRecord(mod) : null,
    loop: true, loopStart: 0, loopEnd: pcm.length, sample,
    env: A.envRecord(Object.assign({ attack: -12000, hold: -12000, decay: -12000, sustain: 0, release: tc(0.05) }, env), false) };
  return A.writeFm1({ zones: [zone], samples: [sample], presets: [{ name: 'sine', bank: 0, preset: 0, first: 0, count: 1 }], rate, bits: 16 });
}
// A bank of the sf2-lfo branch: the 64-byte header, records of 20 bytes (their LFOs and key scaling only).
function to64(bin) {
  const d = new DataView(bin.buffer, bin.byteOffset), mods = d.getUint32(56, true), n = d.getUint32(60, true), size = d.getUint32(64, true);
  const smp = d.getUint32(36, true), cut = (size - 20) * n, at = x => x < mods ? x - 4 : x >= smp ? x - 4 - cut : x - 4;
  const o = new Uint8Array(bin.length - 4 - cut), od = new DataView(o.buffer);
  o.set(bin.subarray(0, 64)); o.set(bin.subarray(68, mods), 64);
  for (let i = 0; i < n; ++i) o.set(bin.subarray(mods + i * size, mods + i * size + 20), at(mods) + i * 20);
  o.set(bin.subarray(smp), at(smp));
  [16, 20, 24, 28, 32, 36, 40, 44, 48, 56].forEach(f => { const v = d.getUint32(f, true); od.setUint32(f, v ? at(v) : 0, true); });
  const zones = d.getUint32(32, true), nz = d.getUint16(14, true);
  for (let i = 0; i < nz; ++i) od.setUint32(at(zones) + i * 24, at(d.getUint32(zones + i * 24, true)), true);
  return o;
}

const BLOCK = 128;
for (const rate of [44100, 32000]) {
  const w = engine(rate), tag = `${rate / 1000} kHz:`, f0 = rate / 64;
  function play(image, note, seconds, offAt, velocity = 127) {
    new Uint8Array(w.memory.buffer, w.mt_bank(), image.length).set(image);
    if (w.mt_bind(image.length) !== 0) throw new Error('bank rejected');
    w.mt_program(0, 0);
    w.mt_note(0, note, velocity);
    const blocks = Math.ceil(seconds * rate / BLOCK), all = new Float64Array(blocks * BLOCK);
    for (let k = 0; k < blocks; ++k) {
      if (offAt !== undefined && k === Math.round(offAt * rate / BLOCK)) w.mt_note(0, note, 0);
      w.mt_block(); all.set(new Int32Array(w.memory.buffer, w.mt_out(), BLOCK), k * BLOCK);
    }
    w.mt_panic();
    return all;
  }
  const rms = (a, t0, n) => { let s = 0; const i0 = Math.round(t0 * rate); for (let i = i0; i < i0 + n; ++i) s += a[i] * a[i]; return Math.sqrt(s / n); };
  const db = (x, ref) => 20 * Math.log10(x / ref);

  // 1. The steady response: octaves of the sine (exact samples, no interpolation) through a 1 kHz filter, flat and resonant.
  const notes = [48, 60, 72, 84, 96], plain = notes.map(n => rms(play(sineBank(rate, null), n, 0.5), 0.3, 6400));
  // and near the top: 8 kHz, and 20 kHz held at 0.45 of the rate
  for (const [fc, q] of [[1000, 0], [1000, 100], [1000, 190], [8000, 100], [20000, 190]]) {
    let worst = 0;
    const got = notes.map((n, i) => {
      const f = f0 * Math.pow(2, (n - 60) / 12), want = responseDb(hzCents(fc), q, f, rate);
      const have = db(rms(play(sineBank(rate, { filterFc: Math.min(13499, hzCents(fc)), filterQ: q }), n, 0.5), 0.3, 6400), plain[i]);
      worst = Math.max(worst, Math.abs(have - want) / (want < -40 ? 5 : 1));
      return `${Math.round(f)} Hz ${have.toFixed(2)} (${want.toFixed(2)})`;
    });
    // 64 bits within 0.01 dB; a cutoff that holds still at 1/50 of the rate or more runs in 32 bits, its coefficients in Q13: 0.2 dB
    check(worst < 0.2, `${tag} ${fc / 1000} kHz low-pass, Q ${q} cB: ${got.join(', ')} dB`);
  }
  // 1b. Sample by sample against the same filter in floating point on the voice's own unfiltered output, at full level: what the
  // integer coefficients change (64 bits: nothing to speak of; 32 bits, Q13: under 1 %, some 0.1 dB). Then the noise alone: a note
  // 30 dB down (velocity 23), scaled up, against the loud one; the unfiltered voice's own rounding the same way is the yardstick.
  // 0.24 of the rate, with all of the resonance, is just under where the 32 bits stop.
  {
    const float = (input, fcCents, q) => {
      const f0 = Math.min(8.176 * Math.pow(2, fcCents / 1200), 0.45 * rate), w0 = 2 * Math.PI * f0 / rate;
      const ql = Math.pow(10, (q / 10 - 3.01) / 20), alpha = Math.sin(w0) / (2 * ql), a0 = 1 + alpha;
      const b0 = (1 - Math.cos(w0)) / 2 / a0 / Math.sqrt(ql), a1 = -2 * Math.cos(w0) / a0, a2 = (1 - alpha) / a0;
      const y = new Float64Array(input.length);
      let x1 = 0, x2 = 0, y1 = 0, y2 = 0;
      for (let i = 0; i < input.length; ++i) { y[i] = b0 * (input[i] + 2 * x1 + x2) - a1 * y1 - a2 * y2; x2 = x1; x1 = input[i]; y2 = y1; y1 = y[i]; }
      return y;
    };
    const snr = (ref, got, scale = 1) => { let sig = 0, err = 0;
      for (let i = Math.round(0.15 * rate); i < ref.length; ++i) { sig += ref[i] * ref[i]; err += (got[i] * scale - ref[i]) ** 2; } return 10 * Math.log10(sig / err); };
    const rows = [], noise = [];
    let bad = 0;
    for (const [hz, q, note, need] of [[100, 60, 48, 70], [1000, 0, 72, 40], [1000, 190, 72, 40], [4000, 100, 84, 40], [Math.round(rate * 0.24), 190, 84, 40]]) {
      const bank = sineBank(rate, { filterFc: hzCents(hz), filterQ: q });
      const dry = play(sineBank(rate, null), note, 0.4), wet = play(bank, note, 0.4);
      const dryQ = play(sineBank(rate, null), note, 0.4, undefined, 23), wetQ = play(bank, note, 0.4, undefined, 23);
      const k = Math.sqrt(dry.reduce((s, v) => s + v * v, 0) / dryQ.reduce((s, v) => s + v * v, 0));
      const coef = snr(float(dry, hzCents(hz), q), wet), own = snr(dry, dryQ, k), filt = snr(wet, wetQ, k);
      bad += coef < need || filt < Math.min(54, own - 3);   // 54 dB: under the noise of the BFP8 samples themselves (8-bit mantissas)
      rows.push(`${hz} Hz Q ${q} ${coef.toFixed(0)}`);
      noise.push(`${hz} Hz Q ${q} ${filt.toFixed(0)} (voice ${own.toFixed(0)})`);
    }
    check(!bad, `${tag} against floating point, signal to difference in dB: ${rows.join(', ')}; a note 30 dB down, signal to noise: ${noise.join(', ')}`);
  }
  // 1c. A sweep from 200 Hz to the top and back (64 bits while it moves, 32 bits while it holds): no click as it changes way, the
  // level as the floating-point filter's.
  {
    const mod = { filterFc: hzCents(200), filterQ: 20, envFc: 8000, envAttack: tc(0.3), envDecay: tc(0.6), envSustain: 700, envHold: tc(0.3) };
    const a = play(sineBank(rate, mod), 72, 1.6), ref = plain[2];
    let jump = 0;
    for (let i = 2; i < a.length; ++i) jump = Math.max(jump, Math.abs(a[i] - 2 * a[i - 1] + a[i - 2]));
    const smooth = (2 * Math.PI * f0 * 2 / rate) ** 2 * Math.max(...a.map(Math.abs));   // a sine's own second difference at its peak
    const env = t => t < 0.3 ? t / 0.3 : t < 0.6 ? 1 : Math.max(0.3, 1 - (t - 0.6) / 0.6);
    let worst = 0;
    for (const t of [0.15, 0.45, 1.3]) worst = Math.max(worst, Math.abs(db(rms(a, t, 640), ref) - responseDb(hzCents(200) + 8000 * env(t + 0.01), 20, f0 * 2, rate)));
    check(jump < 1.2 * smooth && worst < 0.6, `${tag} a sweep to the top and back: no click (second difference ${(jump / smooth).toFixed(2)} of a sine's), level within ${worst.toFixed(2)} dB`);
  }
  // 2. A low cutoff (100 Hz, the poles close to 1): the right level, and silence after the release (no limit cycles).
  {
    const bank = sineBank(rate, { filterFc: hzCents(100), filterQ: 60 }), a = play(bank, 48, 1.2, 0.6);
    const have = db(rms(a, 0.3, 6400), plain[0]), want = responseDb(hzCents(100), 60, f0 / 2, rate);
    const tail = a.subarray(Math.round(1.0 * rate)).reduce((m, v) => Math.max(m, Math.abs(v)), 0);
    check(Math.abs(have - want) < 0.15 && tail === 0, `${tag} 100 Hz low-pass, Q 60: ${have.toFixed(2)} dB (${want.toFixed(2)}), silent after the release (peak ${tail})`);
  }
  // 3. The modulation envelope on the cutoff: 1 kHz up two octaves in a 0.5 s attack, back in a 1 s decay to sustain 0.
  {
    const note = 84, f = f0 * 4, mod = { filterFc: hzCents(1000), envFc: 2400, envAttack: tc(0.5), envDecay: tc(1), envSustain: 1000 };
    const a = play(sineBank(rate, mod), note, 1.6), ref = plain[3];
    const env = t => t < 0.5 ? t / 0.5 : Math.max(0, 1 - (t - 0.5));
    let worst = 0;
    const got = [0.1, 0.25, 0.45, 0.75, 1.0, 1.4].map(t => {
      const want = responseDb(hzCents(1000) + 2400 * env(t + 0.005), 0, f, rate), have = db(rms(a, t, 320), ref);
      worst = Math.max(worst, Math.abs(have - want));
      return `${t} s ${have.toFixed(1)} (${want.toFixed(1)})`;
    });
    check(worst < 0.6, `${tag} envelope to cutoff, +2400 cents: ${Math.round(f)} Hz at ${got.join(', ')} dB`);
  }
  // 4. The modulation envelope on the pitch: +1200 cents in a 0.5 s attack, so +600 at 0.25 s.
  {
    const a = play(sineBank(rate, { envPitch: 1200, envAttack: tc(0.5), envDecay: tc(100), envSustain: 0 }), 60, 0.8);
    const pitch = t => { const i0 = Math.round(t * rate), up = [];
      for (let i = i0; i < i0 + Math.round(rate / 50); ++i) if (a[i - 1] < 0 && a[i] >= 0) up.push(i - 1 + a[i - 1] / (a[i - 1] - a[i]));
      return 1200 * Math.log2((up.length - 1) / (up[up.length - 1] - up[0]) * rate / f0); };
    const p0 = pitch(0.01), p1 = pitch(0.24), p2 = pitch(0.6);
    check(Math.abs(p0 - 48) < 15 && Math.abs(p1 - 600) < 15 && Math.abs(p2 - 1200) < 3,
      `${tag} envelope to pitch, +1200 cents in 0.5 s: ${p0.toFixed(0)}, ${p1.toFixed(0)}, ${p2.toFixed(0)} cents at 0.02, 0.25, 0.6 s`);
  }
  // 5. The mod LFO on the cutoff: 1 kHz +-1200 cents at 2 Hz, a 2 kHz-ish tone swings between the responses at 500 Hz and 2 kHz.
  {
    const note = 72, f = f0 * 2, a = play(sineBank(rate, { filterFc: hzCents(1000), lfoFc: 1200, lfoFreq: tc(2 / 8.176) }), note, 2.1);
    const levels = []; for (let t = 0.1; t < 2; t += 0.01) levels.push(db(rms(a, t, 64 * 4), plain[2]));
    const hi = Math.max(...levels), lo = Math.min(...levels), wantHi = responseDb(hzCents(2000), 0, f, rate), wantLo = responseDb(hzCents(500), 0, f, rate);
    check(Math.abs(hi - wantHi) < 0.4 && Math.abs(lo - wantLo) < 0.6,
      `${tag} mod LFO to cutoff, +-1200 cents: ${lo.toFixed(1)} to ${hi.toFixed(1)} dB (${wantLo.toFixed(1)} to ${wantHi.toFixed(1)})`);
  }
  // 6. A bank of the sf2-lfo branch (64-byte header, 20-byte records): its LFOs play, it has no filter.
  {
    const vib = { vibPitch: 50, vibFreq: tc(3 / 8.176) }, both = Object.assign({ filterFc: hzCents(1000) }, vib);
    const same = (x, y) => x.length === y.length && x.every((v, i) => v === y[i]);
    check(same(play(to64(sineBank(rate, both)), 72, 0.5), play(sineBank(rate, vib), 72, 0.5)) &&
          !same(play(sineBank(rate, both), 72, 0.5), play(sineBank(rate, vib), 72, 0.5)),
      `${tag} a bank with the 64-byte header plays its LFOs and no filter`);
  }
}

// ---- 1mgm.sf2 ----
{
  const b = fs.readFileSync(process.argv[2] || path.join(root, '1mgm.sf2'));
  const sf = A.parseSf2(b.buffer.slice(b.byteOffset, b.byteOffset + b.length));
  const bank = A.buildBank(sf, -2, { rate: 32000, bfp: true, bits: 16 });
  const bin = A.writeFm1(bank), dv = new DataView(bin.buffer), ex = dv.getUint32(48, true), S = dv.getUint32(52, true);
  // the same bank with every filter off (cutoff 13500, no envelope or LFO on it), its LFOs and the rest as they are
  const off = bin.slice(), od = new DataView(off.buffer), mods = dv.getUint32(56, true), M = dv.getUint32(64, true);
  for (let r = 0; r < dv.getUint32(60, true); ++r) { od.setInt16(mods + r * M + 18, 13500, true); od.setInt16(mods + r * M + 22, 0, true); od.setInt16(mods + r * M + 24, 0, true); }
  const w = engine(32000);
  const play = (image, program, note) => {
    new Uint8Array(w.memory.buffer, w.mt_bank(), image.length).set(image);
    if (w.mt_bind(image.length) !== 0) throw new Error('bank rejected');
    w.mt_program(0, program); w.mt_note(0, note, 100);
    const all = new Float64Array(BLOCK * 200);
    for (let k = 0; k < 200; ++k) { w.mt_block(); all.set(new Int32Array(w.memory.buffer, w.mt_out(), BLOCK), k * BLOCK); }
    w.mt_panic(); return all;
  };
  // brightness: the first difference's level against the signal's (higher for more treble)
  const bright = a => { let s = 0, d = 0; for (let i = 1; i < a.length; ++i) { s += a[i] * a[i]; d += (a[i] - a[i - 1]) ** 2; } return Math.sqrt(d / s); };
  const rows = [[38, 'Synth Bass 1', 36], [89, 'Warm Pad', 60]].map(([p, name, n]) => {
    const f = play(bin, p, n), u = play(off, p, n);
    return { name, ok: f.some(v => v !== 0) && bright(f) < 0.9 * bright(u), r: bright(f) / bright(u) };
  });
  check(rows.every(r => r.ok), `1mgm.sf2: the filter darkens ${rows.map(r => `${r.name} (treble ${(100 * r.r).toFixed(0)} %)`).join(', ')}`);
  // every program with a filter, three notes: it sounds, and its peak stays within 6 dB of the unfiltered one's (resonance, no blow-up)
  const filtered = bank.presets.filter(p => p.bank === 0 && bank.zones.slice(p.first, p.first + p.count).some(z => z.mod && (z.mod.filterFc < 13500 || z.mod.envFc || z.mod.lfoFc)));
  let worst = -99, silent = 0;
  for (const p of filtered) for (const n of [36, 60, 84]) {
    const peak = a => a.reduce((m, v) => Math.max(m, Math.abs(v)), 0), f = peak(play(bin, p.preset, n)), u = peak(play(off, p.preset, n));
    if (!u) continue;
    if (!f) ++silent; else worst = Math.max(worst, 20 * Math.log10(f / u));
  }
  check(filtered.length > 40 && !silent && worst < 6, `1mgm.sf2: ${filtered.length} programs with a filter all sound, peaks at most ${worst.toFixed(1)} dB over unfiltered`);
}

console.log(failed ? `${failed} of ${checks} checks FAILED` : `${checks}/${checks} checks passed`);
process.exit(failed ? 1 : 0);
