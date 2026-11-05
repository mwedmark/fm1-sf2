// The FM-1's SF2 player against FluidSynth, the reference for how a SoundFont should sound. The same MIDI goes to FluidSynth
// with the SoundFont (1mgm.sf2) and to the player's whole output stage on WebAssembly (fm1b + sf2fx + sf2out, 32 kHz and
// resampled to 44.1 kHz, as on the device) with the FM1B bank made from it; both at 44.1 kHz, reverb and chorus off unless --fx.
//   node compare_fluid.js notes [--programs=0,4,...] [--note=60]   velocity sweep: level and brightness per velocity, per program
//   node compare_fluid.js spectra [--programs=..] [--notes=48,60,72]  every program's spectrum by octave band, FM-1 - FluidSynth
//   node compare_fluid.js midi <file.mid> [--seconds=60] [--fx]     a whole MIDI file: level over time, spectrum by octave band
// To find a cause: --interp=1 (FluidSynth linear, as the player), --polyphony=32, --rate=44100 with a 44.1 kHz bank (no resampler)
// Common options: --bank=dist/gm_32k.fm1b  --sf2=1mgm.sf2  --fluidsynth=<exe>  --out=test_fluid/out (WAVs to listen to, A/B)
// FluidSynth: tools/fluidsynth/ (the Windows zip from github.com/FluidSynth/fluidsynth/releases, unpacked; git ignores it).
const fs = require('fs'), path = require('path'), { execFileSync } = require('child_process');
const root = __dirname;
const args = process.argv.slice(2), opt = {}, pos = [];
for (const a of args) { const m = a.match(/^--([^=]+)(?:=(.*))?$/); if (m) opt[m[1]] = m[2] === undefined ? true : m[2]; else pos.push(a); }
const RATE = 44100, BLOCK = 128;
const outDir = path.resolve(root, opt.out || 'test_fluid/out');
fs.mkdirSync(outDir, { recursive: true });

// ---- FluidSynth ----
function findFluid() {
  if (opt.fluidsynth) return opt.fluidsynth;
  const base = path.join(root, 'tools/fluidsynth');
  if (fs.existsSync(base)) for (const d of fs.readdirSync(base)) {
    const exe = path.join(base, d, 'bin/fluidsynth.exe');
    if (fs.existsSync(exe)) return exe;
  }
  return 'fluidsynth';
}
const sf2 = path.resolve(root, opt.sf2 || '1mgm.sf2');
// --interp=0|1|4|7 (none, linear, the default 4-point, 7-point) and --polyphony=N (default 256) change FluidSynth, to find
// what makes a difference.
function fluid(midiPath, wavPath, fx) {
  const extra = [];
  if (opt.polyphony) extra.push('-o', 'synth.polyphony=' + opt.polyphony);
  if (opt.interp !== undefined) { const cmd = path.join(outDir, 'interp.txt'); fs.writeFileSync(cmd, `interp ${opt.interp}
`); extra.push('-f', cmd); }
  execFileSync(findFluid(), ['-n', '-i', '-q', '-r', String(RATE), '-O', 'float', '-T', 'wav', '-g', '0.5', ...extra,
    '-R', fx ? '1' : '0', '-C', fx ? '1' : '0', '-F', wavPath, sf2, midiPath], { stdio: ['ignore', 'ignore', 'inherit'] });
  return readWav(wavPath);
}

