// Puts what install.html installs in dist/install/: the firmware package of the last build, the bank, and manifest.json with the
// update metadata the SDK's updater (fm1_open/sdk/WebUpdate.js) checks: product, build id, SHA-256 of the package and its loader.
// Run it right after building the firmware (fm1_open/sdk/Build.ps1 -FirmwareOnly), so that the build id is that package's.
//   node make_install.js [bank.fm1b]      (default dist/gm_32k.fm1b)
const fs = require('fs'), path = require('path'), crypto = require('crypto');
const root = __dirname, build = path.join(root, 'fm1_open/build/dx7');
const sha = b => crypto.createHash('sha256').update(b).digest('hex');
const firmware = fs.readFileSync(path.join(build, 'generated/FM1_OFFICIAL_OPEN.fwsc'));
const identity = JSON.parse(fs.readFileSync(path.join(build, 'build-identity.json'), 'utf8').replace(/^﻿/, ''));
const version = fs.readFileSync(path.join(root, 'fm1_open/app/version.h'), 'utf8');
const product = (version.match(/^#define FM1_UPDATE_PRODUCT "(FM-1_8\d{6})"/m) || [])[1];
const voices = +(fs.readFileSync(path.join(root, 'fm1_open/app/fm1b.h'), 'utf8').match(/^#define FM1B_VOICES (\d+)U/m) || [])[1];
const loader = fs.readFileSync(path.join(root, 'fm1_open/sdk/tools/pack/ota.bin'));
const bankPath = process.argv[2] || path.join(root, 'dist/gm_32k.fm1b'), bank = fs.readFileSync(bankPath);
if (!product || !/^[0-9a-f]{64}$/.test(identity.build_id)) throw new Error('no product or build id: build the firmware first');
if (fs.statSync(path.join(build, 'build-identity.json')).mtimeMs + 600000 < fs.statSync(path.join(build, 'generated/FM1_OFFICIAL_OPEN.fwsc')).mtimeMs)
  throw new Error('the package is newer than its build id: build the firmware again');
if (bank.toString('latin1', 0, 4) !== 'FM1B') throw new Error(bankPath + ' is not an FM1B bank');
let crc = 0xffffffff;
for (const b of bank) { crc ^= b; for (let k = 0; k < 8; k++) crc = (crc >>> 1) ^ (0xedb88320 & -(crc & 1)); }
const out = path.join(root, 'dist/install');
fs.mkdirSync(out, { recursive: true });
fs.writeFileSync(path.join(out, 'firmware.fwsc'), firmware);
fs.writeFileSync(path.join(out, 'bank.fm1b'), bank);
const manifest = {
  firmware: { schema: 1, product, family: 7, release_version: identity.release_version, build_id: identity.build_id,
    sha256: sha(firmware), bytes: firmware.length, loader_sha256: sha(loader), url: 'dist/install/firmware.fwsc?v=' + sha(firmware).slice(0, 12), voices },   // the version in the address: never a cached package
  bank: { url: 'dist/install/bank.fm1b?v=' + sha(bank).slice(0, 12), bytes: bank.length, crc32: (~crc) >>> 0, source: path.relative(root, bankPath).replace(/\\/g, '/') },
  made: new Date().toISOString(),
};
fs.writeFileSync(path.join(out, 'manifest.json'), JSON.stringify(manifest, null, 2));
console.log(`dist/install: firmware ${firmware.length} bytes (${voices} voices, build ${identity.build_id.slice(0, 16)}), bank ${bank.length} bytes`);
