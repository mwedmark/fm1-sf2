// Runs the SF2 player's real voice engine (fm1_open/app/fm1b.c) on WebAssembly and checks the multitimbral behaviour: program
// change per channel, the mix of channels, note off per channel, volume/expression, pan, the channel controllers, the percussion
// channel, and that a player which never sends a program change sounds exactly as it did before.
//   node test_multitimbral.js [bank.fm1b]       (default: dist/gm_32k.fm1b, the SEA6 bank the firmware plays)
const fs = require('fs'), path = require('path'), { execFileSync } = require('child_process');
const root = __dirname;
const sdk = path.join(root, 'fm1_open/build/tools/wasi-sdk-27.0-x86_64-windows');
const wasmPath = path.join(root, 'test_mt/mt_test.wasm');
const sources = [path.join(root, 'test_mt/wrap.c'), path.join(root, 'fm1_open/app/fm1b.c'), path.join(root, 'fm1_open/web/synth/sea_decode.c')];
const headers = ['fm1b.h', 'fm1b_config.h', 'fm1b_dqt.inc', 'fm1b_dqt6.inc'].map(h => path.join(root, 'fm1_open/app', h));
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
for (let n = 0; n < w.mt_voice_max() + 8; ++n) w.mt_note(n % 2, 30 + n, 100);   // more than FM1B_VOICES
const chord = render(100);
check(sumAbs(chord) > 0 && w.mt_voices() <= w.mt_voice_max(), 'a dense chord on two channels sounds and never uses more voices than there are');

// 11. Pitch bend. The period by autocorrelation (parabolic peak), for comparing pitches across notes and zones.
function period(a, from = 20000, n = 4096) {
  const x = Array.from(a.subarray(from, from + n + 1200));
  let best = 0, lag = 0;
  const r = [];
  for (let l = 40; l < 1200; ++l) {
    let s = 0;
    for (let i = 0; i < n; ++i) s += x[i] * x[i + l];
    r[l] = s;
  }
  const top = Math.max(...r.slice(40));
  for (let l = 41; l < 1199; ++l)                     // the first peak near the top, not a multiple of the period
    if (r[l] > 0.9 * top && r[l] >= r[l - 1] && r[l] >= r[l + 1]) { lag = l; break; }
  const y0 = r[lag - 1], y1 = r[lag], y2 = r[lag + 1];
  return lag + 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2);
}
const cents = (pa, pb) => 1200 * Math.log2(pb / pa);   // how much higher a sounds than b, from their periods
const BEND_BLOCKS = 260;
fresh(SUSTAIN);
w.mt_note(0, NOTE, 100);
const plain = render(BEND_BLOCKS);
fresh(SUSTAIN);
w.mt_bend(0, 8192);
w.mt_note(0, NOTE, 100);
check(same(render(BEND_BLOCKS), plain), 'bend at the centre is bit exact no bend');
fresh(SUSTAIN);
w.mt_bend(0, 0);
w.mt_note(0, NOTE + 2, 100);
let d = cents(period(render(BEND_BLOCKS)), period(plain));
check(Math.abs(d) < 8, `bend fully down (2 semitones) on D4 sounds as C4 (off by ${d.toFixed(1)} cents)`);
check(w.mt_info(0, 3) === -200, 'the channel reports -200 cents of bend');
fresh(SUSTAIN);
[[101, 0], [100, 0], [6, 12], [38, 0]].forEach(([c, v]) => w.mt_control(0, c, v));
w.mt_bend(0, 0);
w.mt_note(0, NOTE + 12, 100);
d = cents(period(render(BEND_BLOCKS)), period(plain));
check(Math.abs(d) < 8, `RPN 0 sets the range: 12 semitones down on C5 sounds as C4 (off by ${d.toFixed(1)} cents)`);
fresh(SUSTAIN);
w.mt_note(0, NOTE, 100);
render(100);
w.mt_bend(0, 16383);
const bent = render(BEND_BLOCKS);
d = cents(period(bent), period(plain));
check(Math.abs(d - 200) < 8, `a bend reaches a note that already sounds (+${d.toFixed(1)} cents, want 200)`);
fresh(SUSTAIN);
w.mt_note(1, NOTE, 100);
const ch2 = render(BEND_BLOCKS);
fresh(SUSTAIN);
w.mt_bend(0, 0);
w.mt_note(1, NOTE, 100);
check(same(render(BEND_BLOCKS), ch2), 'a bend on channel 1 leaves channel 2 alone');
fresh(SUSTAIN);
w.mt_bend(0, 0);
w.mt_control(0, 121, 0);
w.mt_note(0, NOTE, 100);
check(same(render(BEND_BLOCKS), plain), 'CC121 resets the bend');

