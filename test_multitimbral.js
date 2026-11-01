// Runs the SF2 player's real voice engine (fm1_open/app/fm1b.c) on WebAssembly and checks the multitimbral behaviour: program
// change per channel, the mix of channels, note off per channel, volume/expression, the channel controllers, the percussion
// channel, and that a player which never sends a program change sounds exactly as it did before.
//   node test_multitimbral.js [bank.fm1b]       (default: dist/gm_32k.fm1b, the SEA5 bank the firmware plays)
const fs = require('fs'), path = require('path'), { execFileSync } = require('child_process');
const root = __dirname;
const sdk = path.join(root, 'fm1_open/build/tools/wasi-sdk-27.0-x86_64-windows');
const wasmPath = path.join(root, 'test_mt/mt_test.wasm');
const sources = [path.join(root, 'test_mt/wrap.c'), path.join(root, 'fm1_open/app/fm1b.c'), path.join(root, 'fm1_open/web/synth/sea_decode.c')];
const headers = ['fm1b.h', 'fm1b_config.h', 'fm1b_dqt.inc'].map(h => path.join(root, 'fm1_open/app', h));
if (!fs.existsSync(wasmPath) || [...sources, ...headers].some(s => fs.statSync(s).mtimeMs > fs.statSync(wasmPath).mtimeMs))
  execFileSync(path.join(sdk, 'bin/clang.exe'), ['--sysroot=' + path.join(sdk, 'share/wasi-sysroot'), '-O1', '-DFM1_HOST_TEST', '-mexec-model=reactor', '-Wl,--no-entry',
    '-Wl,--export-dynamic', ...['fm1_open/app', 'fm1_open/app/synth', 'fm1_open/sdk/include'].map(d => '-I' + path.join(root, d)), ...sources, '-o', wasmPath], { stdio: 'inherit' });
const w = new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(wasmPath)), { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }) }).exports;
if (w._initialize) w._initialize();

const bank = fs.readFileSync(process.argv[2] || path.join(root, 'dist/gm_32k.fm1b'));
new Uint8Array(w.memory.buffer, w.mt_bank(), bank.length).set(bank);
const BLOCK = 128;
let failed = 0, checks = 0;
function check(ok, what) { ++checks; if (!ok) { ++failed; console.log('FAIL', what); } else console.log('ok  ', what); }

// Start from a fresh bind (it forgets every channel), with `program` selected for the panel, and flush the event queue.
function fresh(program = 0) {
  if (w.mt_bind(bank.length) !== 0) throw new Error('bank rejected');
  if (w.mt_program(0, program) !== 0) throw new Error('no program ' + program);
  w.mt_block();
}
function render(blocks) {
  const all = new Int32Array(blocks * BLOCK);
  for (let b = 0; b < blocks; ++b) {
    w.mt_block();
    all.set(new Int32Array(w.memory.buffer, w.mt_out(), BLOCK), b * BLOCK);
  }
  return all;
}
const same = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
const sumAbs = a => a.reduce((s, v) => s + Math.abs(v), 0);
const add = (a, b) => a.map((v, i) => v + b[i]);
const NOTE = 60, LONG = 300;                        // blocks: about 0.87 s
// A program whose note keeps sounding while the key is held (a looped sample), for the tests that need a voice to outlast them.
let SUSTAIN = -1;
for (let p = 0; p < 128 && SUSTAIN < 0; ++p) {
  fresh(p);
  w.mt_note(0, NOTE, 100);
  render(1500);
  if (w.mt_voices() === 1) SUSTAIN = p;
}
if (SUSTAIN < 0) throw new Error('no program in this bank sustains');
console.log('sustaining program for the held-note tests:', SUSTAIN);

// 1. No program change: a channel plays the panel's preset, bit for bit what the panel itself plays.
fresh(0);
w.mt_panel_note(NOTE, 100);
const panelPiano = render(LONG);
check(sumAbs(panelPiano) > 0, 'the panel plays something');
fresh(0);
w.mt_note(0, NOTE, 100);
check(same(render(LONG), panelPiano), 'channel 1 without a program change = the panel preset');
fresh(0);
w.mt_note(5, NOTE, 100);
check(same(render(LONG), panelPiano), 'any channel without a program change = the panel preset');
check(w.mt_multitimbral() === 0, 'no channel has a preset of its own');

