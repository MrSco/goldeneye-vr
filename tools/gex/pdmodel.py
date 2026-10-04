"""Walk a Perfect Dark model file (as pdvr src/include/types.h: struct modeldef,
struct modelnode) and print its node tree. Read-only; files come from the
player's own ROM through pdrom.py.

usage: python pdmodel.py rom.z64 FileNameZ [more files...]
"""
import struct
import sys
from collections import Counter

from pdrom import PdRom

NODE_NAMES = {
    0x01: "CHRINFO", 0x02: "POSITION", 0x04: "GUNDL", 0x05: "05", 0x08: "DISTANCE",
    0x09: "REORDER", 0x0A: "BBOX", 0x0B: "0B", 0x0C: "CHRGUNFIRE", 0x0D: "0D",
    0x0E: "0E", 0x0F: "0F", 0x11: "11", 0x12: "TOGGLE", 0x15: "POSITIONHELD",
    0x16: "STARGUNFIRE", 0x17: "HEADSPOT", 0x18: "DL", 0x19: "19",
}


def seg(ptr):
    """A model pointer is segment 5 plus an offset into the file."""
    return ptr & 0x00FFFFFF if ptr else None


class PdModel:
    def __init__(self, data):
        self.d = data
        (root, skel, parts, self.numparts, self.nummatrices, self.scale,
         self.rwdatalen, self.numtexconfigs, texconfigs) = struct.unpack_from(">IIIhhfhhI", data, 0)
        self.root, self.skel, self.parts, self.texconfigs = seg(root), seg(skel), seg(parts), seg(texconfigs)

    def node(self, ofs):
        typ, rodata, parent, nxt, prev, child = struct.unpack_from(">HxxIIIII", self.d, ofs)
        return typ & 0xFF, seg(rodata), seg(nxt), seg(child)

    def walk(self, ofs=None, depth=0, out=None):
        out = [] if out is None else out
        ofs = self.root if ofs is None else ofs
        seen = 0
        while ofs is not None and seen < 4096:
            typ, rodata, nxt, child = self.node(ofs)
            out.append((depth, ofs, typ, rodata))
            if child is not None:
                self.walk(child, depth + 1, out)
            ofs = nxt
            seen += 1
        return out


if __name__ == "__main__":
    rom = PdRom(sys.argv[1])
    for name in sys.argv[2:]:
        m = PdModel(rom.load(name))
        nodes = m.walk()
        kinds = Counter(NODE_NAMES.get(t, "%02X" % t) for _, _, t, _ in nodes)
        print("%s: %d bytes, %d parts, %d matrices, scale %.3f, %d textures, %d nodes"
              % (name, len(m.d), m.numparts, m.nummatrices, m.scale, m.numtexconfigs, len(nodes)))
        print("   " + ", ".join("%s %d" % kv for kv in sorted(kinds.items())))
