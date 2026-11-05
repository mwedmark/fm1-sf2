// Builds an FM1B bank of a whole SoundFont with index.html's converter, in the sample format to try.
//   node make_bank.js <sea5|sea6|bfp8|pcm8|pcm12|pcm16> [rate] [in.sf2] [out.fm1b]
// Defaults: 32000 Hz, 1mgm.sf2, dist/gm_<rate/1000>k_<format>.fm1b. Every format plays on the same firmware: the engine reads
// the bank's header. Make it the standard bank by copying it to dist/gm_32k.fm1b, or start the emulator with it
// (.\start_emu.ps1 -Bank dist\gm_32k_bfp8.fm1b).
const fs = require('fs'), path = require('path');
const [format, rateArg, inArg, outArg] = process.argv.slice(2);
const options = { sea5: { sea: true, seaBits: 5 }, sea6: { sea: true, seaBits: 6 }, bfp8: { bfp: true },
                  pcm8: { bits: 8 }, pcm12: { bits: 12 }, pcm16: { bits: 16 } }[format];
if (!options) { console.error('format: sea5, sea6, bfp8, pcm8, pcm12 or pcm16'); process.exit(1); }
const rate = Number(rateArg || 32000);
const html = fs.readFileSync(path.join(__dirname, 'index.html'), 'utf8');
eval(html.match(/<script id="core">([\s\S]*?)<\/script>/)[1] + '\nglobalThis.api = { parseSf2, buildBank, writeFm1, bankInfo, FM1_SAMPLE_BANK };');
const A = globalThis.api;
const b = fs.readFileSync(inArg || path.join(__dirname, '1mgm.sf2'));
const sf = A.parseSf2(b.buffer.slice(b.byteOffset, b.byteOffset + b.length));
const out = outArg || path.join(__dirname, `dist/gm_${Math.round(rate / 1000)}k_${format}.fm1b`);
const bin = A.writeFm1(A.buildBank(sf, -2, { rate, ...options }), A.bankInfo(path.basename(out)));   // the panel's info page shows the name and date
fs.mkdirSync(path.dirname(out), { recursive: true });
fs.writeFileSync(out, bin);
console.log(`wrote ${out}: ${bin.length} bytes, ${(bin.length / A.FM1_SAMPLE_BANK * 100).toFixed(1)} % of the ${A.FM1_SAMPLE_BANK} bytes the FM-1 has for samples` +
  (bin.length > A.FM1_SAMPLE_BANK ? ' - TOO BIG' : ''));