// ---- The player ----
const sdk = path.join(root, 'fm1_open/build/tools/wasi-sdk-27.0-x86_64-windows');
const app = path.join(root, 'fm1_open/app');
const sources = [path.join(root, 'test_fluid/wrap.c'), ...['fm1b.c', 'sf2fx.c', 'sf2out.c'].map(f => path.join(app, f))];
const inputs = [...sources, ...['fm1b.h', 'fm1b_config.h', 'sf2fx.h', 'sf2out.h', 'fm1b_dqt.inc', 'fm1b_dqt6.inc', 'resample_table.inc'].map(f => path.join(app, f))];
const linear = !('interp4' in opt);                      // as the firmware: linear (FM1B_INTERP4 0); --interp4 the 4-point one
const playerRate = +(opt.rate || 32000);                 // --rate=44100: the player at 44.1 kHz (no resampler), for a bank made at that rate
function engine() {
  const wasm = path.join(root, `test_fluid/player_${playerRate}${linear ? '_linear' : ''}.wasm`);
  if (!fs.existsSync(wasm) || inputs.some(s => fs.statSync(s).mtimeMs > fs.statSync(wasm).mtimeMs))
    execFileSync(path.join(sdk, 'bin/clang.exe'), ['--sysroot=' + path.join(sdk, 'share/wasi-sysroot'), '-O2', '-DFM1_HOST_TEST', `-DFM1B_RATE=${playerRate}U`, `-DFM1B_INTERP4=${linear ? 0 : 1}`,
      '-mexec-model=reactor', '-Wl,--no-entry', '-Wl,--export-dynamic', '-Wall', '-Werror',
      ...['fm1_open/app', 'fm1_open/app/synth', 'fm1_open/sdk/include'].map(d => '-I' + path.join(root, d)), ...sources, '-o', wasm], { stdio: 'inherit' });
  const w = new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(wasm)), { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }) }).exports;
  if (w._initialize) w._initialize();
  return w;
}
const bankPath = path.resolve(root, opt.bank || 'dist/gm_32k.fm1b');
// Play timed MIDI events [{t, d: [status, ...]}] through the player for `seconds`; stereo Float32 at 44.1 kHz.
function player(events, seconds, fx) {
  const w = engine(), bank = fs.readFileSync(bankPath);
  new Uint8Array(w.memory.buffer, w.cf_bank(), bank.length).set(bank);
  if (w.cf_bind(bank.length) !== 0 || w.cf_program(0) !== 0) throw new Error('bank rejected: ' + bankPath);
  w.cf_fx(fx ? 127 : 0, fx ? 127 : 0);
  const msb = new Uint8Array(16), lsb = new Uint8Array(16);
  const midi = d => {                                    // as transfer.c's fm1_app_midi_received
    const type = d[0] & 0xf0, ch = d[0] & 15;
    if (type === 0x80 || type === 0x90) { const v = type === 0x90 ? d[2] : 0; if (ch === 9) w.cf_drum(ch, d[1], v); else w.cf_note(ch, d[1], v); }
    else if (type === 0xc0) { const drum = msb[ch] === 120 || (msb[ch] === 1 && !lsb[ch]); if (ch !== 9) w.cf_channel_program(ch, drum ? 128 : 0, d[1]); }
    else if (type === 0xb0) {
      if (d[1] === 0) msb[ch] = d[2]; else if (d[1] === 32) lsb[ch] = d[2];
      else if ([91, 93, 1, 64, 6, 38, 100, 101, 7, 11, 10, 120, 121, 123].includes(d[1])) w.cf_control(ch, d[1], d[2]);
    }
    else if (type === 0xe0) w.cf_bend(ch, d[1] | d[2] << 7);
  };
  const blocks = Math.ceil(seconds * RATE / BLOCK), L = new Float32Array(blocks * BLOCK), R = new Float32Array(blocks * BLOCK);
  let e = 0, peakVoices = 0;
  for (let b = 0; b < blocks; ++b) {
    const t = b * BLOCK / RATE;
    while (e < events.length && events[e].t <= t) midi(events[e++].d);
    w.cf_block();
    const o = new Int32Array(w.memory.buffer, w.cf_out(), BLOCK * 2);
    for (let i = 0; i < BLOCK; ++i) { L[b * BLOCK + i] = o[2 * i] / 8388608; R[b * BLOCK + i] = o[2 * i + 1] / 8388608; }
    peakVoices = Math.max(peakVoices, w.cf_sounding());
  }
  return { L, R, peakVoices, cut: w.cf_cut(), limited: w.cf_limited(), voices: w.cf_voice_max() };
}

