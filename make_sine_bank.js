// A test bank of one looped sine (64 samples a period, 500 Hz at 32 kHz at note 60) in four programs with different modulation
// records, for test_emu/sine.script: 0 none, 1 vibrato 100 cents at 2 Hz, 2 tremolo 6 dB at 1 Hz, 3 the mod LFO's vibrato after 1 s.
//   node make_sine_bank.js [out.fm1b]   (default dist/sine_lfo.fm1b; start the emulator with -Bank dist\sine_lfo.fm1b)
const fs = require('fs'), path = require('path');
const html = fs.readFileSync(path.join(__dirname, 'index.html'), 'utf8');
eval(html.match(/<script id="core">([\s\S]*?)<\/script>/)[1] + '\nglobalThis.api = { writeFm1, envRecord, modRecord };');
const A = globalThis.api, tc = s => Math.round(1200 * Math.log2(s));
const pcm = new Int16Array(64 * 100);
for (let i = 0; i < pcm.length; ++i) pcm[i] = Math.round(12000 * Math.sin(2 * Math.PI * i / 64));
const sample = { pcm, enc: null, bytes: pcm.byteLength };
const mods = [null, { vibPitch: 100, vibFreq: tc(2 / 8.176) }, { lfoVolume: 60, lfoFreq: tc(1 / 8.176) },
  { lfoPitch: 100, lfoFreq: tc(2 / 8.176), lfoDelay: tc(1) }];
const zones = mods.map(m => ({ lo: 0, hi: 127, root: 60, fine: 0, pan: 0, atten: 0, excl: 0, scale: 100, reverb: 0, chorus: 0,
  mod: m && A.modRecord(m), loop: true, loopStart: 0, loopEnd: pcm.length, sample,
  env: A.envRecord({ attack: -12000, hold: -12000, decay: -12000, sustain: 0, release: -12000 }, false) }));
const presets = mods.map((m, i) => ({ name: ['sine', 'vibrato', 'tremolo', 'delayed'][i], bank: 0, preset: i, first: i, count: 1 }));
const bin = A.writeFm1({ zones, samples: [sample], presets, rate: 32000, bits: 16 });
const out = process.argv[2] || path.join(__dirname, 'dist/sine_lfo.fm1b');
fs.writeFileSync(out, bin);
console.log(`wrote ${out}: ${bin.length} bytes`);
