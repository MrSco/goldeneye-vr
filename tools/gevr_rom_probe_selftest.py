#!/usr/bin/env python3
"""
Self-test for gevr_rom_probe.py.

Builds a synthetic ROM laid out the way GoldenEye's is - filename literals in
one run, a 12-byte file_resource_table pointing at them through a constant
offset - then checks the prober recovers the table position, the offset, and
the per-file sizes.

This exists because the prober cannot be tested against a real cartridge dump
in CI, or anywhere a ROM should not be. It caught a real bug on first run: the
scan had a hardcoded pointer range that rejected valid tables.

    python tools/gevr_rom_probe_selftest.py
"""

import os
import re
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
PROBE = os.path.join(HERE, "gevr_rom_probe.py")
TABLE_SRC = os.path.join(REPO, "assets", "obseg", "file_resource_table.inc.c")

SIZE = 12 * 1024 * 1024
STR_BASE = 0x00A00000
TBL_BASE = 0x00B40000
DELTA = 0x80100000
DATA_BASE = 0x00100000
PAYLOAD = 0x800


def build_fake_rom(path):
    with open(TABLE_SRC, "r", encoding="utf-8", errors="surrogateescape") as f:
        src = f.read()
    rows = re.findall(r"\{\s*([A-Z0-9_]+)\s*,\s*\"([^\"]*)\"\s*,\s*([^}]+)\}", src)

    rom = bytearray(SIZE)
    rom[0:4] = b"\x80\x37\x12\x40"

    offsets = {}
    cur = STR_BASE
    for _ident, name, _addr in rows:
        if not name or name in offsets:
            continue
        blob = name.encode("ascii") + b"\x00"
        rom[cur:cur + len(blob)] = blob
        offsets[name] = cur
        cur += len(blob)

    data = DATA_BASE
    for i, (_ident, name, _addr) in enumerate(rows):
        off = TBL_BASE + i * 12
        if name:
            name_va = offsets[name] + DELTA
            hw = 0xB0000000 | data
            data += PAYLOAD
        else:
            name_va = 0
            hw = 0
        rom[off:off + 12] = struct.pack(">iII", i, name_va, hw)

    with open(path, "wb") as f:
        f.write(bytes(rom))
    return len(rows)


def main():
    tmp = tempfile.mkdtemp(prefix="gevr_probe_selftest_")
    rom_path = os.path.join(tmp, "synthetic.z64")
    csv_path = os.path.join(tmp, "manifest.csv")

    n = build_fake_rom(rom_path)
    print("built synthetic ROM with %d table entries" % n)

    res = subprocess.run([sys.executable, PROBE, rom_path, "-o", csv_path],
                         capture_output=True, text=True)
    sys.stdout.write(res.stdout)
    sys.stderr.write(res.stderr)

    failures = []
    if res.returncode != 0:
        failures.append("prober exited %d" % res.returncode)
    if ("at ROM offset 0x%06X" % TBL_BASE) not in res.stdout:
        failures.append("did not report the table at 0x%06X" % TBL_BASE)
    if ("0x%08X" % DELTA) not in res.stdout:
        failures.append("did not recover the pointer offset 0x%08X" % DELTA)

    if os.path.exists(csv_path):
        with open(csv_path, encoding="utf-8") as f:
            lines = f.read().strip().splitlines()[1:]
        sizes = [int(l.rsplit(",", 1)[1]) for l in lines if l]
        good = [s for s in sizes if s == PAYLOAD]
        # Every entry but the last has a next entry to measure against.
        if len(good) < n - 2:
            failures.append("only %d of %d sizes came back as 0x%X"
                            % (len(good), n - 2, PAYLOAD))
    else:
        failures.append("no manifest written")

    print()
    if failures:
        for f in failures:
            print("FAIL: %s" % f)
        return 1
    print("PASS: table position, pointer offset and %d file sizes all recovered" % (n - 2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
