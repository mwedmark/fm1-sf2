#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
"""Counts what a SoundFont holds, in the converter's terms (FORMAT.md, "Proposal: more of the SoundFont"): which presets the bank map
takes, the seconds of audio the bank has today and what it would have with every velocity layer, stereo pairs, percussion kits and
the modulators. Pure python3, no packages; the file is only read.

    python3 -I measure_font_features.py font.sf2

The converter's rules, as index.html applies them: only presets of MIDI bank 0 and 128 (and at most 254 zones) are written; the right
channel of a stereo pair is skipped; of a preset's zones the ones whose velocity range holds 100 are kept (all of them if none does).
Sample frames are counted once however many zones use a sample. `font_features()` is what measure_zone_codecs.py --used calls."""
import struct, sys
from collections import Counter

GEN = {5: 'modLfo->pitch', 6: 'vibLfo->pitch', 7: 'modEnv->pitch', 8: 'filter cutoff', 9: 'filter Q', 10: 'modLfo->filter', 11: 'modEnv->filter',
       13: 'modLfo->volume', 15: 'chorus send', 16: 'reverb send', 17: 'pan', 48: 'attenuation', 51: 'coarse tune', 52: 'fine tune',
       58: 'overriding root key'}


def chunks(d, o, e, out):
    while o + 8 <= e:
        tag, size = d[o:o + 4], struct.unpack('<I', d[o + 4:o + 8])[0]
        if tag == b'LIST':
            chunks(d, o + 12, o + 8 + size, out)
        else:
            out[tag] = (o + 8, size)
        o += 8 + size + (size & 1)


