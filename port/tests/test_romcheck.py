"""Production ROM checks and bounded resource decoding for crash 109c2b77.

All cartridge data in this test is synthetic. --verify-baseline demonstrates
that v0.4.18's launcher accepts the damaged asset which the new probe rejects.
"""
import argparse
import ctypes as C
from pathlib import Path
import random
import shutil
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[2]
ROM_SIZE = 12 * 1024 * 1024


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def compressed(data, level=6, strategy=zlib.Z_DEFAULT_STRATEGY):
    codec = zlib.compressobj(level, zlib.DEFLATED, -15, strategy=strategy)
    return b"\x11\x72" + codec.compress(data) + codec.flush()


def build_probe(directory, launcher, name):
    source = directory / (name + ".cpp")
    source.write_text('''#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <utility>
#include "gevr_romcheck.h"
const long kRomSize = 12582912;
struct RomInfo { std::string path, status; bool good = false; };
''' + function(launcher, "RomInfo probeRom(") + '''
extern "C" int probe(const char *path, char *error, size_t size) {
    RomInfo info = probeRom(path);
    snprintf(error, size, "%s", info.status.c_str());
    return info.good;
}
''')
    library = directory / (name + ".so")
    subprocess.run([shutil.which("g++") or "g++", "-std=c++17", "-O2",
                    "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC",
                    "-I" + str(ROOT / "port/include"), str(source),
                    str(directory / "romcheck.o"), str(directory / "manifest.o"),
                    "-lz", "-o", str(library)], check=True)
    lib = C.CDLL(str(library))
    lib.probe.argtypes = [C.c_char_p, C.c_void_p, C.c_size_t]
    return lib


