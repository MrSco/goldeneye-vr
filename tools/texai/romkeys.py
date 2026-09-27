#!/usr/bin/env python3
"""
Name ROM textures the way the texture pack does, without running the game,
and write the ones a pack lacks as a dump batch.py can take.

    python tools/texai/romkeys.py <rom.z64> <fork_dir> <out_dir> [--all]

The pack (GLideN64 "Rice" names) keys each texture by a checksum of the
texture as it sits in RDRAM when drawn (gfx_pc.cpp gevr_texpack_lookup). For
GoldenEye's zlib textures that memory is fully determined by the ROM
(image.c texInflateZlib):
  - palette indices, each row padded to 8 bytes (texAlignIndices);
  - odd rows with their 32-bit words swapped in pairs (texSwapAltRowBytes),
    unless the header's LOD count is 0 with no explicit LODs;
  - the palette after the image, which the game loads at an offset from the
    same image, so the pack's palette checksum covers the texture's own first
    bytes (GLideN64 hashes a TLUT load from its image's start).
The texture checksum covers width x height texels at the padded stride.

The textures in Rare's own scheme (texInflateNonZlib: huffman, RLE, lookup
tables, blur; RGBA, IA and I formats) go through rareimg.py, a port of that
decompressor, and are named the same way (no palette checksum). Checked
2026-09-27: 782 of 954 are names ge007.tdb lists, and the 54 of those also in
a headset texture dump decode to the very texels the game drew.

Every computed name is checked against the authors' texture database
(<fork_dir>/GOLDENEYE/ge007.tdb, every texture the game draws): only names it
lists are written, so a wrong guess about how a texture is drawn is dropped,
never generated.

Writes <out_dir>/<name> (RGBA as the renderer shows it) and
<out_dir>/index.tsv in the texture dump's format (gfx_pc.cpp
gevr_packdump_note). The wrap flags are a guess from the size (power-of-two
sides repeat, others clamp): the ROM does not hold them.
"""

import argparse
import os
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
sys.path.insert(0, TOOLS)
import gevr_model_probe as probe   # noqa: E402
import gevr_tex_decode as dec      # noqa: E402
import rareimg                      # noqa: E402

CI8 = (0x09, 0x0b)   # TEXFORMAT_RGBA16_CI8, TEXFORMAT_IA16_CI8
FMT_CI = 2


def nbyte(n64, h):
    """byte h of the emulator's RDRAM (native little-endian words): N64 byte h ^ 3"""
    i = h ^ 3
    return n64[i] if 0 <= i < len(n64) else 0


def rice(n64, start, w, h, size, stride):
    """GLideN64 RiceCRC32 over N64-order bytes (gfx_pc.cpp tp_rice)"""
    bpl = (w << size) >> 1
    crc = 0
    row = start
    for y in range(h - 1, -1, -1):
        e = 0
        x = bpl - 4
        while True:
            a = row + x
            word = nbyte(n64, a) | nbyte(n64, a + 1) << 8 | nbyte(n64, a + 2) << 16 | nbyte(n64, a + 3) << 24
            e = word ^ (x & 0xffffffff)
            crc = ((((crc << 4) | (crc >> 28)) & 0xffffffff) + e) & 0xffffffff
            x -= 4
            if x < 0:
                break
        crc = (crc + (e ^ y)) & 0xffffffff
        row += stride
    return crc


