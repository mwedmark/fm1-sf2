// Runs the SF2 player firmware's bank upload (fm1_open/app/sf2_upload.c) on a computer against the host side in index.html:
// the frames uploadFrames() makes go in as the firmware's SysEx callback would get them, and the flash (a byte array that
// only lets bits be cleared) must end up holding the bank. Also the refusals: a bad CRC, a data frame before begin, a
// write outside the bank, a flash error.
//   node test_upload.js [bank.bin]        (without one: a bank made from the SoundFont in 1mgm.sf2, or random bytes)
const fs = require('fs'), path = require('path'), { execFileSync } = require('child_process');
const root = __dirname;
const sdk = path.join(root, 'fm1_open/build/tools/wasi-sdk-27.0-x86_64-windows');
const wasmPath = path.join(root, 'test_upload/upload_test.wasm');
const sources = [path.join(root, 'test_upload/stubs.c'), path.join(root, 'fm1_open/app/sf2_upload.c')];
const stale = !fs.existsSync(wasmPath) || sources.some(s => fs.statSync(s).mtimeMs > fs.statSync(wasmPath).mtimeMs);
if (stale) {
  const incl = ['fm1_open/app', 'fm1_open/sdk/include', 'fm1_open/third_party', 'fm1_open/app/synth'].map(d => '-I' + path.join(root, d));
  execFileSync(path.join(sdk, 'bin/clang.exe'), ['--sysroot=' + path.join(sdk, 'share/wasi-sysroot'), '-O1', '-DFM1_HOST_TEST', '-mexec-model=reactor',
    '-Wl,--no-entry', '-Wl,--export-dynamic', '-Wl,--allow-undefined', ...incl, ...sources, '-o', wasmPath], { stdio: 'inherit' });
}
const html = fs.readFileSync(path.join(root, 'index.html'), 'utf8');
eval(html.match(/<script id="core">([\s\S]*?)<\/script>/)[1] + '\nglobalThis.api = { uploadFrames, crc32, parseSf2, buildBank, writeFm1 };');

let bin;
if (process.argv[2]) bin = new Uint8Array(fs.readFileSync(process.argv[2]));
else {
  const sf2 = path.join(root, '1mgm.sf2');
  if (fs.existsSync(sf2)) {
    const b = fs.readFileSync(sf2), sf = globalThis.api.parseSf2(b.buffer.slice(b.byteOffset, b.byteOffset + b.length));
    bin = globalThis.api.writeFm1(globalThis.api.buildBank(sf, [sf.presets.findIndex(p => p.bank === 0 && p.preset === 0)], { rate: 12000, sea: true }));
  } else { bin = new Uint8Array(20000).map((_, i) => (i * 2654435761) >>> 24); bin.set([0x46, 0x4d, 0x31, 0x42]); }
}
// the firmware only binds a bank whose header says FM1B and its own size
const dv0 = new DataView(bin.buffer, bin.byteOffset, bin.length);
if (String.fromCharCode(...bin.subarray(0, 4)) === 'FM1B') dv0.setUint32(40, bin.length, true);

const instance = new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(wasmPath)), { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }) });
const w = instance.exports;
if (w._initialize) w._initialize();
const mem = () => new Uint8Array(w.memory.buffer);
const OFFSET = 69632;
let failed = 0;
const check = (name, ok, extra = '') => { console.log((ok ? 'ok   ' : 'FAIL ') + name + (extra ? ' ' + extra : '')); if (!ok) failed++; };

function send(frame) {                               // one frame in, polled, the reply read back
  mem().set(frame, w.test_frame_buffer());
  w.test_clear_replies();
  const before = 0;
  w.test_offer(frame.length);
  w.test_poll();
  if (w.test_reply_count() !== before + 1) return null;
  const r = mem().subarray(w.test_replies() + before * 16, w.test_replies() + before * 16 + 8);
  return { cmd: r[5], status: r[6], ok: r[0] === 0xf0 && r[1] === 0x7d && r[2] === 0x53 && r[3] === 0x46 && r[4] === 2 && r[7] === 0xf7 };
}
const flashAt = (at, n) => mem().subarray(w.test_flash() + OFFSET + at, w.test_flash() + OFFSET + at + n);

