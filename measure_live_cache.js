// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
// A live cache of decoded sounds in SRAM (FORMAT.md, "Calculation: a live cache of decoded sounds"): the flash holds the bank as MP3, a note that
// needs a sound that is not in the cache has it decoded (to BFP8: 33 bytes for 32 frames), and the least recently used sounds that nothing is
// playing go when the cache is full. This plays standard MIDI files against the converter's bank and counts, for each cache size, what the
// song does to it: sounds touched, misses, the audio that has to be decoded each second, and notes that find the cache full of playing sounds.
//   node measure_live_cache.js font.sf2 file.mid [file.mid ...]       (the font at 32 kHz, bank 0 and the percussion kits of bank 128)
// Assumptions, not measurements: a miss decodes the whole sound plus 1 MP3 frame of lead-in (the stream is coded with the bit reservoir off); the decoder needs
// K million cycles for each second of mono audio (5 and 12 are tried), on a 240 MHz core; a voice keeps its sound until the note has ended
// and 0.4 s of release, a percussion sound until it has played out. Bank select is ignored, so GS variation banks are not used.
const fs = require('fs'), path = require('path');
const html = fs.readFileSync(path.join(__dirname, 'index.html'), 'utf8');
const A = new Function(html.match(/<script id="core">([\s\S]*?)<\/script>/)[1] + '\nreturn { parseSf2, buildBank };')();
const CACHE_KB = [96, 128, 192, 256], K_CYCLES = [5e6, 12e6], CLOCK = 240e6, RELEASE = 0.4, LEAD_IN_FRAMES = 1152, RATE = 32000;
const bfp = frames => Math.ceil(frames / 32) * 33;

function loadBank(file) {
  const b = fs.readFileSync(file), sf = A.parseSf2(b.buffer.slice(b.byteOffset, b.byteOffset + b.length));
  const bank = A.buildBank(sf, -2, { rate: RATE, bits: 16 });
  const melodic = new Map(), kits = new Map();
  for (const p of bank.presets) {
    if (p.count > 254 || (p.bank !== 0 && p.bank !== 128)) continue;
    const zones = bank.zones.slice(p.first, p.first + p.count);
    (p.bank === 0 ? melodic : kits).set(p.preset, zones);
  }
  return { melodic, kits };
}

function parseMidi(file) {                                         // notes as { t0, t1, ch, note, program } in seconds
  const d = fs.readFileSync(file);
  if (d.toString('latin1', 0, 4) !== 'MThd') throw new Error(file + ': not a MIDI file');
  const ntrk = d.readUInt16BE(10), division = d.readUInt16BE(12);
  if (division & 0x8000) throw new Error(file + ': SMPTE time');
  let pos = 8 + d.readUInt32BE(4);
  const events = [], tempos = [];
  for (let t = 0; t < ntrk && pos + 8 <= d.length; t++) {
    const len = d.readUInt32BE(pos + 4); let i = pos + 8; const end = i + len; pos = end;
    let tick = 0, status = 0;
    const vlq = () => { let v = 0, c; do { c = d[i++]; v = (v << 7) | (c & 0x7f); } while (c & 0x80); return v; };
    while (i < end) {
      tick += vlq();
      let s = d[i];
      if (s & 0x80) { status = s; ++i; } else s = status;
      if (s === 0xff) { const type = d[i++], n = vlq(); if (type === 0x51 && n === 3) tempos.push({ tick, us: (d[i] << 16) | (d[i + 1] << 8) | d[i + 2] }); i += n; }
      else if (s === 0xf0 || s === 0xf7) i += vlq();
      else {
        const hi = s & 0xf0, ch = s & 15, a = d[i++], b = hi === 0xc0 || hi === 0xd0 ? 0 : d[i++];
        if (hi === 0x90 || hi === 0x80 || hi === 0xc0) events.push({ tick, hi: hi === 0x90 && b === 0 ? 0x80 : hi, ch, a, b });
      }
    }
  }
  tempos.sort((x, y) => x.tick - y.tick);
  if (!tempos.length || tempos[0].tick > 0) tempos.unshift({ tick: 0, us: 500000 });
  const seconds = tick => { let s = 0, last = tempos[0]; for (const t of tempos) { if (t.tick >= tick) break; s += (t.tick - last.tick) * last.us / 1e6 / division; last = t; } return s + (tick - last.tick) * last.us / 1e6 / division; };
  events.sort((x, y) => x.tick - y.tick);
  const program = new Array(16).fill(0), open = new Map(), notes = [];
  for (const e of events) {
    const t = seconds(e.tick);
    if (e.hi === 0xc0) program[e.ch] = e.a;
    else if (e.hi === 0x90) { const k = e.ch * 128 + e.a; if (!open.has(k)) open.set(k, []); open.get(k).push({ t0: t, ch: e.ch, note: e.a, program: program[e.ch] }); }
    else { const q = open.get(e.ch * 128 + e.a); if (q && q.length) { const n = q.shift(); n.t1 = t; notes.push(n); } }
  }
  const last = events.length ? seconds(events[events.length - 1].tick) : 0;
  for (const q of open.values()) for (const n of q) { n.t1 = last; notes.push(n); }
  return notes.sort((x, y) => x.t0 - y.t0);
}

function soundsOf(bank, n) {                                       // the zones that cover the key sound, all of them (stacked layers)
  const list = n.ch === 9 ? (bank.kits.get(n.program) || bank.kits.get(0) || [...bank.kits.values()][0]) : bank.melodic.get(n.program);
  return (list || []).filter(z => n.note >= z.lo && n.note <= z.hi);
}

