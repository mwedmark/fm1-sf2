// Runs the converter core from index.html in node and checks the FM1B file with an independent reader.
//   node test_core.js 1mgm.sf2 [outdir]
const fs = require('fs');
const html = fs.readFileSync(__dirname + '/index.html', 'utf8');
const core = html.match(/<script id="core">([\s\S]*?)<\/script>/)[1];
eval(core + '\nglobalThis.api = { parseSf2, buildBank, writeFm1, writeSf2, presetZones, seaDecode };');
const api = globalThis.api;

// The lookup a CPU does, straight from FORMAT.md: returns the sample address, length, loop and step for a note.
function lookup(f, bank, program, note) {
  const dv = new DataView(f.buffer, f.byteOffset, f.length);
  if (String.fromCharCode(...f.slice(0, 4)) !== 'FM1B') throw new Error('magic');
  const [offPitch, offBank, offPre, offKey, offZone] = [16, 20, 24, 28, 32].map(o => dv.getUint32(o, true));
  const slot = bank === 0 ? program : 128 + program;
  const p = dv.getUint16(offBank + slot * 2, true);
  if (p === 0xffff) return null;
  const k = f[offKey + p * 128 + note];
  if (k === 0xff) return null;
  const first = dv.getUint16(offPre + p * 24, true);
  const z = offZone + (first + k) * 24;
  const root = f[z + 16];
  const idx = Math.max(0, Math.min(127, note - root + 64));
  return { addr: dv.getUint32(z, true), len: dv.getUint32(z + 4, true), loopStart: dv.getUint32(z + 8, true),
           loopEnd: dv.getUint32(z + 12, true), loop: f[z + 17] & 1, root, step: dv.getUint32(offPitch + idx * 4, true) };
}

const inPath = process.argv[2], outDir = process.argv[3] || '.';
const b = fs.readFileSync(inPath);
const sf = api.parseSf2(b.buffer.slice(b.byteOffset, b.byteOffset + b.length));
console.log('presets', sf.presets.length, 'samples', sf.shdr.length - 1);

const pianoIdx = sf.presets.findIndex(p => p.bank === 0 && p.preset === 0);
for (const [name, idx, opt] of [['all', -2, { rate: 22050, bits: 8 }], ['piano', pianoIdx, { rate: 22050, bits: 8 }],
    ['all_sea', -2, { rate: 32000, sea: true }], ['piano_sea', pianoIdx, { rate: 32000, sea: true }]]) {
  const bk = api.buildBank(sf, idx, opt);
  const bin = api.writeFm1(bk);
  fs.writeFileSync(`${outDir}/t_${name}.bin`, bin);
  fs.writeFileSync(`${outDir}/t_${name}.sf2`, api.writeSf2(bk, name));
  // every note of every preset must resolve to the first zone covering it, and its bytes must be in the file
  let checked = 0, bad = 0;
  for (const p of bk.presets) {
    for (let n = 0; n < 128; n++) {
      const want = bk.zones.slice(p.first, p.first + p.count).find(z => z.lo <= n && n <= z.hi);
      const got = lookup(bin, p.bank, p.preset, n);
      checked++;
      if (!want !== !got) { bad++; continue; }
      if (!want) continue;
      const s = want.sample;
      if (got.addr !== s.fileOff || got.len !== s.pcm.length || got.root !== want.root || got.loop !== (want.loop ? 1 : 0) ||
          got.addr % 4 || got.addr + (bk.sea ? Math.ceil(got.len / 320) * 228 : got.len * (bk.bits / 8)) > bin.length ||
          bin[got.addr] !== (bk.sea ? 1 : s.pcm[0] & 255)) bad++;
    }
  }
  const dv = new DataView(bin.buffer, bin.byteOffset, bin.length);
  console.log(name, 'presets', bk.presets.length, 'zones', bk.zones.length, 'samples', bk.samples.length, 'bytes', bin.length,
    'declared size', dv.getUint32(40, true), 'lookups', checked, 'bad', bad, 'step(root+12)', lookup(bin, 0, 0, 72) && lookup(bin, 0, 0, 72).step);
  if (bk.sea) {                                   // the file's own chunks must decode back to the source, sample by sample
    let worst = 99, sum = 0;
    for (const s of bk.samples) {
      const dec = api.seaDecode(bin.subarray(s.fileOff, s.fileOff + s.bytes), s.pcm.length);
      let e = 0, p = 0;
      for (let i = 0; i < s.pcm.length; i++) { e += (s.pcm[i] - dec[i]) ** 2; p += s.pcm[i] ** 2; }
      if (p > 1e6) { const snr = 10 * Math.log10(p / Math.max(e, 1)); worst = Math.min(worst, snr); sum += snr; }
    }
    console.log(name, 'SEA5 round trip: worst SNR', worst.toFixed(1), 'dB, mean', (sum / bk.samples.length).toFixed(1), 'dB');
    if (worst < 5) process.exit(1);   // short percussive clicks are the weak spot of 5-bit ADPCM
  }
  if (bad || dv.getUint32(40, true) !== bin.length) process.exit(1);
}
