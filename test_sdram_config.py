#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
"""make_sdram_config.py: the boot configuration is read as the entries it is, and a copy with the SDRAM on differs from it in exactly the
bytes of the two keys that are meant to change (ENABLE_SDRAM and SDRAM_SIZE), keeps its length, and never overwrites the original.
    python3 test_sdram_config.py"""
import hashlib, importlib.util, os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('make_sdram_config', os.path.join(HERE, 'make_sdram_config.py'))
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
blob = open(m.DEFAULT_IN, 'rb').read()
failed = checks = 0


def check(ok, what):
    global failed, checks
    checks += 1
    if not ok:
        failed += 1
    print('ok  ' if ok else 'FAIL', what)


def run(*args):
    return subprocess.run([sys.executable, '-I', os.path.join(HERE, 'make_sdram_config.py'), *args], capture_output=True, text=True)


entries = m.parse(blob)
names = [n for _, n, _ in entries]
check(len(blob) == 721 and len(entries) >= 29, f'boot-config.bin is {len(blob)} bytes, {len(entries)} entries, parsed to the end with no byte left over')
check(len(set(names)) == len(names), 'every key is there once')
values = {n: m.number(v) for _, n, v in entries}
check(values['OSC_FREQ'] == 24_000_000 and values['SYS_CLK'] == 320_000_000 and values['HSB_DIV'] == 1 and values['LSB_DIV'] == 2,
      'the entries decode to what is known of them: 24 MHz crystal, 320 MHz system clock, bus dividers 1 and 2')
check(values['SDRAM_TEST_SIZE'] == 4096 and values['SDRAM_CONFIG_VAL'] == 0, 'the two keys that are not in JieLi\'s ini file (SDRAM_TEST_SIZE 4096, SDRAM_CONFIG_VAL 0) are there, and the copy leaves them as they are')
off = {k: (values.get(k), v) for k, v in m.SDRAM_ON.items() if v is not None and values.get(k) != v}
check(off == {'ENABLE_SDRAM': (0, 1)}, f'M-VAVE\'s file has JieLi\'s SDRAM values for every key but ENABLE_SDRAM (and SDRAM_SIZE, which is 0 against 2M or 8M): {off}')
check(values['SDRAM_SIZE'] == 0, 'SDRAM_SIZE is 0 in it')

before = hashlib.sha256(blob).hexdigest()
for size, megabytes in (('2M', 2), ('8M', 8)):
    for encoding, want in (('bytes', megabytes * 1024 * 1024), ('decimal', megabytes * 1_000_000), ('megabytes', megabytes)):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, 'copy.bin')
            r = run('--size', size, '--encoding', encoding, '-o', path)
            out = open(path, 'rb').read() if r.returncode == 0 else b''
            diff = [i for i in range(min(len(out), len(blob))) if out[i] != blob[i]]
            where = {}
            for offset, name, _ in entries:
                if name in ('ENABLE_SDRAM', 'SDRAM_SIZE'):
                    where[name] = set(range(offset + 1 + len(name) + 1, offset + 1 + len(name) + 5))
            allowed = where['ENABLE_SDRAM'] | where['SDRAM_SIZE']
            new = {n: m.number(v) for _, n, v in m.parse(out)} if out else {}
            check(r.returncode == 0 and len(out) == 721 and set(diff) <= allowed and new.get('ENABLE_SDRAM') == 1 and new.get('SDRAM_SIZE') == want,
                  f'--size {size} --encoding {encoding}: 721 bytes, only the bytes of the two keys differ ({len(diff)} of them), ENABLE_SDRAM 1, SDRAM_SIZE {want}')
check(hashlib.sha256(open(m.DEFAULT_IN, 'rb').read()).hexdigest() == before, 'boot-config.bin itself is untouched')

with tempfile.TemporaryDirectory() as d:
    path = os.path.join(d, 'copy.bin')
    check(run('--size', '2M', '-o', path).returncode == 0 and run('--size', '2M', '-o', path).returncode != 0, 'an output file that exists is not overwritten without --force')
    check(run('--size', '2M', '-o', path, '--force').returncode == 0, '--force overwrites it')
check(run('--size', '2M', '-o', m.DEFAULT_IN, '--force').returncode != 0 and hashlib.sha256(open(m.DEFAULT_IN, 'rb').read()).hexdigest() == before,
      'it refuses to write over the input file, --force or not')
check('ENABLE_SDRAM' in run('--show').stdout and 'differs' in run('--show').stdout, '--show lists the keys and marks the one that differs')
try:
    m.parse(blob[:-2])
    check(False, 'a truncated file is refused')
except ValueError:
    check(True, 'a truncated file is refused')

print(f'{failed} of {checks} checks FAILED' if failed else f'all {checks} checks passed')
sys.exit(1 if failed else 0)