function simulate(bank, notes, capacityKB) {
  const cache = new Map(), pins = [];                              // sound -> last use; pins: { sound, until }
  let bytes = 0, misses = 0, overflow = 0, decodedFrames = 0, hits = 0;
  const perSecond = new Map(), missTimes = [];
  const cap = capacityKB * 1024;
  for (const n of notes) {
    for (const z of soundsOf(bank, n)) {
      const s = z.sample, size = bfp(s.pcm.length), seconds = s.pcm.length / RATE;
      const until = n.t0 + (n.ch === 9 || (!z.loop && seconds < n.t1 - n.t0 + RELEASE) ? seconds : n.t1 - n.t0 + RELEASE);
      for (let i = pins.length - 1; i >= 0; --i) if (pins[i].until < n.t0) pins.splice(i, 1);
      if (cache.has(s)) { cache.set(s, n.t0); ++hits; pins.push({ sound: s, until }); continue; }
      const pinned = new Set(pins.map(p => p.sound));
      const lru = [...cache].filter(([k]) => !pinned.has(k)).sort((x, y) => x[1] - y[1]);
      while (bytes + size > cap && lru.length) { const [k] = lru.shift(); bytes -= bfp(k.pcm.length); cache.delete(k); }
      if (bytes + size > cap) { ++overflow; continue; }
      cache.set(s, n.t0); bytes += size; ++misses; pins.push({ sound: s, until });
      const frames = s.pcm.length + LEAD_IN_FRAMES;
      decodedFrames += frames;
      const sec = Math.floor(n.t0);
      perSecond.set(sec, (perSecond.get(sec) || 0) + frames / RATE);
      missTimes.push(n.t0);
    }
  }
  const worst = Math.max(0, ...perSecond.values());
  let burst = 0;                                                   // most misses inside 50 ms
  for (let i = 0, j = 0; i < missTimes.length; ++i) { while (missTimes[i] - missTimes[j] > 0.05) ++j; burst = Math.max(burst, i - j + 1); }
  return { hits, misses, overflow, decodedSeconds: decodedFrames / RATE, worstSecond: worst, burst };
}

const [font, ...files] = process.argv.slice(2);
if (!font || !files.length) { console.error('usage: node measure_live_cache.js font.sf2 file.mid [file.mid ...]'); process.exit(1); }
const bank = loadBank(font);
console.log(`${path.basename(font)}: ${bank.melodic.size} melodic presets, ${bank.kits.size} kits. K = ${K_CYCLES.map(k => k / 1e6).join(' and ')} million cycles per second of audio on ${CLOCK / 1e6} MHz.`);
const totals = CACHE_KB.map(() => ({ hits: 0, misses: 0, overflow: 0, worst: 0, burst: 0, secs: 0, decoded: 0 }));
for (const file of files) {
  const notes = parseMidi(file), song = Math.max(...notes.map(n => n.t1));
  const touched = new Set(); let missingNotes = 0;
  for (const n of notes) { const z = soundsOf(bank, n); if (!z.length) ++missingNotes; z.forEach(x => touched.add(x.sample)); }
  const touchedKB = [...touched].reduce((a, s) => a + bfp(s.pcm.length), 0) / 1024;
  console.log(`\n${path.basename(file)}: ${notes.length} notes, ${song.toFixed(0)} s, ${touched.size} sounds touched = ${touchedKB.toFixed(0)} KB of BFP8` + (missingNotes ? `, ${missingNotes} notes with no sound in the bank` : ''));
  console.log('  cache KB   hits  misses  notes lost   decoded s   worst second: s of audio -> % of a core (K=' + K_CYCLES.map(k => k / 1e6).join('/') + ')   most misses in 50 ms');
  CACHE_KB.forEach((kb, idx) => {
    const r = simulate(bank, notes, kb), pct = K_CYCLES.map(k => (r.worstSecond * k / CLOCK * 100).toFixed(0)).join('/');
    const t = totals[idx]; t.hits += r.hits; t.misses += r.misses; t.overflow += r.overflow; t.worst = Math.max(t.worst, r.worstSecond); t.burst = Math.max(t.burst, r.burst); t.secs += song; t.decoded += r.decodedSeconds;
    console.log(`  ${String(kb).padStart(7)}  ${String(r.hits).padStart(6)}  ${String(r.misses).padStart(6)}  ${String(r.overflow).padStart(10)}   ${r.decodedSeconds.toFixed(1).padStart(8)}   ${r.worstSecond.toFixed(2).padStart(6)} s -> ${pct.padStart(7)} %   ${String(r.burst).padStart(5)}`);
  });
}
console.log(`\nall ${files.length} files together:`);
CACHE_KB.forEach((kb, i) => {
  const t = totals[i], notes = t.hits + t.misses + t.overflow;
  console.log(`  ${String(kb).padStart(4)} KB: ${(t.hits / notes * 100).toFixed(1)} % of sounds found in the cache, ${t.misses} misses, ${t.overflow} lost, decoding ${(t.decoded / t.secs * 100).toFixed(0)} % of the song's length in audio on average (${(t.decoded / t.secs * K_CYCLES[0] / CLOCK * 100).toFixed(1)} to ${(t.decoded / t.secs * K_CYCLES[1] / CLOCK * 100).toFixed(1)} % of a core), worst second ${t.worst.toFixed(2)} s of audio, up to ${t.burst} misses in 50 ms`);
});