// ---- MIDI files ----
function readMidi(buf) {
  if (buf.toString('latin1', 0, 4) !== 'MThd') throw new Error('not a MIDI file');
  const division = buf.readUInt16BE(12), ntracks = buf.readUInt16BE(10), raw = [];
  if (division & 0x8000) throw new Error('SMPTE time is not supported');
  let o = 8 + buf.readUInt32BE(4);
  for (let tr = 0; tr < ntracks && o < buf.length; ++tr) {
    const end = o + 8 + buf.readUInt32BE(o + 4);
    let p = o + 8, tick = 0, status = 0;
    const vlq = () => { let v = 0, c; do { c = buf[p++]; v = v << 7 | c & 127; } while (c & 128); return v; };
    while (p < end) {
      tick += vlq();
      let s = buf[p];
      if (s & 128) ++p; else s = status;
      if (s === 0xff) { const type = buf[p++], len = vlq(); if (type === 0x51) raw.push({ tick, tempo: buf.readUIntBE(p, 3), order: raw.length }); p += len; }
      else if (s === 0xf0 || s === 0xf7) { const len = vlq(); p += len; }      // not p += vlq(): p would be read before vlq() moves it
      else {
        status = s;
        const n = (s & 0xf0) === 0xc0 || (s & 0xf0) === 0xd0 ? 1 : 2;
        raw.push({ tick, d: [s, ...buf.subarray(p, p + n)], order: raw.length }); p += n;
      }
    }
    o = end;
  }
  raw.sort((a, b) => a.tick - b.tick || a.order - b.order);
  let tempo = 500000, lastTick = 0, t = 0;
  const events = [];
  for (const r of raw) {
    t += (r.tick - lastTick) * tempo / division / 1e6; lastTick = r.tick;
    if (r.tempo) tempo = r.tempo; else events.push({ t, d: r.d });
  }
  return events;
}
function writeMidi(events, file) {                       // format 0, 1000 ticks a quarter at 120 bpm: a tick is 0.5 ms
  const bytes = [], vlq = v => { const s = [v & 127]; while ((v >>= 7)) s.unshift(v & 127 | 128); bytes.push(...s); };
  let last = 0;
  for (const e of [...events].sort((a, b) => a.t - b.t)) { const tick = Math.round(e.t * 2000); vlq(tick - last); last = tick; bytes.push(...e.d); }
  vlq(0); bytes.push(0xff, 0x2f, 0);
  const head = Buffer.from([0x4d, 0x54, 0x68, 0x64, 0, 0, 0, 6, 0, 0, 0, 1, 0x03, 0xe8, 0x4d, 0x54, 0x72, 0x6b]);
  const len = Buffer.alloc(4); len.writeUInt32BE(bytes.length);
  fs.writeFileSync(file, Buffer.concat([head, len, Buffer.from(bytes)]));
}

// ---- WAV ----
function readWav(file) {
  const b = fs.readFileSync(file);
  let o = 12, fmt, data;
  while (o < b.length) { const id = b.toString('latin1', o, o + 4), n = b.readUInt32LE(o + 4); if (id === 'fmt ') fmt = o + 8; if (id === 'data') data = [o + 8, n]; o += 8 + n + (n & 1); }
  const format = b.readUInt16LE(fmt), ch = b.readUInt16LE(fmt + 2), bits = b.readUInt16LE(fmt + 14);
  const frames = data[1] / (ch * bits / 8), L = new Float32Array(frames), R = new Float32Array(frames);
  for (let i = 0; i < frames; ++i) for (let c = 0; c < ch; ++c) {
    const at = data[0] + (i * ch + c) * bits / 8;
    const v = format === 3 || format === 0xfffe && bits === 32 ? b.readFloatLE(at) : b.readInt16LE(at) / 32768;
    (c === 0 ? L : R)[i] = v; if (ch === 1) R[i] = v;
  }
  return { L, R };
}
function writeWav(file, L, R, gain = 1) {               // 32-bit float stereo
  const n = L.length, b = Buffer.alloc(44 + n * 8);
  b.write('RIFF', 0); b.writeUInt32LE(36 + n * 8, 4); b.write('WAVEfmt ', 8); b.writeUInt32LE(16, 16); b.writeUInt16LE(3, 20); b.writeUInt16LE(2, 22);
  b.writeUInt32LE(RATE, 24); b.writeUInt32LE(RATE * 8, 28); b.writeUInt16LE(8, 32); b.writeUInt16LE(32, 34); b.write('data', 36); b.writeUInt32LE(n * 8, 40);
  for (let i = 0; i < n; ++i) { b.writeFloatLE(L[i] * gain, 44 + 8 * i); b.writeFloatLE(R[i] * gain, 48 + 8 * i); }
  fs.writeFileSync(file, b);
}

