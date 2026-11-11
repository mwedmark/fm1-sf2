// Puts what install.html installs in dist/install/: the firmware package of the last build, the bank, and manifest.json with the
// update metadata the SDK's updater (fm1_open/sdk/WebUpdate.js) checks: product, build id, SHA-256 of the package and its loader.
// Run it right after building the firmware (fm1_open/sdk/Build.ps1 -FirmwareOnly), so that the build id is that package's.
//   node make_install.js [bank.fm1b] [--voices=N]      (default dist/gm_32k.fm1b, the voices fm1b.h has)
// A package built elsewhere (the Pages site, .github/make_site.js): --fwsc=<package> --identity=<its build-identity.json>
// [--out=<folder>, default dist/install] [--name=<the package's name>].
// The live-cache build too (the install page's choice for MP3 banks): --live-fwsc=<package> --live-identity=<its build-identity.json>
// [--live-name=<its name>]. It gets the same voices. Since 2026-11-11 the default build has the live cache itself (its build-identity.json
// says live_cache: true): the manifest then says main_live, and the page installs that one firmware for BFP8 and MP3 banks alike.
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
// M-VAVE's original V15 (fm1_open/sdk/firmware, Apache-2.0 from M-VAVE): the step in between for an FM-1 on other firmware
// whose own update loader takes only packages of the stock layout (Felucca's: flash.bin exactly 0x93000; ours is 0x70000)
const stockPath = path.join(root, 'fm1_open/sdk/firmware/FM-1_015.fwsc'), stock = fs.readFileSync(stockPath);
const STOCK_SHA = 'db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a';
if (sha(stock) !== STOCK_SHA) throw new Error(stockPath + ' is not the original V15');
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
fs.writeFileSync(path.join(out, 'stock.fwsc'), stock);
// the build's name: --name, else the dist/*.fwsc that build_fw.ps1 copied from this package (none for a plain sdk\Build.ps1)
const dist = path.join(root, 'dist');
const name = option('name') || (fs.existsSync(dist) ? fs.readdirSync(dist) : []).filter(f => f.endsWith('.fwsc')).sort()
  .find(f => fs.statSync(path.join(dist, f)).size === firmware.length && fs.readFileSync(path.join(dist, f)).equals(firmware)) || '';
// the update metadata of a package of ours (the version in the address: never a cached package)
const entry = (bytes, id, file) => ({ schema: 1, product, family: 7, release_version: id.release_version, build_id: id.build_id,
  sha256: sha(bytes), bytes: bytes.length, loader_sha256: sha(loader), url: 'dist/install/' + file + '?v=' + sha(bytes).slice(0, 12), voices });
let live = null;
if (option('live-fwsc')) {
  const bytes = fs.readFileSync(option('live-fwsc'));
  const id = JSON.parse(fs.readFileSync(option('live-identity'), 'utf8').replace(/^\uFEFF/, ''));
  if (!/^[0-9a-f]{64}$/.test(id.build_id)) throw new Error('no build id for the live-cache package');
  // the page tells the builds apart by their id (fm1_open/sdk/Build.ps1 hashes the -D switches too): the same id would make one look installed
  if (id.build_id === identity.build_id) throw new Error('the live-cache package has the same build id as the plain one');
  fs.writeFileSync(path.join(out, 'firmware_live.fwsc'), bytes);
  live = { name: option('live-name') || '', firmware: entry(bytes, id, 'firmware_live.fwsc') };
}
const manifest = {
  name,
  firmware: entry(firmware, identity, 'firmware.fwsc'),
  bank: { url: 'dist/install/bank.fm1b?v=' + sha(bank).slice(0, 12), bytes: bank.length, crc32: (~crc) >>> 0, source: path.relative(root, bankPath).replace(/\\/g, '/') },
  // as fm1_open/sdk/Build.ps1 describes it for the SDK (kind 'stock': its loader is the vendor one, the same as ours)
  stock: { schema: 1, kind: 'stock', product: 'FM-1_015', family: 7, release_version: 'V15', build_id: STOCK_SHA, sha256: STOCK_SHA,
    bytes: stock.length, loader_sha256: 'ef1f2db89b011fa53e71df363f0122de0ea896618c53fb53e8da676322004541', url: 'dist/install/stock.fwsc?v=' + STOCK_SHA.slice(0, 12) },
  ...(identity.live_cache ? { main_live: true } : {}),
  ...(live ? { live } : {}),
  made: new Date().toISOString(),
};
fs.writeFileSync(path.join(out, 'manifest.json'), JSON.stringify(manifest, null, 2));
console.log(`${path.relative(root, out).replace(/\\/g, '/')}: firmware ${name || '(no dist/*.fwsc is this package)'}, ${firmware.length} bytes (${voices} voices, build ${identity.build_id.slice(0, 16)}${identity.live_cache ? ', with the live cache' : ''}), bank ${bank.length} bytes` +
  (live ? `; live cache ${live.firmware.bytes} bytes (build ${live.firmware.build_id.slice(0, 16)})` : ''));
