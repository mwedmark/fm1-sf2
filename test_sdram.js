// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
// The SDRAM test of the info page (fm1_open/app/sdram_test.c) against a model of the memory with faults: a good memory of 8, 4, 2 and
// 1 MB, a data line stuck or shorted, nothing there (zeros, or a bus that holds what was last written), an address line stuck low or
// high or shorted to another, a cell with a stuck bit, and a memory whose refresh does not work. The real C code runs on WebAssembly,
// built freestanding (no libc), so any clang that targets wasm32 will do: FM1_CLANG, else the WASI SDK the other tests use, else clang
// on the PATH.
//   node test_sdram.js
const fs = require('fs'), path = require('path'), { execFileSync } = require('child_process');
const root = __dirname;
const wasiClang = path.join(root, 'fm1_open/build/tools/wasi-sdk-27.0-x86_64-windows/bin/clang.exe');
const clang = process.env.FM1_CLANG || (fs.existsSync(wasiClang) ? wasiClang : 'clang');
const sources = [path.join(root, 'test_sdram/harness.c'), path.join(root, 'fm1_open/app/sdram_test.c'), path.join(root, 'fm1_open/app/sdram_test.h')];
const wasmPath = path.join(root, 'test_sdram/harness.wasm');
if (!fs.existsSync(wasmPath) || sources.some(s => fs.statSync(s).mtimeMs > fs.statSync(wasmPath).mtimeMs))
  execFileSync(clang, ['--target=wasm32', '-nostdlib', '-O1', '-Wall', '-Wextra', '-I' + path.join(root, 'fm1_open/app'), '-Wl,--no-entry', '-Wl,-z,stack-size=65536',
    sources[0], sources[1], '-o', wasmPath], { stdio: 'inherit' });
const w = new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(wasmPath)), {}).exports;
let failed = 0, checks = 0;
function check(ok, what) { ++checks; if (!ok) { ++failed; console.log('FAIL', what); } else console.log('ok  ', what); }

const KIND = { GOOD: 0, DATA_STUCK: 1, DATA_SHORT: 2, NO_MEMORY: 3, FLOATING_BUS: 4, ADDRESS_STUCK_LOW: 5, ADDRESS_STUCK_HIGH: 6, ADDRESS_SHORT: 7, CELL_STUCK_LOW: 8, NO_REFRESH: 9 };
const V = { RUNNING: 0, PASS: 1, FAIL_DATA_BUS: 2, FAIL_ADDRESS_BUS: 3, FAIL_CELLS: 4, FAIL_RETENTION: 5, TOO_SMALL: 6 };
const WORDS_8MB = 21, WORDS_4MB = 20, WORDS_2MB = 19, WORDS_1MB = 18;   // log2 of 32-bit words
// run(kind, memory size, a, b, window, required, budget, hold ms)
function go(kind, size, a = 0, b = 0, o = {}) {
  const verdict = w.run(kind, size, a, b, o.window || WORDS_8MB, o.required || WORDS_2MB, o.budget || 4096, o.hold === undefined ? 200 : o.hold);
  return { verdict, size: w.size_words(), errors: w.errors(), late: w.late_errors(), first: w.first_error(), bits: w.bit_mask() >>> 0, lines: w.address_mask() >>> 0,
           slices: w.slices(), firstSlice: w.first_slice(), maxSlice: w.max_slice(), monotonic: w.percent_monotonic() };
}

// good memories: the size is found by the address lines, whatever the window
for (const [log2, label] of [[WORDS_8MB, '8 MB'], [WORDS_4MB, '4 MB'], [WORDS_2MB, '2 MB']]) {
  const r = go(KIND.GOOD, log2);
  check(r.verdict === V.PASS && r.size === 2 ** log2 && r.errors === 0, `good ${label}: PASS, size found ${r.size * 4 / 1048576} MB, no errors`);
  if (log2 === WORDS_8MB) {
    check(r.slices > 100 && r.maxSlice <= 4096 && r.firstSlice <= 4096 + 1000, `the work comes in slices: ${r.slices}, the largest of ${r.maxSlice} accesses against a budget of 4096 (the first has the bus steps, ${r.firstSlice})`);
    check(r.monotonic === 1, 'the progress never goes back, and ends at 100');
  }
}
const small = go(KIND.GOOD, WORDS_1MB);
check(small.verdict === V.TOO_SMALL && small.size === 2 ** WORDS_1MB && small.errors === 0, 'a good 1 MB against 2 MB required: TOO SMALL, and the size it found is 1 MB');
check(go(KIND.GOOD, WORDS_1MB, 0, 0, { required: WORDS_1MB }).verdict === V.PASS, 'the same 1 MB with 1 MB required: PASS (the requirement is a parameter)');