def records(d, c, tag, size, fmt):
    if tag not in c:
        return []
    o, n = c[tag]
    return [struct.unpack(fmt, d[o + k * size:o + (k + 1) * size]) for k in range(n // size)]


def rng(amount):
    return amount & 255, amount >> 8


def font_features(path):
    d = open(path, 'rb').read()
    c = {}
    chunks(d, 12, len(d), c)
    phdr = records(d, c, b'phdr', 38, '<20sHHHIII')
    pbag = records(d, c, b'pbag', 4, '<HH')
    pmod = records(d, c, b'pmod', 10, '<HHhHH')
    pgen = records(d, c, b'pgen', 4, '<Hh')
    inst = records(d, c, b'inst', 22, '<20sH')
    ibag = records(d, c, b'ibag', 4, '<HH')
    imod = records(d, c, b'imod', 10, '<HHhHH')
    igen = records(d, c, b'igen', 4, '<Hh')
    shdr = records(d, c, b'shdr', 46, '<20sIIIIIBbHH')

    def zone_list(bags, gens, mods, first, nxt, last_oper):
        """Generator dicts of the zones from bag `first` up to `nxt`, global zone folded in; each with its modulator count."""
        out, glob = [], None
        for b in range(first, nxt):
            g = {}
            for oper, amount in gens[bags[b][0]:bags[b + 1][0]]:
                g[oper & 0xffff] = amount
            m = bags[b + 1][1] - bags[b][1]
            if last_oper not in g:
                if b == first:
                    glob = (g, m)
                continue
            if glob:
                g = {**glob[0], **g}
            out.append((g, m + (glob[1] if glob else 0)))
        return out

    # per instrument: its zones as (sample, key range, velocity range)
    inst_zones = []
    for i in range(len(inst) - 1):
        zs = []
        for g, m in zone_list(ibag, igen, imod, inst[i][1], inst[i + 1][1], 53):
            kr = rng(g[43] & 0xffff) if 43 in g else (0, 127)
            vr = rng(g[44] & 0xffff) if 44 in g else (0, 127)
            zs.append((g[53] & 0xffff, kr, vr))
        inst_zones.append(zs)
    presets = []
    for p in range(len(phdr) - 1):
        name, program, bank, bag = phdr[p][:4]
        zones = []
        for g, m in zone_list(pbag, pgen, pmod, bag, phdr[p + 1][3], 41):
            plo, phi = rng(g[43] & 0xffff) if 43 in g else (0, 127)
            pvl, pvh = rng(g[44] & 0xffff) if 44 in g else (0, 127)
            for sample, (klo, khi), (vlo, vhi) in inst_zones[g[41] & 0xffff] if (g[41] & 0xffff) < len(inst_zones) else []:
                lo, hi = max(klo, plo), min(khi, phi)
                if lo > hi or sample >= len(shdr) - 1:
                    continue
                zones.append(dict(sample=sample, lo=lo, hi=hi, vlo=max(vlo, pvl), vhi=min(vhi, pvh)))
        presets.append(dict(name=name.split(b'\0')[0].decode('latin1'), program=program, bank=bank, zones=zones))
    for pr in presets:                                       # the converter's velocity rule
        hit = [z for z in pr['zones'] if z['vlo'] <= 100 <= z['vhi']]
        pr['kept'] = hit or pr['zones']
        pr['written'] = pr['bank'] in (0, 128) and len(pr['zones']) <= 254
    return dict(file_size=len(d), has_sm24=b'sm24' in c, presets=presets, shdr=shdr, pmod=pmod[:-1], imod=imod[:-1], chunks=c)


def used_samples(f, with_layers=False):
    """Indexes of the samples the bank has: of the written presets' kept zones (today), or of all their zones (every velocity layer)."""
    out = set()
    for pr in f['presets']:
        if pr['written']:
            for z in (pr['zones'] if with_layers else pr['kept']):
                if f['shdr'][z['sample']][9] != 2:          # the right channel of a pair is not used
                    out.add(z['sample'])
    return out


def seconds(f, indexes, rate=None):
    return sum((f['shdr'][i][2] - f['shdr'][i][1]) / (rate or f['shdr'][i][5]) for i in indexes)


def frames(f, indexes, rate):
    return sum((f['shdr'][i][2] - f['shdr'][i][1]) * rate / f['shdr'][i][5] for i in indexes)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    f = font_features(sys.argv[1])
    pre, sh = f['presets'], f['shdr'][:-1]
    print(f'{sys.argv[1]}: {f["file_size"]} bytes, {len(sh)} samples, {len(pre)} presets, 24-bit part (sm24): {"yes" if f["has_sm24"] else "no"}')
    banks = Counter(p['bank'] for p in pre)
    print('\npresets by MIDI bank:', ', '.join(f'{b}: {n}' for b, n in sorted(banks.items())))
    dropped = [p for p in pre if not p['written']]
    print(f'written by the converter: {len(pre) - len(dropped)}; dropped (outside bank 0/128 or over 254 zones): {len(dropped)}')
    kits = sorted(p['program'] for p in pre if p['bank'] == 128)
    print(f'percussion kits (bank 128 programs): {len(kits)} {kits}')
    print('\nsamples:', Counter(s[9] for s in sh).most_common(), '(1 mono, 2 right, 4 left, linked ones have their pair next to them)',
          '| rates', sorted(Counter(s[5] for s in sh).items(), key=lambda kv: -kv[1])[:5])
    all_i = set(range(len(sh)))
    today, layered = used_samples(f), used_samples(f, True)
    other = {z['sample'] for p in pre if not p['written'] for z in p['zones']} - layered
    for label, s in (('all samples in the file', all_i), ('in the bank today', today), ('with every velocity layer', layered),
                     ('used only by the dropped presets', other)):
        print(f'  {label:34s}{len(s):5d} samples {seconds(f, s):7.1f} s at their rates, {frames(f, s, 32000) / 32000:7.1f} s at 32 kHz '
              f'= {frames(f, s, 32000) * 2 / 1e6:5.2f} MB as 16-bit at 32 kHz')
    layered_presets = [p for p in pre if len({(z['vlo'], z['vhi']) for z in p['zones']}) > 1]
    print(f'\nvelocity: {len(layered_presets)} presets have zones with different velocity ranges ({sum(1 for p in layered_presets if p["written"])} of them written);',
          f'zones kept today {sum(len(p["kept"]) for p in pre if p["written"])} of {sum(len(p["zones"]) for p in pre if p["written"])}')
    stereo = [i for i in layered if sh[i][9] == 4]
    right = [i for i in range(len(sh)) if sh[i][9] == 2]
    print(f'stereo: {len(right)} right-channel samples in the file; the bank would double {len(stereo)} of its samples '
          f'({seconds(f, stereo, 32000):.1f} s at 32 kHz) if both channels were kept')
    for label, mods in (('instrument', f['imod']), ('preset', f['pmod'])):
        dest = Counter(GEN.get(m[1], f'dest {m[1]}') if m[1] < 58 else f'link {m[1]}' for m in mods)
        print(f'modulators, {label}: {len(mods)} records, {len({m[:2] + m[3:] for m in mods})} distinct (source, destination, secondary, transform);',
              'to', dest.most_common(6))


if __name__ == '__main__':
    main()