def run(directory, verify_baseline):
    compiler = shutil.which("gcc") or "gcc"
    # Link the actual cartridge decoder alongside zlib, as the Android app does.
    # An exported legacy inflate(void) silently hijacks zlib's inflate(stream, flush).
    (directory / "ultra64.h").write_text('''#include <stddef.h>
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t s32;
''')
    legacy = directory / "legacy.o"
    subprocess.run([compiler, "-std=c11", "-O2", "-fPIC",
                    "-Wno-pointer-to-int-cast", "-Wno-int-conversion",
                    "-I" + str(directory), "-c", str(ROOT / "src/inflate/inflate.c"),
                    "-o", str(legacy)], check=True)
    for name, path in (("romcheck", "port/src/gevr_romcheck.c"),
                       ("manifest", "port/src/gevr_rom_manifest.c")):
        subprocess.run([compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-fPIC", "-I" + str(ROOT / "port/include"), "-c", str(ROOT / path),
                        "-o", str(directory / (name + ".o"))], check=True)
    library = directory / "romcheck.so"
    subprocess.run([compiler, "-shared", str(directory / "romcheck.o"),
                    str(directory / "manifest.o"), str(legacy),
                    "-lz", "-o", str(library)], check=True)
    lib = C.CDLL(str(library))
    lib.gevrInflate1172.argtypes = [C.c_void_p, C.c_uint, C.c_void_p, C.c_uint,
                                   C.POINTER(C.c_uint)]
    lib.gevrRomNormaliseOrder.argtypes = [C.c_void_p, C.c_uint]
    lib.gevrRomCheckFiles.argtypes = [C.c_void_p, C.c_uint, C.c_void_p, C.c_size_t]

    def decode(blob, capacity, expected=None, overlap=False):
        guard = 32
        allocation = guard + capacity + len(blob) + guard
        output = C.create_string_buffer(b"\xa5" * allocation, allocation)
        address = C.addressof(output) + guard
        if overlap:
            assert len(blob) <= capacity
            src = address + capacity - len(blob)
            C.memmove(src, blob, len(blob))
        else:
            source = C.create_string_buffer(blob)
            src = C.addressof(source)
        written = C.c_uint(123)
        result = lib.gevrInflate1172(src, len(blob), address, capacity, C.byref(written))
        assert output.raw[:guard] == b"\xa5" * guard
        assert output.raw[guard + capacity:] == b"\xa5" * (len(blob) + guard)
        if expected is None:
            assert result != 0 and written.value == 0, (result, written.value)
        else:
            assert result == 0 and written.value == len(expected), (result, written.value)
            assert output.raw[guard:guard + written.value] == expected

    rng = random.Random(109)
    data = bytes(rng.randrange(256) for _ in range(70000))
    for payload in (b"", b"hello language bank", data, b"GoldenEye VR " * 10000):
        for level, strategy in ((0, zlib.Z_DEFAULT_STRATEGY),
                                (6, zlib.Z_FIXED), (6, zlib.Z_DEFAULT_STRATEGY)):
            blob = compressed(payload, level, strategy)
            decode(blob, len(payload), payload)
            decode(blob + b"\0" * 16, len(payload) + 16, payload)
            if len(blob) < len(payload):
                decode(blob, len(payload), payload, overlap=True)
            if payload:
                decode(blob, len(payload) - 1)
            decode(blob[:-1], len(payload))
    # The report's bad header, invalid BTYPE, stored LEN/NLEN mismatch,
    # missing final block, and an invalid backward distance.
    for blob in (b"\xb8\x8d" + bytes(1822), b"\x11\x72\x07",
                 b"\x11\x72\x01\x01\x00\x00\x00A",
                 b"\x11\x72" + bytes(64), b"\x11\x72\x03\x02\x00"):
        decode(blob, 1024)
    assert not lib.gevrRomNormaliseOrder(None, 0)
    for size in range(4):
        assert not lib.gevrRomNormaliseOrder(C.create_string_buffer(b"\x80\x37\x12\x40"), size)
    print("bounded decoding with linked legacy decoder: stored/fixed/dynamic, exact capacity, overlap, padding and malformed input passed")

    fixture = (ROOT / "port/tests/rom_resource_native.c").read_text()
    resource_source = directory / "resource.c"
    resource_source.write_text(fixture.replace("/* INSERT_RESOURCE */", function(
        (ROOT / "src/game/ob.c").read_text(), "void load_resource(")))
    resource_library = directory / "resource.so"
    subprocess.run([compiler, "-std=c11", "-O2", "-shared", "-fPIC", "-DGEVR=1",
                    "-I" + str(ROOT / "port/include"), str(resource_source),
                    str(directory / "romcheck.o"), str(directory / "manifest.o"),
                    "-lz", "-o", str(resource_library)], check=True)
    resource = C.CDLL(str(resource_library))
    resource.test_load.argtypes = [C.c_void_p, C.c_uint, C.c_void_p, C.c_int,
                                  C.POINTER(C.c_uint)]
    resource.test_diagnostic.restype = C.c_char_p

    def load(blob, capacity, expected=None, copied=True):
        source = C.create_string_buffer(blob)
        output = C.create_string_buffer(b"\xa5" * (capacity + 32), capacity + 32)
        written = C.c_uint(0)
        good = resource.test_load(source, len(blob), output, capacity, C.byref(written))
        assert output.raw[capacity:] == b"\xa5" * 32
        assert resource.test_copies() == int(copied)
        assert resource.test_conversions() == int(expected is not None)
        if expected is not None:
            assert good and written.value == len(expected)
            assert output.raw[:written.value] == expected
        else:
            assert not good and written.value == 0
            assert b"LtestE" in resource.test_diagnostic()

    payload = b"synthetic language bank " * 100
    load(compressed(payload), len(payload), payload)
    load(b"\xb8\x8d" + bytes(1822), 8192)
    load(compressed(payload), 128)  # fits compressed data, but not expanded output
    load(compressed(payload), 16, copied=False)
    load(b"\x11\x72" + bytes(9), 19, copied=False)  # rounded compressed size needs space
    print("resource loader: valid bank converted; bad input/insufficient space rejected before conversion")

    class File(C.Structure):
        _fields_ = [("offset", C.c_uint), ("size", C.c_uint), ("compressed", C.c_uint)]
    count = C.c_uint.in_dll(lib, "g_GevrRomFileCount").value
    files = (File * count).in_dll(lib, "g_GevrRomFiles")
    rom = bytearray(ROM_SIZE)
    rom[:4] = b"\x80\x37\x12\x40"
    rom[0x20:0x29] = b"GOLDENEYE"
    rom[0x3b:0x3f] = b"NGEE"
    for file in files:
        if file.offset and file.compressed:
            blob = compressed(b"test")
            assert len(blob) <= file.size
            rom[file.offset:file.offset + len(blob)] = blob
    launcher = (ROOT / "port/vr/vr_launcher.cpp").read_text()
    probe = build_probe(directory, launcher, "probe")
    path = directory / "synthetic.z64"
    error = C.create_string_buffer(256)

    def check(image, good):
        path.write_bytes(image)
        assert probe.probe(str(path).encode(), error, len(error)) == good, error.value

    check(rom, 1)
    for stride in (2, 4):
        swapped = bytearray(rom)
        for i in range(0, len(swapped), stride):
            swapped[i:i + stride] = swapped[i:i + stride][::-1]
        check(swapped, 1)
    damaged = bytearray(rom)
    damaged[0x8ed250:0x8ed252] = b"\xb8\x8d"  # LgunE: first langInit bank
    check(damaged, 0)
    assert b"damaged or incompatible" in error.value
    check(rom[:-1], 0)
    malformed = bytearray(rom)
    malformed[0x8ed252:0x8ed255] = b"\x07\0\0"
    check(malformed, 0)
    # A later file must be checked too, not just the initial language banks.
    last = next(file for file in reversed(files) if file.offset and file.compressed)
    damaged_later = bytearray(rom)
    damaged_later[last.offset:last.offset + 2] = b"\0\0"
    check(damaged_later, 0)
    print("launcher: synthetic valid USA z64/v64/n64 accepted; damaged and malformed assets rejected")
    if verify_baseline:
        baseline = subprocess.check_output(["git", "show", "869d9b8:port/vr/vr_launcher.cpp"],
                                           cwd=ROOT, text=True)
        old_probe = build_probe(directory, baseline, "baseline")
        path.write_bytes(damaged)
        assert old_probe.probe(str(path).encode(), error, len(error)) == 1
        print("v0.4.18 baseline accepts the report-shaped invalid LgunE asset, as expected")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify-baseline", action="store_true")
    options = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="gevr-romcheck-") as temporary:
        run(Path(temporary), options.verify_baseline)
