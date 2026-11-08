// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
// The zones and sounds of presets the bank map cannot reach (any bank but 0 and 128, or over 254 zones) are not written. A font with
// GS and MT-32 variation banks (4gmgsmt.sf2) had 47 % of its zones in the file for nothing. No font is needed: the bank is made by hand.
//   node test_dropped_presets.js
const fs = require('fs'), path = require('path');
const html = fs.readFileSync(path.join(__dirname, 'index.html'), 'utf8');
eval(html.match(/<script id="core">([\s\S]*?)<\/script>/)[1] + '\nglobalThis.api = { writeFm1, envRecord };');
const A = globalThis.api;
let failed = 0, checks = 0;
function check(ok, what) { ++checks; if (!ok) { ++failed; console.log('FAIL', what); } else console.log('ok  ', what); }

// Three sounds with values nothing else in a bank has: shared by a written and a dropped preset (A), only in written ones (B),
// only in dropped ones (D).
const sound = (value, n) => { const pcm = new Int16Array(n).fill(value); return { pcm, enc: null, bytes: pcm.byteLength }; };
const A_ = sound(0x1111, 64), B_ = sound(0x2222, 96), D_ = sound(0x5a5a, 128);
const zone = (lo, hi, sample, root = 60) => ({ lo, hi, root, fine: 0, pan: 0, atten: 0, excl: 0, scale: 100, reverb: 0, chorus: 0, mod: null, loop: false, loopStart: 0, loopEnd: 0,
  sample, env: A.envRecord({ attack: -12000, hold: -12000, decay: -12000, sustain: 0, release: -12000 }, false) });
const zones = [
  zone(0, 63, A_, 40), zone(64, 127, B_, 80),            // 0: bank 0 program 0                    (written)
  zone(0, 127, D_), zone(0, 127, A_),                      // 1: bank 7 program 0, a GS variation    (dropped)
  zone(36, 36, A_, 36),                                    // 2: bank 128 program 0, a kit            (written)
  ...Array.from({ length: 255 }, () => zone(0, 127, D_)),  // 3: bank 0 program 9, 255 zones          (dropped: over 254)
  zone(0, 127, B_, 70)];                                   // 4: bank 0 program 3                     (written)
const presets = [
  { name: 'Piano', bank: 0, preset: 0, first: 0, count: 2 },
  { name: 'Variation', bank: 7, preset: 0, first: 2, count: 2 },
  { name: 'Kit', bank: 128, preset: 0, first: 4, count: 1 },
  { name: 'Too many', bank: 0, preset: 9, first: 5, count: 255 },
  { name: 'Bass', bank: 0, preset: 3, first: 260, count: 1 }];
const bank = { zones, samples: [A_, B_, D_], presets, rate: 32000, bits: 16 };
const bin = A.writeFm1(bank), dv = new DataView(bin.buffer, bin.byteOffset, bin.length);
const u16 = o => dv.getUint16(o, true), u32 = o => dv.getUint32(o, true);

check(bank.dropped === 2, 'two presets are dropped (bank 7, and 255 zones)');
check(u16(12) === 3 && u16(14) === 4, `3 presets and 4 zones are written (got ${u16(12)} and ${u16(14)}), not the ${zones.length} zones given`);
check(bank.zonesWritten === 4 && bank.soundsWritten === 2, 'the bank says what the file holds: 4 zones, 2 sounds');
const map = u32(20), pre = u32(24), key = u32(28), zt = u32(32);
const slot = s => u16(map + s * 2);
check(slot(0) === 0 && slot(128) === 1 && slot(3) === 2, 'bank map: program 0 is preset 0, kit 0 is preset 1, program 3 is preset 2');
check(slot(9) === 0xffff, 'bank map: the dropped program 9 is empty');
check(u16(pre) === 0 && u16(pre + 2) === 2 && u16(pre + 24) === 2 && u16(pre + 26) === 1 && u16(pre + 48) === 3 && u16(pre + 50) === 1,
  'presets point at their zones in the compacted table: (0, 2), (2, 1), (3, 1)');
check(bin[key + 10] === 0 && bin[key + 100] === 1 && bin[key + 128 + 36] === 0 && bin[key + 256 + 5] === 0, 'key maps pick the right zone in each preset');

// the sound each written zone points at holds that sound's values
const at = (zi) => { const o = u32(zt + zi * 24), n = u32(zt + zi * 24 + 4); return Array.from(new Int16Array(bin.buffer.slice(bin.byteOffset + o, bin.byteOffset + o + n * 2))); };
const all = (arr, v, n) => arr.length === n && arr.every(x => x === v);
check(all(at(0), 0x1111, 64) && all(at(1), 0x2222, 96) && all(at(2), 0x1111, 64) && all(at(3), 0x2222, 96), 'zones 0..3 read sound A, B, A, B from the file');
check(u32(zt) === u32(zt + 2 * 24), 'a sound two written zones share is in the file once');
check(Buffer.from(bin).indexOf(Buffer.from([0x5a, 0x5a, 0x5a, 0x5a])) === -1, 'the sound only the dropped presets use is not in the file');
const sizeOfTables = u32(36);
check(bin.length === sizeOfTables + 128 + 192, `the sample data is exactly sound A and B (${bin.length - sizeOfTables} bytes)`);
check(u32(72) === 0, 'no layer entries: the zones of the written presets do not overlap, and the dropped ones add none');

// a bank with no presets (loose samples) keeps its zones: nothing to drop them for
const loose = A.writeFm1({ zones: [zone(0, 127, A_), zone(0, 127, B_)], samples: [A_, B_], presets: [], rate: 32000, bits: 16 });
check(new DataView(loose.buffer, loose.byteOffset).getUint16(14, true) === 2, 'zones that belong to no preset are written');
// nothing dropped: every zone and sound stays, and the file is the same as before the change
const whole = A.writeFm1({ zones: [zones[0], zones[1], zones[4]], samples: [A_, B_], presets: [presets[0], Object.assign({}, presets[2], { first: 2 })], rate: 32000, bits: 16 });
const wd = new DataView(whole.buffer, whole.byteOffset);
check(wd.getUint16(12, true) === 2 && wd.getUint16(14, true) === 3 && whole.length === wd.getUint32(36, true) + 128 + 192, 'with nothing dropped all 3 zones and both sounds are written');
console.log(failed ? `${failed} of ${checks} checks FAILED` : `all ${checks} checks passed`);
process.exit(failed ? 1 : 0);
