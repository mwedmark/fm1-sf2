#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
"""Looks at the boot configuration of the FM-1 firmware package (fm1_open/sdk/tools/pack/boot-config.bin) and, only if you ask, writes a
COPY of it with the SDRAM switched on. NOTHING in the build uses the copy: sdk/Package.cs reads tools/pack/boot-config.bin and nothing
else, and this script never writes that file.

    python3 make_sdram_config.py --show                    the SDRAM keys of the file, against JieLi's values for a build with SDRAM
    python3 make_sdram_config.py --size 2M [-o out.bin]    a copy with ENABLE_SDRAM=1 and SDRAM_SIZE=2M (AC7911BA); 8M for the BB

THIS IS NOT PROVEN ON A DEVICE. Do not put the copy in the package for the only FM-1 you have: if the board has no SDRAM, or the value of
SDRAM_SIZE is stored in another way than guessed here, the boot loader may not start the firmware, and no recovery from the ROM is known
(see FORMAT.md, "What is known about the SDRAM so far"). The values are those of JieLi's own generator of the file (isd_config_rule.c in
the AC79 SDK, system clock 320 MHz, not the dev-kit board); the file is a list of entries [length][name, 0][value of that length], numbers
as 32-bit little-endian, and the boot loader reads it as it is. `2M` is stored as 2 * 1024 * 1024 by default (the guess, like `4K`, which
is 4096 in this file); --encoding says another way.
"""
import argparse, os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_IN = os.path.join(HERE, 'fm1_open/sdk/tools/pack/boot-config.bin')
DEFAULT_OUT_DIR = os.path.join(HERE, 'dist')
EXPECTED_BYTES = 721                                          # sdk/Package.cs insists on it

# JieLi's values with the SDRAM in use, 320 MHz system clock, all but the first two as in M-VAVE's file (isd_config_rule.c).
SDRAM_ON = {
    'ENABLE_SDRAM': 1, 'SDRAM_SIZE': None,                  # None: from --size
    'SDRAM_REFRESH_TIME': 32, 'SDRAM_REFRESH_CYCLES': 4096, 'SDRAM_MODE': 0, 'SDRAM_COLUM': 0,
    'SDRAM_PLL3_EN': 0, 'SDRAM_PLL3_NOUSB_EN': 0, 'SDRAM_CL': 2, 'SDRAM_DQ_DLY_TRM': 4,
    'SDRAM_WLCNT': 3, 'SDRAM_D_DLY': 1, 'SDRAM_RLCNT': 1, 'SDRAM_Q_DLY': 1, 'SDRAM_PHASE': 3,
    'SDRAM_TRFC': 60, 'SDRAM_TRP': 18, 'SDRAM_TRCD': 18, 'SDRAM_TRRD': 12, 'SDRAM_TWTR': 20, 'SDRAM_TRTW': 60, 'SDRAM_TWR': 60, 'SDRAM_TRC': 55,
    'SDRAM_DQS_DLY_TRM': 0, 'SDRAM_UDQS_DLY_TRM': 10, 'SDRAM_LDQS_DLY_TRM': 10, 'SDRAM_DQ_DLY_INNER_TRM': 0,
}
SIZES = {'2M': 2 * 1024 * 1024, '8M': 8 * 1024 * 1024}
ENCODINGS = {                                                 # how the number for `2M` could be stored; the first is the guess
    'bytes': lambda megabytes: megabytes * 1024 * 1024,
    'decimal': lambda megabytes: megabytes * 1000 * 1000,
    'megabytes': lambda megabytes: megabytes,
}


def parse(blob):
    """The entries as (offset of the length byte, name, value bytes). Raises ValueError unless they end exactly at the end of the file."""
    entries, i = [], 0
    while i < len(blob):
        length = blob[i]
        end_of_name = blob.find(b'\0', i + 1)
        if end_of_name < 0 or end_of_name == i + 1:
            raise ValueError(f'entry at {i}: no name')
        name = blob[i + 1:end_of_name]
        if not all(65 <= c <= 90 or 48 <= c <= 57 or c == 95 for c in name):
            raise ValueError(f'entry at {i}: the name {name!r} is not upper case letters, digits and _')
        value = blob[end_of_name + 1:end_of_name + 1 + length]
        if len(value) != length:
            raise ValueError(f'entry at {i} ({name.decode()}): {length} bytes of value, {len(value)} left in the file')
        entries.append((i, name.decode(), value))
        i = end_of_name + 1 + length
    return entries


