// Compares a recording of the FM-1's USB audio with the host render of the same song (render_host.js): what the unit played against what it should play.
//   node analyze_usb.js <recording.wav> <host.wav> [--start=<seconds into the recording, where the song begins>] [--length=60] [--search=4] [--json=out.json] [--aligned=out.wav]
// Both are read through ffmpeg as 44.1 kHz stereo; the song's start in the recording is found by correlating the loudness envelopes (+-search s around --start).
// Reports: lag, gain, spectral distance (dB per octave band over the frames where the host plays), frames where the unit is much quieter than the host
// (missing notes), clicks and zero runs against the host's own, and the level. It measures how near the unit is to the host render, not how good it sounds.
const fs = require('fs'), { execFileSync } = require('child_process');
const opt = {}, pos = [];
for (const a of process.argv.slice(2)) { const m = a.match(/^--([^=]+)=(.*)$/); if (m) opt[m[1]] = m[2]; else pos.push(a); }
if (pos.length !== 2) { console.error('usage: node analyze_usb.js <recording.wav> <host.wav> [--start=s] [--length=60] [--search=4] [--json=f] [--aligned=f.wav]'); process.exit(1); }
const R = 44100;
function read(file) {                       // [left, right] Float32Array at 44.1 kHz
  const buf = execFileSync('ffmpeg', ['-v', 'error', '-i', file, '-f', 'f32le', '-ac', '2', '-ar', String(R), '-'], { maxBuffer: 1 << 30 });
  const f = new Float32Array(buf.buffer, buf.byteOffset, buf.length >> 2), n = f.length >> 1, l = new Float32Array(n), r = new Float32Array(n);
  for (let i = 0; i < n; ++i) { l[i] = f[2 * i]; r[i] = f[2 * i + 1]; }
  return [l, r];
}
const dev = read(pos[0]), host = read(pos[1]);
const startGuess = +(opt.start || 0), length = +(opt.length || 60), search = +(opt.search || 4);
const mono = c => { const m = new Float32Array(c[0].length); for (let i = 0; i < m.length; ++i) m[i] = (c[0][i] + c[1][i]) / 2; return m; };
const dm = mono(dev), hm = mono(host);
const HOP = 441;                             // 10 ms envelope
const env = (x, from, n) => { const e = new Float64Array(n); for (let k = 0; k < n; ++k) { let s = 0; const a = from + k * HOP; for (let i = 0; i < HOP; ++i) { const v = x[a + i] || 0; s += v * v; } e[k] = Math.log(Math.sqrt(s / HOP) + 1e-4); } return e; };
const hostLen = Math.min(Math.floor(length * R / HOP), Math.floor((hm.length - HOP) / HOP));
const he = env(hm, 0, hostLen);
// search the lag (in 10 ms steps, then in samples) that makes the device envelope follow the host's
let best = { c: -Infinity, lag: 0 };
const lo = Math.round((startGuess - search) * 100), hi = Math.round((startGuess + search) * 100);
const hmean = he.reduce((a, b) => a + b, 0) / he.length;
for (let lag = lo; lag <= hi; ++lag) {
  const start = lag * HOP; if (start < 0 || start + hostLen * HOP > dm.length) continue;
  const de = env(dm, start, hostLen), dmean = de.reduce((a, b) => a + b, 0) / de.length;
  let c = 0, a = 0, b = 0; for (let k = 0; k < hostLen; ++k) { const x = de[k] - dmean, y = he[k] - hmean; c += x * y; a += x * x; b += y * y; }
  c /= Math.sqrt(a * b) + 1e-12; if (c > best.c) best = { c, lag };
}
// the sample-exact lag: raw correlation of the low-passed (decimated) signals around the coarse lag
let lagSamples = best.lag * HOP, fine = { c: -Infinity };
{
  const L = 3 * R, span = HOP * 2, h0 = 5 * R;                       // 3 s of the host from 5 s in
  for (let d = -span; d <= span; ++d) {
    let c = 0, a = 0, b = 0;
    for (let i = 0; i < L; i += 2) { const x = dm[lagSamples + d + h0 + i] || 0, y = hm[h0 + i]; c += x * y; a += x * x; b += y * y; }
    c /= Math.sqrt(a * b) + 1e-12; if (c > fine.c) fine = { c, d };
  }
  lagSamples += fine.d;
}
const total = Math.min(length * R, hm.length, dev[0].length - lagSamples);
const dl = dev[0].subarray(lagSamples, lagSamples + total), dr = dev[1].subarray(lagSamples, lagSamples + total), hl = host[0].subarray(0, total), hr = host[1].subarray(0, total);
// gain: least squares over the whole span (the unit's level is the knob's)
// (gain scales the unit to the host: the RMS ratio, not a least-squares fit: the samples of two renderings of the same song do not line up, only their loudness does)
let sdd = 0, shh = 0; for (let i = 0; i < total; ++i) { sdd += dl[i] * dl[i] + dr[i] * dr[i]; shh += hl[i] * hl[i] + hr[i] * hr[i]; }
const gain = Math.sqrt(shh / (sdd + 1e-20));
// per frame octave-band energies
const N = 4096, bands = [[0, 125], [125, 250], [250, 500], [500, 1000], [1000, 2000], [2000, 4000], [4000, 8000], [8000, 16000]];
function fft(re, im) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; ++i) { let bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) { [re[i], re[j]] = [re[j], re[i]]; [im[i], im[j]] = [im[j], im[i]]; } }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = -2 * Math.PI / len, wr = Math.cos(ang), wi = Math.sin(ang);
    for (let i = 0; i < n; i += len) { let cr = 1, ci = 0; for (let k = 0; k < len / 2; ++k) {
      const a = i + k, b = a + len / 2, tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
      re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti; const nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr; } }
  }
}
const win = new Float64Array(N).map((_, i) => 0.5 - 0.5 * Math.cos(2 * Math.PI * i / N));
const bandE = (x, off) => {
  const re = new Float64Array(N), im = new Float64Array(N); for (let i = 0; i < N; ++i) re[i] = (x[off + i] || 0) * win[i];
  fft(re, im); const e = new Float64Array(bands.length);
  for (let k = 1; k < N / 2; ++k) { const f = k * R / N; for (let b = 0; b < bands.length; ++b) if (f >= bands[b][0] && f < bands[b][1]) e[b] += re[k] * re[k] + im[k] * im[k]; }
  return e;
};
const db = e => 10 * Math.log10(e + 1e-9);
let frames = 0, quiet6 = 0, quiet12 = 0, loud6 = 0, distSum = 0, bandDist = new Float64Array(bands.length), bandN = 0;
const perSec = [];
for (let off = 0; off + N <= total; off += N) {
  const he_ = bandE(hl, off), de_ = bandE(dl, off), hTot = he_.reduce((a, b) => a + b, 0);
  if (db(hTot) < 40) continue;               // the host is nearly silent: nothing to compare
  ++frames;
  const dTot = de_.reduce((a, b) => a + b, 0) * gain * gain, diff = db(dTot) - db(hTot);
  if (diff < -6) ++quiet6; if (diff < -12) ++quiet12; if (diff > 6) ++loud6;
  let s = 0, n = 0;
  for (let b = 0; b < bands.length; ++b) if (db(he_[b]) > 30) { const d = Math.abs(db(de_[b] * gain * gain) - db(he_[b])); s += d; bandDist[b] += d; n++; }
  if (n) { distSum += s / n; ++bandN; }
  const sec = Math.floor(off / R); (perSec[sec] = perSec[sec] || []).push(diff);
}
// clicks: second difference far above its neighbourhood; zero runs: the unit silent while the host plays
function clicks(x) {
  let n = 0, last = -9999;
  for (let i = 2; i + 64 < x.length; ++i) {
    const d = Math.abs(x[i] - 2 * x[i - 1] + x[i - 2]);
    if (d < 0.02) continue;
    let ref = 0; for (let k = 4; k < 64; k += 4) ref = Math.max(ref, Math.abs(x[i + k] - 2 * x[i + k - 1] + x[i + k - 2]), Math.abs(x[i - k] - 2 * x[i - k - 1] + x[i - k - 2]));
    if (d > 8 * ref + 0.02 && i - last > 200) { ++n; last = i; }
  }
  return n;
}
function zeroRuns(x, y) {                    // runs of >= 3 ms of exact zero in x where y (the host, scaled) is above -50 dBFS over the run
  let n = 0, run = 0; const min = Math.round(0.003 * R);
  for (let i = 0; i < x.length; ++i) { if (x[i] === 0) ++run; else { if (run >= min) { let m = 0; for (let k = i - run; k < i; ++k) m = Math.max(m, Math.abs(y[k])); if (m > 0.003) ++n; } run = 0; } }
  return n;
}
const dlScaled = Float32Array.from(dl, v => v * gain);          // the unit's left channel at the host's scale
const peak = x => { let m = 0; for (const v of x) m = Math.max(m, Math.abs(v)); return m; };
const rep = {
  recording: pos[0], host: pos[1], lagSeconds: +(lagSamples / R).toFixed(4), envelopeCorr: +best.c.toFixed(4), sampleCorr: +fine.c.toFixed(4), seconds: +(total / R).toFixed(1),
  gainDb: +(20 * Math.log10(Math.abs(gain) + 1e-12)).toFixed(2), devPeakDb: +(20 * Math.log10(peak(dl) + 1e-9)).toFixed(1),
  frames, framesQuiet6: quiet6, framesQuiet12: quiet12, framesLoud6: loud6, quiet6Pct: +(100 * quiet6 / (frames || 1)).toFixed(2),
  spectralDistDb: +(distSum / (bandN || 1)).toFixed(2), bandDistDb: Array.from(bandDist, v => +(v / (bandN || 1)).toFixed(2)),
  clicksDevice: clicks(dlScaled), clicksHost: clicks(hl), zeroRunsDevice: zeroRuns(dl, hl),
  worstSeconds: perSec.map((a, s) => a && { s, d: +(a.reduce((x, y) => x + y, 0) / a.length).toFixed(1) }).filter(Boolean).sort((a, b) => a.d - b.d).slice(0, 5)
};
console.log(JSON.stringify(rep, null, 1));
if (opt.json) fs.writeFileSync(opt.json, JSON.stringify(rep, null, 1));
if (opt.aligned) {                           // the unit's stereo, cut at the song's start and gain-matched, 16-bit, for listening
  const o = Buffer.alloc(total * 4); for (let i = 0; i < total; ++i) { o.writeInt16LE(Math.max(-32768, Math.min(32767, Math.round(dl[i] * gain * 32768 * 0.5))), 4 * i); o.writeInt16LE(Math.max(-32768, Math.min(32767, Math.round(dr[i] * gain * 32768 * 0.5))), 4 * i + 2); }
  const h = Buffer.alloc(44); h.write('RIFF', 0); h.writeUInt32LE(36 + o.length, 4); h.write('WAVEfmt ', 8); h.writeUInt32LE(16, 16); h.writeUInt16LE(1, 20); h.writeUInt16LE(2, 22);
  h.writeUInt32LE(R, 24); h.writeUInt32LE(R * 4, 28); h.writeUInt16LE(4, 32); h.writeUInt16LE(16, 34); h.write('data', 36); h.writeUInt32LE(o.length, 40);
  fs.writeFileSync(opt.aligned, Buffer.concat([h, o]));
}