// 2. Program change on a channel = selecting that program for everything, for that channel only.
const program = 73;                                  // flute in GM
fresh(program);
w.mt_panel_note(NOTE, 100);
const panelFlute = render(LONG);
check(!same(panelFlute, panelPiano), 'the flute is not the piano');
fresh(0);
w.mt_channel_program(1, 0, program);
w.mt_note(1, NOTE, 100);
check(same(render(LONG), panelFlute), 'channel 2 after program change plays the flute exactly');
check(w.mt_multitimbral() === 1 && w.mt_status(0) === 1, 'one channel has its own preset');
check(w.mt_status(1) === 1 && w.mt_status(2) === program, 'the panel is told: channel 2, program ' + program);
fresh(0);
w.mt_channel_program(1, 0, program);
w.mt_note(0, NOTE, 100);
check(same(render(LONG), panelPiano), 'channel 1 still plays the panel preset');
fresh(0);
w.mt_channel_program(1, 0, program);
w.mt_panel_note(NOTE, 100);
check(same(render(LONG), panelPiano), 'the panel keys are not affected by a channel\'s program');

// 3. Two channels at once are the sum of the two alone.
fresh(0);
w.mt_channel_program(0, 0, 0);
w.mt_channel_program(1, 0, program);
w.mt_note(0, NOTE, 100);
const alone0 = render(LONG);
fresh(0);
w.mt_channel_program(1, 0, program);
w.mt_note(1, NOTE + 7, 90);
const alone1 = render(LONG);
fresh(0);
w.mt_channel_program(1, 0, program);
w.mt_note(0, NOTE, 100);
w.mt_note(1, NOTE + 7, 90);
check(same(render(LONG), add(alone0, alone1)), 'channels 1 and 2 together = the sum of each alone');

// 4. Order: a program change applies to the notes after it, not to the ones before it.
fresh(0);
w.mt_note(1, NOTE, 100);
w.mt_channel_program(1, 0, program);
check(same(render(LONG), panelPiano), 'a note before the program change keeps the old preset');
fresh(0);
w.mt_channel_program(1, 0, program);
w.mt_note(1, NOTE, 100);
check(same(render(LONG), panelFlute), 'a note after it plays the new one');

// 5. Note off only releases the channel it was played on.
fresh(SUSTAIN);                                      // sustains until released
w.mt_note(0, NOTE, 100);
w.mt_note(1, NOTE, 100);
render(20);
check(w.mt_voices() === 2, 'two voices for the same note on two channels');
w.mt_note(0, NOTE, 0);
render(2000);                                        // well past the release
check(w.mt_voices() === 1, 'note off on channel 1 leaves the voice of channel 2 sounding');
w.mt_note(1, NOTE, 0);
render(2000);
check(w.mt_voices() === 0, 'note off on channel 2 releases the last one');
fresh(SUSTAIN);
w.mt_note(0, NOTE, 100);
render(20);
w.mt_note(1, NOTE, 0);
render(2000);
check(w.mt_voices() === 1, 'note off on another channel does nothing');

