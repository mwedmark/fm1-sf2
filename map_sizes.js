// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
// What takes the room in the linked firmware: the biggest sections of the link map that sdk/Build.ps1 writes (fm1_open\build\dx7\sdk.map), the code and data
// that come from each part of the application, and the live cache's share. Needs only node; reads GNU ld style maps (a section on one line, or its name alone on a
// line and the address and size on the next), and says so if it finds none.
//   node map_sizes.js fm1_open\build\dx7\sdk.map [how many, default 40]
const fs = require('fs');
const [file, topArg] = process.argv.slice(2);
if (!file) { console.error('usage: node map_sizes.js sdk.map [top]'); process.exit(1); }
const lines = fs.readFileSync(file, 'latin1').split(/\r?\n/);
const rows = [];
for (let i = 0; i < lines.length; i++) {
  let m = lines[i].match(/^\s*(\.[^\s]+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s*(.*)$/);
  if (!m) {
    const n = lines[i].match(/^\s*(\.[^\s]+)\s*$/), next = lines[i + 1] && lines[i + 1].match(/^\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s*(.*)$/);
    if (n && next) { m = [null, n[1], next[1], next[2], next[3]]; ++i; }
  }
  if (m && parseInt(m[3], 16) > 0 && !/^\.(debug|comment|symtab|strtab|shstrtab)/.test(m[1])) rows.push({ section: m[1], address: parseInt(m[2], 16), size: parseInt(m[3], 16), object: (m[4] || '').trim() });
}
if (!rows.length) { console.log('no sections found: is this a GNU ld map? first lines:\n' + lines.slice(0, 12).join('\n')); process.exit(1); }
const total = rows.reduce((a, r) => a + r.size, 0), kb = n => (n / 1024).toFixed(1).padStart(7) + ' KB';
console.log(`${rows.length} sections, ${total} bytes in all (RAM sections and the image both)`);
console.log(`\nthe biggest ${topArg || 40} sections:`);
for (const r of [...rows].sort((a, b) => b.size - a.size).slice(0, Number(topArg) || 40)) console.log(String(r.size).padStart(8), r.section.padEnd(46), r.object.slice(-60));
const by = new Map();
for (const r of rows) { const k = (r.object.match(/([^\\/]+?)(?:\.o|\.obj|\.a\([^)]*\))?\s*$/) || [0, r.object || '(none)'])[1]; by.set(k, (by.get(k) || 0) + r.size); }
console.log('\nthe biggest objects (the application is compiled with LTO, so it may be one):');
for (const [k, v] of [...by].sort((a, b) => b[1] - a[1]).slice(0, 25)) console.log(String(v).padStart(8), k);
const live = rows.filter(r => /mp3|minimp3|sound_cache|L3_|mp3d_|bs_|hdr_|g_scf|g_pow|g_win|g_mod|g_expfrac|g_sec|g_pan|g_aa|pk_|packed_|fm1b_cache/.test(r.section + ' ' + r.object));
console.log(`\nsections that look like the live cache's (names with mp3, L3_, mp3d_, sound_cache, pk_, packed_): ${live.reduce((a, r) => a + r.size, 0)} bytes in ${live.length}`);
for (const r of live.sort((a, b) => b.size - a.size).slice(0, 25)) console.log(String(r.size).padStart(8), r.section);