def decode_raw(rom, texnum, seg, offs):
    """(format, width, height, indices, palette, header byte) of the base image, or None"""
    src = rom[seg + offs[texnum]: seg + offs[texnum + 1] + 16]
    head = src[0]
    if not (head >> 6) & 1:
        return None
    bits = dec.Bits(src, 1)
    fmt = bits.read(8)
    ncol = bits.read(8) + 1
    pal = [bits.read(16) for _ in range(ncol)]
    if fmt not in dec.FORMATS:
        return None
    w, h = bits.read(8), bits.read(8)
    raw = zlib.decompressobj(-15).decompress(bytes(src[bits.pos + 2:]))
    ci8 = fmt in CI8
    if ci8:
        idx = list(raw[:w * h])
    else:
        idx = []
        for b in raw[:(w * h + 1) // 2]:
            idx += [b >> 4, b & 15]
        idx = idx[:w * h]
    return fmt, w, h, idx, pal, head


def pool_bytes(fmt, w, h, idx, head):
    """the base image as texInflateZlib leaves it in the pool (N64 byte order)"""
    ci8 = fmt in CI8
    rowbytes = w if ci8 else (w + 1) // 2
    stride = (rowbytes + 7) & ~7
    out = bytearray(stride * h)
    for y in range(h):
        r = idx[y * w:(y + 1) * w]
        if ci8:
            out[y * stride:y * stride + w] = bytes(r)
        else:
            for x in range(0, w, 2):
                out[y * stride + x // 2] = (r[x] << 4) | (r[x + 1] if x + 1 < w else 0)
    explicit, lod = head >> 7, head & 0x3f
    if (explicit and lod > 0) or (not explicit and lod >= 1):
        # texSwapAltRowBytes: alignedwidth words per row; swap word pairs on odd rows
        words = (((w + 7) & 0xff8) >> 2) if ci8 else (((w + 0xf) & 0xff0) >> 3)
        for y in range(1, h, 2):
            base = y * words * 4
            for x in range(0, words, 2):
                a, b = base + x * 4, base + x * 4 + 4
                if b + 4 <= len(out):
                    out[a:a + 4], out[b:b + 4] = out[b:b + 4], out[a:a + 4]
    return bytes(out), stride


def names_for(fmt, w, h, idx, pal, head):
    data, stride = pool_bytes(fmt, w, h, idx, head)
    siz = 1 if fmt in CI8 else 0
    tex = rice(data, 0, w, h, siz, stride)
    cimax = max(idx) if idx else 0
    palcrc = rice(data, 0, cimax + 1, 1, 2, 512)
    return 'GOLDENEYE#%08X#%d#%d#%08X_ciByRGBA.png' % (tex, FMT_CI, siz, palcrc), tex, siz


def rgba(fmt, idx, pal):
    kind = dec.FORMATS[fmt][0]
    px = bytearray()
    for i in idx:
        px += bytes(dec.colour(kind, pal[i] if i < len(pal) else 0))
    return bytes(px)


def named(rom, t, seg, offs):
    """(pack name, G_IM_FMT, G_IM_SIZ, w, h, RGBA texels, texture checksum) of
    texture t's base image, zlib (paletted) or Rare's own scheme (rareimg.py)"""
    src = rom[seg + offs[t]: seg + offs[t + 1] + 16]
    if (src[0] >> 6) & 1:
        r = decode_raw(rom, t, seg, offs)
        if r is None:
            return None
        fmt, w, h, idx, pal, head = r
        if w == 0 or h == 0:
            return None
        name, crc, siz = names_for(fmt, w, h, idx, pal, head)
        return name, FMT_CI, siz, w, h, rgba(fmt, idx, pal), crc
    img = rareimg.decode(bytes(src[1:]), src[0])
    if img is None:
        return None
    crc = rice(img.data, 0, img.w, img.h, img.siz, img.stride)
    name = 'GOLDENEYE#%08X#%d#%d_all.png' % (crc, img.gbifmt, img.siz)
    return name, img.gbifmt, img.siz, img.w, img.h, img.rgba, crc


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rom')
    ap.add_argument('fork_dir')
    ap.add_argument('out_dir')
    ap.add_argument('--all', action='store_true', help='write every matched texture, even ones the fork has')
    a = ap.parse_args()

    from PIL import Image
    tdb = {}
    with open(os.path.join(a.fork_dir, 'GOLDENEYE', 'ge007.tdb'), encoding='utf-8') as f:
        for line in f:
            if ';' in line:
                n, size = line.strip().split(';')
                tdb[n.upper() + '.PNG'] = size
    tdb_tex = {}
    for n in tdb:
        p = n.split('#')
        tdb_tex.setdefault((p[1], p[2], p[3][:1]), []).append(n)
    have = set()
    for root, _, files in os.walk(os.path.join(a.fork_dir, 'GOLDENEYE')):
        have.update(x.upper() for x in files if x.lower().endswith('.png'))

    rom = probe.load_rom(a.rom)
    seg, _ = dec.images_segment()
    offs = dec.texture_offsets()
    os.makedirs(a.out_dir, exist_ok=True)
    index = open(os.path.join(a.out_dir, 'index.tsv'), 'w', encoding='utf-8', newline='\n')
    stats = dict(decoded=0, exact=0, texonly=0, none=0, inpack=0, written=0)
    for t in range(len(offs) - 1):
        try:
            got = named(rom, t, seg, offs)
        except Exception:
            got = None
        if got is None:
            continue
        name, gfmt, siz, w, h, px, crc = got
        stats['decoded'] += 1
        if name.upper() in tdb:
            stats['exact'] += 1
        elif ('%08X' % crc, str(gfmt), str(siz)) in tdb_tex:
            stats['texonly'] += 1     # the texture, drawn with another palette or tile
            continue
        else:
            stats['none'] += 1
            continue
        if name.upper() in have:
            stats['inpack'] += 1
            if not a.all:
                continue
        Image.frombytes('RGBA', (w, h), px).save(os.path.join(a.out_dir, name))
        pow2 = lambda v: v & (v - 1) == 0
        cms = 0 if pow2(w) else 2
        cmt = 0 if pow2(h) else 2
        masks = w.bit_length() - 1 if pow2(w) else 0
        maskt = h.bit_length() - 1 if pow2(h) else 0
        # level -1: the ROM does not say where a texture is drawn; the texture number follows
        index.write('%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n' % (name, w, h, gfmt, siz, cms, cmt, masks, maskt, -1, t))
        stats['written'] += 1
    index.close()
    print(stats)
    return 0


if __name__ == '__main__':
    sys.exit(main())
