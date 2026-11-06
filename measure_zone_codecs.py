#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
"""Measures what packing the sample data of a SoundFont bank would save, sample by sample (see FORMAT.md, "Proposal: packed
sample data"). Needs python3, numpy and ffmpeg (with libmp3lame) on the PATH.

    python3 measure_zone_codecs.py [1mgm.sf2] [--rate 32000] [--bank dist/gm_32k_bfp8.fm1b] [--tables bytes] [--csv out.csv]

For every sample of the SoundFont (resampled to the bank's rate) it works out the bytes of
  BFP8      what the bank stores today (groups of 32: 1 shift byte + 32 int8), and the same through xz,
  MP3       at 128 and 64 kbit/s mono, each sample as its own stream, and the waveform SNR of the decoded sample against the source.
Then it prints the totals by sample length and what a per-sample choice would come to: the smallest of BFP8 + xz, MP3 128 and
MP3 64 among those whose SNR is at least a threshold. The SNR of MP3 is not what the ear hears (the codec hides its noise
under the signal), it is the measure that BFP8 (about 42-44 dB) can be compared with. The converter's loop repeats are not in the
sample figures (a packed bank would make them when it unpacks). The zone tables are added to the totals xz packed, as they are
in the bank given with --bank (made by make_bank.js bfp8); --tables gives their size by hand instead."""
import argparse, csv, lzma, struct, subprocess, sys
import numpy as np

SNR_THRESHOLDS = (20, 25, 30)
BUCKETS = ((0, 1152, 'under 1152 (one MP3 frame)'), (1152, 4096, '1152..4095'), (4096, 16384, '4096..16383'), (16384, 1 << 30, '16384 and up'))


def ffmpeg(args, data):
    return subprocess.run(['ffmpeg', '-v', 'error'] + args, input=data, capture_output=True, check=True).stdout


