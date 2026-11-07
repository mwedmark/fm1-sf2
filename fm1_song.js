// The MIDI file player's song format (fm1_open/app/song.c) and its upload frames. Used by song.html (a <script> tag: window.Fm1Song)
// and by node (require('./fm1_song.js')): node fm1_song.js <in.mid> [out.fm1s] writes a song file and prints what it holds.
//
// A song ("FM1S", little endian): 'FM1S', u32 events, u32 length in ms, char name[32]; then each event: the ms since the previous
// one (7 bits a byte, most significant first, the high bit set on all but the last byte) and a channel message with its status
// byte (2 bytes for C0..DF, 3 for the others). Tempo changes are worked out here; meta events, SysEx and aftertouch are left out.
(function (root) {
  'use strict';
  const HEADER = 44;

  // All tracks of a standard MIDI file merged in time order: [{ ms, bytes }].
  function readMidi(b) {
    const str = (at, n) => String.fromCharCode(...b.subarray(at, at + n));
    const u16 = (at) => b[at] << 8 | b[at + 1], u32 = (at) => (b[at] << 24 | b[at + 1] << 16 | b[at + 2] << 8 | b[at + 3]) >>> 0;
    if (str(0, 4) !== 'MThd') throw new Error('Not a MIDI file (no MThd).');
    const tracks = u16(10), division = u16(12);
    if (division & 0x8000) throw new Error('MIDI files with SMPTE time are not supported.');
    const events = [];
    let at = 8 + u32(4), order = 0;
    for (let t = 0; t < tracks && at + 8 <= b.length; t++) {
      while (at + 8 <= b.length && str(at, 4) !== 'MTrk') at += 8 + u32(at + 4);
      if (at + 8 > b.length) break;
      const end = Math.min(b.length, at + 8 + u32(at + 4));
      let p = at + 8, tick = 0, status = 0;
      const vlq = () => { let v = 0, c; do { c = b[p++]; v = v * 128 + (c & 0x7f); } while (c & 0x80 && p < end); return v; };
      while (p < end) {
        tick += vlq();
        let s = b[p];
        if (s & 0x80) p++; else s = status;
        if (s === 0xff) {
          const type = b[p++], len = vlq();
          if (type === 0x51 && len === 3) events.push({ tick, order: order++, tempo: b[p] << 16 | b[p + 1] << 8 | b[p + 2] });
          p += len;
        } else if (s === 0xf0 || s === 0xf7) {
          const len = vlq();
          p += len;
        } else if (s >= 0x80) {
          status = s;
          const n = (s & 0xf0) === 0xc0 || (s & 0xf0) === 0xd0 ? 1 : 2;
          events.push({ tick, order: order++, bytes: [s, ...b.subarray(p, p + n)] });
          p += n;
        } else break;                                       // data with no status: the track is broken
      }
      at = end;
    }
    events.sort((x, y) => x.tick - y.tick || x.order - y.order);
    let tempo = 500000, lastTick = 0, ms = 0;
    const out = [];
    for (const e of events) {
      ms += (e.tick - lastTick) * tempo / division / 1000;
      lastTick = e.tick;
      if (e.tempo) { tempo = e.tempo; continue; }
      const type = e.bytes[0] & 0xf0;
      if (type === 0xa0 || type === 0xd0) continue;       // aftertouch: the player ignores it
      if (e.bytes.slice(1).some((x) => x === undefined || x > 127)) continue;
      out.push({ ms, bytes: e.bytes });
    }
    return out;
  }

  // The panel's font has A-Z, 0-9 and - / + % . : # ( ) _
  function panelName(name) {
    return name.replace(/\.midi?$/i, '').toUpperCase().replace(/[^A-Z0-9\-/+%.:#()_ ]+/g, ' ').replace(/\s+/g, ' ').trim().slice(0, 32);
  }

  function makeSong(midiBytes, name) {
    const events = readMidi(midiBytes);
    const body = [];
    let last = 0;
    for (const e of events) {
      const t = Math.round(e.ms), d = t - last;
      last = t;
      const groups = [d & 127];
      for (let v = Math.floor(d / 128); v; v = Math.floor(v / 128)) groups.unshift(v & 127 | 128);
      body.push(...groups, ...e.bytes);
    }
    const song = new Uint8Array(HEADER + body.length);
    song.set([0x46, 0x4d, 0x31, 0x53]);
    const view = new DataView(song.buffer);
    view.setUint32(4, events.length, true);
    view.setUint32(8, last, true);
    const label = panelName(name || '');
    for (let i = 0; i < label.length; i++) song[12 + i] = label.charCodeAt(i);
    song.set(body, HEADER);
    return { song, events: events.length, ms: last, name: label };
  }

  // ---- SysEx frames (sf2_upload.c): F0 7D 'S' 'F' 02 cmd ... F7; every number 5 x 7 bits, least significant first ----
  const HEAD = [0xf0, 0x7d, 0x53, 0x46, 2], CHUNK = 448;
  function crc32(bytes) {
    let c = 0xffffffff;
    for (let i = 0; i < bytes.length; i++) { c ^= bytes[i]; for (let k = 0; k < 8; k++) c = (c >>> 1) ^ (0xedb88320 & -(c & 1)); }
    return (~c) >>> 0;
  }
  const num35 = (v) => [v & 127, (v >>> 7) & 127, (v >>> 14) & 127, (v >>> 21) & 127, (v >>> 28) & 127];
  function pack(bytes) {
    const out = [];
    for (let i = 0; i < bytes.length; i += 7) {
      const n = Math.min(7, bytes.length - i);
      let high = 0;
      for (let k = 0; k < n; k++) high |= (bytes[i + k] >> 7) << k;
      out.push(high);
      for (let k = 0; k < n; k++) out.push(bytes[i + k] & 127);
    }
    return out;
  }
  // begin (4), data (5) and end (6): the song into the FM-1's RAM
  function songFrames(song) {
    const frames = [Uint8Array.from([...HEAD, 4, ...num35(song.length), 0xf7])];
    for (let at = 0; at < song.length; at += CHUNK)
      frames.push(Uint8Array.from([...HEAD, 5, ...num35(at), ...pack(song.subarray(at, at + CHUNK)), 0xf7]));
    frames.push(Uint8Array.from([...HEAD, 6, ...num35(crc32(song)), 0xf7]));
    return frames;
  }
  const playFrame = () => Uint8Array.from([...HEAD, 7, 0, 0xf7]);
  const stopFrame = () => Uint8Array.from([...HEAD, 8, 0, 0xf7]);
  const ERRORS = { 1: 'malformed frame or song', 2: 'the song is too big', 4: 'checksum mismatch, or not a song', 5: 'no song (upload it first)', 6: 'not enough free RAM on the FM-1' };

  const api = { readMidi, makeSong, songFrames, playFrame, stopFrame, crc32, ERRORS, HEADER };
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.Fm1Song = api;

  if (typeof require === 'function' && typeof module === 'object' && require.main === module) {
    const fs = require('fs'), path = require('path');
    const [inPath, outPath] = process.argv.slice(2);
    if (!inPath) { console.error('node fm1_song.js <in.mid> [out.fm1s]'); process.exit(1); }
    const r = makeSong(new Uint8Array(fs.readFileSync(inPath)), path.basename(inPath));
    if (outPath) fs.writeFileSync(outPath, r.song);
    console.log(`${r.name}: ${r.events} events, ${(r.ms / 1000).toFixed(1)} s, ${r.song.length} bytes (MIDI file ${fs.statSync(inPath).size}), ${songFrames(r.song).length} frames`);
  }
})(typeof window !== 'undefined' ? window : globalThis);
