// A test bank of one looped sine (64 samples a period, 500 Hz at 32 kHz at note 60) in seven programs with different modulation
// records, for test_emu/sine.script: 0 none, 1 vibrato 100 cents at 2 Hz, 2 tremolo 6 dB at 1 Hz, 3 the mod LFO's vibrato after 1 s,
// 4 a 1 kHz low-pass that the modulation envelope opens two octaves in 0.5 s and closes in 1 s (played at 2 kHz: louder, then quieter),
// 5 a still 2 kHz low-pass, 6 the same with 10 dB resonance and chorus (both in 32 bits on the device; test_emu/sine_filter.script).
//   node make_sine_bank.js [out.fm1b]   (default dist/sine_lfo.fm1b; start the emulator with -Bank dist\sine_lfo.fm1b)
const fs = require('fs'), path = require('path');
const html = fs.readFileSync(path.join(__dirname, 'index.html'), 'utf8');
eval(html.match(/<script id="core">([\s\S]*?)<\/script>/)[1] + '\nglobalThis.api = { writeFm1, envRecord, modRecord };');
const A = globalThis.api, tc = s => Math.round(1200 * Math.log2(s));
const pcm = new Int16Array(64 * 100);
for (let i = 0; i < pcm.length; ++i) pcm[i] = Math.round(12000 * Math.sin(2 * Math.PI * i / 64));
const sample = { pcm, enc: null, bytes: pcm.byteLength };
const mods = [null, { vibPitch: 100, vibFreq: tc(2 / 8.176) }, { lfoVolume: 60, lfoFreq: tc(1 / 8.176) },
  { lfoPitch: 100, lfoFreq: tc(2 / 8.176), lfoDelay: tc(1) },
  { filterFc: Math.round(1200 * Math.log2(1000 / 8.176)), envFc: 2400, envAttack: tc(0.5), envDecay: tc(1), envSustain: 1000 },
  { filterFc: Math.round(1200 * Math.log2(2000 / 8.176)) }, { filterFc: Math.round(1200 * Math.log2(2000 / 8.176)), filterQ: 100 }];
const zones = mods.map((m, i) => ({ lo: 0, hi: 127, root: 60, fine: 0, pan: 0, atten: 0, excl: 0, scale: 100, reverb: 0, chorus: i === 6 ? 300 : 0,
  mod: m && A.modRecord(m), loop: true, loopStart: 0, loopEnd: pcm.length, sample,
  env: A.envRecord({ attack: -12000, hold: -12000, decay: -12000, sustain: 0, release: -12000 }, false) }));
const presets = mods.map((m, i) => ({ name: ['sine', 'vibrato', 'tremolo', 'delayed', 'filter', 'lp2k', 'lp2k_q'][i], bank: 0, preset: i, first: i, count: 1 }));
const bin = A.writeFm1({ zones, samples: [sample], presets, rate: 32000, bits: 16 });
const out = process.argv[2] || path.join(__dirname, 'dist/sine_lfo.fm1b');
fs.writeFileSync(out, bin);
console.log(`wrote ${out}: ${bin.length} bytes`);