// the data bus
let r = go(KIND.DATA_STUCK, WORDS_8MB, 5, 0);
check(r.verdict === V.FAIL_DATA_BUS && (r.bits & (1 << 5)) !== 0, `data line 5 stuck low: FAIL DATA BUS, bits ${r.bits.toString(16)}`);
r = go(KIND.DATA_STUCK, WORDS_8MB, 12, 1);
check(r.verdict === V.FAIL_DATA_BUS && (r.bits & (1 << 12)) !== 0, `data line 12 stuck high: FAIL DATA BUS, bits ${r.bits.toString(16)}`);
r = go(KIND.DATA_SHORT, WORDS_8MB, 3, 4);
check(r.verdict === V.FAIL_DATA_BUS && (r.bits & 0x18) !== 0, `data lines 3 and 4 shorted: FAIL DATA BUS, bits ${r.bits.toString(16)}`);
check(go(KIND.NO_MEMORY, WORDS_8MB).verdict === V.FAIL_DATA_BUS, 'nothing there (reads give 0): FAIL DATA BUS');
check(go(KIND.FLOATING_BUS, WORDS_8MB).verdict === V.FAIL_DATA_BUS, 'nothing there but a bus that holds the last value written: FAIL DATA BUS (the opposite value one word away finds it)');

// the address bus
r = go(KIND.ADDRESS_STUCK_LOW, WORDS_8MB, 7);
check(r.verdict === V.FAIL_ADDRESS_BUS && r.lines === (1 << 7), `address line 7 stuck low: FAIL ADDRESS BUS, line mask ${r.lines.toString(16)}`);
r = go(KIND.ADDRESS_STUCK_HIGH, WORDS_8MB, 10);
check(r.verdict === V.FAIL_ADDRESS_BUS && r.lines === (1 << 10), `address line 10 stuck high: FAIL ADDRESS BUS, line mask ${r.lines.toString(16)}`);
r = go(KIND.ADDRESS_SHORT, WORDS_8MB, 4, 9);
check(r.verdict === V.FAIL_ADDRESS_BUS && r.lines === ((1 << 4) | (1 << 9)), `address lines 4 and 9 shorted: FAIL ADDRESS BUS, line mask ${r.lines.toString(16)}`);
r = go(KIND.ADDRESS_STUCK_LOW, WORDS_8MB, WORDS_8MB - 1);
check(r.verdict === V.PASS && r.size === 2 ** WORDS_4MB, 'the top address line stuck low looks like a memory of half the size (4 MB): found as 4 MB, which still passes 2 MB. It cannot be told from a smaller memory');

// the cells
r = go(KIND.CELL_STUCK_LOW, WORDS_8MB, 12345, 9);
check(r.verdict === V.FAIL_CELLS && r.first === 12345 && (r.bits & (1 << 9)) !== 0, `word 12345 bit 9 stuck low: FAIL CELLS, first error at ${r.first}, bits ${r.bits.toString(16)} (found in the value or in its inverse, whichever has a 1 there)`);

// a bit stuck low where the cell's own value has a 0: the first check passes, only the inverse shows it, and it is still a cell fault (not a
// refresh fault, which is what it would be called if the inverse were only read after the wait)
const valueOf = word => (Math.imul(word, 0x9e3779b1) ^ ((word << 27) | (word >>> 5)) ^ 0x5a5ac3c3) >>> 0;   // sdram_test.c's
let zeroBit = 0;
while (valueOf(777) & (1 << zeroBit)) ++zeroBit;
r = go(KIND.CELL_STUCK_LOW, WORDS_8MB, 777, zeroBit);
check(r.verdict === V.FAIL_CELLS && r.first === 777 && r.errors >= 1, `word 777 bit ${zeroBit} stuck low where its value has a 0: FAIL CELLS (found by the inverse checked at once), not RETENTION`);

// refresh
r = go(KIND.NO_REFRESH, WORDS_8MB, 0, 0, { hold: 200 });
check(r.verdict === V.FAIL_RETENTION && r.errors > 0 && r.errors === r.late, `no refresh, 200 ms wait: FAIL RETENTION, ${r.errors} errors all after the wait`);
r = go(KIND.NO_REFRESH, WORDS_8MB, 0, 0, { hold: 20 });
check(r.verdict === V.PASS, 'no refresh but a wait of only 20 ms (the cells hold for 64 ms): PASS. The wait has to be longer than the refresh period; the device uses 3000');

// the budget: the smallest still finishes, and a window of any size works
r = go(KIND.GOOD, 12, 0, 0, { window: 12, required: 10, budget: 1, hold: 5 });
check(r.verdict === V.PASS && r.size === 4096 && r.maxSlice <= 2, `budget 1 is taken as 2 and the test still finishes (a 16 KB memory, ${r.slices} slices of at most ${r.maxSlice} accesses)`);

console.log(failed ? `${failed} of ${checks} checks FAILED` : `all ${checks} checks passed`);
process.exit(failed ? 1 : 0);