// 6. Volume and expression follow the General MIDI curve, per channel, and the default is unity.
fresh(0);
w.mt_control(1, 7, 127);
w.mt_control(1, 11, 127);
w.mt_note(1, NOTE, 100);
check(same(render(LONG), panelPiano), 'volume 127, expression 127 is bit exact unity');
fresh(0);
w.mt_control(1, 7, 64);
w.mt_note(1, NOTE, 100);
let ratio = sumAbs(render(LONG)) / sumAbs(panelPiano);
check(Math.abs(ratio - (64 / 127) ** 2) < 0.01, `volume 64 is (64/127)^2 = ${((64 / 127) ** 2).toFixed(4)} (got ${ratio.toFixed(4)})`);
fresh(0);
w.mt_control(1, 11, 64);
w.mt_note(1, NOTE, 100);
ratio = sumAbs(render(LONG)) / sumAbs(panelPiano);
check(Math.abs(ratio - (64 / 127) ** 2) < 0.01, `expression 64 is the same curve (got ${ratio.toFixed(4)})`);
fresh(0);
w.mt_control(1, 7, 64);
w.mt_control(1, 11, 64);
w.mt_note(1, NOTE, 100);
ratio = sumAbs(render(LONG)) / sumAbs(panelPiano);
check(Math.abs(ratio - (64 / 127) ** 4) < 0.005, `volume and expression multiply (got ${ratio.toFixed(4)}, want ${((64 / 127) ** 4).toFixed(4)})`);
fresh(0);
w.mt_control(1, 7, 0);
w.mt_note(1, NOTE, 100);
check(sumAbs(render(LONG)) === 0, 'volume 0 is silent');
fresh(0);
w.mt_control(1, 7, 20);
w.mt_control(1, 121, 0);
w.mt_note(1, NOTE, 100);
check(same(render(LONG), panelPiano), 'CC121 puts volume back to unity');
fresh(0);
w.mt_control(1, 7, 20);
w.mt_note(0, NOTE, 100);
check(same(render(LONG), panelPiano), 'the volume of channel 2 does not touch channel 1');
fresh(SUSTAIN);
w.mt_note(1, NOTE, 100);
render(100);
const before = sumAbs(render(10));
w.mt_control(1, 7, 0);
render(2);
const after = sumAbs(render(10));
check(before > 0 && after < before * 0.001, 'a volume change reaches a note that is already sounding');

// 7. Channel controllers: all notes off releases, all sound off cuts, both only on their channel.
fresh(SUSTAIN);
w.mt_note(0, NOTE, 100);
w.mt_note(1, NOTE + 4, 100);
render(20);
w.mt_control(0, 123, 0);
render(2000);
check(w.mt_voices() === 1, 'CC123 releases the notes of channel 1 only');
w.mt_control(1, 120, 0);
render(1);
check(w.mt_voices() === 0, 'CC120 cuts the notes of channel 2');

// 8. A preset the bank does not have leaves the channel alone.
fresh(0);
w.mt_channel_program(2, 128, 127);
w.mt_block();
check(w.mt_multitimbral() === 0, 'a missing preset in the percussion bank is ignored');
w.mt_channel_program(2, 0, program);
w.mt_block();
check(w.mt_multitimbral() === 1, 'a program change that exists counts');
w.mt_bind(bank.length);
check(w.mt_multitimbral() === 0, 'binding a bank forgets the channels');

// 9. Percussion on channel 10: the kit, scaled by that channel's volume.
fresh(0);
w.mt_panel_drum(38, 100);
const panelKit = render(LONG);
check(sumAbs(panelKit) > 0, 'the panel plays the percussion kit');
fresh(0);
w.mt_drum(9, 38, 100);
const kit = render(LONG);
check(same(kit, panelKit), 'channel 10 plays the percussion kit exactly as the panel does');
fresh(0);
w.mt_control(9, 7, 64);
w.mt_drum(9, 38, 100);
ratio = sumAbs(render(LONG)) / sumAbs(kit);
check(Math.abs(ratio - (64 / 127) ** 2) < 0.01, `volume of channel 10 scales the kit (got ${ratio.toFixed(4)})`);
fresh(0);
w.mt_channel_program(9, 0, program);
w.mt_drum(9, 38, 100);
check(same(render(LONG), kit), 'a program change on a drum channel does not change the kit');

// 10. Many voices at once: more notes than voices must steal, not crash.
fresh(0);
w.mt_channel_program(1, 0, program);
for (let n = 0; n < 24; ++n) w.mt_note(n % 2, 40 + n, 100);
const chord = render(100);
check(sumAbs(chord) > 0 && w.mt_voices() <= 16, 'a dense chord on two channels sounds and never uses more voices than there are');

console.log(`${checks - failed}/${checks} checks passed`);
process.exit(failed ? 1 : 0);
