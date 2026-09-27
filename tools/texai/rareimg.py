"""
GoldenEye's non-zlib texture decompressor (src/game/image.c texInflateNonZlib
and its helpers), ported to Python for tools/texai/romkeys.py.

It rebuilds a texture's base image as the N64 left it in RDRAM: rows padded
as the C code pads them, 16/32-bit texels big-endian, odd rows word-swapped
per the header's LOD bits (texSwapAltRowBytes). Where the port and the N64
differ (byte reads of a u16 lookup table), the N64's behaviour is kept: the
texture pack's names were taken from the real game.

decode(src, head) -> Image or None, src being the bytes after the header byte.
"""

# per TEXFORMAT_* (image.c tables)
NUMCH = [4, 3, 3, 3, 2, 2, 1, 1, 1, 1, 1, 1, 1]
HAS1A = [0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0]
CHSIZE = [0x100, 0x20, 0x100, 0x20, 0x100, 0x10, 8, 0x100, 0x10, 0x100, 0x10, 0x100, 0x10]
BPP = [0x20, 0x10, 0x18, 0xF, 0x10, 8, 4, 8, 4, 0x10, 0x10, 0x10, 0x10]
GBIFMT = [0, 0, 0, 0, 3, 3, 3, 4, 4, 2, 2, 2, 2]     # G_IM_FMT_*
DEPTH = [3, 2, 3, 2, 2, 1, 0, 1, 0, 1, 0, 1, 0]      # G_IM_SIZ_*

RGBA32, RGBA16, RGB24, RGB15, IA16, IA8, IA4, I8, I4 = range(9)


class Bits:
    """texSetBitstring / texReadBits (image_bank.c): MSB first"""

    def __init__(self, data, pos=0):
        self.d, self.pos, self.acc, self.n = data, pos, 0, 0

    def read(self, count):
        while self.n < count:
            b = self.d[self.pos] if self.pos < len(self.d) else 0
            self.acc = ((self.acc << 8) | b) & 0xFFFFFFFF
            self.pos += 1
            self.n += 8
        self.n -= count
        return (self.acc >> self.n) & ((1 << count) - 1)


class Image:
    def __init__(self, fmt, w, h, data, stride, rgba):
        self.fmt, self.w, self.h = fmt, w, h
        self.gbifmt, self.siz = GBIFMT[fmt], DEPTH[fmt]
        self.data, self.stride, self.rgba = data, stride, rgba   # data: N64 byte order


def huffman(bits, numiter, chansize):
    """texInflateHuffman: a tree built from 8-bit frequencies, then numiter values"""
    freq = [bits.read(8) for _ in range(chansize)] + [0xFFFF] * (2048 - chansize)
    nodes = [[-1, -1] for _ in range(2048)]
    min1 = min2 = 9999
    i1 = i2 = 0
    for i in range(chansize):
        if freq[i] < min1:
            if min2 < min1:
                min1, i1 = freq[i], i
            else:
                min2, i2 = freq[i], i
        elif freq[i] < min2:
            min2, i2 = freq[i], i
    root = 0
    leaf = lambda k: nodes[k][0] < 0 and nodes[k][1] < 0
    while True:
        s = freq[i1] + freq[i2]
        if s == 0:
            s = 1
        s &= 0xFFFF
        freq[i1] = 9999
        freq[i2] = 9999
        if leaf(i1):
            nodes[i1][0] = i1 + 10000
            root = i1
            freq[i1] = s
            nodes[i1][1] = i2 + 10000 if leaf(i2) else i2
        elif leaf(i2):
            nodes[i2][0] = i2 + 10000
            root = i2
            freq[i2] = s
            nodes[i2][1] = i1 + 10000 if leaf(i1) else i1
        else:
            root = 0
            while nodes[root][0] >= 0 or nodes[root][1] >= 0 or freq[root] < 9999:
                root += 1
            freq[root] = s
            nodes[root][0] = i1
            nodes[root][1] = i2
        min1 = min2 = 9999
        for i in range(chansize):
            if freq[i] < min1:
                if min1 > min2:
                    min1, i1 = freq[i], i
                else:
                    min2, i2 = freq[i], i
            elif freq[i] < min2:
                min2, i2 = freq[i], i
        if min1 == 9999 or min2 == 9999:
            break
    out = []
    for _ in range(numiter):
        v = root
        while v < 10000:
            v = nodes[v][bits.read(1)]
        out.append(v - 10000)
    return out


