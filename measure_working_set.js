// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
// How much sample data the programs that sound at once need, for a cache in SRAM (FORMAT.md, "Calculation: a sample cache in the FM-1's SRAM").
// With the converter's own banks (32 kHz): every written preset's sounds counted once, and random sets of programs on the 15 melodic
// channels plus a percussion kit, as 16-bit PCM and as BFP8 (groups of 32: 33 bytes), and the whole bank in BFP8 against the flash.
//   node measure_working_set.js [font.sf2 ...]        (default: 1mgm.sf2)
const fs = require('fs'), path = require('path');
const html = fs.readFileSync(path.join(__dirname, 'index.html'), 'utf8');
const A = new Function(html.match(/<script id="core">([\s\S]*?)<\/script>/)[1] + '\nreturn { parseSf2, buildBank };')();
const FLASH_KB = 456;                                               // the flash the figures below were made for: 466 944 bytes (FM1B_FLASH_BYTES is 536 576 since the bank moved to the start of the user partition)
const bfp = frames => Math.ceil(frames / 32) * 33;
const kb = bytes => Math.round(bytes / 1024);
function presetsOf(file) {
  const b = fs.readFileSync(file), sf = A.parseSf2(b.buffer.slice(b.byteOffset, b.byteOffset + b.length));
  const bank = A.buildBank(sf, -2, { rate: 32000, bits: 16 });
  return bank.presets.filter(p => (p.bank === 0 || p.bank === 128) && p.count <= 254).map(p => {
    const sounds = new Set();
    for (let k = 0; k < p.count; k++) sounds.add(bank.zones[p.first + k].sample);
    return { name: p.name.trim(), bank: p.bank, program: p.preset, sounds };
  });
}
const frames = set => [...set].reduce((a, s) => a + s.pcm.length, 0);
const union = list => { const s = new Set(); list.forEach(i => i.sounds.forEach(x => s.add(x))); return s; };
function report(file) {
  const info = presetsOf(file), mel = info.filter(i => i.bank === 0), kits = info.filter(i => i.bank === 128);
  const all = union(info);
  console.log(`\n=== ${path.basename(file)}: ${mel.length} melodic presets, ${kits.length} kits, ${all.size} distinct sounds, ${(frames(all) / 32000).toFixed(1)} s`);
  const f = mel.map(i => frames(i.sounds)).sort((a, b) => a - b);
  console.log(`one melodic preset, PCM16 KB: median ${kb(f[f.length >> 1] * 2)}, 90 % ${kb(f[Math.floor(f.length * .9)] * 2)}, largest ${kb(f[f.length - 1] * 2)}`);
  console.log('kits, PCM16 KB: ' + kits.map(k => `${k.program}:${kb(frames(k.sounds) * 2)}`).join(' '));
  let seed = 12345;
  const rnd = () => (seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296;
  for (const n of [4, 8, 12, 15]) {
    const res = [];
    for (let t = 0; t < 4000; t++) {
      const pick = new Set();
      while (pick.size < n) pick.add(Math.floor(rnd() * mel.length));
      res.push(frames(union([...[...pick].map(i => mel[i]), kits[Math.floor(rnd() * kits.length)]])));
    }
    res.sort((a, b) => a - b);
    const pc = p => res[Math.floor(p * (res.length - 1))];
    console.log(`${String(n).padStart(2)} random programs + a kit: PCM16 KB median ${kb(pc(.5) * 2)} 99 % ${kb(pc(.99) * 2)} max ${kb(pc(1) * 2)}   BFP8 KB median ${kb(bfp(pc(.5)))} 99 % ${kb(bfp(pc(.99)))}`);
  }
  const band = [0, 4, 16, 25, 29, 33, 48, 61, 88, 80];                // piano, e-piano, organ, steel guitar, overdrive, bass, strings, brass, pad, lead
  const set = union([...band.map(p => mel.find(m => m.program === p)).filter(Boolean), kits.find(k => k.program === 0) || kits[0]]);
  console.log(`a typical band (programs ${band.join(',')} and kit 0): ${kb(frames(set) * 2)} KB PCM16, ${kb(bfp(frames(set)))} KB BFP8, ${(frames(set) / 32000).toFixed(1)} s`);
  const k0 = kits.filter(k => k.program === 0);
  console.log(`whole bank in BFP8 against ${FLASH_KB} KB of flash: all melodic ${kb(bfp(frames(union(mel))))} KB, kit 0 ${kb(bfp(frames(union(k0))))} KB, melodic + kit 0 ${kb(bfp(frames(union([...mel, ...k0]))))} KB, all kits ${kb(bfp(frames(union(kits))))} KB, everything ${kb(bfp(frames(all)))} KB`);
}
for (const file of process.argv.length > 2 ? process.argv.slice(2) : [path.join(__dirname, '1mgm.sf2')]) report(file);
