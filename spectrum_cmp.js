// Long-term spectrum (third-octave bands, Welch average, mean over the frames where the first file plays) of several recordings of the same song, level-matched
// to the first over 200 Hz - 2 kHz, printed as dB relative to the first.   node spectrum_cmp.js [--from=0 --length=40] ref.wav other1.wav other2.wav ...
// Files are taken from --from s; the unit's aligned recordings (analyze_usb.js --aligned) already start at the song.
const { execFileSync } = require('child_process');
const opt = {}, files = [];
for (const a of process.argv.slice(2)) { const m = a.match(/^--([^=]+)=(.*)$/); if (m) opt[m[1]] = m[2]; else files.push(a); }
const R = 44100, N = 8192, from = +(opt.from || 0), length = +(opt.length || 40);
const read = f => { const b = execFileSync('ffmpeg', ['-v', 'error', '-ss', String(from), '-t', String(length), '-i', f, '-f', 'f32le', '-ac', '1', '-ar', String(R), '-'], { maxBuffer: 1 << 30 }); return new Float32Array(b.buffer, b.byteOffset, b.length >> 2); };
function fft(re, im) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; ++i) { let bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) { [re[i], re[j]] = [re[j], re[i]]; [im[i], im[j]] = [im[j], im[i]]; } }
  for (let len = 2; len <= n; len <<= 1) { const ang = -2 * Math.PI / len, wr = Math.cos(ang), wi = Math.sin(ang);
    for (let i = 0; i < n; i += len) { let cr = 1, ci = 0; for (let k = 0; k < len / 2; ++k) { const a = i + k, b = a + len / 2, tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
      re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti; const nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr; } } }
}
const win = new Float64Array(N).map((_, i) => 0.5 - 0.5 * Math.cos(2 * Math.PI * i / N));
const centres = []; for (let f = 50; f < 20000; f *= Math.pow(2, 1 / 3)) centres.push(f);
function spectrum(x, active) {                        // power per third-octave band, summed over frames
  const e = new Float64Array(centres.length); let frames = 0;
  for (let off = 0; off + N <= x.length; off += N / 2) {
    if (active && !active.has(off)) continue; ++frames;
    const re = new Float64Array(N), im = new Float64Array(N); for (let i = 0; i < N; ++i) re[i] = x[off + i] * win[i];
    fft(re, im);
    for (let k = 1; k < N / 2; ++k) { const f = k * R / N, b = Math.round(3 * Math.log2(f / 50)); if (b >= 0 && b < centres.length) e[b] += re[k] * re[k] + im[k] * im[k]; }
  }
  return { e, frames };
}
const sig = files.map(read), n = Math.min(...sig.map(s => s.length));
const active = new Set(); { const x = sig[0]; for (let off = 0; off + N <= n; off += N / 2) { let s = 0; for (let i = 0; i < N; ++i) s += x[off + i] ** 2; if (10 * Math.log10(s / N + 1e-12) > -55) active.add(off); } }
const sp = sig.map(s => spectrum(s.subarray(0, n), active).e);
const band = (e, lo, hi) => { let s = 0; centres.forEach((c, i) => { if (c >= lo && c < hi) s += e[i]; }); return s; };
const ref = band(sp[0], 200, 2000);
const gains = sp.map(e => ref / band(e, 200, 2000));
console.log('Hz     ' + files.map((f, i) => i ? ('#' + i).padStart(7) : ' ref(dB)').join(''));
centres.forEach((c, b) => console.log(String(Math.round(c)).padEnd(7) + sp.map((e, i) => i ? (10 * Math.log10(e[b] * gains[i] / (sp[0][b] + 1e-30) + 1e-30)).toFixed(1).padStart(7) : (10 * Math.log10(e[b]) + 0).toFixed(1).padStart(8)).join('')));
files.forEach((f, i) => console.log('#' + i + ' ' + f));
