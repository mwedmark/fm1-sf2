// The simulator's group-wise SEA5 player must give exactly what a raw 16-bit PCM bank holding the decoded samples gives.
//   node test_sea_equiv.js <1mgm.sf2> <simulator.wasm>
// Every program of the bank is played on a spread of notes, long enough to cross chunks and loop points.
const fs = require('fs');
const html = fs.readFileSync(__dirname + '/index.html', 'utf8');
eval(html.match(/<script id="core">([\s\S]*?)<\/script>/)[1] + '\nglobalThis.api = { parseSf2, buildBank, writeFm1, seaDecode };');
const api = globalThis.api;
const [sfPath, wasmPath] = process.argv.slice(2);
const b = fs.readFileSync(sfPath);
const sf = api.parseSf2(b.buffer.slice(b.byteOffset, b.byteOffset + b.length));

const seaBank = api.buildBank(sf, -2, { rate: 32000, sea: true });
const seaBin = api.writeFm1(seaBank);
for (const s of seaBank.samples) {          // the same bank with the decoded samples as plain 16-bit PCM
  s.pcm = Int16Array.from(api.seaDecode(s.enc, s.pcm.length));
  s.enc = null; s.bytes = s.pcm.byteLength;
}
seaBank.sea = false;
const pcmBin = api.writeFm1(seaBank);

function player(bin) {
  const c = new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(wasmPath)), {}).exports;
  c._initialize(); c.sim_boot(1); c.sim_select(0, 5);
  new Uint8Array(c.memory.buffer, c.sim_melodic_buffer(), bin.length).set(bin);
  if (c.sim_melodic_load(bin.length) !== 0) throw new Error('bank rejected');
  const frames = c.sim_block_frames();
  return {
    program: (bk, p) => c.sim_melodic_program(bk, p),
    note: (n, v) => { new Uint8Array(c.memory.buffer, c.sim_file(), 3).set([0x90, n, v]); c.sim_midi(3); },
    render(blocks) {
      const out = new Float32Array(blocks * frames * 2);
      for (let i = 0; i < blocks; i++) out.set(new Float32Array(c.memory.buffer, c.sim_render(), frames * 2), i * frames * 2);
      return out;
    },
  };
}
const A = player(seaBin), B = player(pcmBin);
let programs = 0, notes = 0, bad = 0;
const slots = seaBank.presets.map(p => [p.bank === 128 ? 128 : 0, p.preset]);
for (const [bk, prog] of slots) {
  if (A.program(bk, prog) !== 0 || B.program(bk, prog) !== 0) continue;
  programs++;
  for (const n of [28, 48, 60, 72, 96]) {
    A.note(n, 100); B.note(n, 100);
    const a = A.render(220), c = B.render(220);          // about 0.64 s: past the sample and around the loop
    A.note(n, 0); B.note(n, 0);
    const a2 = A.render(40), c2 = B.render(40);
    notes++;
    let same = a.length === c.length;
    for (let i = 0; same && i < a.length; i++) if (a[i] !== c[i]) same = false;
    for (let i = 0; same && i < a2.length; i++) if (a2[i] !== c2[i]) same = false;
    if (!same) { bad++; if (bad < 6) console.log('MISMATCH preset', bk, prog, 'note', n); }
  }
}
console.log('programs', programs, 'notes', notes, 'mismatches', bad);
process.exit(bad ? 1 : 0);