def rle(bits, blockstotal):
    """texInflateRle"""
    bt = bits.read(3)
    rl = bits.read(3)
    bs = bits.read(4)
    cost = bt + rl + bs + 1
    fudge = 0
    while cost > 0:
        cost -= bs + 1
        fudge += 1
    out = []
    while len(out) < blockstotal:
        if bits.read(1) == 0:
            out.append(bits.read(bs))
        else:
            start = len(out) - bits.read(bt) - 1
            n = bits.read(rl) + fudge
            for i in range(start, start + n):
                out.append(out[i] if 0 <= i < len(out) else 0)
            out.append(bits.read(bs))
    return out


def build_lookup(bits, bpp):
    """texBuildLookup"""
    n = bits.read(11)
    if bpp <= 24:
        return [bits.read(bpp) for _ in range(n)], n
    return [(bits.read(24) << 8) | bits.read(bpp - 24) for _ in range(n)], n


def bitsize(n):
    """texGetBitSize"""
    c = 0
    n -= 1
    while n > 0:
        n >>= 1
        c += 1
    return c


def tdiv2(v):
    """C's v / 2 (towards zero)"""
    return -((-v) // 2) if v < 0 else v // 2


def blur(px, width, height, method, chansize):
    """texBlur, in place on the stacked channel planes"""
    for y in range(height):
        for x in range(width):
            cur = px[y * width + x] + chansize * 2
            left = px[y * width + x - 1] if x > 0 else 0
            above = px[(y - 1) * width + x] if y > 0 else 0
            al = px[(y - 1) * width + x - 1] if x > 0 and y > 0 else 0
            if method == 0:
                v = cur + left
            elif method == 1:
                v = cur + above
            elif method == 2:
                v = cur + al
            elif method == 3:
                v = cur + (left + above - al)
            elif method == 4:
                v = cur + (tdiv2(above - al) + left)
            elif method == 5:
                v = cur + (tdiv2(left - al) + above)
            elif method == 6:
                v = cur + tdiv2(left + above)
            else:
                continue
            px[y * width + x] = (v % chansize) & 0xFF


def rowspec(fmt, w):
    """(bytes per texel slot kind, row stride in bytes) as the C code pads rows"""
    if fmt in (RGBA32, RGB24):
        return ((w + 3) & 0xffc) * 4
    if fmt in (RGBA16, RGB15, IA16):
        return ((w + 3) & 0xffc) * 2
    if fmt in (IA8, I8):
        return (w + 7) & 0xff8
    return ((w + 15) & 0xff0) >> 1


class Writer:
    """the pool bytes, N64 byte order"""

    def __init__(self, size):
        self.b = bytearray(size)

    def u32(self, at, v):
        self.b[at:at + 4] = (v & 0xFFFFFFFF).to_bytes(4, 'big')

    def u16(self, at, v):
        self.b[at:at + 2] = (v & 0xFFFF).to_bytes(2, 'big')

    def u8(self, at, v):
        self.b[at] = v & 0xFF


def texels(fmt, w, h, get):
    """write w x h texel values get(x, y) at the format's stride"""
    stride = rowspec(fmt, w)
    out = Writer(stride * h + 16)
    for y in range(h):
        row = y * stride
        for x in range(w):
            v = get(x, y)
            if fmt in (RGBA32, RGB24):
                out.u32(row + x * 4, v)
            elif fmt in (RGBA16, RGB15, IA16):
                out.u16(row + x * 2, v)
            elif fmt in (IA8, I8):
                out.u8(row + x, v)
    return out, stride


def channels_to_pixels(src, w, h, fmt):
    """texChannelsToPixels: channel planes -> texels"""
    m = w * h
    at = lambda i: src[i] if 0 <= i < len(src) else 0
    if fmt in (RGBA32, RGB24, RGBA16, IA16, RGB15, IA8, I8):
        def get(x, y):
            p = y * w + x
            if fmt == RGBA32:
                return at(p) << 24 | at(p + m) << 16 | at(p + 2 * m) << 8 | at(p + 3 * m)
            if fmt == RGB24:
                return at(p) << 24 | at(p + m) << 16 | at(p + 2 * m) << 8 | 0xff
            if fmt == RGBA16:
                return at(p) << 11 | at(p + m) << 6 | at(p + 2 * m) << 1 | at(p + 3 * m)
            if fmt == IA16:
                return at(p) << 8 | at(p + m)
            if fmt == RGB15:
                return at(p) << 11 | at(p + m) << 6 | at(p + 2 * m) << 1 | 1
            if fmt == IA8:
                return at(p) << 4 | at(p + m)
            return at(p)
        return texels(fmt, w, h, get)
    # 4-bit: two texels a byte, pos stepping per the C (odd widths step back one)
    std = rowspec(fmt, w)
    rowstep = ((w + 15) & 0xff0) if fmt == IA4 else std   # IA4 advances a full (w+15)&~15 (the C does)
    out = Writer(rowstep * h + std + 16)
    pos = 0
    for y in range(h):
        row = y * rowstep
        for x in range(0, w, 2):
            if fmt == IA4:
                v = at(pos) << 5 | at(pos + m * 3) << 4 | at(pos + 1) << 1 | at(pos + m * 3 + 1)
            else:
                v = at(pos) << 4 | at(pos + 1)
            out.u8(row + (x >> 1), v)
            pos += 2
        if w & 1:
            pos -= 1
    out.rowstep = rowstep   # where the rows really are (the preview reads them there)
    return out, std


def lookup_texels(fmt, w, h, lut, n, idx):
    """texInflateLookup / texInflateLookupFromBuffer: idx(x, y) -> index"""
    at = lambda i: lut[i] if 0 <= i < len(lut) else 0
    if fmt in (IA4, I4):
        std = rowspec(fmt, w)
        out = Writer(std * h + 16)
        for y in range(h):
            for x in range(0, w, 2):
                v = (at(idx(x, y)) << 4) & 0xFF
                if x + 1 < w or idx.frombuffer:
                    v |= at(idx(x + 1, y)) & 0xFF   # the N64 reads the entry's low byte
                out.u8(y * std + (x >> 1), v)
        return out, std

    def get(x, y):
        v = at(idx(x, y))
        if fmt == RGB24:
            return (v << 8) | (0xff if idx.frombuffer else 0)
        if fmt == RGB15:
            return (v << 1) | 1
        return v
    return texels(fmt, w, h, get)


def swap_alt_rows(buf, w, h, fmt):
    """texSwapAltRowBytes on the N64 bytes (u32 word pairs)"""
    if fmt in (RGBA32, RGB24):
        aw = (w + 3) & 0xffc
    elif fmt in (RGBA16, RGB15, IA16):
        aw = ((w + 3) & 0xffc) >> 1
    elif fmt in (IA8, I8, 9, 11):
        aw = ((w + 7) & 0xff8) >> 2
    else:
        aw = ((w + 0xf) & 0xff0) >> 3
    b = buf.b
    row = aw
    for y in range(1, h, 2):
        if fmt in (RGBA32, RGB24):
            for x in range(0, aw, 4):
                for k in (0, 1):
                    a0, a1 = (row + x + k) * 4, (row + x + k + 2) * 4
                    if a1 + 4 <= len(b):
                        b[a0:a0 + 4], b[a1:a1 + 4] = b[a1:a1 + 4], b[a0:a0 + 4]
        else:
            for x in range(0, aw, 2):
                a0, a1 = (row + x) * 4, (row + x + 1) * 4
                if a1 + 4 <= len(b):
                    b[a0:a0 + 4], b[a1:a1 + 4] = b[a1:a1 + 4], b[a0:a0 + 4]
        row += aw * 2


def to_rgba(fmt, w, h, data, stride):
    """the texels as the renderer shows them (I: alpha = intensity)"""
    px = bytearray()
    for y in range(h):
        row = y * stride
        for x in range(w):
            if fmt in (RGBA32, RGB24):
                px += data[row + x * 4: row + x * 4 + 4]
            elif fmt in (RGBA16, RGB15):
                v = int.from_bytes(data[row + x * 2: row + x * 2 + 2], 'big')
                px += bytes(((v >> 11) * 255 // 31, ((v >> 6) & 31) * 255 // 31, ((v >> 1) & 31) * 255 // 31,
                             255 if v & 1 else 0))
            elif fmt == IA16:
                i, a = data[row + x * 2], data[row + x * 2 + 1]
                px += bytes((i, i, i, a))
            elif fmt == IA8:
                v = data[row + x]
                px += bytes(((v >> 4) * 17,) * 3 + ((v & 15) * 17,))
            elif fmt == I8:
                v = data[row + x]
                px += bytes((v, v, v, v))
            else:
                v = data[row + (x >> 1)]
                n = (v >> 4) if x % 2 == 0 else (v & 15)
                if fmt == IA4:
                    i = (n >> 1) * 0x24   # gfx_pc.cpp SCALE_3_8
                    px += bytes((i, i, i, 255 if n & 1 else 0))
                else:
                    px += bytes((n * 17,) * 4)
    return bytes(px)


def decode(src, head):
    """the base image of a non-zlib texture (header byte head, then src), or None"""
    explicit, lod = head >> 7, head & 0x3f
    bits = Bits(src)
    fmt = bits.read(4)
    w = bits.read(8)
    h = bits.read(8)
    method = bits.read(4)
    if fmt > I4 or w == 0 or h == 0 or w * h > 0x2000:
        return None
    raw_rgba_fmt = fmt
    if method in (0, 1):
        def get(x, y):
            if fmt == RGBA32:
                return (bits.read(16) << 16) | bits.read(16)
            if fmt == RGB24:
                return (bits.read(24) << 8) | 0xff
            if fmt in (RGBA16, IA16):
                return bits.read(16)
            if fmt == RGB15:
                return (bits.read(15) << 1) | 1
            return bits.read(8)
        if fmt in (IA4, I4):
            std = rowspec(fmt, w)
            out = Writer(std * h + 16)
            for y in range(h):
                for x in range(0, w, 2):
                    out.u8(y * std + (x >> 1), bits.read(8))
            stride = std
        else:
            out, stride = texels(fmt, w, h, get)
    elif method in (2, 3, 4, 8, 9):
        m = w * h
        if method == 3:
            planes = []
            for _ in range(NUMCH[fmt]):
                planes += huffman(bits, m, CHSIZE[fmt])
        elif method in (2, 8):
            if method == 8:
                stack = bits.read(3)
            planes = huffman(bits, NUMCH[fmt] * m, CHSIZE[fmt])
        else:
            if method == 9:
                stack = bits.read(3)
            planes = rle(bits, NUMCH[fmt] * m)
        planes = [v & 0xFF for v in planes] + [0] * max(0, 4 * m - len(planes))
        if method in (8, 9):
            blur(planes, w, NUMCH[fmt] * h, stack, CHSIZE[fmt])
        if HAS1A[fmt]:
            for i in range(m):
                planes[3 * m + i] = bits.read(1)
        out, stride = channels_to_pixels(planes, w, h, fmt)
    elif method in (5, 6, 7):
        lut, n = build_lookup(bits, BPP[fmt])
        if method == 5:
            bpc = bitsize(n)
            # indices read in order, a row at a time (4-bit: two a byte, the second only inside the row)
            seq = {}

            def idx(x, y):
                key = (x, y)
                if key not in seq:
                    seq[key] = bits.read(bpc)
                return seq[key]
            idx.frombuffer = False
            out, stride = lookup_texels(fmt, w, h, lut, n, idx)
        else:
            buf = huffman(bits, w * h, n) if method == 6 else rle(bits, w * h)

            def idx(x, y):
                i = y * w + x
                return buf[i] if 0 <= i < len(buf) else 0
            idx.frombuffer = True
            out, stride = lookup_texels(fmt, w, h, lut, n, idx)
    else:
        return None
    if (explicit and lod > 0) or (not explicit and lod >= 1):
        swap_alt_rows(out, w, h, fmt)
    data = bytes(out.b)
    # the preview reads the texels before the swap (the renderer unswizzles TMEM rows)
    pre = bytearray(data)
    if (explicit and lod > 0) or (not explicit and lod >= 1):
        tmp = Writer(0)
        tmp.b = pre
        swap_alt_rows(tmp, w, h, fmt)   # the swap is its own inverse
    return Image(fmt, w, h, data, stride, to_rgba(raw_rgba_fmt, w, h, bytes(pre), getattr(out, 'rowstep', stride)))