def sf2_samples(path):
    d = open(path, 'rb').read()
    found = {}

    def walk(o, e):
        while o < e:
            tag, size = d[o:o + 4], struct.unpack('<I', d[o + 4:o + 8])[0]
            if tag == b'LIST':
                walk(o + 12, o + 8 + size)
            elif tag in (b'smpl', b'shdr'):
                found[tag] = (o + 8, size)
            o += 8 + size + (size & 1)

    walk(12, len(d))
    base, _ = found[b'smpl']
    start, size = found[b'shdr']
    for k in range(size // 46 - 1):                      # the last header is the terminal record
        r = d[start + k * 46:start + k * 46 + 46]
        name = r[:20].split(b'\0')[0].decode('latin1')
        st, en, ls, le, sr = struct.unpack('<5I', r[20:40])
        if en - st >= 64:
            yield name, np.frombuffer(d[base + 2 * st:base + 2 * en], dtype=np.int16), sr


def resample(x, rate_in, rate_out):
    if rate_in == rate_out:
        return x
    return np.frombuffer(ffmpeg(['-f', 's16le', '-ar', str(rate_in), '-ac', '1', '-i', '-', '-ar', str(rate_out), '-f', 's16le', '-'], x.tobytes()), dtype=np.int16)


def bfp8(p):
    """The bank's BFP8 (FORMAT.md): the bytes, and the samples as the engine plays them."""
    n = (len(p) + 31) // 32 * 32
    g = np.pad(p.astype(np.int32), (0, n - len(p))).reshape(-1, 32)
    shifts = np.zeros(len(g), dtype=np.int32)
    for s in range(9):                                   # the smallest shift whose rounded mantissas fit -128..127
        fits = np.abs(np.round(g / 2.0 ** s)).max(axis=1) <= 127
        shifts = np.where((shifts == s) & ~fits, s + 1, shifts)
    shifts = np.minimum(shifts, 8)
    m = np.clip(np.round(g / 2.0 ** shifts[:, None]), -128, 127).astype(np.int32)
    raw = np.concatenate([shifts[:, None].astype(np.int8), m.astype(np.int8)], axis=1).tobytes()
    return raw, (m * 2 ** shifts[:, None]).reshape(-1)[:len(p)]


def xz(b):
    return len(lzma.compress(b, format=lzma.FORMAT_RAW, filters=[{'id': lzma.FILTER_LZMA2, 'preset': 9 | lzma.PRESET_EXTREME}]))


def mp3(y, rate, kbit):
    """Size of the mono stream (no Xing frame) and the decoded samples lined up with y."""
    enc = ffmpeg(['-f', 's16le', '-ar', str(rate), '-ac', '1', '-i', '-', '-c:a', 'libmp3lame', '-b:a', f'{kbit}k', '-write_xing', '0', '-f', 'mp3', '-'], y.tobytes())
    dec = np.frombuffer(ffmpeg(['-f', 'mp3', '-i', '-', '-ar', str(rate), '-f', 's16le', '-'], enc), dtype=np.int16).astype(np.float64)
    ref = y[:8192].astype(np.float64)
    corr = np.correlate(dec[:len(ref) + 2400], ref, 'valid') if len(dec) > len(ref) else np.zeros(1)
    lag = int(np.argmax(corr))                           # the decoder's delay, found rather than assumed
    out = np.pad(dec[lag:lag + len(y)], (0, max(0, len(y) - len(dec[lag:lag + len(y)]))))
    return len(enc), out


def snr(x, y):
    err = np.sum((x.astype(np.float64) - y) ** 2)
    return 99.0 if err == 0 else 10 * np.log10(max(np.sum(x.astype(np.float64) ** 2), 1) / err)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('sf2', nargs='?', default='1mgm.sf2')
    ap.add_argument('--rate', type=int, default=32000)
    ap.add_argument('--bank', default='dist/gm_32k_bfp8.fm1b')
    ap.add_argument('--tables', type=int)
    ap.add_argument('--csv')
    a = ap.parse_args()
    tables = a.tables
    if tables is None:
        try:
            b = open(a.bank, 'rb').read()
            tables = xz(b[:struct.unpack('<I', b[36:40])[0]])          # everything before the sample data, packed
        except OSError:
            tables = 0
            print(f'no bank at {a.bank} (node make_bank.js bfp8): the zone tables are left out of the totals', file=sys.stderr)
    rows = []
    for name, x, sr in sf2_samples(a.sf2):
        y = resample(x, sr, a.rate)
        raw, dec8 = bfp8(y)
        row = dict(name=name, n=len(y), bfp8=len(raw), bfp8_xz=xz(raw), snr_bfp8=snr(y, dec8), raw=raw)
        for k in (128, 64):
            size, dec = mp3(y, a.rate, k)
            row[f'mp3_{k}'], row[f'snr_{k}'] = size, snr(y, dec)
        rows.append(row)
    total = lambda key: sum(r[key] for r in rows)
    n_all = total('n')
    whole_xz = xz(b''.join(r['raw'] for r in rows))
    print(f'{len(rows)} samples, {n_all} sample frames at {a.rate} Hz ({n_all / a.rate:.1f} s), PCM16 {n_all * 2} bytes')
    print(f'BFP8 {total("bfp8")} bytes; xz per sample {total("bfp8_xz")}; xz of all BFP8 in one stream {whole_xz}')
    print(f'MP3 128k {total("mp3_128")}, MP3 64k {total("mp3_64")} (one stream per sample, SNR of the whole set: see the table)\n')
    print(f'{"samples by length":30s}{"n":>5s}{"frames":>9s}{"BFP8":>9s}{"BFP8 xz":>9s}{"MP3 128":>9s}{"SNR":>6s}{"MP3 64":>9s}{"SNR":>6s}')
    for lo, hi, label in BUCKETS:
        sel = [r for r in rows if lo <= r['n'] < hi]
        if not sel:
            continue
        s = lambda key: sum(r[key] for r in sel)
        # the mean SNR is weighted by length, so long samples count for what they are
        w = lambda key: sum(r[key] * r['n'] for r in sel) / s('n')
        print(f'{label:30s}{len(sel):5d}{s("n"):9d}{s("bfp8"):9d}{s("bfp8_xz"):9d}{s("mp3_128"):9d}{w("snr_128"):6.1f}{s("mp3_64"):9d}{w("snr_64"):6.1f}')
    print(f'\nBFP8 itself: SNR {sum(r["snr_bfp8"] * r["n"] for r in rows) / n_all:.1f} dB (length weighted)\n')
    print(f'Per-sample choice, tables {tables} bytes added (BFP8 samples left over are xz packed in one stream):')
    print(f'{"smallest with SNR at least":28s}{"MP3 samples":>12s}{"MP3 bytes":>11s}{"BFP8 xz":>9s}{"bank":>9s}{"vs BFP8 xz":>12s}')
    base = tables + whole_xz
    for thr in SNR_THRESHOLDS:
        use_mp3, mp3_bytes, rest = 0, 0, []
        for r in rows:
            best = min((r[f'mp3_{k}'], k) for k in (128, 64) if r[f'snr_{k}'] >= thr) if any(r[f'snr_{k}'] >= thr for k in (128, 64)) else None
            if best and best[0] < r['bfp8_xz']:
                use_mp3, mp3_bytes = use_mp3 + 1, mp3_bytes + best[0]
            else:
                rest.append(r['raw'])
        rest_xz = xz(b''.join(rest)) if rest else 0
        bank = tables + mp3_bytes + rest_xz
        print(f'{thr:>25d} dB{use_mp3:12d}{mp3_bytes:11d}{rest_xz:9d}{bank:9d}{(bank / base - 1) * 100:11.1f}%')
    print(f'{"BFP8 + xz, nothing lossy":28s}{0:12d}{0:11d}{whole_xz:9d}{base:9d}{0.0:11.1f}%')
    if a.csv:
        with open(a.csv, 'w', newline='') as f:
            wr = csv.writer(f)
            keys = ['name', 'n', 'bfp8', 'bfp8_xz', 'snr_bfp8', 'mp3_128', 'snr_128', 'mp3_64', 'snr_64']
            wr.writerow(keys)
            for r in rows:
                wr.writerow([f'{r[k]:.1f}' if isinstance(r[k], float) else r[k] for k in keys])


if __name__ == '__main__':
    sys.exit(main())
