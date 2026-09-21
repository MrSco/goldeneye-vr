"""Verify native level metadata conversion against a user-supplied ROM.

Usage: python tools/gevr_stage_probe.py path/to/GoldenEye.z64
Compiles the production converter; never writes extracted cartridge assets.
"""
import ctypes as C
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
from gevr_model_probe import load_rom, decompress_1172

ROOT = Path(__file__).resolve().parents[1]


class Room(C.Structure):
    _fields_ = [('points', C.c_void_p), ('primary', C.c_void_p),
                ('secondary', C.c_void_p), ('pos', C.c_float * 3)]


class Portal(C.Structure):
    _fields_ = [('points', C.c_void_p), ('fields', C.c_uint8 * 4)]


def main():
    rom = load_rom(sys.argv[1])
    manifest = (ROOT / 'port/src/gevr_rom_manifest.c').read_text()
    entries = re.findall(r'\{\s*0x([0-9a-fA-F]+),\s*(\d+),\s*\d\s*\},\s*/\*\s*\S+\s+(\S+)', manifest)
    totals = [0, 0, 0]
    with tempfile.TemporaryDirectory(prefix='gevr-stage-') as td:
        dll = Path(td) / ('stage.dll' if os.name == 'nt' else 'stage.so')
        compiler = r'C:\Strawberry\c\bin\gcc.exe' if os.name == 'nt' else 'gcc'
        subprocess.run([compiler, '-shared', '-fPIC', '-std=c11', '-Wall', '-Wextra',
                        '-I' + str(ROOT / 'port/include'), str(ROOT / 'port/src/gevr_stage.c'),
                        '-o', str(dll)], check=True)
        lib = C.CDLL(str(dll))
        for name in ('gevrConvertBg', 'gevrConvertStan'):
            fn = getattr(lib, name)
            fn.argtypes = [C.c_void_p, C.c_size_t, C.c_size_t]
            fn.restype = C.c_size_t
        lib.gevrBgHeaderSize.argtypes = [C.c_void_p]
        lib.gevrBgHeaderSize.restype = C.c_uint32
        try:
            for off, size, name in entries:
                isbg = name.startswith('bg/') and name.endswith('.seg')
                isstan = name.startswith('Tbg_')
                if not (isbg or isstan) or int(size) == 0:
                    continue
                off, size = int(off, 16), int(size)
                data = rom[off:off + size]
                if isstan:
                    data = decompress_1172(data)
                else:
                    length = lib.gevrBgHeaderSize(C.c_char_p(data[:64]))
                    assert 64 <= length <= len(data), (name, length)
                    data = data[:length]
                buf = C.create_string_buffer(len(data) * 4 + 4096)
                C.memmove(buf, data, len(data))
                fn = lib.gevrConvertBg if isbg else lib.gevrConvertStan
                assert fn(buf, len(data), len(data)) == 0, 'undersized destination accepted'
                C.memmove(buf, data, len(data))
                result = fn(buf, len(data), C.sizeof(buf) - 16)
                assert result, name
                assert buf.raw[-16:] == bytes(16), name
                base = C.addressof(buf)
                u32 = lambda ofs: struct.unpack_from('>I', data, ofs)[0]
                if isbg:
                    ro, po = u32(4) & 0xffffff, u32(8) & 0xffffff
                    newro, newpo = struct.unpack_from('=II', buf, 4)
                    rooms = C.cast(base + (newro & 0xffffff), C.POINTER(Room))
                    portals = C.cast(base + (newpo & 0xffffff), C.POINTER(Portal))
                    i = 0
                    while True:
                        src = struct.unpack_from('>IIIfff', data, ro + i*24)
                        d = rooms[i]
                        assert [d.points or 0, d.primary or 0, d.secondary or 0] == list(src[:3])
                        assert list(d.pos) == list(src[3:])
                        if i and not src[1]:
                            break
                        i += 1
                    for i in range(200):
                        ptr, *flags = struct.unpack_from('>IBBBB', data, po+i*8)
                        assert portals[i].points == (ptr or None) and list(portals[i].fields) == flags
                        if not ptr:
                            break
                        ofs = ptr & 0xffffff
                        n = data[ofs]*3
                        assert struct.unpack_from('='+str(n)+'f', buf, ofs+4) == struct.unpack_from('>'+str(n)+'f', data, ofs+4)
                    totals[0] += 1
                else:
                    first = u32(4)
                    native_first = C.c_void_p.from_address(base + C.sizeof(C.c_void_p)).value
                    shift = native_first-base-first
                    i = first
                    while u32(i):
                        n = struct.unpack_from('>H', data, i+6)[0] >> 12
                        assert buf.raw[shift+i+3] == data[i+3]
                        assert int.from_bytes(buf.raw[shift+i:shift+i+3], sys.byteorder) == u32(i)>>8
                        fields = 2 + max(n, 3)*4
                        assert struct.unpack_from('='+str(fields)+'H', buf, shift+i+4) == struct.unpack_from('>'+str(fields)+'H', data, i+4)
                        i += 8 + max(n, 3)*8
                        totals[2] += 1
                    totals[1] += 1
            print('PASS: %d BG headers, %d collision files, %d tiles; native pointers, fields, floats, canaries' % tuple(totals))
        finally:
            if os.name == 'nt':
                import _ctypes
                _ctypes.FreeLibrary(lib._handle)


if __name__ == '__main__':
    main()
