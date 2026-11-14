// Renders a MIDI file through the SF2 player's own output stage on the host (WebAssembly: fm1b + sf2fx + sf2out, as the firmware has them), with no CPU
// limit: what the FM-1 should sound like when it keeps up. The messages are the ones dualcore.html plays (midi_events.js), the effect levels the firmware's
// defaults (reverb 30, chorus 30). Stereo 32-bit float WAV at 44.1 kHz, not scaled.
//   node render_host.js <song.mid> <out.wav> [--seconds=75] [--bank=test_fluid/cmp/mp3_64_44k_fill_unpacked.fm1b] [--rate=44100] [--interp4=1] [--tail=3]
const fs = require('fs'), path = require('path'), { execFileSync } = require('child_process'), { parseMidi } = require('./midi_events');
const root = __dirname, opt = {}, pos = [];
for (const a of process.argv.slice(2)) { const m = a.match(/^--([^=]+)=(.*)$/); if (m) opt[m[1]] = m[2]; else pos.push(a); }
if (pos.length !== 2) { console.error('usage: node render_host.js <song.mid> <out.wav> [--seconds=75] [--bank=...] [--rate=44100] [--interp4=1] [--tail=3]'); process.exit(1); }
const seconds = +(opt.seconds || 75), tail = +(opt.tail || 3), rate = +(opt.rate || 44100), interp4 = +(opt.interp4 || 0);
const bankPath = path.resolve(root, opt.bank || 'test_fluid/cmp/mp3_64_44k_fill_unpacked.fm1b');
const sdk = path.join(root, 'fm1_open/build/tools/wasi-sdk-27.0-x86_64-windows'), app = path.join(root, 'fm1_open/app');
const sources = [path.join(root, 'test_fluid/wrap.c'), ...['fm1b.c', 'sf2fx.c', 'sf2out.c'].map(f => path.join(app, f))];
const wasm = path.join(root, `test_fluid/out/host_${rate}${interp4 ? '_i4' : ''}.wasm`);
fs.mkdirSync(path.dirname(wasm), { recursive: true });
const newest = Math.max(...[...sources, ...fs.readdirSync(app).filter(f => /\.(h|inc)$/.test(f)).map(f => path.join(app, f))].map(f => fs.statSync(f).mtimeMs));
if (!fs.existsSync(wasm) || fs.statSync(wasm).mtimeMs < newest)
  execFileSync(path.join(sdk, 'bin/clang.exe'), ['--sysroot=' + path.join(sdk, 'share/wasi-sysroot'), '-O2', '-DFM1_HOST_TEST', `-DFM1B_RATE=${rate}U`, `-DFM1B_INTERP4=${interp4}`,
    '-mexec-model=reactor', '-Wl,--no-entry', '-Wl,--export-dynamic', '-Wall', '-Werror', ...['fm1_open/app', 'fm1_open/app/synth', 'fm1_open/sdk/include'].map(d => '-I' + path.join(root, d)),
    ...sources, '-o', wasm], { stdio: 'inherit' });
const w = new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(wasm)), { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }) }).exports;
if (w._initialize) w._initialize();
const bank = fs.readFileSync(bankPath);
new Uint8Array(w.memory.buffer, w.cf_bank(), bank.length).set(bank);
if (w.cf_bind(bank.length) !== 0 || w.cf_program(0) !== 0) throw new Error('bank rejected: ' + bankPath);
w.cf_fx(30, 30);
const events = parseMidi(fs.readFileSync(pos[0])), msb = new Uint8Array(16), lsb = new Uint8Array(16);
const midi = d => {                                    // as transfer.c's fm1_app_midi_received
  const type = d[0] & 0xf0, ch = d[0] & 15;
  if (type === 0x80 || type === 0x90) { const v = type === 0x90 ? d[2] : 0; if (ch === 9) w.cf_drum(ch, d[1], v); else w.cf_note(ch, d[1], v); }
  else if (type === 0xc0) { const drum = msb[ch] === 120 || (msb[ch] === 1 && !lsb[ch]); if (ch !== 9) w.cf_channel_program(ch, drum ? 128 : 0, d[1]); }
  else if (type === 0xb0) {
    if (d[1] === 0) msb[ch] = d[2]; else if (d[1] === 32) lsb[ch] = d[2];
    else w.cf_control(ch, d[1], d[2]);
  }
  else if (type === 0xe0) w.cf_bend(ch, d[1] | d[2] << 7);
};
const BLOCK = 128, blocks = Math.ceil((seconds + tail) * rate / BLOCK), out = new Float32Array(blocks * BLOCK * 2);
let e = 0, ended = false;
for (let b = 0; b < blocks; ++b) {
  const t = b * BLOCK / rate * 1000;
  while (e < events.length && events[e][0] <= t && events[e][0] <= seconds * 1000) midi(events[e++][1]);
  if (!ended && t >= seconds * 1000) { ended = true; for (let c = 0; c < 16; ++c) w.cf_control(c, 123, 0); }   // dualcore.html stops with all-notes-off
  w.cf_block();
  const o = new Int32Array(w.memory.buffer, w.cf_out(), BLOCK * 2);
  for (let i = 0; i < BLOCK * 2; ++i) out[b * BLOCK * 2 + i] = o[i] / 8388608;
}
const bytes = out.length * 4, hdr = Buffer.alloc(44);
hdr.write('RIFF', 0); hdr.writeUInt32LE(36 + bytes, 4); hdr.write('WAVEfmt ', 8); hdr.writeUInt32LE(16, 16); hdr.writeUInt16LE(3, 20); hdr.writeUInt16LE(2, 22);
hdr.writeUInt32LE(rate, 24); hdr.writeUInt32LE(rate * 8, 28); hdr.writeUInt16LE(8, 32); hdr.writeUInt16LE(32, 34); hdr.write('data', 36); hdr.writeUInt32LE(bytes, 40);
fs.writeFileSync(pos[1], Buffer.concat([hdr, Buffer.from(out.buffer)]));
console.log(`${pos[1]}: ${seconds} s + ${tail} s of ${path.basename(pos[0])}, ${events.length} messages`);