// ---- Analysis ----
const N = 4096, hann = Float64Array.from({ length: N }, (_, i) => 0.5 - 0.5 * Math.cos(2 * Math.PI * i / N));
function power(x, from) {                                // the power spectrum of N samples from `from` (Hann window), N/2 bins
  const re = new Float64Array(N), im = new Float64Array(N);
  for (let i = 0; i < N; ++i) re[i] = (x[from + i] || 0) * hann[i];
  for (let i = 1, j = 0; i < N; ++i) { let bit = N >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) { [re[i], re[j]] = [re[j], re[i]]; [im[i], im[j]] = [im[j], im[i]]; } }
  for (let len = 2; len <= N; len <<= 1) {
    const a = -2 * Math.PI / len;
    for (let i = 0; i < N; i += len) for (let k = 0; k < len / 2; ++k) {
      const c = Math.cos(a * k), s = Math.sin(a * k), xr = re[i + k + len / 2] * c - im[i + k + len / 2] * s, xi = re[i + k + len / 2] * s + im[i + k + len / 2] * c;
      re[i + k + len / 2] = re[i + k] - xr; im[i + k + len / 2] = im[i + k] - xi; re[i + k] += xr; im[i + k] += xi;
    }
  }
  return Float64Array.from({ length: N / 2 }, (_, k) => re[k] * re[k] + im[k] * im[k]);
}
const mono = s => Float32Array.from(s.L, (v, i) => (v + s.R[i]) / 2);
const BANDS = [63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000];        // octave bands, centre frequencies
const bandOf = k => { const f = k * RATE / N; for (let b = 0; b < BANDS.length; ++b) if (f < BANDS[b] * Math.SQRT2) return f < BANDS[b] / Math.SQRT2 ? -1 : b; return -1; };
const binBand = Int8Array.from({ length: N / 2 }, (_, k) => bandOf(k));
// Over [from, to) in frames: mean power by band, the spectral centroid (Hz) and the RMS.
function spectrum(x, from, to) {
  const bands = new Float64Array(BANDS.length), sum = new Float64Array(N / 2);
  let frames = 0;
  for (let at = from; at + N <= to; at += N / 2, ++frames) { const p = power(x, at); for (let k = 0; k < N / 2; ++k) sum[k] += p[k]; }
  let pw = 0, pf = 0;
  for (let k = 1; k < N / 2; ++k) { pw += sum[k]; pf += sum[k] * k * RATE / N; if (binBand[k] >= 0) bands[binBand[k]] += sum[k] / Math.max(frames, 1); }
  let ss = 0; for (let i = from; i < to; ++i) ss += x[i] * x[i];
  return { bands, centroid: pf / (pw || 1), rms: Math.sqrt(ss / Math.max(to - from, 1)) };
}
const dB = v => 10 * Math.log10(v + 1e-20), f1 = v => (v >= 0 ? '+' : '') + v.toFixed(1);
// The lag (frames) of b against a that best lines up their 10 ms energy envelopes, within +-100 ms.
function lag(a, b) {
  const hop = 441, env = x => Float64Array.from({ length: Math.floor(x.length / hop) }, (_, i) => { let s = 0; for (let j = 0; j < hop; ++j) s += x[i * hop + j] ** 2; return Math.sqrt(s); });
  const ea = env(a), eb = env(b);
  let best = 0, bestC = -Infinity;
  for (let d = -10; d <= 10; ++d) { let c = 0; for (let i = 0; i < ea.length; ++i) c += ea[i] * (eb[i + d] || 0); if (c > bestC) { bestC = c; best = d; } }
  return best * hop;
}

