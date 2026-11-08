#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
"""What a rate of its own for each sound would save (FORMAT.md, "Calculation: a sample cache in the FM-1's SRAM"). The bank stores every sound at one
rate (32 kHz). For each sound this finds the frequency below which 99.9 % (and 99.99 %) of its energy lies, takes the lowest of 8, 11.025, 16, 22.05,
24 and 32 kHz whose Nyquist frequency is 5 % above it, and counts the BFP8 bytes at that rate against 32 kHz. The engine has no rate per zone:
this is only what it would be worth. python3 and numpy.
    python3 measure_sample_rates.py font.sf2 [font.sf2 ...]"""
import importlib.util, os, sys
import numpy as np
spec = importlib.util.spec_from_file_location('mff', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'measure_font_features.py'))
mff = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mff)
RATES = [8000, 11025, 16000, 22050, 24000, 32000]
bfp = lambda frames: -(-int(frames) // 32) * 33


def run(path):
    f = mff.font_features(path)
    d = open(path, 'rb').read()
    base = f['chunks'][b'smpl'][0]
    used = sorted(mff.used_samples(f))
    at32, adaptive, by_rate = 0, {0.999: 0, 0.9999: 0}, {0.999: {}, 0.9999: {}}
    for i in used:
        h = f['shdr'][i]
        x = np.frombuffer(d[base + 2 * h[1]:base + 2 * h[2]], dtype=np.int16).astype(float)
        if len(x) < 64:
            continue
        sr = h[5]
        at32 += bfp(len(x) * 32000 / sr)
        n = 1 << int(np.ceil(np.log2(len(x))))
        power = np.abs(np.fft.rfft(x * np.hanning(len(x)), n)) ** 2
        cumulative = np.cumsum(power) / max(power.sum(), 1e-9)
        freqs = np.arange(len(power)) * sr / n
        for q in adaptive:
            cutoff = freqs[np.searchsorted(cumulative, q)]
            rate = next((r for r in RATES if r / 2 >= cutoff * 1.05), 32000)
            adaptive[q] += bfp(len(x) * rate / sr)
            by_rate[q][rate] = by_rate[q].get(rate, 0) + 1
    print(f'{os.path.basename(path)}: {len(used)} sounds, BFP8 at 32 kHz for all {at32 / 1024:.0f} KB')
    for q, v in adaptive.items():
        print(f'   rate from {q * 100:.2f} % of the energy: {v / 1024:.0f} KB ({(1 - v / at32) * 100:.0f} % less); sounds by rate: ' + ', '.join(f'{r}: {c}' for r, c in sorted(by_rate[q].items())))


for p in sys.argv[1:] or [os.path.join(os.path.dirname(os.path.abspath(__file__)), '1mgm.sf2')]:
    run(p)
