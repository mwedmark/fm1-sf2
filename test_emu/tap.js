// Test tap: records level and spectral centroid of whatever the emulator's AudioWorkletNode outputs, into window.__cap.
// With &mute=1 in the page's address nothing reaches the speakers: the measuring goes on, silently.
window.__cap = [];
const orig = AudioNode.prototype.connect, mute = new URLSearchParams(location.search).has('mute');
AudioNode.prototype.connect = function (dest, ...a) {
  if (typeof AudioWorkletNode !== 'undefined' && this instanceof AudioWorkletNode && !this.__tapped) {
    this.__tapped = true;
    const an = this.context.createAnalyser(); an.fftSize = 2048; an.smoothingTimeConstant = 0;
    orig.call(this, an); window.__ctx = this.context;
    const buf = new Float32Array(2048), spec = new Float32Array(1024), bin = this.context.sampleRate / 2048;
    setInterval(() => {
      an.getFloatTimeDomainData(buf);
      let pk = 0, rms = 0; for (const v of buf) { const x = Math.abs(v); if (x > pk) pk = x; rms += v * v; }
      an.getFloatFrequencyData(spec);
      let num = 0, den = 0; for (let i = 1; i < 1024; i++) { const p = Math.pow(10, spec[i] / 10); num += p * i * bin; den += p; }
      // hz: the rising zero crossings' rate, the pitch of a plain tone such as test_emu/sine.script's
      let first = -1, last = -1, n = 0;
      for (let i = 1; i < 2048; i++) if (buf[i - 1] < 0 && buf[i] >= 0) { const at = i - 1 + buf[i - 1] / (buf[i - 1] - buf[i]); if (first < 0) first = at; last = at; n++; }
      const hz = n > 2 ? +((n - 1) / (last - first) * this.context.sampleRate).toFixed(2) : 0;
      window.__cap.push({ t: +this.context.currentTime.toFixed(2), pk: +pk.toFixed(4), rms: +Math.sqrt(rms / 2048).toFixed(4), cen: den > 0 ? Math.round(num / den) : 0, hz });
    }, 100);
  }
  if (mute && dest instanceof AudioDestinationNode) return dest;   // after the tap: the analyser still hears it
  return orig.call(this, dest, ...a);
};
