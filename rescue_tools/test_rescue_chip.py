"""The rescue on a pretend FM-1: its UBOOT answers like the real one, with the flash chip of the first units
(Puya P25Q80, 856014), of the units sold since autumn 2026 (UC25HQ80, B36014), or some other chip.
The first two are rescued (the sectors that differ written, the head and the device data left alone);
any other is refused before anything is read or written. No FM-1 or USB is needed.
  python rescue_tools/test_rescue_chip.py
"""
import os
import struct
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fm1_rescue as R

failed = 0


def check(ok, what):
    global failed
    failed += not ok
    print("ok  " if ok else "FAIL", what)


class FakeFm1(R.Uboot):
    """The UBOOT's commands on a flash in memory; R.Uboot's own chip(), read() and write_sector() run on it."""

    def __init__(self, flash_id, flash):
        super().__init__(None)
        self.flash_id, self.flash, self.writes, self.reads = flash_id, bytearray(flash), [], 0

    def inquiry(self):
        return "WL82", "UBOOT1.00"

    def cmd(self, cmd, args=b""):
        if cmd == R.CMD_READ_KEY:                       # the key 980F, ciphered as the chip sends it
            k = bytearray([0x0F, 0x98])
            R.crc_cipher(k)
            return bytes([0, 0, 0, 0, k[1], k[0]]) + bytes(8)
        if cmd == R.CMD_GET_ONLINE_DEVICE:              # type 3 (SPI NOR), then the JEDEC ID
            return bytes([3, 0]) + struct.pack("<I", self.flash_id) + bytes(8)
        if cmd == R.CMD_ERASE_SECTOR:
            a = struct.unpack(">I", args[:4])[0]
            self.flash[a:a + R.SECTOR] = b"\xff" * R.SECTOR
            return bytes(14)
        return bytes(14)                                 # jump, run, keep-alive

    def write_block(self, cmd, addr, data):
        if cmd == R.CMD_WRITE_FLASH:
            self.flash[addr:addr + len(data)] = data
            self.writes.append(addr)

    def start_loader(self, blob):
        pass

    def read(self, addr, n):
        self.reads += 1
        return bytes(self.flash[addr:addr + n])


# a stock package: the FM-1's own head, a firmware area that differs in a few sectors, device data after it
head = bytes((i * 7) & 255 for i in range(R.WRITE_MIN))
end = 0x93000
image = bytearray(head + bytes((i * 13 + 5) & 255 for i in range(end - R.WRITE_MIN)))
flash = bytearray(image + bytes((i * 3) & 255 for i in range(R.FLASH_SIZE - end)))
for a in (0x10000, 0x40000, 0x92000):                    # three sectors the crash left wrong
    flash[a:a + R.SECTOR] = b"\x00" * R.SECTOR

with tempfile.TemporaryDirectory() as backups:
    for fid, name in ((0x856014, "Puya P25Q80, the first units"), (0xB36014, "UC25HQ80, sold since autumn 2026")):
        dev = FakeFm1(fid, flash)
        out = []
        done = R.rescue(dev, [("V15", bytes(image))], b"", True, backups, say=lambda *a, **k: out.append(" ".join(map(str, a))))
        check(done and dev.flash[:end] == image and dev.flash[end:] == flash[end:],
              f"{fid:06X} ({name}): rescued, the device data after the firmware kept")
        check(sorted({a - a % R.SECTOR for a in dev.writes}) == [0x10000, 0x40000, 0x92000], f"{fid:06X}: only the 3 wrong sectors written")
        check(any(f"id {fid:06X}" in line for line in out), f"{fid:06X}: the chip is named in the output")

    for fid in (0xEF4014, 0x856015, 0xB36015, 0):       # another chip, or another size of either
        dev = FakeFm1(fid, flash)
        try:
            R.rescue(dev, [("V15", bytes(image))], b"", True, backups, say=lambda *a, **k: None)
            refused = False
        except R.Fail as e:
            refused = "856014 or B36014" in str(e)
        check(refused and dev.reads == 0 and not dev.writes and dev.flash == flash, f"{fid:06X}: refused, nothing read or written")
    check(len(os.listdir(backups)) >= 1, "a backup was saved before writing")

print(f"{failed} FAILED" if failed else "all passed")
sys.exit(1 if failed else 0)
