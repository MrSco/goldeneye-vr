"""Run the real blood decoder on the host; no ROM data is copied into this test.

Requires a C compiler (gcc by default). Builds a temporary shared library with
only ultra64's integer typedefs stubbed, then checks every authored frame.
"""
import ctypes as C
from contextlib import ExitStack
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
U8 = C.c_uint8
PTR = C.POINTER(U8)
PIXELS = 80 * 96


def reference_frame(data, offset):
    leading = data[offset]
    offset += 1
    pixels = []
    while len(pixels) < PIXELS:
        cmd = data[offset]
        offset += 1
        if cmd == 255:
            row = []
            value = 255
            while data[offset] != 255:
                row.extend([value] * data[offset])
                value ^= 255
                offset += 1
            offset += 1
            assert len(row) <= 80
            pixels.extend(row + [value] * (80 - len(row)))
        else:
            filled = leading + (cmd & 31)
            assert filled <= 80
            pixels.extend(([255] * filled + [0] * (80 - filled)) * ((cmd >> 5) + 1))
    assert len(pixels) == PIXELS
    return offset, bytes(pixels)


def main():
    source = (ROOT / 'src/game/blood_animation.c').read_text()
    array = source.split('u8 die_blood_image_1[] = {', 1)[1].split('};', 1)[0]
    data = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]+)', array))
    # Verify the production call passes the number of packed output bytes.
    call = re.search(r'sub_GAME_7F01CC94\([^;]+;', source).group()
    assert 'BLOOD_IMG_WIDTH * BLOOD_IMG_HEIGHT / 2' in call, 'I4 packer call overreads its input'
    with tempfile.TemporaryDirectory(prefix='gevr-blood-') as tmp, ExitStack() as cleanup:
        tmp = Path(tmp)
        (tmp / 'ultra64.h').write_text(
            '#include <stdint.h>\n'
            'typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32;\n'
            'typedef int16_t s16; typedef int32_t s32;\n')
        libpath = tmp / ('blood.dll' if os.name == 'nt' else 'blood.so')
        subprocess.run([os.environ.get('CC', 'gcc'), '-shared', '-O1', '-g',
                        '-Wall', '-Wextra', '-I', str(tmp),
                        str(ROOT / 'src/game/blood_decrypt.c'), '-o', str(libpath)], check=True)
        lib = C.CDLL(str(libpath))
        if os.name == 'nt':
            import _ctypes
            cleanup.callback(_ctypes.FreeLibrary, lib._handle)
        decode = lib.decrypt_bleeding_animation_data
        decode.argtypes = [PTR, U8, U8, PTR, PTR]
        decode.restype = PTR
        lib.bloodImgTranspose.argtypes = [PTR, C.c_int32, C.c_int32, PTR]
        lib.sub_GAME_7F01CC94.argtypes = [PTR, C.c_uint16, PTR]
        for name in ('sub_GAME_7F01D02C', 'sub_GAME_7F01CEEC'):
            getattr(lib, name).argtypes = [PTR, C.c_int32, PTR]
        stream = (U8 * len(data)).from_buffer_copy(data)
        offset = 0
        frame = 0
        coverage = []
        while offset < len(data):
            end, expected = reference_frame(data, offset)
            decoded = (U8 * (PIXELS + 32))(*([0xA5] * (PIXELS + 32)))
            marker = U8()
            next_ptr = decode(C.cast(C.byref(stream, offset), PTR), 80, 96, decoded, C.byref(marker))
            assert C.cast(next_ptr, C.c_void_p).value - C.addressof(stream) == end
            assert bytes(decoded[:PIXELS]) == expected
            assert bytes(decoded[PIXELS:]) == bytes([0xA5] * 32)
            transposed = (U8 * (PIXELS + 32))(*([0xA5] * (PIXELS + 32)))
            lib.bloodImgTranspose(decoded, 80, 96, transposed)
            assert all(transposed[x * 96 + y] == expected[y * 80 + x]
                       for y in range(96) for x in range(80))
            for name in ('sub_GAME_7F01D02C', 'sub_GAME_7F01CEEC'):
                getattr(lib, name)(transposed, 80, transposed)
            assert bytes(transposed[PIXELS:]) == bytes([0xA5] * 32)
            before = bytes(transposed)
            packed = bytes((before[i] & 0xF0) | (before[i + 1] >> 4)
                           for i in range(0, PIXELS, 2))
            lib.sub_GAME_7F01CC94(transposed, PIXELS // 2, transposed)
            assert bytes(transposed[:PIXELS // 2]) == packed
            assert bytes(transposed[PIXELS // 2:]) == before[PIXELS // 2:]
            coverage.append(sum(v != 0 for v in expected))
            offset = end
            frame += 1
        assert frame == 42 and offset == len(data)
        assert coverage[0] == 41 and coverage[-1] == PIXELS
        print(f'PASS: {frame} frames, {offset} stream bytes; decode, transpose, filtering, I4 packing and canaries')
        print(f'Mask coverage: {coverage[0]}/{PIXELS} -> {coverage[-1]}/{PIXELS}')


if __name__ == '__main__':
    main()