// 12. Modulation wheel: vibrato around the note's pitch, nothing at 0.
fresh(SUSTAIN);
w.mt_control(0, 1, 0);
w.mt_note(0, NOTE, 100);
check(same(render(BEND_BLOCKS), plain), 'modulation wheel at 0 is bit exact without it');
fresh(SUSTAIN);
w.mt_control(0, 1, 127);
w.mt_note(0, NOTE, 100);
const vib = render(BEND_BLOCKS);
check(!same(vib, plain), 'modulation wheel up changes the sound');
const periods = [];
for (let at = 8000; at + 6000 < vib.length; at += 1000) periods.push(period(vib, at, 1024));
const spread = cents(Math.min(...periods), Math.max(...periods));
check(spread > 40 && spread < 120, `the vibrato swings about +-50 cents (${spread.toFixed(0)} cents peak to peak)`);
fresh(SUSTAIN);
w.mt_control(0, 1, 127);
w.mt_note(1, NOTE, 100);
check(same(render(BEND_BLOCKS), ch2), 'the wheel on channel 1 leaves channel 2 alone');

// 13. Sustain pedal: a note released while it is down sounds on, and releases with the pedal.
fresh(SUSTAIN);
w.mt_control(0, 64, 127);
w.mt_note(0, NOTE, 100);
render(20);
w.mt_note(0, NOTE, 0);
render(2000);
check(w.mt_voices() === 1, 'with the pedal down a released note keeps sounding');
check(w.mt_info(0, 5) === 1, 'the channel reports the pedal');
w.mt_control(0, 64, 0);
render(2000);
check(w.mt_voices() === 0, 'pedal up releases it');
fresh(SUSTAIN);
w.mt_control(0, 64, 127);
w.mt_note(1, NOTE, 100);
render(20);
w.mt_note(1, NOTE, 0);
render(2000);
check(w.mt_voices() === 0, 'the pedal of channel 1 does not hold channel 2');
fresh(SUSTAIN);
w.mt_control(0, 64, 127);
w.mt_note(0, NOTE, 100);
w.mt_note(0, NOTE, 0);
w.mt_control(0, 121, 0);
render(2000);
check(w.mt_voices() === 0, 'CC121 lifts the pedal');

// 14. What the channel page reads.
fresh(0);
w.mt_channel_program(3, 0, program);
w.mt_control(3, 1, 64);
w.mt_note(3, NOTE, 100);
w.mt_note(3, NOTE + 4, 100);
w.mt_block();
check(w.mt_info(3, 0) === 1 && w.mt_info(3, 2) === program && w.mt_info(3, 1) === 0, 'the channel reports its own program');
check(w.mt_info(3, 4) === 64 && w.mt_info(3, 7) === 2 && w.mt_info(3, 6) === 2, 'the channel reports its wheel, note count and voices');
check(w.mt_info(4, 0) === 0 && w.mt_info(4, 6) === 0 && w.mt_info(4, 8) === 127, 'a silent channel follows the panel at full volume');

// 15. The channel page's KNOB2: back to following the panel.
check(w.mt_has_program(0, 0) === 1 && w.mt_has_program(0, program) === 1, 'the bank has program 1 and the flute');
fresh(0);
w.mt_channel_program(0, 0, program);
w.mt_follow(0);
w.mt_note(0, NOTE, 100);
check(same(render(LONG), panelPiano) && w.mt_multitimbral() === 0, 'a channel told to follow plays the panel preset again, bit exact');

