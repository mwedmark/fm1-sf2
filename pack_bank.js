// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
// Packs a BFP8 bank (make_bank.js bfp8, or the converter page) for the live cache of FORMAT.md ("Calculation: a live cache of decoded sounds"):
// the sounds go into the flash as ONE constant-bit-rate mono MP3 stream (ffmpeg, bit reservoir off), a few that MP3 handles badly stay BFP8,
// and a pack table (FORMAT.md, "Proposal: packed sample data") says where each sound is and how to decode it. The zone table is unchanged but
// for the 12 or 16 bytes the longer header moves everything by, so a zone still addresses the sound as in the unpacked bank.
//   node pack_bank.js in.fm1b out.fm1b [--kbit 64] [--flash 466944] [--margin 3072]
// Needs ffmpeg with libmp3lame. The sounds are taken out of the BFP8 bank (its 44 dB noise is far below the MP3's), so no .sf2 is needed.
// Sounds go lossless (codec 0, the BFP8 as it is) when MP3 would be badly wrong for them (SNR under 12 dB, worst first by dB per byte it costs),
// as many as fit in --flash minus the packed file without them minus --margin; sounds with a very short loop (under 16 samples; the renderings listened to had every shorter-than-512 loop as MP3, crossfade clipped to half the loop) or zones that
// disagree about the loop are lossless always. Pack entries (24 bytes): see FORMAT.md; for the live cache an entry is one sound:
//   unpacked offset = the sound's offset in the unpacked image, unpacked bytes = its exact BFP8 size (entries may leave up to 3 bytes between them),
//   codec 2: packed offset = the byte where the MP3 frame to start decoding at begins (one lead-in frame before the sound's first), packed bytes = to the
//   end of the frame holding the sound's last sample, delay (u16 at 18) = samples of that decode to throw away, crossfade (u16 at 20) = loop crossfade;
//   codec 0: the BFP8 stored, packed bytes = unpacked bytes.
const fs = require('fs'), { execFileSync } = require('child_process');
const FRAME = 1152, RATE_BYTES_PER_FRAME = kbit => Math.floor(144 * kbit * 1000 / 32000), LEAD = 1, XFADE = 256, MIN_LOOP = 16, NEW_HEADER = 92, ENTRY = 24;
const bfpBytes = n => Math.ceil(n / 32) * 33, align4 = n => (n + 3) & ~3;

function unpackBfp8(b, off, n) {                 // n samples from a BFP8 run at b[off]
  const out = new Int16Array(Math.ceil(n / 32) * 32);
  for (let g = 0; g * 32 < n; g++) { const sh = b[off + g * 33]; for (let i = 0; i < 32; i++) out[g * 32 + i] = ((b[off + g * 33 + 1 + i] << 24) >> 24) << sh; }
  return out.subarray(0, n);
}
const ffmpeg = (args, input) => execFileSync('ffmpeg', ['-v', 'error', ...args], { input, maxBuffer: 1 << 29 });
function encode(pcm, kbit) {
  return ffmpeg(['-f', 's16le', '-ar', '32000', '-ac', '1', '-i', '-', '-c:a', 'libmp3lame', '-b:a', kbit + 'k', '-reservoir', '0', '-write_xing', '0', '-id3v2_version', '0', '-f', 'mp3', '-'], Buffer.from(pcm.buffer, pcm.byteOffset, pcm.byteLength));
}
function decode(stream) { const d = ffmpeg(['-f', 'mp3', '-i', '-', '-ar', '32000', '-f', 's16le', '-'], stream); return new Int16Array(d.buffer.slice(d.byteOffset, d.byteOffset + d.length)); }
const snr = (a, c) => { let s = 0, e = 0; for (let i = 0; i < a.length; i++) { s += a[i] * a[i]; e += (a[i] - c[i]) ** 2; } return 10 * Math.log10(Math.max(s, 1e-9) / Math.max(e, 1e-9)); };

