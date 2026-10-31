// Wraps the FM-1 simulator (built from fm1_open) as simulator_wasm.js so index.html can use it from file://
//   node make_wasm_js.js [path\to\simulator.wasm]
const fs = require('fs');
const src = process.argv[2] || __dirname + '/fm1_open/build/web/simulator.wasm';
const b64 = fs.readFileSync(src).toString('base64');
fs.writeFileSync(__dirname + '/simulator_wasm.js',
  '// FM-1 simulator (fm1_open, GPLv3 with the FM1 SDK linking exception) with the FM1B melodic sampler.\n' +
  'const FM1_WASM_B64 = "' + b64 + '";\n');
console.log('wrote simulator_wasm.js,', Math.round(b64.length / 1024), 'KB');
