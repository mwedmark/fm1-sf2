// The CRC32s the panel's info page (REC) shows, worked out on the PC, to check a device against a build.
//   node fm1_info.js [sdk.elf] [bank.fm1b]
// Defaults: fm1_open/build/dx7/sdk.elf and dist/gm_32k.fm1b.
// FIRMWARE is the CRC32 of the application image the packager gets (.text .data .ram0_data .cache_ram_data back to back, as
// sdk/Build.ps1 cuts them out of the ELF); the .fwsc package itself is ciphered, so its own CRC32 differs. BANK is the CRC32 of
// the bank file (the same as zip and 7-Zip show). SOUNDSET is one CRC32 over the firmware image followed by the bank file.
const fs = require('fs'), path = require('path');
const [elfArg, bankArg] = process.argv.slice(2);
const elfPath = path.resolve(elfArg || path.join(__dirname, 'fm1_open/build/dx7/sdk.elf'));
const bankPath = path.resolve(bankArg || path.join(__dirname, 'dist/gm_32k.fm1b'));

const table = new Uint32Array(256).map((_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
const crcAdd = (crc, bytes) => { for (const b of bytes) crc = table[(crc ^ b) & 0xff] ^ (crc >>> 8); return crc >>> 0; };
const hex = v => (~v >>> 0).toString(16).toUpperCase().padStart(8, '0');

function firmwareImage(file) {
  const b = fs.readFileSync(file), dv = new DataView(b.buffer, b.byteOffset, b.length);
  if (b.readUInt32BE(0) !== 0x7f454c46) throw new Error(`${file} is not an ELF file`);
  const shoff = dv.getUint32(32, true), size = dv.getUint16(46, true), count = dv.getUint16(48, true), strings = dv.getUint16(50, true);
  const header = i => shoff + i * size, strOff = dv.getUint32(header(strings) + 16, true);
  const sections = {};
  for (let i = 0; i < count; i++) {
    const h = header(i);
    let n = strOff + dv.getUint32(h, true), name = '';
    while (b[n]) name += String.fromCharCode(b[n++]);
    sections[name] = { type: dv.getUint32(h + 4, true), offset: dv.getUint32(h + 16, true), bytes: dv.getUint32(h + 20, true) };
  }
  const parts = ['.text', '.data', '.ram0_data', '.cache_ram_data'].map(n => sections[n])
    .filter(s => s && s.type !== 8).map(s => b.subarray(s.offset, s.offset + s.bytes));
  return Buffer.concat(parts);
}
// The bank's info block (FORMAT.md, "Info"): name and date, or none in a bank made before it.
function bankInfo(b) {
  const dv = new DataView(b.buffer, b.byteOffset, b.length), at = dv.getUint32(16, true) >= 80 ? dv.getUint32(76, true) : 0;
  const text = (o, n) => { let s = ''; for (let i = 0; i < n && b[o + i] >= 32 && b[o + i] < 127; i++) s += String.fromCharCode(b[o + i]); return s; };
  return at && at + 52 <= b.length ? { name: text(at, 32), date: text(at + 32, 20) } : { name: '', date: '' };
}

const image = firmwareImage(elfPath), fwState = crcAdd(~0 >>> 0, image);
console.log(`FIRMWARE ${path.relative(process.cwd(), elfPath)}`);
console.log(`  CRC32 ${hex(fwState)}  ${image.length} BYTES`);
if (fs.existsSync(bankPath)) {
  const bank = fs.readFileSync(bankPath), info = bankInfo(bank);
  console.log(`BANK ${path.relative(process.cwd(), bankPath)}`);
  console.log(`  NAME ${info.name || '(none: an older bank)'}  BUILT ${info.date || '-'}`);
  console.log(`  CRC32 ${hex(crcAdd(~0 >>> 0, bank))}  ${bank.length} BYTES`);
  console.log(`SOUNDSET CHECKSUM: ${hex(crcAdd(fwState, bank))}`);
} else console.log(`no bank at ${bankPath}`);