function pack(src, opt = {}) {
  const MIN = opt["min-loop"] === undefined ? MIN_LOOP : opt["min-loop"], kbit = opt.kbit || 64, flash = opt.flash || 466944, margin = opt.margin === undefined ? 3072 : opt.margin;
  const dv = new DataView(src.buffer, src.byteOffset, src.byteLength), u32 = o => dv.getUint32(o, true);
  if (String.fromCharCode(...src.subarray(0, 4)) !== 'FM1B' || dv.getUint16(4, true) !== 1 || dv.getUint16(6, true) !== 8) throw new Error('not a version 1 BFP8 bank');
  if (dv.getUint32(8, true) !== 32000) throw new Error('the MP3 stream is coded at 32 kHz; the bank is at ' + u32(8));
  const nz = dv.getUint16(14, true), offZone = u32(32), offSmp = u32(36), pitch = u32(16), hasInfo = pitch >= 80 && u32(76) !== 0, oldHdr = hasInfo ? 80 : pitch;
  if (oldHdr < 76 || oldHdr > NEW_HEADER) throw new Error('header of ' + oldHdr + ' bytes');
  const shift = NEW_HEADER - oldHdr, ZS = 24;
  // the sounds: distinct (offset, frames) of the zones, with their loops
  const map = new Map();
  for (let i = 0; i < nz; i++) {
    const o = offZone + i * ZS, off = u32(o), n = u32(o + 4), ls = u32(o + 8), le = u32(o + 12), loop = src[o + 17] & 1;
    let s = map.get(off);
    if (!s) map.set(off, s = { off, n, loop: loop ? [ls, le] : null, mixed: false });
    else { if (s.n !== n) throw new Error('two zones disagree about a sound\'s length at ' + off); if (!!s.loop !== !!loop || (loop && (s.loop[0] !== ls || s.loop[1] !== le))) s.mixed = true; }
  }
  const sounds = [...map.values()].sort((a, b) => a.off - b.off);
  for (const s of sounds) { s.pcm = unpackBfp8(src, s.off, s.n); s.bytes = bfpBytes(s.n); }
  // the stream of a set of sounds: each starts at its place in the stream, plus the codec delay found by lining the start up with the source
  function stream(list) {
    let total = 0; for (const s of list) { s.t = total; total += s.pcm.length; }
    const all = new Int16Array(total + 3 * FRAME); for (const s of list) all.set(s.pcm, s.t);
    const mp3 = encode(all, kbit);
    if (mp3.length % RATE_BYTES_PER_FRAME(kbit) || mp3[0] !== 0xff) throw new Error('the stream is not whole frames from byte 0 (an ID3 tag?)');
    const back = decode(mp3), n = Math.min(6000, total);   // the 3 frames of silence after the last sound give the encoder its flush and the decoder its last samples
    let delay = 0, best = -Infinity;
    for (let lag = 0; lag < 2400; lag++) { let c = 0; for (let i = 0; i < n; i++) c += back[lag + i] * all[i]; if (c > best) { best = c; delay = lag; } }
    return { mp3, back, delay };
  }
  let first = stream(sounds);
  for (const s of sounds) { s.snr = snr(s.pcm, first.back.subarray(s.t + first.delay, s.t + first.delay + s.n)); s.why = (s.loop && s.loop[1] - s.loop[0] < MIN) ? "loop" : s.mixed ? "mixed" : s.n < 64 ? "short" : ""; s.always = !!s.why; }
  // lossless: always the ones that must be; then the worst by dB under 12 per byte of extra, while the file fits
  const mp3Bytes = sounds.length ? RATE_BYTES_PER_FRAME(kbit) : 0, perSecond = mp3Bytes * 32000 / 1152;
  const extra = s => s.bytes - s.n / 32000 * perSecond;
  const tables = offSmp + shift, entriesBytes = sounds.length * ENTRY;
  let lossless = new Set(sounds.filter(s => s.always));
  let rest = sounds.filter(s => !lossless.has(s)), pk = rest.length ? stream(rest) : { mp3: Buffer.alloc(0), back: new Int16Array(0), delay: 0 };
  const sizeNow = () => tables + align4(pk.mp3.length) + [...lossless].reduce((a, s) => a + align4(s.bytes), 0) + entriesBytes;
  const weak = sounds.filter(s => !lossless.has(s) && s.snr < 12).sort((a, b) => (12 - b.snr) / Math.max(extra(b), 1) - (12 - a.snr) / Math.max(extra(a), 1));
  let budget = flash - margin - sizeNow();
  for (const s of weak) { const cost = align4(s.bytes) - s.n / 32000 * perSecond; if (cost <= budget) { lossless.add(s); budget -= cost; } }
  if (lossless.size > sounds.filter(s => s.always).length) { rest = sounds.filter(s => !lossless.has(s)); pk = rest.length ? stream(rest) : pk; }
  // the file
  let end = tables; const mp3At = end; end += align4(pk.mp3.length);
  for (const s of sounds) if (lossless.has(s)) { s.stored = end; end += align4(s.bytes); }
  const offPack = end; end += sounds.length * ENTRY;
  const out = new Uint8Array(end), o = new DataView(out.buffer);
  out.set(src.subarray(0, oldHdr)); out.set(src.subarray(oldHdr, offSmp), NEW_HEADER);
  o.setUint16(4, 2, true); o.setUint16(6, 8 | 0x10, true);
  for (const f of [16, 20, 24, 28, 32, 36, 44, 48, 56, 68]) if (u32(f)) o.setUint32(f, u32(f) + shift, true);
  if (hasInfo) o.setUint32(76, u32(76) + shift, true);
  o.setUint32(40, end, true); o.setUint32(80, offPack, true); o.setUint32(84, sounds.length, true); o.setUint32(88, ENTRY, true);
  for (let i = 0; i < nz; i++) { const p = offZone + shift + i * ZS; o.setUint32(p, o.getUint32(p, true) + shift, true); }
  out.set(pk.mp3, mp3At);
  const frameBytes = RATE_BYTES_PER_FRAME(kbit), mp3Frames = Math.floor(pk.mp3.length / frameBytes);
  sounds.forEach((s, i) => {
    const e = offPack + i * ENTRY;
    o.setUint32(e, s.off + shift, true); o.setUint32(e + 4, s.bytes, true);
    if (lossless.has(s)) { out.set(src.subarray(s.off, s.off + s.bytes), s.stored); o.setUint32(e + 8, s.stored - tables, true); o.setUint32(e + 12, s.bytes, true); out[e + 16] = 0; return; }
    const t0 = s.t + pk.delay, f0 = Math.max(0, Math.floor(t0 / FRAME) - LEAD), f1 = Math.min(mp3Frames - 1, Math.floor((t0 + s.n - 1) / FRAME));
    s.discard = t0 - f0 * FRAME;
    o.setUint32(e + 8, mp3At - tables + f0 * frameBytes, true); o.setUint32(e + 12, (f1 - f0 + 1) * frameBytes, true);
    out[e + 16] = 2; o.setUint16(e + 18, s.discard, true); o.setUint16(e + 20, s.loop && s.loop[0] >= 64 ? XFADE : 0, true);
  });
  return { bytes: out, sounds: sounds.length, lossless: lossless.size, always: sounds.filter(s => s.always).length, why: sounds.filter(s => s.always).reduce((m, s) => (m[s.why] = (m[s.why] || 0) + 1, m), {}), mp3Bytes: pk.mp3.length, shift,
    unpackedBytes: src.length + shift, meanSnr: sounds.filter(s => !lossless.has(s)).reduce((a, s) => a + s.snr, 0) / Math.max(1, sounds.length - lossless.size),
    lossSnr: sounds.filter(s => lossless.has(s)).map(s => s.snr) };
}
module.exports = { pack, unpackBfp8 };

if (require.main === module) {
  const a = process.argv.slice(2), pos = a.filter((x, i) => !x.startsWith('--') && !(i && a[i - 1].startsWith('--')));
  const opt = {}; for (let i = 0; i < a.length; i++) if (a[i].startsWith('--')) opt[a[i].slice(2)] = Number(a[++i]);
  if (pos.length !== 2) { console.error('usage: node pack_bank.js in.fm1b out.fm1b [--kbit 64] [--flash 466944] [--margin 3072]'); process.exit(1); }
  const r = pack(new Uint8Array(fs.readFileSync(pos[0])), opt);
  fs.writeFileSync(pos[1], r.bytes);
  console.log(`${pos[1]}: ${r.bytes.length} bytes (the unpacked bank would be ${r.unpackedBytes}); ${r.sounds} sounds, ${r.sounds - r.lossless} in a ${r.mp3Bytes}-byte MP3 stream (mean ${r.meanSnr.toFixed(1)} dB), ${r.lossless} lossless (because of: ${JSON.stringify(r.why)})` +
    (r.bytes.length > (opt.flash || 466944) ? ' - TOO BIG' : ''));
}
