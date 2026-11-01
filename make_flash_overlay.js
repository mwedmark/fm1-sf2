// Lays an FM1B bank where the SF2 player firmware looks for it, as the sector file the fm1-emu emulator takes with
// &flash=URL (records of a little-endian u32 flash offset and 4096 bytes). For trying a bank that is too big for the
// firmware image without a device.
//   node make_flash_overlay.js <bank.bin> <out> [user partition start in the flash image, default 0x7C000]
const fs = require('fs');
const [inPath, outPath, baseArg = '0x7C000'] = process.argv.slice(2);
const SECTOR = 4096, BANK_OFFSET = 69632;                 // fm1b_config.h: FM1B_FLASH_OFFSET inside the user partition
const bank = fs.readFileSync(inPath);
const start = Number(baseArg) + BANK_OFFSET;
if (start % SECTOR) throw new Error('the bank must start on a sector');
const sectors = Math.ceil(bank.length / SECTOR);
if (sectors > 256) throw new Error('the emulator takes at most 256 sectors');
const out = Buffer.alloc(sectors * (4 + SECTOR), 0xff);
for (let i = 0; i < sectors; i++) {
  const o = i * (4 + SECTOR);
  out.writeUInt32LE(start + i * SECTOR, o);
  bank.copy(out, o + 4, i * SECTOR, Math.min(bank.length, (i + 1) * SECTOR));
}
fs.writeFileSync(outPath, out);
console.log('wrote', outPath, sectors, 'sectors from 0x' + start.toString(16));
