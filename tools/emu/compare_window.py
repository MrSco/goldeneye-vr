"""The PC build's copy of a hack's memory against an emulator's RDRAM at the same moment.

    python tools/emu/compare_window.py <port.bin> <port.ranges> <emu.bin> <hack.z64>

port.bin/.ranges: the input script's "windowdump <name>" (port/pc/pc_testinput.c),
emu.bin: tools/emu/n64_dump.lua. Reports (1) the ranges the port's level scripts
wrote that the emulator holds otherwise, (2) what the emulator changed from the
cartridge in the scripts' targets that the port never wrote, (3) what the port
changed that the emulator did not. Mario Characters' layout (its pieces below).
"""
import struct
import sys
import zlib

MARIO_PIECES = [(0x80702000, 0xFFD000, 0x3000), (0x80705000, 0xFF8800, 0x4800), (0x8070A000, 0xFF0000, 0x8800)]


class Mem:
    """The cartridge as loaded: its data segment at 0x80020D90, then its Expansion Pak pieces."""

    def __init__(self, path, pieces):
        rom = open(path, "rb").read()
        self.data = zlib.decompressobj(-15).decompress(rom[0x21992:])
        self.pieces = [(v, rom[r:r + n]) for v, r, n in pieces]

    def bytes(self, va, n):
        if 0x80020D90 <= va < 0x80020D90 + len(self.data):
            return self.data[va - 0x80020D90:va - 0x80020D90 + n]
        for v, b in self.pieces:
            if v <= va < v + len(b):
                return b[va - v:va - v + n]
        return None

port_bin, port_ranges, emu_bin, rom = sys.argv[1:5]
P = open(port_bin, "rb").read()
PBASE = 0x80020D90
lines = open(port_ranges).read().split("\n")
written = []
for l in lines[1:]:
    if l.strip():
        a, n = l.split()
        written.append((int(a, 16), int(n)))
E = open(emu_bin, "rb").read()
EMU = [(0x80020D90, 0x3C550), (0x80700000, 0x12800)]
emu = {}
off = 0
for base, n in EMU:
    emu[base] = (off, n)
    off += n


def emu_byte(a):
    for base, (o, n) in emu.items():
        if base <= a < base + n:
            return E[o + a - base]
    return None


def port_byte(a):
    return P[a - PBASE] if PBASE <= a < PBASE + len(P) else None


M = Mem(rom, MARIO_PIECES)


def pristine(a):
    try:
        return M.bytes(a, 1)[0]
    except Exception:
        return 0


# 1. every range the port's scripts wrote: does the emulator hold the same?
bad = []
for a, n in written:
    pe = bytes(port_byte(a + i) or 0 for i in range(n))
    ee = [emu_byte(a + i) for i in range(n)]
    if None in ee:
        continue
    if pe != bytes(ee):
        bad.append((a, n, pe.hex(), bytes(ee).hex()))
print("1. port-written ranges: %d, differing from the emulator: %d" % (len(written), len(bad)))
for b in bad[:20]:
    print("   %08X %d port %s emu %s" % b)

# 2. what the emulator changed from the cartridge in the scripts' targets, that the port did not write
TARGETS = [("mailbox", 0x80700000, 0x80702000), ("characters/hats/props", 0x80702000, 0x80712800),
           ("item file names", 0x80053000, 0x80054000), ("mission folder", 0x8002ABE4, 0x8002B19C),
           ("fog", 0x80044E10, 0x80046000), ("music rows", 0x8004EB10, 0x8004EC30), ("POV", 0x800552FC, 0x80055300)]
wset = set()
for a, n in written:
    wset.update(range(a, a + n))
for name, lo, hi in TARGETS:
    changed = [a for a in range(lo, hi) if emu_byte(a) is not None and emu_byte(a) != pristine(a)]
    missed = [a for a in changed if a not in wset]
    runs = []
    for a in missed:
        if runs and a == runs[-1][1]:
            runs[-1][1] = a + 1
        else:
            runs.append([a, a + 1])
    print("2. %-22s emulator changed %5d bytes, port wrote none of %5d (%d runs)" % (name, len(changed), len(missed), len(runs)))
    for lo2, hi2 in runs[:8]:
        print("     %08X..%08X emu %s pristine %s port %s" % (lo2, hi2, bytes(emu_byte(x) for x in range(lo2, min(hi2, lo2 + 16))).hex(),
              bytes(pristine(x) for x in range(lo2, min(hi2, lo2 + 16))).hex(),
              bytes((port_byte(x) or 0) for x in range(lo2, min(hi2, lo2 + 16))).hex()))

# 3. what the port wrote that the emulator still has as on the cartridge
same = [a for a in sorted(wset) if emu_byte(a) is not None and emu_byte(a) == pristine(a) and port_byte(a) != pristine(a)]
print("3. port changed, emulator unchanged: %d bytes" % len(same))