// ---- notes: the velocity sweep ----
function notes() {
  const programs = (opt.programs || '0,4,19,24,33,48,56,61,73,81,89').split(',').map(Number), note = +(opt.note || 60);
  const velocities = [127, 112, 96, 80, 64, 63, 48, 32, 16], on = 1.2, gap = 1.3;
  const events = [], marks = [];
  let t = 0.1;
  for (const p of programs) {
    events.push({ t, d: [0xc0, p] }); t += 0.1;
    for (const v of velocities) { events.push({ t, d: [0x90, note, v] }, { t: t + on, d: [0x80, note, 0] }); marks.push({ p, v, t }); t += on + gap; }
  }
  events.push({ t, d: [0xb0, 123, 0] });
  const mid = path.join(outDir, 'notes.mid');
  writeMidi(events, mid);
  const ref = fluid(mid, path.join(outDir, 'notes_fluid.wav'), false), ours = player(events, t + 0.5, false);
  const a = mono(ref), b = mono(ours), d = lag(a, b);
  const rows = marks.map(m => {                          // the held part of each note, after its attack
    const from = Math.round((m.t + 0.15) * RATE), to = Math.round((m.t + on) * RATE);
    return { ...m, ref: spectrum(a, from, to), ours: spectrum(b, from + d, to + d) };
  });
  // One gain for the whole render (ours to FluidSynth), from velocity 127 of every program
  const top = rows.filter(r => r.v === 127), gain = Math.sqrt(top.reduce((s, r) => s + r.ref.rms ** 2, 0) / top.reduce((s, r) => s + r.ours.rms ** 2, 0));
  writeWav(path.join(outDir, 'notes_fm1.wav'), ours.L, ours.R, gain);
  console.log(`FluidSynth ${path.basename(findFluid())} with ${path.basename(sf2)} against the player with ${path.basename(bankPath)}, note ${note}, held ${on} s; ` +
    `lag ${(d / RATE * 1000).toFixed(1)} ms, gain to match ${f1(dB(gain * gain))} dB`);
  console.log('level: dB under velocity 127 of the same program; bright: the centroid against velocity 127 of the same program (1.00 = as bright)');
  console.log('prog vel |  level FS  FM-1  diff | bright FS  FM-1 | >2 kHz dB FS  FM-1  diff');
  let sumLevel = 0, sumBright = 0, n = 0;
  for (const r of rows) {
    const t127 = rows.find(x => x.p === r.p && x.v === 127);
    const lf = dB(r.ref.rms ** 2) - dB(t127.ref.rms ** 2), lo = dB(r.ours.rms ** 2) - dB(t127.ours.rms ** 2);
    const bf = r.ref.centroid / t127.ref.centroid, bo = r.ours.centroid / t127.ours.centroid;
    const hi = s => dB(s.bands.slice(5).reduce((x, y) => x + y, 0)), hf = hi(r.ref) - hi(t127.ref), ho = hi(r.ours) - hi(t127.ours);
    if (r.v !== 127) { sumLevel += Math.abs(lo - lf); sumBright += Math.abs(bo - bf); ++n; }
    console.log(`${String(r.p).padStart(4)} ${String(r.v).padStart(3)} | ${f1(lf).padStart(9)} ${f1(lo).padStart(5)} ${f1(lo - lf).padStart(5)} |` +
      ` ${bf.toFixed(2).padStart(9)} ${bo.toFixed(2).padStart(5)} | ${f1(hf).padStart(12)} ${f1(ho).padStart(5)} ${f1(ho - hf).padStart(5)}`);
  }
  console.log(`mean |level diff| ${(sumLevel / n).toFixed(2)} dB, mean |brightness diff| ${(sumBright / n).toFixed(3)} (velocity 127 left out)`);
  console.log(`WAVs: ${path.join(outDir, 'notes_fluid.wav')} and notes_fm1.wav (level matched)`);
}

