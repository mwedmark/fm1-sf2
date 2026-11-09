// Makes the GitHub Pages site: the fm1-emu emulator (MIT, https://github.com/joakimeriksson/fm1-emu) fetched from its own site,
// this repo's firmware package registered in its catalog as "sf2", and the sine test bank as a flash overlay.
//   node .github/make_site.js <outdir> <firmware.fwsc> [version]
// The same steps as start_emu.ps1 (RUNNING.md section 4), for a Pages site instead of a local server.
const fs = require('fs'), path = require('path'), { execFileSync } = require('child_process');
const [out, fwsc, version = String(Date.now())] = process.argv.slice(2);
if (!out || !fwsc) throw new Error('usage: node .github/make_site.js <outdir> <firmware.fwsc> [version]');
const root = path.join(__dirname, '..');
const SITE = 'https://joakimeriksson.github.io/fm1-emu/';
const FILES = ['main.js', 'index.html', 'worklet.js', 'persistence.js', 'fwsc.js', 'tryout.js', 'style.css', 'firmwares.json', 'fm1_web.wasm'];

async function get(url) {
  const r = await fetch(url);
  if (!r.ok) throw new Error(`${url}: ${r.status}`);
  return Buffer.from(await r.arrayBuffer());
}

(async () => {
  fs.rmSync(out, { recursive: true, force: true });
  fs.mkdirSync(out, { recursive: true });
  for (const f of FILES) fs.writeFileSync(path.join(out, f), await get(SITE + f));

  // The emulator's own page becomes emulator.html; index.html is ours.
  fs.renameSync(path.join(out, 'index.html'), path.join(out, 'emulator.html'));

  // A browser keeps the first package it ran under the id sf2 and boots those bytes again: take a kept copy only while the address
  // (?v= below) is the same, so a new build is fetched (start_emu.ps1, 1b).
  const mainPath = path.join(out, 'main.js');
  let main = fs.readFileSync(mainPath, 'utf8');
  const kept = /(  const kept = await keptEntry\(f\.id\);\r?\n)  if \(kept\) \{/;
  if (kept.test(main))
    main = main.replace(kept, '$1  if (kept && (f.id !== "sf2" || kept.url === f.package)) {   // a new build of sf2 is fetched');
  else console.warn('WARNING: main.js changed upstream: a browser may boot an older sf2 build');
  // window.__midi(bytes): MIDI into the emulator's DIN jack, for player.html (JZZ). Same line as start_emu.ps1 adds.
  if (!/window\.__midi/.test(main))
    main = main.trimEnd() + '\nwindow.__midi = (bytes) => send({ type: "midi", bytes: Array.from(bytes) });   // local addition: the page plays MIDI into the DIN jack\n';
  fs.writeFileSync(mainPath, main);

  // fm1-emu takes ?flash= only from http(s) addresses on its own origin (web/tryout.js sameOrigin). player.html hands it a blob: address
  // of the bank the visitor chose; a blob: URL's origin is the page's own, so allow that scheme too (nothing leaves the page).
  const tryPath = path.join(out, 'tryout.js');
  const tryCode = fs.readFileSync(tryPath, 'utf8');
  const allowed = tryCode.replace('/^https?:$/.test(url.protocol)', '/^(https?|blob):$/.test(url.protocol)');
  if (allowed === tryCode) throw new Error('tryout.js changed upstream: the blob: patch no longer applies');
  fs.writeFileSync(tryPath, allowed);

  // The catalog entry, with the version in the address.
  const catPath = path.join(out, 'firmwares.json');
  const cat = JSON.parse(fs.readFileSync(catPath, 'utf8'));
  cat.firmwares = (cat.firmwares || []).filter(e => e.id !== 'sf2');
  cat.firmwares.unshift({ id: 'sf2', name: 'sf2player', version: version.slice(0, 7), author: 'mwedmark', license: 'GPL-3.0',
    about: 'SoundFont player for the FM-1', repo: 'https://github.com/mwedmark/fm1-sf2', package: `sf2player.fwsc?v=${version}` });
  fs.writeFileSync(catPath, JSON.stringify(cat, null, 2));
  fs.copyFileSync(fwsc, path.join(out, 'sf2player.fwsc'));

  // Flash overlay as make_flash_overlay.js writes it: records of a little-endian u32 flash offset and 4096 bytes.
  const overlay = bank => {
    const SECTOR = 4096, start = 0x7C000 + 69632, sectors = Math.ceil(bank.length / SECTOR);
    if (sectors > 256) throw new Error(`the emulator takes banks up to 1 MB, this one is ${bank.length} bytes`);
    const o = Buffer.alloc(sectors * (4 + SECTOR), 0xff);
    for (let i = 0; i < sectors; i++) {
      o.writeUInt32LE(start + i * SECTOR, i * (4 + SECTOR));
      bank.copy(o, i * (4 + SECTOR) + 4, i * SECTOR, Math.min(bank.length, (i + 1) * SECTOR));
    }
    return o;
  };
  // The banks the site hosts: banks/*.fm1b (default.fm1b first), then soundfonts/*.fm1b, then the sine test bank. The first is selected at the start.
  const banks = [], seen = new Set();
  const found = [];
  for (const dir of ['banks', 'soundfonts']) {
    const d = path.join(root, dir);
    if (fs.existsSync(d)) for (const f of fs.readdirSync(d).filter(f => /\.fm1b$/i.test(f)).sort((a, b) => (b === 'default.fm1b') - (a === 'default.fm1b') || a.localeCompare(b))) found.push({ dir, f });
  }
  for (const { dir, f } of found) {
    const id = f.replace(/\.fm1b$/i, '');
    if (seen.has(id) || id === 'sine') { console.warn(`${dir}/${f}: the name ${id} is taken, skipped`); continue; }
    const bytes = fs.readFileSync(path.join(root, dir, f));
    if (bytes.toString('latin1', 0, 4) !== 'FM1B' || bytes.readUInt16LE(4) !== 1) throw new Error(`${dir}/${f} is not a plain FM1B bank (the site runs the plain firmware; MP3-packed banks are refused)`);
    seen.add(id);
    fs.writeFileSync(path.join(out, `${id}.flash`), overlay(bytes));
    fs.copyFileSync(path.join(root, dir, f), path.join(out, f));
    banks.push({ flash: `${id}.flash`, label: `${id === 'default' ? 'Default bank' : id} (${(bytes.length / 1024).toFixed(0)} KB)` });
    console.log(`bank ${dir}/${f}: ${bytes.length} bytes`);
  }

  // The bank: make_sine_bank.js (seven programs of one looped sine, no soundfont needed), laid where the firmware looks for it.
  const tmp = fs.mkdtempSync(path.join(require('os').tmpdir(), 'bank-'));
  const bank = path.join(tmp, 'sine.fm1b');
  execFileSync('node', [path.join(root, 'make_sine_bank.js'), bank], { stdio: 'inherit' });
  fs.writeFileSync(path.join(out, 'sine.flash'), overlay(fs.readFileSync(bank)));
  banks.push({ flash: 'sine.flash', label: 'sine test bank' });
  fs.copyFileSync(path.join(root, 'test_emu', 'sine.script'), path.join(out, 'sine.script'));

  // JZZ.js (MIT, https://github.com/jazz-soft/JZZ) and its SMF player, from .github/site (npm install there first).
  const nm = path.join(__dirname, 'site', 'node_modules');
  for (const [pkg, file] of [['jzz', 'JZZ.js'], ['jzz-midi-smf', 'JZZ.midi.SMF.js']]) {
    fs.copyFileSync(path.join(nm, pkg, 'javascript', file), path.join(out, file));
    const lic = fs.readdirSync(path.join(nm, pkg)).find(f => /^licen[sc]e/i.test(f));
    if (lic) fs.copyFileSync(path.join(nm, pkg, lic), path.join(out, `LICENSE-${pkg}.txt`));
  }
  // Every MIDI file of the repo: MIDIs/ (songs; Rick Astley is selected at the start) and test_emu/midi/ (made from the test scripts by make_midi.js).
  const midi = [];
  for (const [dir, group, sub] of [['MIDIs', 'Songs', 'songs'], ['test_emu/midi', 'Test files', 'test']]) {
    const d = path.join(root, dir);
    if (!fs.existsSync(d)) continue;
    fs.mkdirSync(path.join(out, 'midi', sub), { recursive: true });
    for (const f of fs.readdirSync(d).filter(f => /\.midi?$/i.test(f)).sort((a, b) => a.localeCompare(b, 'en', { sensitivity: 'base' }))) {
      fs.copyFileSync(path.join(d, f), path.join(out, 'midi', sub, f));
      midi.push({ file: `midi/${sub}/${f}`, label: f.replace(/\.midi?$/i, '').replace(/_/g, ' '), group, selected: /astley/i.test(f) });
    }
  }
  if (!midi.some(m => m.selected) && midi.length) midi[0].selected = true;
  console.log(`${midi.length} MIDI files`);
  fs.writeFileSync(path.join(out, 'player.html'), fs.readFileSync(path.join(__dirname, 'player.template.html'), 'utf8')
    .replace('__FILES__', JSON.stringify(midi)).replace('__BANKS__', JSON.stringify(banks)));

  // The emulator's licence (MIT): its repository's file, or a pointer if that cannot be fetched.
  let licence = null;
  for (const branch of ['main', 'master']) {
    try { licence = await get(`https://raw.githubusercontent.com/joakimeriksson/fm1-emu/${branch}/LICENSE`); break; } catch (e) { /* next */ }
  }
  fs.writeFileSync(path.join(out, 'LICENSE-fm1-emu.txt'), licence || 'fm1-emu is MIT licensed: https://github.com/joakimeriksson/fm1-emu\n');

  const q = `fw=sf2&flash=${banks[0].flash}&mhz=240`;
  // "Buy me a beer": the first custom link of .github/FUNDING.yml (GitHub's Sponsor button), none if the file has none.
  const fundingPath = path.join(__dirname, 'FUNDING.yml');
  const funding = fs.existsSync(fundingPath) && (fs.readFileSync(fundingPath, 'utf8').match(/^custom:\s*\[?\s*["']?(https:\/\/[^"'\],\s]+)/m) || [])[1];
  const beer = funding ? `<p><a href="${funding}"><b>&#127866; Buy me a beer</b></a></p>\n` : '';
  fs.writeFileSync(path.join(out, 'index.html'), `<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>FM-1 SF2 player emulator</title>
<style>:root{--room:#18191b;--ink:#e9e6de;--dim:#a9a7a1;--text:#c9c6be;--amber:#f2a33a;--amber-hi:#ffc56e;--well:#232427;--line:#3a3b3f;--bar:#161719;color-scheme:dark}html,body{background:var(--room);color:var(--ink)}a{color:var(--amber)}a:hover{color:var(--amber-hi)}a:focus-visible,button:focus-visible,select:focus-visible,input:focus-visible{outline:2px solid var(--amber);outline-offset:2px}code{background:var(--well);border:1px solid var(--line);border-radius:4px;padding:0 .3em;color:var(--ink)}button,select,input[type=file]{background:var(--well);color:var(--ink);border:1px solid var(--line);border-radius:6px;padding:4px 10px}button:hover,select:hover{border-color:var(--dim)}button:active{background:var(--line)}small{color:var(--dim)}body{font:16px/1.5 system-ui,sans-serif;max-width:40em;margin:2em auto;padding:0 16px}</style></head>
<body>
<h1>FM-1 SF2 player in the emulator</h1>
<p>The firmware of <a href="https://github.com/mwedmark/fm1-sf2">mwedmark/fm1-sf2</a> (build ${version.slice(0, 7)}) running in
<a href="https://github.com/joakimeriksson/fm1-emu">fm1-emu</a>, in your browser, with ${banks[0].label}. Other banks: the list on the MIDI player page, or your own FM1B file.
The sine test bank has seven programs (plain, vibrato, tremolo, delayed vibrato, filter sweep, two low-passes).</p>
<ul>
<li><a href="emulator.html?${q}"><b>Open the emulator</b></a> and play it with the keys or a MIDI keyboard (Chrome or Edge, Web MIDI).</li>
<li><a href="emulator.html?${q}&amp;script=sine.script">The same, with the sine test script</a> (steps through the programs by itself).</li>
<li><a href="player.html"><b>MIDI player</b></a>: the emulator as a MIDI output (JZZ.js): play the MIDI files of the repo or one of your own.
Its <b>Copy link</b> gives a link to a bank and song that plays at the first click, e.g. <a href="player.html?song=astley&amp;play=1">player.html?song=astley&amp;play=1</a>.</li>
<li><a href="sf2player.fwsc">sf2player.fwsc</a>, the firmware package of this build.</li>
</ul>
<p>Press HOME for the channel page. EDIT arms the channel under the cursor and GLO steps its MIDI channel.</p>
${beer}<p><small>The emulator is MIT licensed, see <a href="LICENSE-fm1-emu.txt">its licence</a>; JZZ.js and its SMF player are MIT too (<a href="LICENSE-jzz.txt">licence</a>).</small></p>
</body></html>
`);
  console.log('site written to', out);
})().catch(e => { console.error(e); process.exit(1); });
