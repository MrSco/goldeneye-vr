"""Check the production optic profile loader with generated files."""
import ctypes as c
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

from test_packoptic_runtime import optic_profile

ROOT = Path(__file__).resolve().parents[2]


def main():
    cc = shutil.which("gcc") or "cc"
    with tempfile.TemporaryDirectory(prefix="gevr-packoptic-parser-") as tmp:
        temp = Path(tmp)
        stub = temp / "log.c"
        stub.write_text("void sysLogPrintf(int level, const char *fmt, ...) {}\n", encoding="ascii")
        path = temp / ("optic.dll" if os.name == "nt" else "optic.so")
        subprocess.run([
            cc, "-std=c11", "-D_LANGUAGE_C=1", "-DGEVR=1", "-shared", "-fPIC", "-I", str(ROOT / "port/include"),
            "-I", str(ROOT / "include"), str(ROOT / "port/src/gevr_modoptic_profile.c"),
            str(stub), "-lm", "-o", str(path),
        ], check=True)
        lib = c.CDLL(str(path))
        parse = lib.gevrModOpticParse
        parse.argtypes = (c.c_void_p, c.c_ulong, c.c_char_p)
        parse.restype = c.c_void_p
        lib.gevrModOpticFree.argtypes = (c.c_void_p,)
        lib.gevrModOpticFree.restype = None

        data = optic_profile()
        profile = parse(data, len(data), b"generated")
        assert profile and c.c_int.from_address(profile).value == 2
        lib.gevrModOpticFree(profile)

        def reject(blob):
            assert parse(blob, len(blob), b"malformed") is None

        for cut in range(len(data)):
            reject(data[:cut])
        reject(data + b"x")
        for fmt, offset, value in [
            ("I", 8, 13),          # mode count
            ("I", 12, 1 << 12),    # unknown flags
            ("I", 16, 65),         # outline point count
            ("I", 20, 1025),       # reticle width
            ("f", 28, float("nan")),  # lens centre
            ("f", 28 + 13 * 4, 0),   # lens half size
            ("f", 28 + 18 * 4, 1),   # max zoom below min
            ("f", 28 + 19 * 4, 2),   # point outside half size
        ]:
            malformed = bytearray(data)
            struct.pack_into("<" + fmt, malformed, offset, value)
            reject(bytes(malformed))
        if os.name == "nt":
            import _ctypes
            _ctypes.FreeLibrary(lib._handle)
    print("PASS: GEVROPT4 profiles, every truncated length, trailing data and malformed fields")


if __name__ == "__main__":
    main()
