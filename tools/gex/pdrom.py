"""Read Perfect Dark's file table from a PD NTSC 1.1 ROM or a ROM patched from one (GoldenEye X).

As pdvr port/src/romdata.c: the data segment at 0x39850 is 1173-compressed
(0x11 0x73, a 24-bit length, raw deflate); the file offset table sits at 0x28080
inside it, file 0 unused, ending at a zero; the last offset points at the name table.

usage: python pdrom.py rom.z64 [pattern]
"""
import fnmatch
import struct
import sys
import zlib

DATA_OFS = 0x39850
FILES_OFS = 0x28080


def inflate1173(blob):
    if blob[0] != 0x11 or blob[1] != 0x73:
        raise ValueError("not 1173-compressed")
    size = (blob[2] << 16) | (blob[3] << 8) | blob[4]
    out = zlib.decompressobj(-15).decompress(blob[5:])
    if len(out) < size:
        raise ValueError("inflated %d of %d bytes" % (len(out), size))
    return out[:size]


class PdRom:
    def __init__(self, path):
        self.rom = open(path, "rb").read()
        if self.rom[:4] != b"\x80\x37\x12\x40":
            raise ValueError("not a big-endian .z64 ROM")
        self.title = self.rom[0x20:0x34].decode("ascii", "replace").strip()
        self.code = self.rom[0x3B:0x3F].decode("ascii", "replace")
        self.data = inflate1173(self.rom[DATA_OFS:])
        offsets = []
        i = 1
        while True:
            ofs = struct.unpack_from(">I", self.data, FILES_OFS + 4 * i)[0]
            if ofs == 0:
                break
            offsets.append(ofs)
            i += 1
        names_at = offsets[-1]
        self.files = []   # (num, name, rom offset, size)
        for n in range(1, len(offsets)):
            rel = struct.unpack_from(">I", self.rom, names_at + 4 * n)[0]
            if rel == 0:
                break
            end = self.rom.index(b"\0", names_at + rel)
            name = self.rom[names_at + rel:end].decode("ascii", "replace")
            self.files.append((n, name, offsets[n - 1], offsets[n] - offsets[n - 1]))
        self.by_name = {f[1]: f for f in self.files}

    def raw(self, name):
        n, _, ofs, size = self.by_name[name]
        return self.rom[ofs:ofs + size]

    def load(self, name):
        """The file's bytes, inflated if it is 1173-compressed."""
        blob = self.raw(name)
        return inflate1173(blob) if blob[:2] == b"\x11\x73" else blob


if __name__ == "__main__":
    r = PdRom(sys.argv[1])
    pattern = sys.argv[2] if len(sys.argv) > 2 else "*"
    print("%s (%s): %d files" % (r.title, r.code, len(r.files)))
    for n, name, ofs, size in r.files:
        if fnmatch.fnmatch(name, pattern):
            print("%4d %-28s rom %08X size %7d" % (n, name, ofs, size))
