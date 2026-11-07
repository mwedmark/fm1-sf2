// Writes the 32 kHz -> 44.1 kHz resampling filter the firmware uses (fm1_open/app/resample_table.inc): a 12-tap
// Kaiser-windowed sinc (cutoff 0.45 of the input rate, beta 6) at 256 phases, Q14, each phase summing to exactly 16384.
// Response: flat to 10 kHz (-0.1 dB), -1.1 dB at 12 kHz, -4.8 dB at 14 kHz; images above 16 kHz at -20 dB or lower.
//   node make_resampler.js
const fs = require('fs'), path = require('path');
const TAPS = 12, PHASES = 256, CUT = 0.45, BETA = 6;
function bessel0(x) { let s = 1, t = 1; for (let k = 1; k < 30; k++) { t *= (x / 2 / k) ** 2; s += t; } return s; }
const rows = [];
for (let p = 0; p < PHASES; p++) {
  const f = p / PHASES, row = [];
  for (let k = 0; k < TAPS; k++) {
    const x = k - (TAPS / 2 - 1) - f;                    // tap k sits at input sample floor(position) - 5 + k
    const w = Math.abs(x) < TAPS / 2 ? bessel0(BETA * Math.sqrt(1 - (x / (TAPS / 2)) ** 2)) / bessel0(BETA) : 0;
    row.push(2 * CUT * (x === 0 ? 1 : Math.sin(2 * Math.PI * CUT * x) / (2 * Math.PI * CUT * x)) * w);
  }
  const sum = row.reduce((a, v) => a + v, 0), q = row.map(v => Math.round(v / sum * 16384));
  q[TAPS / 2 - 1 + (f >= 0.5 ? 1 : 0)] += 16384 - q.reduce((a, v) => a + v, 0);   // the largest tap takes the rounding
  rows.push('{' + q.join(',') + '},');
}
const out = path.join(__dirname, 'fm1_open/app/resample_table.inc');
fs.writeFileSync(out, `/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Written by make_resampler.js (the fm1-sf2 repository): ${TAPS}-tap Kaiser-windowed sinc (cutoff ${CUT} of the input rate,
 * beta ${BETA}) at ${PHASES} phases, Q14; each row sums to 16384. Row p is for a position p/${PHASES} past input sample n, tap k
 * multiplies sample n - ${TAPS / 2 - 1} + k. */
#define RESAMPLE_TAPS ${TAPS}
#define RESAMPLE_PHASES ${PHASES}
static FM1_AUDIO_DATA int16_t resample_table[RESAMPLE_PHASES][RESAMPLE_TAPS] = {   /* in RAM: the resampler reads it 3 072 times a block, and from the flash it cost 4 times the time */
${rows.join('\n')}
};
`);
console.log('wrote', out);