for (const chunk of [448, 112, 7, 1]) {
  if (chunk < 112 && bin.length > 30000) continue;       // the small chunks are for the edge cases, not the big bank
  w.test_reset();
  const frames = uploadFrames(bin, chunk);
  let bad = null;
  for (const [i, f] of frames.entries()) {
    const r = send(f);
    if (!r || !r.ok || r.status !== 0 || r.cmd !== (f[5] | 0x40)) { bad = { i, r }; break; }
  }
  check(`upload in ${frames.length} frames of ${chunk} bytes`, !bad, bad ? JSON.stringify(bad) : '');
  check('  the flash holds the bank', Buffer.compare(Buffer.from(flashAt(0, bin.length)), Buffer.from(bin)) === 0);
  check('  nothing after the bank was touched', flashAt(bin.length, 64).every(b => b === 0xff));
  check('  the bank is bound once, after the CRC', w.test_counter(2) === 1 && w.test_bound_bytes() === bin.length, `binds ${w.test_counter(2)} bound ${w.test_bound_bytes()}`);
  check('  playing stopped first (unbind) and the sectors were erased', w.test_counter(3) === 1 && w.test_counter(0) === Math.ceil(bin.length / 4096), `erases ${w.test_counter(0)}`);
}

w.test_reset();
let frames = uploadFrames(bin, 448);
check('data before begin is refused (status 5)', send(frames[1])?.status === 5);
check('end before begin is refused (status 5)', send(frames[frames.length - 1])?.status === 5);
check('begin with a size over the flash is refused (status 2)', send(Uint8Array.from([0xf0, 0x7d, 0x53, 0x46, 2, 1, 127, 127, 127, 127, 15, 0xf7]))?.status === 2);
check('begin with a tiny size is refused (status 2)', send(Uint8Array.from([0xf0, 0x7d, 0x53, 0x46, 2, 1, 5, 0, 0, 0, 0, 0xf7]))?.status === 2);
check('an unknown command is refused (status 1)', send(Uint8Array.from([0xf0, 0x7d, 0x53, 0x46, 2, 99, 1, 7, 0xf7, 0xf7].slice(0, 9).concat([0xf7]).slice(0, 10)))?.status === 1);

w.test_reset();
check('begin', send(frames[0])?.status === 0);
check('data past the end of the bank is refused (status 2)', send(uploadFrames(new Uint8Array(448), 448)[1].map((b, i) => i >= 6 && i < 11 ? [127, 127, 127, 127, 3][i - 6] : b))?.status === 2);
for (let i = 1; i < frames.length - 1; i++) send(frames[i]);
const badEnd = Uint8Array.from(frames[frames.length - 1]); badEnd[6] ^= 1;
w.test_reset();
for (let i = 0; i < frames.length - 1; i++) send(frames[i]);
check('a wrong CRC is refused (status 4) and nothing is bound', send(badEnd)?.status === 4 && w.test_counter(2) === 0);
check('the right CRC afterwards is not accepted: the transfer ended (status 5)', send(frames[frames.length - 1])?.status === 5);

w.test_reset();
send(frames[0]);
w.test_fail_write_at(OFFSET + 448);
const results = frames.slice(1, 4).map(send).map(r => r.status);
check('a flash write error is reported (status 3)', results[0] === 0 && results[1] === 3, JSON.stringify(results));

// cmd 9: does the flash hold this bank (size, CRC) and does it play?
const checkFrame = (size, crc) => Uint8Array.from([0xf0, 0x7d, 0x53, 0x46, 2, 9, ...num35(size), ...num35(crc), 0xf7]);
w.test_reset();
check('check with an empty flash: not there (status 4)', send(checkFrame(bin.length, crc32(bin)))?.status === 4);
for (const f of uploadFrames(bin, 448)) send(f);
const c9 = send(checkFrame(bin.length, crc32(bin)));
check('check after the upload: the same bank (status 0, reply cmd 0x49)', c9?.status === 0 && c9.cmd === 0x49);
check('check with another CRC: not the same (status 4)', send(checkFrame(bin.length, crc32(bin) ^ 1))?.status === 4);
check('check with another size: not the same (status 4)', send(checkFrame(bin.length - 1, crc32(bin.subarray(0, bin.length - 1))))?.status === 4);
check('check of a malformed frame (status 1)', send(checkFrame(bin.length, crc32(bin)).slice(0, 16).map((b, i) => i === 15 ? 0xf7 : b))?.status === 1);
send(frames[0]);   // a new upload begins: the bank no longer plays
check('check while the bank does not play: not the same (status 4)', send(checkFrame(bin.length, crc32(bin)))?.status === 4);

check('crc32 of "123456789" is the standard one', crc32(Uint8Array.from(Buffer.from('123456789'))) === 0xcbf43926);
console.log(failed ? failed + ' FAILED' : 'ALL OK');
process.exit(failed ? 1 : 0);