// ---- spectra: every program's spectrum against FluidSynth's, by octave band ----
function spectra() {
  const programs = opt.programs ? opt.programs.split(',').map(Number) : [...Array(128).keys()];
  const keys = (opt.notes || '48,60,72').split(',').map(Number), on = 1.0, gap = 0.6, vel = +(opt.velocity || 100);
  const events = [], marks = [];
  let t = 0.1;
  for (const p of programs) {
    events.push({ t, d: [0xc0, p] }); t += 0.05;
    for (const n of keys) { events.push({ t, d: [0x90, n, vel] }, { t: t + on, d: [0x80, n, 0] }); marks.push({ p, n, t }); t += on + gap; }
  }
  if (opt.drums) for (const n of opt.drums.split(',').map(Number)) { events.push({ t, d: [0x99, n, vel] }, { t: t + 0.3, d: [0x89, n, 0] }); marks.push({ p: 'kit', n, t }); t += on + gap; }
  events.push({ t, d: [0xb0, 123, 0] });
  const mid = path.join(outDir, 'spectra.mid');
  writeMidi(events, mid);
  const a = mono(fluid(mid, path.join(outDir, 'spectra_fluid.wav'), false)), ours = player(events, t + 0.5, false), b = mono(ours);
  const rows = marks.map(m => { const from = Math.round(m.t * RATE), to = Math.round((m.t + on) * RATE); return { ...m, ref: spectrum(a, from, to), ours: spectrum(b, from, to) }; });
  const g = dB(rows.reduce((s, r) => s + r.ref.rms ** 2, 0) / rows.reduce((s, r) => s + r.ours.rms ** 2, 0));
  writeWav(path.join(outDir, 'spectra_fm1.wav'), ours.L, ours.R, 10 ** (g / 20));
  console.log(`velocity ${vel}, notes ${keys.join(' ')}, held ${on} s; one gain for all: ${f1(g)} dB. Per program: FM-1 - FluidSynth by octave band (dB), the notes summed`);
  console.log('prog note  level ' + BANDS.map(f => String(f >= 1000 ? f / 1000 + 'k' : f).padStart(6)).join(''));
  const total = BANDS.map(() => [0, 0]), byProgram = [];
  for (const p of [...new Set(rows.map(r => r.p))]) {
    const rs = rows.filter(r => r.p === p);
    const band = k => { const x = rs.reduce((s, r) => s + r.ours.bands[k], 0), y = rs.reduce((s, r) => s + r.ref.bands[k], 0); return { d: dB(x) + g - dB(y), y }; };
    const lv = dB(rs.reduce((s, r) => s + r.ours.rms ** 2, 0)) + g - dB(rs.reduce((s, r) => s + r.ref.rms ** 2, 0));
    const top = Math.max(...BANDS.map((_, k) => band(k).y)), cells = BANDS.map((_, k) => { const x = band(k); return dB(x.y) > dB(top) - 40 ? x.d : NaN; });
    cells.forEach((v, k) => { if (!isNaN(v)) { total[k][0] += v; total[k][1]++; } });
    byProgram.push({ p, lv, cells });
    console.log(`${String(p).padStart(4)}  all ${f1(lv).padStart(6)} ` + cells.map(v => (isNaN(v) ? '.' : f1(v)).padStart(6)).join(''));
  }
  console.log('mean             ' + total.map(([s, n]) => (n ? f1(s / n) : '.').padStart(6)).join(''));
  const hi = r => [6, 7].map(k => r.cells[k]).filter(v => !isNaN(v)), worst = byProgram.filter(r => hi(r).length).sort((x, y) => Math.min(...hi(x)) - Math.min(...hi(y))).slice(0, 12);
  console.log('most treble lost (4k/8k): ' + worst.map(r => `${r.p} (${hi(r).map(f1).join('/')})`).join(', '));
  console.log(`WAVs: ${path.join(outDir, 'spectra_fluid.wav')} and spectra_fm1.wav`);
}