// 16. Effect sends per channel (CC 91, CC 93). While all sounding channels send alike there are no buses, only the sends.
function renderSends(blocks) {
  const o = new Int32Array(blocks * BLOCK), r = new Int32Array(blocks * BLOCK), c = new Int32Array(blocks * BLOCK);
  let flags = 0;
  for (let b = 0; b < blocks; ++b) {
    const f = w.mt_block_sends();
    flags |= f;
    o.set(new Int32Array(w.memory.buffer, w.mt_out(), BLOCK), b * BLOCK);
    if (f & 1) r.set(new Int32Array(w.memory.buffer, w.mt_rev(), BLOCK), b * BLOCK);
    if (f & 2) c.set(new Int32Array(w.memory.buffer, w.mt_cho(), BLOCK), b * BLOCK);
  }
  return { o, r, c, flags };
}
fresh(SUSTAIN);
w.mt_note(0, NOTE, 100);
let rs = renderSends(BEND_BLOCKS);
check(rs.flags === 0 && w.mt_send(0) === 30 && w.mt_send(1) === 20, 'default sends 30 and 20, no buses');
check(same(rs.o, plain), 'rendering with sends leaves the dry mix bit exact');
check(w.mt_info(0, 9) === 30 && w.mt_info(0, 10) === 20, 'the channel reports its default sends');
fresh(SUSTAIN);
w.mt_control(0, 91, 90); w.mt_control(1, 91, 90); w.mt_control(0, 93, 0); w.mt_control(1, 93, 0);
w.mt_note(0, NOTE, 100); w.mt_note(1, NOTE + 7, 100);
rs = renderSends(50);
check(rs.flags === 0 && w.mt_send(0) === 90 && w.mt_send(1) === 0, 'two channels with the same sends still need no buses');
fresh(SUSTAIN);
w.mt_note(1, NOTE + 7, 100);
const fifth = render(50);
fresh(SUSTAIN);
w.mt_control(0, 91, 127); w.mt_control(1, 91, 0);
w.mt_note(0, NOTE, 100); w.mt_note(1, NOTE + 7, 100);
rs = renderSends(50);
const c1 = plain.subarray(0, 50 * BLOCK);
let worst = 0;
for (let i = 0; i < rs.r.length; ++i) worst = Math.max(worst, Math.abs(rs.r[i] - c1[i]));
check(rs.flags === 1 && worst <= 2, `send 127 and 0: the reverb bus is channel 1 alone (off by at most ${worst})`);
check(same(rs.o, add(c1, fifth)), 'the dry mix is both channels, as without sends');
fresh(SUSTAIN);
w.mt_control(0, 93, 64);
w.mt_note(0, NOTE, 100); w.mt_note(1, NOTE + 7, 100);
rs = renderSends(50);
const want = add(c1.map(v => v * 65 / 128), fifth.map(v => v * 20 / 128));
let err = 0, size = 0;
for (let i = 0; i < want.length; ++i) { err += Math.abs(rs.c[i] - want[i]); size += Math.abs(want[i]); }
check(rs.flags === 2 && err / size < 0.001, `the chorus bus is each channel times its send (relative error ${(err / size).toExponential(1)})`);

