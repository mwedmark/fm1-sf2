// Writes the engine's SEA quantizer tables for code sizes other than 5 (fm1_open/app/fm1b_dqt<bits>.inc) from seaDqt() in
// index.html, and checks that seaDqt(5) is the fixed SEA5 table bit for bit.
//   node make_dqt.js [bits ...]          (default: 6)
const fs = require('fs'), path = require('path');
const html = fs.readFileSync(path.join(__dirname, 'index.html'), 'utf8');
eval(html.match(/<script id="core">([\s\S]*?)<\/script>/)[1] + '\nglobalThis.api = { seaDqt, SEA_DQT };');
const A = globalThis.api;
if (!A.seaDqt(5).every((v, i) => v === A.SEA_DQT[i])) throw new Error('seaDqt(5) is not the SEA5 table');
for (const bits of (process.argv.length > 2 ? process.argv.slice(2) : ['6']).map(Number)) {
  const t = A.seaDqt(bits), rows = [];
  for (let i = 0; i < t.length; i += 16) rows.push(Array.from(t.subarray(i, i + 16)).join(',') + ',');
  const out = path.join(__dirname, `fm1_open/app/fm1b_dqt${bits}.inc`);
  fs.writeFileSync(out, `/* SPDX-License-Identifier: MIT
 * SEA${bits} quantizer table, written by make_dqt.js (the fm1-sf2 repository) from seaDqt() in its index.html: the 16 scales of
 * the fixed SEA5 table (fm1b_dqt.inc, Copyright (c) 2025 Dani Biro, see THIRD_PARTY.md) at ${1 << (bits - 5)} times its resolution.
 * 16 scales x ${1 << bits} codes; code 2k is +m_k, 2k+1 is -m_k. */
static const int16_t fm1b_dqt${bits}[${t.length}] = {
${rows.join('\n')}
};
`);
  console.log('wrote', out, t.length, 'entries');
}
