// A standard MIDI file to timed messages [[ms, [bytes]], ...] (notes, controllers, program, bend; no meta events or SysEx): the same as dualcore.html's
// parseMidi, for the tools that play or render the same song as the FM-1 test did. CC 102 on channel 16 is the dual-core test's own and is left out.
function parseMidi(buf) {
  const u = new Uint8Array(buf.buffer ? buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength) : buf), v = new DataView(u.buffer);
  if (String.fromCharCode(...u.subarray(0, 4)) !== 'MThd') throw new Error('not a MIDI file');
  const ntracks = v.getUint16(10), division = v.getUint16(12);
  if (division & 0x8000) throw new Error('SMPTE time is not supported');
  let o = 8 + v.getUint32(4); const raw = [];
  for (let tr = 0; tr < ntracks && o < u.length; ++tr) {
    const end = o + 8 + v.getUint32(o + 4);
    let p = o + 8, tick = 0, status = 0;
    const vlq = () => { let x = 0, c; do { c = u[p++]; x = x << 7 | c & 127; } while (c & 128); return x; };
    while (p < end) {
      tick += vlq();
      let s = u[p];
      if (s & 128) ++p; else s = status;
      if (s === 0xff) { const type = u[p++], len = vlq(); if (type === 0x51) raw.push({ tick, tempo: u[p] << 16 | u[p + 1] << 8 | u[p + 2], o: raw.length }); p += len; }
      else if (s === 0xf0 || s === 0xf7) { const len = vlq(); p += len; }
      else { status = s; const n = (s & 0xf0) === 0xc0 || (s & 0xf0) === 0xd0 ? 1 : 2; raw.push({ tick, d: [s, ...u.subarray(p, p + n)], o: raw.length }); p += n; }
    }
    o = end;
  }
  raw.sort((a, b) => a.tick - b.tick || a.o - b.o);
  let tempo = 500000, last = 0, ms = 0; const ev = [];
  for (const r of raw) {
    ms += (r.tick - last) * tempo / division / 1000; last = r.tick;
    if (r.tempo) tempo = r.tempo;
    else if (!(r.d[0] === 0xbf && r.d[1] === 102)) ev.push([ms, r.d]);
  }
  return ev;
}
module.exports = { parseMidi };