def number(value):
    return int.from_bytes(value, 'little') if len(value) == 4 else None


def with_values(blob, changes):
    """A copy of blob with the 32-bit entries named in `changes` set; every other byte is the same and the length is."""
    out = bytearray(blob)
    for offset, name, value in parse(blob):
        if name in changes:
            if len(value) != 4:
                raise ValueError(f'{name} is not a 32-bit entry')
            at = offset + 1 + len(name) + 1
            out[at:at + 4] = struct.pack('<I', changes[name])
    return bytes(out)


def show(blob):
    entries = {name: value for _, name, value in parse(blob)}
    print(f'{len(blob)} bytes, {len(entries)} entries ({"as sdk/Package.cs expects" if len(blob) == EXPECTED_BYTES else "NOT the " + str(EXPECTED_BYTES) + " bytes sdk/Package.cs expects"})')
    print(f'{"key":26s}{"in the file":>12s}{"JieLi, SDRAM in use":>22s}')
    for key, want in SDRAM_ON.items():
        have = number(entries[key]) if key in entries else None
        shown = 'missing' if key not in entries else have
        note = '' if want is None else ('' if have == want else '   <-- differs')
        wanted = '2M or 8M' if want is None else want
        print(f'{key:26s}{shown!s:>12s}{wanted!s:>22s}{note}')
    print('\nThe two keys that differ are the whole of the change; everything else is already as JieLi has it for a system clock of 320 MHz.')
    clk = entries.get('SYS_CLK')
    if clk is not None and number(clk) != 320_000_000:
        print(f'SYS_CLK is {number(clk)}: the CL and DQ_DLY values above are for 320 MHz.')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0], formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--input', default=DEFAULT_IN)
    ap.add_argument('--show', action='store_true')
    ap.add_argument('--size', choices=sorted(SIZES), help='write a copy with the SDRAM on: 2M (AC7911BA) or 8M (AC7911BB)')
    ap.add_argument('--encoding', choices=sorted(ENCODINGS), default='bytes', help='how SDRAM_SIZE is stored (the guess is bytes)')
    ap.add_argument('-o', '--output')
    ap.add_argument('--force', action='store_true', help='overwrite an existing output file')
    a = ap.parse_args()
    blob = open(a.input, 'rb').read()
    if a.show or not a.size:
        show(blob)
        if not a.size:
            return 0
    megabytes = SIZES[a.size] // (1024 * 1024)
    out_path = a.output or os.path.join(DEFAULT_OUT_DIR, f'boot-config-sdram-{a.size}.bin')
    if os.path.abspath(out_path) == os.path.abspath(DEFAULT_IN) or os.path.abspath(out_path) == os.path.abspath(a.input):
        sys.exit('refusing to write over the input file; this script only writes copies')
    if os.path.exists(out_path) and not a.force:
        sys.exit(f'{out_path} exists (--force overwrites it)')
    changes = dict(SDRAM_ON, SDRAM_SIZE=ENCODINGS[a.encoding](megabytes))
    out = with_values(blob, changes)
    assert len(out) == len(blob)
    before, after = {n: number(v) for _, n, v in parse(blob)}, {n: number(v) for _, n, v in parse(out)}
    os.makedirs(os.path.dirname(out_path) or '.', exist_ok=True)
    open(out_path, 'wb').write(out)
    changed = {k: (before[k], after[k]) for k in after if before.get(k) != after[k]}
    print(f'wrote {out_path} ({len(out)} bytes): {len(changed)} entries changed: ' + ', '.join(f'{k} {b} -> {v}' for k, (b, v) in changed.items()))
    print('NOT proven on a device, and not used by the build. See the top of this file and FORMAT.md before using it.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
