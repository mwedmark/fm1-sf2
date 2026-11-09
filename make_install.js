// Puts what install.html installs in dist/install/: the firmware package of the last build, the bank, and manifest.json with the
// update metadata the SDK's updater (fm1_open/sdk/WebUpdate.js) checks: product, build id, SHA-256 of the package and its loader.
// Run it right after building the firmware (fm1_open/sdk/Build.ps1 -FirmwareOnly), so that the build id is that package's.
//   node make_install.js [bank.fm1b] [--voices=N]      (default dist/gm_32k.fm1b, the voices fm1b.h has)
// A package built elsewhere (the Pages site, .github/make_site.js): --fwsc=<package> --identity=<its build-identity.json>
// [--out=<folder>, default dist/install] [--name=<the package's name>].
const fs = require('fs'), path = require('path'), crypto = require('crypto');
const root = __dirname, build = path.join(root, 'fm1_open/build/dx7');
const option = name => { const a = process.argv.find(a => a.startsWith('--' + name + '=')); return a ? a.slice(name.length + 3) : null; };
const fwscPath = option('fwsc') || path.join(build, 'generated/FM1_OFFICIAL_OPEN.fwsc');
const identityPath = option('identity') || path.join(build, 'build-identity.json');
const sha = b => crypto.createHash('sha256').update(b).digest('hex');
const firmware = fs.readFileSync(fwscPath);
const identity = JSON.parse(fs.readFileSync(identityPath, 'utf8').replace(/^﻿/, ''));
const version = fs.readFileSync(path.join(root, 'fm1_open/app/version.h'), 'utf8');
const product = (version.match(/^#define FM1_UPDATE_PRODUCT "(FM-1_8\d{6})"/m) || [])[1];
// the voices as fm1b.h has them now; --voices=N for a build made with another value (the 24-voice package)
const voicesArg = process.argv.find(a => a.startsWith('--voices='));
const voices = voicesArg ? +voicesArg.slice(9) : +(fs.readFileSync(path.join(root, 'fm1_open/app/fm1b.h'), 'utf8').match(/^#define FM1B_VOICES (\d+)U/m) || [])[1];
const loader = fs.readFileSync(path.join(root, 'fm1_open/sdk/tools/pack/ota.bin'));
const bankPath = process.argv.slice(2).find(a => !a.startsWith('--')) || path.join(root, 'dist/gm_32k.fm1b'), bank = fs.readFileSync(bankPath);
if (!product || !/^[0-9a-f]{64}$/.test(identity.build_id)) throw new Error('no product or build id: build the firmware first');
if (!option('fwsc') && fs.statSync(identityPath).mtimeMs + 600000 < fs.statSync(fwscPath).mtimeMs)
  throw new Error('the package is newer than its build id: build the firmware again');
if (bank.toString('latin1', 0, 4) !== 'FM1B') throw new Error(bankPath + ' is not an FM1B bank');
let crc = 0xffffffff;
for (const b of bank) { crc ^= b; for (let k = 0; k < 8; k++) crc = (crc >>> 1) ^ (0xedb88320 & -(crc & 1)); }
const out = option('out') || path.join(root, 'dist/install');
fs.mkdirSync(out, { recursive: true });
fs.writeFileSync(path.join(out, 'firmware.fwsc'), firmware);
fs.writeFileSync(path.join(out, 'bank.fm1b'), bank);
// the build's name: --name, else the dist/*.fwsc that build_fw.ps1 copied from this package (none for a plain sdk\Build.ps1)
const dist = path.join(root, 'dist');
const name = option('name') || (fs.existsSync(dist) ? fs.readdirSync(dist) : []).filter(f => f.endsWith('.fwsc')).sort()
  .find(f => fs.statSync(path.join(dist, f)).size === firmware.length && fs.readFileSync(path.join(dist, f)).equals(firmware)) || '';
const manifest = {
  name,
  firmware: { schema: 1, product, family: 7, release_version: identity.release_version, build_id: identity.build_id,
    sha256: sha(firmware), bytes: firmware.length, loader_sha256: sha(loader), url: 'dist/install/firmware.fwsc?v=' + sha(firmware).slice(0, 12), voices },   // the version in the address: never a cached package
  bank: { url: 'dist/install/bank.fm1b?v=' + sha(bank).slice(0, 12), bytes: bank.length, crc32: (~crc) >>> 0, source: path.relative(root, bankPath).replace(/\\/g, '/') },
  made: new Date().toISOString(),
};
fs.writeFileSync(path.join(out, 'manifest.json'), JSON.stringify(manifest, null, 2));
console.log(`${path.relative(root, out).replace(/\\/g, '/')}: firmware ${name || '(no dist/*.fwsc is this package)'}, ${firmware.length} bytes (${voices} voices, build ${identity.build_id.slice(0, 16)}), bank ${bank.length} bytes`);
