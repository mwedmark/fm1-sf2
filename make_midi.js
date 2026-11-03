// Turns the fm1-emu input scripts (test_emu/*.script, "ms:mHEX" events) into standard MIDI files for the real FM-1, played from
// Windows over USB, DIN or BLE (RUNNING.md section 6). Times start at the script's first event (the emulator's boot time is left
// out) and every file ends, `hold` seconds after its last event, on every channel it used, with reset all controllers (bend,
// wheel, pedal), the default reverb and chorus sends (30, 20) and all notes off.
//   node make_midi.js [hold seconds, default 6]        writes test_emu/midi/<name>.mid for every script with MIDI events
const fs = require('fs'), path = require('path');
const hold = Number(process.argv[2] || 6);
const dir = path.join(__dirname, 'test_emu'), out = path.join(dir, 'midi');
fs.mkdirSync(out, { recursive: true });
const vlq = n => { const b = [n & 127]; while ((n >>= 7)) b.unshift((n & 127) | 128); return b; };
for (const file of fs.readdirSync(dir).filter(f => f.endsWith('.script'))) {
  const events = fs.readFileSync(path.join(dir, file), 'utf8').trim().split(',').map(e => e.trim().match(/^(\d+):m([0-9A-Fa-f]+)$/))
    .filter(Boolean).map(m => ({ t: Number(m[1]), bytes: m[2].match(/../g).map(h => parseInt(h, 16)) }));
  if (!events.length) continue;
  events.sort((a, b) => a.t - b.t);
  const start = events[0].t, end = events[events.length - 1].t + hold * 1000;
  const channels = [...new Set(events.map(e => e.bytes[0] & 15))];
  for (const c of channels) events.push(...[[121, 0], [91, 30], [93, 20], [123, 0]].map(([cc, v]) => ({ t: end, bytes: [0xb0 | c, cc, v] })));
  const track = [];
  let now = start;
  for (const e of events) { track.push(...vlq(e.t - now), ...e.bytes); now = e.t; }
  track.push(0, 0xff, 0x2f, 0);
  // format 0, one track, 1000 ticks a quarter at 1 000 000 us a quarter: one tick is 1 ms
  const head = [0x4d, 0x54, 0x68, 0x64, 0, 0, 0, 6, 0, 0, 0, 1, 0x03, 0xe8];
  const tempo = [0, 0xff, 0x51, 3, 0x0f, 0x42, 0x40];
  const body = [...tempo, ...track], len = body.length;
  const bytes = Buffer.from([...head, 0x4d, 0x54, 0x72, 0x6b, len >>> 24, (len >>> 16) & 255, (len >>> 8) & 255, len & 255, ...body]);
  fs.writeFileSync(path.join(out, file.replace(/\.script$/, '.mid')), bytes);
  console.log(`${file} -> midi/${file.replace(/\.script$/, '.mid')}: ${events.length} events, ${((end - start) / 1000).toFixed(1)} s`);
}