// ---- midi: a whole file ----
function midiFile() {
  const file = path.resolve(pos[1] || ''), fx = !!opt.fx;
  const all = readMidi(fs.readFileSync(file)), seconds = Math.min(+(opt.seconds || 60), all.length ? all[all.length - 1].t + 2 : 0);
  const events = all.filter(e => e.t < seconds);
  events.push({ t: seconds - 0.01, d: [0xb0, 123, 0] });
  for (let c = 0; c < 16; ++c) events.push({ t: seconds - 0.01, d: [0xb0 | c, 123, 0] });
  const name = path.basename(file, path.extname(file)).replace(/[^\w.-]+/g, '_'), mid = path.join(outDir, name + '.mid');
  writeMidi(events, mid);
  const ref = fluid(mid, path.join(outDir, name + '_fluid.wav'), fx), ours = player(events, seconds, fx);
  const a = mono(ref), b0 = mono(ours), d = lag(a, b0), len = Math.min(a.length, b0.length - Math.max(d, 0));
  const b = b0.subarray(Math.max(d, 0), Math.max(d, 0) + len);
  // Level over time (100 ms frames, where FluidSynth is above -50 dB of its loudest), after one gain for the whole file
  const hop = 4410, fr = Math.floor(len / hop), la = [], lb = [];
  for (let i = 0; i < fr; ++i) { let sa = 0, sb = 0; for (let j = i * hop; j < (i + 1) * hop; ++j) { sa += a[j] ** 2; sb += b[j] ** 2; } la.push(dB(sa / hop)); lb.push(dB(sb / hop)); }
  const sumA = a.reduce((s, v) => s + v * v, 0), sumB = b.reduce((s, v) => s + v * v, 0), g = dB(sumA / sumB);
  const loud = Math.max(...la), idx = la.map((v, i) => v > loud - 50 ? i : -1).filter(i => i >= 0);
  const mean = v => v.reduce((x, y) => x + y, 0) / v.length;
  const diffs = idx.map(i => lb[i] + g - la[i]), ma = mean(idx.map(i => la[i])), mb = mean(idx.map(i => lb[i]));
  const corr = mean(idx.map(i => (la[i] - ma) * (lb[i] - mb))) / Math.sqrt(mean(idx.map(i => (la[i] - ma) ** 2)) * mean(idx.map(i => (lb[i] - mb) ** 2)));
  // Spectrum: the whole file by octave band, and band by band frame for frame (the spectral distance)
  const sa = spectrum(a, 0, len), sb = spectrum(b, 0, len);
  let dist = 0, nd = 0;
  for (let at = 0; at + N <= len; at += N * 4) {
    const pa = power(a, at), pb = power(b, at), ba = new Float64Array(BANDS.length), bb = new Float64Array(BANDS.length);
    for (let k = 1; k < N / 2; ++k) if (binBand[k] >= 0) { ba[binBand[k]] += pa[k]; bb[binBand[k]] += pb[k]; }
    const top = Math.max(...ba);
    if (dB(top) < dB(sa.rms ** 2 * N) - 40) continue;
    let s = 0, m = 0;
    for (let k = 0; k < BANDS.length; ++k) if (dB(ba[k]) > dB(top) - 60) { s += (dB(bb[k]) + g - dB(ba[k])) ** 2; ++m; }
    if (m) { dist += Math.sqrt(s / m); ++nd; }
  }
  writeWav(path.join(outDir, name + '_fm1.wav'), ours.L, ours.R, Math.sqrt(sumA / sumB));
  console.log(`${path.basename(file)}: ${seconds.toFixed(1)} s, ${events.length} events, reverb and chorus ${fx ? 'on' : 'off'}; lag ${(d / RATE * 1000).toFixed(1)} ms, gain to match ${f1(g)} dB`);
  console.log(`player: up to ${ours.peakVoices} of ${ours.voices} voices, ${ours.cut} cut for the limit, the limiter held ${(ours.limited / RATE).toFixed(1)} s`);
  console.log(`level over time (100 ms): correlation ${corr.toFixed(3)}, mean |diff| ${mean(diffs.map(Math.abs)).toFixed(2)} dB, 90th percentile ${diffs.map(Math.abs).sort((x, y) => x - y)[Math.floor(diffs.length * 0.9)].toFixed(2)} dB`);
  console.log('octave band (Hz) ' + BANDS.map(f => String(f >= 1000 ? f / 1000 + 'k' : f).padStart(6)).join(''));
  console.log('FM-1 - FS (dB)   ' + BANDS.map((f, k) => f1(dB(sb.bands[k]) + g - dB(sa.bands[k])).padStart(6)).join(''));
  console.log(`spectral distance ${(dist / nd).toFixed(2)} dB (RMS of the band differences, ${nd} frames of 93 ms); centroid FS ${sa.centroid.toFixed(0)} Hz, FM-1 ${sb.centroid.toFixed(0)} Hz`);
  console.log(`WAVs: ${path.join(outDir, name + '_fluid.wav')} and ${name}_fm1.wav (level matched)`);
}

if (pos[0] === 'notes') notes();
else if (pos[0] === 'spectra') spectra();
else if (pos[0] === 'midi' && pos[1]) midiFile();
else { console.log(fs.readFileSync(__filename, 'utf8').split('\n').slice(0, 7).join('\n')); process.exit(1); }
