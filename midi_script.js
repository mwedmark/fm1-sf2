// Turns a standard MIDI file into an fm1-emu input script ("ms:mHEX" events into the DIN jack), to play a song in the emulator.
//   node midi_script.js <in.mid> [out.script] [--start=3000] [--seconds=N]
// Default out: emu/<name>.script (start_emu.ps1 serves emu/). Notes, controllers, program changes and pitch bend are kept;
// meta events and SysEx are left out. The first event plays at --start ms (the firmware has booted by then).
const fs = require('fs'), path = require('path');
const args = process.argv.slice(2), flag = (n, d) => { const a = args.find(x => x.startsWith(`--${n}=`)); return a ? Number(a.split('=')[1]) : d; };
const [inPath, outArg] = args.filter(a => !a.startsWith('--'));
if (!inPath) { console.error('node midi_script.js <in.mid> [out.script] [--start=3000] [--seconds=N]'); process.exit(1); }
const start = flag('start', 3000), limit = flag('seconds', Infinity) * 1000;
const b = fs.readFileSync(inPath);
if (b.toString('latin1', 0, 4) !== 'MThd') throw new Error('not a MIDI file');
const tracks = b.readUInt16BE(10), division = b.readUInt16BE(12);
if (division & 0x8000) throw new Error('SMPTE time is not supported');

const events = [];                       // { tick, order, bytes } or { tick, order, tempo }
let at = 14, order = 0;
for (let t = 0; t < tracks; t++) {
  while (b.toString('latin1', at, at + 4) !== 'MTrk') at += 8 + b.readUInt32BE(at + 4);
  const end = at + 8 + b.readUInt32BE(at + 4);
  let p = at + 8, tick = 0, status = 0;
  const vlq = () => { let v = 0, c; do { c = b[p++]; v = v << 7 | c & 0x7f; } while (c & 0x80); return v; };
  while (p < end) {
    tick += vlq();
    let s = b[p];
    if (s & 0x80) p++; else s = status;
    if (s === 0xff) {
      const type = b[p++], len = vlq();
      if (type === 0x51) events.push({ tick, order: order++, tempo: b.readUIntBE(p, 3) });
      p += len;
    } else if (s === 0xf0 || s === 0xf7) {
      const len = vlq();                                     // (not p += vlq(): that reads p before vlq moves it)
      p += len;
    }
    else {
      status = s;
      const n = (s & 0xf0) === 0xc0 || (s & 0xf0) === 0xd0 ? 1 : 2;
      events.push({ tick, order: order++, bytes: [s, ...b.subarray(p, p + n)] });
      p += n;
    }
  }
  at = end;
}
events.sort((x, y) => x.tick - y.tick || x.order - y.order);
let tempo = 500000, lastTick = 0, ms = 0;
const out = [];
for (const e of events) {
  ms += (e.tick - lastTick) * tempo / division / 1000;
  lastTick = e.tick;
  if (e.tempo) { tempo = e.tempo; continue; }
  if (ms > limit) break;
  const type = e.bytes[0] & 0xf0;
  if (type === 0xa0 || type === 0xd0) continue;   // aftertouch: the player ignores it
  out.push(`${Math.round(start + ms)}:m${e.bytes.map(x => x.toString(16).padStart(2, '0')).join('').toUpperCase()}`);
}
const outPath = outArg || path.join(__dirname, 'emu', path.basename(inPath).replace(/\.midi?$/i, '').replace(/[^A-Za-z0-9_-]+/g, '_') + '.script');
fs.writeFileSync(outPath, out.join(','));
console.log(`${outPath}: ${out.length} events, ${(ms / 1000).toFixed(1)} s`);
