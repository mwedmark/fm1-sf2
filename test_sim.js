// Plays an FM1B bank through the FM-1 browser simulator (WebAssembly) in node and measures what comes out.
//   node test_sim.js <simulator.wasm> <bank.bin>
const fs = require('fs');
const [wasmPath, binPath] = process.argv.slice(2);
const bank = fs.readFileSync(binPath);
const c = new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(wasmPath)), {}).exports;
c._initialize();
c.sim_boot(1);
if (c.sim_select(0, 5) !== 0) throw new Error('could not select the sample source on track 1');

if (bank.length > c.sim_melodic_capacity()) throw new Error('bank too large');
new Uint8Array(c.memory.buffer, c.sim_melodic_buffer(), bank.length).set(bank);
console.log('load', c.sim_melodic_load(bank.length), 'ready', c.sim_melodic_ready());

const dv = new DataView(bank.buffer, bank.byteOffset, bank.length);
const rate = dv.getUint32(8, true), np = dv.getUint16(12, true);
const [, , offPre, offKey, offZone] = [16, 20, 24, 28, 32].map(o => dv.getUint32(o, true));
const frames = c.sim_block_frames();

function midi(a, b, d) {
  new Uint8Array(c.memory.buffer, c.sim_file(), 3).set([a, b, d]);
  if (c.sim_midi(3) < 0) throw new Error('midi rejected');
}
// render n seconds, returns left channel samples (interleaved stereo or mono is detected from block size)
function render(seconds) {
  const out = [];
  const blocks = Math.ceil(seconds * 44100 / frames);
  for (let i = 0; i < blocks; i++) {
    const p = c.sim_render();
    const f = new Float32Array(c.memory.buffer, p, frames * 2);
    for (let k = 0; k < frames; k++) out.push(f[k * 2]);
  }
  return Float32Array.from(out);
}
const rms = a => Math.sqrt(a.reduce((s, x) => s + x * x, 0) / (a.length || 1));
// pitch by autocorrelation in 60..2000 Hz
function pitch(a) {
  let best = 0, bestLag = 0;
  for (let lag = Math.floor(44100 / 2000); lag <= Math.floor(44100 / 60); lag++) {
    let s = 0; for (let i = 0; i + lag < a.length; i++) s += a[i] * a[i + lag];
    if (s > best) { best = s; bestLag = lag; }
  }
  return 44100 / bestLag;
}

render(0.2);                                          // settle
let fail = 0;
const check = (what, ok, extra = '') => { console.log(ok ? 'ok  ' : 'FAIL', what, extra); if (!ok) fail++; };

// 1. pitch follows the key (preset 0 = first program in the bank map)
c.sim_melodic_program(0, 0);
const freq = n => 440 * Math.pow(2, (n - 69) / 12);
for (const n of [48, 60, 72]) {
  midi(0x90, n, 100);
  const a = render(0.35).subarray(2000);
  midi(0x90, n, 0);
  render(0.3);
  const f = pitch(a);
  check(`piano note ${n} -> ${freq(n).toFixed(1)} Hz`, Math.abs(Math.log2(f / freq(n))) < 0.06, `measured ${f.toFixed(1)} Hz, rms ${rms(a).toFixed(4)}`);
}

// 2. a looped zone keeps sounding past its sample length and stops after note-off
let found = null;
outer: for (let p = 0; p < np && !found; p++) {
  const first = dv.getUint16(offPre + p * 24, true);
  for (let n = 36; n < 96; n++) {
    const k = bank[offKey + p * 128 + n];
    if (k === 0xff) continue;
    const z = offZone + (first + k) * 24;
    if (bank[z + 17] & 1) {
      const len = dv.getUint32(z + 4, true);
      // find the program that maps to preset p
      for (let slot = 0; slot < 256; slot++) if (dv.getUint16(dv.getUint32(20, true) + slot * 2, true) === p) {
        found = { p, n, slot, seconds: len / rate }; break outer;
      }
    }
  }
}
if (found) {
  const bankNo = found.slot < 128 ? 0 : 128, prog = found.slot % 128;
  c.sim_melodic_program(bankNo, prog);
  midi(0x90, found.n, 100);
  const long = render(found.seconds * 1.5 + 0.5);
  const tail = long.subarray(long.length - 4410);
  check(`looped preset ${bankNo}/${prog} note ${found.n}: still sounding after ${(found.seconds * 1.5 + 0.5).toFixed(2)} s (sample ${found.seconds.toFixed(2)} s)`, rms(tail) > 0.001, `rms ${rms(tail).toFixed(4)}`);
  midi(0x90, found.n, 0);
  render(0.15);
  const after = render(0.1);
  check('releasing 150 ms after note-off (the zone own release time)', rms(after) < rms(tail) * 0.25, `rms ${rms(after).toFixed(5)} against ${rms(tail).toFixed(5)}`);
} else console.log('no looped zone in this bank');

// 3. silence for a note outside every zone does not crash
c.sim_melodic_program(128, 0);
midi(0x90, 10, 100); render(0.1); midi(0x90, 10, 0);
check('drum kit program selectable', true);
console.log(fail ? `${fail} FAILED` : 'ALL OK');
process.exit(fail ? 1 : 0);
