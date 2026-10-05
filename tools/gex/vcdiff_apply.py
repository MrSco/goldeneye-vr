"""Apply a VCDIFF (RFC 3284) patch, as xdelta3 writes it without secondary compression.

usage: python vcdiff_apply.py source patch target
"""
import sys
import zlib

NOOP, ADD, RUN, COPY = 0, 1, 2, 3


def default_code_table():
    t = [(RUN, 0, 0, NOOP, 0, 0)]
    t += [(ADD, s, 0, NOOP, 0, 0) for s in range(0, 18)]
    for mode in range(9):
        t.append((COPY, 0, mode, NOOP, 0, 0))
        t += [(COPY, s, mode, NOOP, 0, 0) for s in range(4, 19)]
    for mode in range(6):
        for add in range(1, 5):
            for copy in range(4, 7):
                t.append((ADD, add, 0, COPY, copy, mode))
    for mode in range(6, 9):
        for add in range(1, 5):
            t.append((ADD, add, 0, COPY, 4, mode))
    for mode in range(9):
        t.append((COPY, 4, mode, ADD, 1, 0))
    assert len(t) == 256
    return t


class Reader:
    def __init__(self, data, pos=0, end=None):
        self.d, self.p = data, pos
        self.end = len(data) if end is None else end

    def byte(self):
        b = self.d[self.p]
        self.p += 1
        return b

    def varint(self):
        v = 0
        while True:
            b = self.byte()
            v = (v << 7) | (b & 0x7F)
            if not b & 0x80:
                return v

    def take(self, n):
        s = self.d[self.p:self.p + n]
        self.p += n
        return s


def apply(source, patch):
    r = Reader(patch)
    if r.take(4) != b"\xd6\xc3\xc4\x00":
        raise ValueError("not a VCDIFF file")
    hdr = r.byte()
    if hdr & 0x01:
        raise ValueError("secondary compression %d is not supported" % r.byte())
    if hdr & 0x02:
        raise ValueError("custom code tables are not supported")
    if hdr & 0x04:
        r.take(r.varint())   # xdelta3's application header (file names)
    table = default_code_table()
    out = bytearray()
    windows = 0
    while r.p < len(patch):
        win = r.byte()
        seg = b""
        if win & 0x03:
            size, pos = r.varint(), r.varint()
            seg = (source if win & 0x01 else out)[pos:pos + size]
            if len(seg) != size:
                raise ValueError("window %d: source segment out of range" % windows)
        r.varint()                      # length of the delta encoding
        tlen = r.varint()
        if r.byte():
            raise ValueError("window %d: compressed sections are not supported" % windows)
        dlen, ilen, alen = r.varint(), r.varint(), r.varint()
        checksum = int.from_bytes(r.take(4), "big") if win & 0x04 else None
        data = Reader(patch, r.p, r.p + dlen)
        inst = Reader(patch, r.p + dlen, r.p + dlen + ilen)
        addr = Reader(patch, r.p + dlen + ilen, r.p + dlen + ilen + alen)
        r.p += dlen + ilen + alen

        target = bytearray()
        near, near_i, same = [0] * 4, 0, [0] * (3 * 256)
        slen = len(seg)

        def copy_addr(mode):
            nonlocal near_i
            here = slen + len(target)
            if mode == 0:
                a = addr.varint()
            elif mode == 1:
                a = here - addr.varint()
            elif mode < 6:
                a = near[mode - 2] + addr.varint()
            else:
                a = same[(mode - 6) * 256 + addr.byte()]
            near[near_i] = a
            near_i = (near_i + 1) % 4
            same[a % (3 * 256)] = a
            return a

        while inst.p < inst.end:
            code = table[inst.byte()]
            for kind, size, mode in ((code[0], code[1], code[2]), (code[3], code[4], code[5])):
                if kind == NOOP:
                    continue
                if size == 0:
                    size = inst.varint()
                if kind == ADD:
                    target += data.take(size)
                elif kind == RUN:
                    target += data.take(1) * size
                else:
                    a = copy_addr(mode)
                    t0 = a - slen
                    if a + size <= slen:
                        target += seg[a:a + size]
                    elif t0 >= 0 and t0 + size <= len(target):
                        target += target[t0:t0 + size]
                    else:
                        for k in range(size):   # overlaps the target being written
                            u = a + k
                            target.append(seg[u] if u < slen else target[u - slen])
        if len(target) != tlen:
            raise ValueError("window %d: built %d bytes, expected %d" % (windows, len(target), tlen))
        if checksum is not None and zlib.adler32(bytes(target)) != checksum:
            raise ValueError("window %d: Adler-32 mismatch" % windows)
        out += target
        windows += 1
    return bytes(out), windows


if __name__ == "__main__":
    src = open(sys.argv[1], "rb").read()
    result, n = apply(src, open(sys.argv[2], "rb").read())
    open(sys.argv[3], "wb").write(result)
    print("%d windows, %d bytes written" % (n, len(result)))
