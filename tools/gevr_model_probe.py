#!/usr/bin/env python3
"""
Decompress a model file out of a GoldenEye cartridge and walk it the way the
port's model converter does, printing every block it finds.

Why
---
port/src/gevr_model.c rebuilds each cartridge model file in the host's layout:
big-endian fields swapped, 4-byte pointer slots widened, 8-byte display list
commands doubled. It discovers the file's blocks by walking the node tree from
the root exactly as modelPromoteNodeOffsetsToPointers() in model.c does. This
script does the same walk in Python, on the real bytes, so the record sizes
and pointer fields the converter assumes can be checked against data before
anything is built for the headset. Gaps between blocks that no record accounts
for show up as "unclaimed", and a block that runs past the next one shows up as
an overlap.

Usage
-----
    python tools/gevr_model_probe.py <rom.z64> <FILENAME> <numSwitches> <numTextures>

e.g.  python tools/gevr_model_probe.py "007 - GoldenEye.z64" PlegalpageZ 0 5

The counts come from the model's MODELFILEHEADER line under assets/obseg
(NUMSWITCHES and NUMTEXTURES). Reads the ROM; writes nothing.
"""

import os
import re
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
MANIFEST = os.path.join(REPO, "port", "src", "gevr_rom_manifest.c")

# Node opcodes, from MODELNODE_OPCODE in bondconstants.h
OP = {1: "HEADER", 2: "GROUP", 3: "OP03", 4: "DL", 5: "OP05", 6: "OP06", 7: "OP07", 8: "LOD",
      9: "BSP", 10: "BBOX", 11: "OP11", 12: "GUNFIRE", 13: "SHADOW", 14: "OP14", 15: "INTERLINK",
      16: "OP16", 17: "OP17", 18: "SWITCH", 19: "OP19", 20: "OP20", 21: "GROUPSIMPLE",
      22: "DLPRIMARY", 23: "HEAD", 24: "DLCOLLISION"}

# Cartridge sizes of each rodata record (bondtypes.h with 4-byte pointers)
RODATA_SIZE = {1: 16, 2: 28, 3: 28, 4: 20, 5: 0x1a8, 6: 24, 7: 0x1b0, 8: 16, 9: 36, 10: 28,
               11: 76, 12: 40, 13: 32, 14: 16, 15: 28, 16: 24, 17: 32, 18: 8, 20: 16, 21: 20,
               22: 16, 23: 4, 24: 32}

NODE_SIZE = 24
TEX_SIZE = 12
VTX_SIZE = 16


def be32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def be16(b, o):
    return struct.unpack_from(">H", b, o)[0]


def load_rom(path):
    with open(path, "rb") as f:
        rom = bytearray(f.read())
    magic = bytes(rom[:4])
    if magic == b"\x37\x80\x40\x12":
        rom[0::2], rom[1::2] = rom[1::2], rom[0::2]
    elif magic == b"\x40\x12\x37\x80":
        for i in range(0, len(rom) - 3, 4):
            rom[i], rom[i + 1], rom[i + 2], rom[i + 3] = rom[i + 3], rom[i + 2], rom[i + 1], rom[i]
    return bytes(rom)


def manifest_lookup(name):
    rx = re.compile(r"\{\s*0x([0-9A-Fa-f]+),\s*(\d+),\s*(\d)\s*\},\s*/\*\s*\S+\s+(\S+)\s*\*/")
    with open(MANIFEST, encoding="utf-8") as f:
        for line in f:
            m = rx.search(line)
            if m and m.group(4) == name:
                return int(m.group(1), 16), int(m.group(2))
    raise SystemExit("%s: not in the manifest" % name)


def decompress_1172(blob):
    if blob[0] != 0x11 or blob[1] != 0x72:
        raise SystemExit("not a 1172 stream (header %02x %02x)" % (blob[0], blob[1]))
    return zlib.decompress(blob[2:], -15)


def ofs(addr):
    """A segment-5 pointer's offset into the file, or None for a null."""
    if addr == 0:
        return None
    if (addr >> 24) != 0x05:
        return None
    return addr & 0xFFFFFF


