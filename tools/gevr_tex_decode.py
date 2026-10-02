#!/usr/bin/env python3
"""
Decode GoldenEye textures out of the ROM to PNG (issue #9: the hand patches
need the real skin texture to choose UVs, and Blender needs it to preview).

Why
---
tools/gevr_model_export.py knows which texture number every triangle uses,
but the images themselves sit compressed in the images segment. This ports
image.c's texLoad() lookup and its zlib path (texInflateZlib): a bit-packed
header (format, palette size, 16-bit palette), then per level of detail a
width, a height and a rarezip stream of palette indices. Of the non-zlib
textures (texInflateNonZlib: Rare's own huffman/RLE/lookup schemes) only
run-length 8-bit intensity is decoded (texInflateRle; the grenade's bottom,
0x5e2), alpha = intensity as the N64's I format has it; the rest are
reported and skipped.

Offsets: assets/images.def lists each texture's compressed size in texture
number order; image_entries_load() turns the sizes into running offsets from
the segment start (GEVR_SEG_IMAGES in port/src/gevr_rom_manifest.c).

Usage
-----
    python tools/gevr_tex_decode.py <rom.z64> <outdir> <texnum> [texnum ...]

e.g.  python tools/gevr_tex_decode.py "007 - GoldenEye.z64" build/handmodels/tex 0x701 0x702

Writes <outdir>/<texnum as 0x%03x>.png (first level of detail only). The PNGs
are ROM-derived: keep them under build/.
"""

import os
import re
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import gevr_model_probe as probe  # noqa: E402

FORMATS = {
    9: ("RGBA16", 8), 10: ("RGBA16", 4), 11: ("IA16", 8), 12: ("IA16", 4),
}


def images_segment():
    rx = re.compile(r"\{\s*0x([0-9A-Fa-f]+),\s*(\d+),\s*\d\s*\},\s*/\*\s*GEVR_SEG_IMAGES\s*\*/")
    with open(os.path.join(REPO, "port", "src", "gevr_rom_manifest.c"), encoding="utf-8") as f:
        for line in f:
            m = rx.search(line)
            if m:
                return int(m.group(1), 16), int(m.group(2))
    raise SystemExit("GEVR_SEG_IMAGES not in the manifest")


def texture_offsets():
    """Running offsets, as image_entries_load() computes them."""
    rx = re.compile(r"IMAGE\(\s*\w+\s*,\s*(0x[0-9A-Fa-f]+|\d+)")
    sizes = []
    with open(os.path.join(REPO, "assets", "images.def"), encoding="utf-8") as f:
        for line in f:
            m = rx.match(line.strip())
            if m:
                sizes.append(int(m.group(1), 0))
    offs, o = [], 0
    for s in sizes:
        offs.append(o)
        o += s
    offs.append(o)
    return offs


class Bits:
    """texSetBitstring / texReadBits: MSB first, whole bytes pulled as needed."""

    def __init__(self, data, pos):
        self.d, self.pos, self.acc, self.n = data, pos, 0, 0

    def read(self, count):
        while self.n < count:
            self.acc = (self.acc << 8) | self.d[self.pos]
            self.pos += 1
            self.n += 8
        self.n -= count
        return (self.acc >> self.n) & ((1 << count) - 1)


def colour(fmt, c):
    if fmt == "RGBA16":
        r, g, b, a = (c >> 11) & 31, (c >> 6) & 31, (c >> 1) & 31, c & 1
        return (r * 255 // 31, g * 255 // 31, b * 255 // 31, 255 if a else 0)
    i, a = c >> 8, c & 0xFF
    return (i, i, i, a)


def inflate_rle(bits, total):
    """texInflateRle: literals and runs copied from earlier blocks (8 bits or less)."""
    btsize, rlsize, blocksize = bits.read(3), bits.read(3), bits.read(4)
    cost, fudge = btsize + rlsize + blocksize + 1, 0
    while cost > 0:
        cost -= blocksize + 1
        fudge += 1
    out = []
    while len(out) < total:
        if bits.read(1) == 0:
            out.append(bits.read(blocksize))
        else:
            start = len(out) - bits.read(btsize) - 1
            for i in range(start, start + bits.read(rlsize) + fudge):
                out.append(out[i])
            out.append(bits.read(blocksize))
    return out[:total]


def decode_nonzlib(src):
    """texInflateNonZlib's first image, for the one scheme handled here."""
    bits = Bits(src, 1)
    fmt, w, h, method = bits.read(4), bits.read(8), bits.read(8), bits.read(4)
    if fmt != 7 or method != 4:
        return "not zlib: format %d, method %d" % (fmt, method)
    px = bytearray()
    for i in inflate_rle(bits, w * h):
        px += bytes((i, i, i, i))
    return w, h, bytes(px)


def decode(rom, texnum):
    """(width, height, rgba bytes) for the texture's first image, or a reason string."""
    seg, _ = images_segment()
    offs = texture_offsets()
    this, nxt = offs[texnum], offs[texnum + 1]
    src = rom[seg + this: seg + nxt + 16]
    head = src[0]
    iszlib = (head >> 6) & 1
    if not iszlib:
        return decode_nonzlib(src)
    bits = Bits(src, 1)
    fmt = bits.read(8)
    ncol = bits.read(8) + 1
    pal = [bits.read(16) for _ in range(ncol)]
    if fmt not in FORMATS:
        return "zlib with format %d" % fmt
    kind, bpp = FORMATS[fmt]
    w, h = bits.read(8), bits.read(8)
    pos = bits.pos
    # decompressdata(): the two-byte rarezip mark, then raw deflate
    raw = zlib.decompressobj(-15).decompress(bytes(src[pos + 2:]))
    idx = []
    if bpp == 8:
        idx = list(raw[:w * h])
    else:
        for b in raw[:(w * h + 1) // 2]:
            idx += [b >> 4, b & 15]
        idx = idx[:w * h]
    px = bytearray()
    for i in idx:
        px += bytes(colour(kind, pal[i] if i < ncol else 0))
    return w, h, bytes(px)


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    rom = probe.load_rom(sys.argv[1])
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)
    from PIL import Image
    for a in sys.argv[3:]:
        n = int(a, 0)
        r = decode(rom, n)
        if isinstance(r, str):
            print("0x%03x: %s" % (n, r))
            continue
        w, h, px = r
        path = os.path.join(out, "0x%03x.png" % n)
        Image.frombytes("RGBA", (w, h), px).save(path)
        print("0x%03x: %dx%d -> %s" % (n, w, h, path))
    return 0


if __name__ == "__main__":
    sys.exit(main())