// 17. Pan per channel (CC 10) on top of the zones' own (SF2 generator 17). The mono mix (the effects' input) never changes; a
// panned voice puts its far side in a cut buffer, and a channel is that mix less its cut.
function renderPan(blocks) {
  const o = new Int32Array(blocks * BLOCK), l = new Int32Array(blocks * BLOCK), r = new Int32Array(blocks * BLOCK), rev = new Int32Array(blocks * BLOCK);
  let flags = 0;
  for (let b = 0; b < blocks; ++b) {
    const f = w.mt_block_sends(), at = b * BLOCK;
    flags |= f;
    const out = new Int32Array(w.memory.buffer, w.mt_out(), BLOCK);
    const cl = new Int32Array(w.memory.buffer, w.mt_cut(0), BLOCK), cr = new Int32Array(w.memory.buffer, w.mt_cut(1), BLOCK);
    o.set(out, at);
    for (let i = 0; i < BLOCK; ++i) { l[at + i] = out[i] - (f & 4 ? cl[i] : 0); r[at + i] = out[i] - (f & 8 ? cr[i] : 0); }
    if (f & 1) rev.set(new Int32Array(w.memory.buffer, w.mt_rev(), BLOCK), at);
  }
  return { o, l, r, rev, flags };
}
const PAN_BLOCKS = 50, plain50 = plain.subarray(0, PAN_BLOCKS * BLOCK);
fresh(SUSTAIN);
w.mt_note(0, NOTE, 100);
let pn = renderPan(PAN_BLOCKS);
check(!(pn.flags & 12) && same(pn.l, plain50) && same(pn.r, plain50) && w.mt_info(0, 11) === 64, 'a centred zone at CC10 64: both sides are the mix, no cut');
fresh(SUSTAIN);
w.mt_control(0, 10, 0);
w.mt_note(0, NOTE, 100);
pn = renderPan(PAN_BLOCKS);
check(pn.flags === 8 && same(pn.o, plain50) && same(pn.l, plain50) && pn.r.every(v => v === 0), 'CC10 0: all left, the right side silent, the mix unchanged');
fresh(SUSTAIN);
w.mt_control(0, 10, 127);
w.mt_note(0, NOTE, 100);
pn = renderPan(PAN_BLOCKS);
check(pn.flags === 4 && same(pn.r, plain50) && pn.l.every(v => v === 0), 'CC10 127: all right');
fresh(SUSTAIN);
w.mt_control(0, 10, 32);
w.mt_note(0, NOTE, 100);
pn = renderPan(PAN_BLOCKS);
const halfway = sumAbs(pn.r) / sumAbs(pn.l);
check(same(pn.l, plain50) && Math.abs(halfway - Math.SQRT1_2) < 0.01, `CC10 32: the right side at sqrt(1/2) of the left (${halfway.toFixed(3)})`);
fresh(SUSTAIN);
w.mt_note(0, NOTE, 100);
renderPan(10);
w.mt_control(0, 10, 0);
pn = renderPan(10);
check(pn.r.slice(BLOCK).every(v => v === 0) && sumAbs(pn.l) > 0, 'CC10 pans a note that already sounds');
check(w.mt_info(0, 11) === 0, 'the channel reports its pan');
w.mt_control(0, 121, 0);
pn = renderPan(10);
check(w.mt_info(0, 11) === 64 && !(pn.flags & 12), 'CC121 centres the pan');
fresh(SUSTAIN);
w.mt_control(1, 10, 0);
w.mt_note(0, NOTE, 100); w.mt_note(1, NOTE + 7, 100);
pn = renderPan(PAN_BLOCKS);
check(same(pn.r, plain50) && same(pn.l, add(plain50, fifth)), 'channel 2 panned left leaves channel 1 in the middle');
// The sends do not follow the pan: the reverb bus of a voice is the same panned or not.
const reverbOf = pan => {
  fresh(SUSTAIN);
  w.mt_control(0, 91, 127); w.mt_control(1, 91, 0); w.mt_control(0, 10, pan);
  w.mt_note(0, NOTE, 100); w.mt_note(1, NOTE + 7, 100);
  return renderPan(PAN_BLOCKS);
};
const revCentre = reverbOf(64), revLeft = reverbOf(0);
check(revCentre.flags === 1 && revLeft.flags === 9 && same(revCentre.rev, revLeft.rev), 'the reverb send is the same panned or not');
// The kit's own pan from the SoundFont: with CC10 at 64 some keys sound left, some right; CC10 moves them all.
let sides = 0, panned = -1;
for (let key = 35; key <= 81; ++key) {
  fresh(0);
  w.mt_drum(9, key, 100);
  const f = renderPan(4).flags & 12;
  sides |= f;
  if (f && panned < 0) panned = key;
}
check(sides === 12, 'the SoundFont pans the kit: keys to the left and to the right at CC10 64');
fresh(0);
w.mt_control(9, 10, 0);
w.mt_drum(9, panned, 100);
pn = renderPan(4);
check(pn.flags === 8 && pn.r.every(v => v === 0), 'CC10 0 moves a panned drum all the way left');

console.log(`${checks - failed}/${checks} checks passed`);
process.exit(failed ? 1 : 0);