class Probe:
    def __init__(self, data, num_switches, num_textures):
        self.d = data
        self.blocks = {}   # offset -> (kind, size or None, note)
        self.warnings = []
        self.num_switches = num_switches
        self.num_textures = num_textures

    def mark(self, o, kind, size=None, note=""):
        if o is None:
            return
        if o >= len(self.d):
            self.warnings.append("%s at 0x%x is past the end (0x%x)" % (kind, o, len(self.d)))
            return
        if o in self.blocks:
            return
        self.blocks[o] = (kind, size, note)
        return True

    def walk(self):
        d = self.d
        self.mark(0, "SWITCHES", 4 * self.num_switches)
        tex = 4 * self.num_switches
        self.mark(tex, "TEXTURES", TEX_SIZE * self.num_textures)
        for i in range(self.num_textures):
            tid = be32(d, tex + TEX_SIZE * i)
            w, h = d[tex + TEX_SIZE * i + 4], d[tex + TEX_SIZE * i + 5]
            self.blocks[tex][2:] and None
            print("  texture %d: id 0x%x %dx%d" % (i, tid, w, h))
        root = tex + TEX_SIZE * self.num_textures
        for i in range(self.num_switches):
            self.mark(ofs(be32(d, 4 * i)), "NODE", NODE_SIZE, "switch %d" % i)
        pending = [root]
        self.mark(root, "NODE", NODE_SIZE, "root")
        seen = set()
        while pending:
            n = pending.pop()
            if n in seen or n is None:
                continue
            seen.add(n)
            op = be16(d, n) & 0xFF
            rod = ofs(be32(d, n + 4))
            for fld, name in ((8, "parent"), (12, "next"), (16, "prev"), (20, "child")):
                o = ofs(be32(d, n + fld))
                if o is not None and self.mark(o, "NODE", NODE_SIZE, name + " of 0x%x" % n):
                    pending.append(o)
                elif o is not None and o not in seen:
                    pending.append(o)
            if rod is None:
                continue
            self.mark(rod, "RODATA_" + OP.get(op, str(op)), RODATA_SIZE.get(op), "node 0x%x" % n)
            self.rodata(op, rod, pending)

    def node_ptr(self, o, pending, note):
        o = ofs(o)
        if o is not None:
            if self.mark(o, "NODE", NODE_SIZE, note):
                pending.append(o)
        return o

    def rodata(self, op, r, pending):
        d = self.d
        p = lambda off: be32(d, r + off)
        if op in (1, 20):
            self.node_ptr(p(4), pending, "HEADER.FirstGroup")
        elif op in (2, 3):
            self.node_ptr(p(0x14), pending, "GROUP.ChildGroup")
        elif op == 17:
            self.node_ptr(p(0x14), pending, "OP17.othernode")
        elif op == 4:
            nv = be16(d, r + 0x10)
            self.mark(ofs(p(0)), "GDL", None, "DL.Primary")
            self.mark(ofs(p(4)), "GDL", None, "DL.Secondary")
            self.mark(ofs(p(0xC)), "VTX", VTX_SIZE * nv, "DL.Vertices x%d" % nv)
        elif op == 22:
            nv = be32(d, r + 0)
            self.mark(ofs(p(4)), "VTX", VTX_SIZE * nv, "DLPRIMARY.Vertices x%d" % nv)
            self.mark(ofs(p(8)), "GDL", None, "DLPRIMARY.Primary")
        elif op == 24:
            nv = struct.unpack_from(">h", d, r + 0xC)[0]
            nc = struct.unpack_from(">h", d, r + 0xE)[0]
            self.mark(ofs(p(0)), "GDL", None, "DLCOL.Primary")
            self.mark(ofs(p(4)), "GDL", None, "DLCOL.Secondary")
            self.mark(ofs(p(8)), "VTX", VTX_SIZE * nv, "DLCOL.Vertices x%d" % nv)
            self.mark(ofs(p(0x10)), "COLVTX", VTX_SIZE * nc, "DLCOL.CollisionVertices x%d" % nc)
            self.mark(ofs(p(0x14)), "S16S", 2 * nv, "DLCOL.PointUsage x%d" % nv)
            cv = ofs(p(0x10))
            if cv is not None:
                for i in range(nc):
                    self.node_ptr(be32(d, cv + VTX_SIZE * i + 8), pending, "colvtx %d LinkedTo" % i)
        elif op == 8:
            self.node_ptr(p(8), pending, "LOD.Affects")
        elif op == 18:
            self.node_ptr(p(0), pending, "SWITCH.Controls")
        elif op == 9:
            self.node_ptr(p(0x18), pending, "BSP.left")
            self.node_ptr(p(0x1C), pending, "BSP.right")
        elif op == 12:
            self.mark(ofs(p(0x18)), "IMAGE", TEX_SIZE, "GUNFIRE.Image")
        elif op == 13:
            self.mark(ofs(p(0x10)), "IMAGE", TEX_SIZE, "SHADOW.image")
            self.node_ptr(p(0x14), pending, "SHADOW.Header")
        elif op == 11:
            self.mark(ofs(p(0x3C)), "DATA", None, "OP11.unk0c[15]")
        elif op in (5, 7):
            base = 0 if op == 5 else 8
            nch = be32(d, r + base)
            self.mark(ofs(p(base + 4)), "CHILDREN", 8 * nch, "OP%02d.Children x%d" % (op, nch))
            self.mark(ofs(p(base + 8)), "VTX", None, "OP%02d.Vertices" % op)
            self.mark(ofs(p(base + 12)), "IMAGES", None, "OP%02d.Images" % op)
            if op == 7:
                self.node_ptr(p(0), pending, "OP07.unk00")
                self.node_ptr(p(4), pending, "OP07.unk04")

    def gdl_len(self, o):
        n = 0
        while o + 8 * n + 8 <= len(self.d):
            opcode = self.d[o + 8 * n]
            n += 1
            if opcode == 0xB8:
                return 8 * n
        return None

    def report(self):
        print("\n%d blocks:" % len(self.blocks))
        offs = sorted(self.blocks)
        end = len(self.d)
        for i, o in enumerate(offs):
            kind, size, note = self.blocks[o]
            nxt = offs[i + 1] if i + 1 < len(offs) else end
            gap = nxt - o
            if kind == "GDL":
                size = self.gdl_len(o)
            flag = ""
            if size is None:
                flag = "  (size from gap)"
                size = gap
            elif size > gap:
                flag = "  OVERLAPS next block by %d" % (size - gap)
            elif size < gap:
                flag = "  +%d unclaimed" % (gap - size)
            print("  0x%05x %-18s %5d bytes %-34s%s" % (o, kind, size, note, flag))
        for w in self.warnings:
            print("  WARNING: " + w)


def main():
    if len(sys.argv) != 5:
        print(__doc__)
        return 2
    rom = load_rom(sys.argv[1])
    name = sys.argv[2]
    ns, nt = int(sys.argv[3]), int(sys.argv[4])
    off, size = manifest_lookup(name)
    data = decompress_1172(rom[off:off + size])
    print("%s: %d compressed -> %d bytes" % (name, size, len(data)))
    pr = Probe(data, ns, nt)
    pr.walk()
    pr.report()
    return 0


if __name__ == "__main__":
    sys.exit(main())
