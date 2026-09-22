"""Check the production TLUT decoder against every IA16/RGBA16 value; no ROM."""
import ctypes
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    source = (ROOT / "port/fast3d/gfx_pc.cpp").read_text()
    start = source.index("static inline void palette_to_rgba32(")
    end = source.index("static void import_texture_ci4(", start)
    decoder = source[start:end]
    harness = """
#include <stdint.h>
#define G_TT_IA16 49152
#define SCALE_5_8(x) (((x) * 255) / 31)
static struct { unsigned palette_fmt; } rdp;
""" + decoder + """
void decode(unsigned format, uint16_t entry, uint8_t *out) {
    rdp.palette_fmt = format;
    palette_to_rgba32(entry, out);
}
"""
    with tempfile.TemporaryDirectory(prefix="gevr-palette-") as tmp:
        tmp = Path(tmp)
        code = tmp / "palette.c"
        code.write_text(harness)
        library = tmp / ("palette.dll" if os.name == "nt" else "palette.so")
        compiler = r"C:\Strawberry\c\bin\gcc.exe" if os.name == "nt" else "cc"
        subprocess.run([compiler, "-shared", "-fPIC", "-O2", str(code),
                        "-o", str(library)], check=True)
        lib = ctypes.CDLL(str(library))
        lib.decode.argtypes = [ctypes.c_uint, ctypes.c_uint16,
                               ctypes.POINTER(ctypes.c_uint8)]
        out = (ctypes.c_uint8 * 4)()
        for entry in range(65536):
            lib.decode(49152, entry, out)
            # Cartridge IA16 bytes: intensity, alpha (same as direct IA16).
            intensity, alpha = entry.to_bytes(2, "big")
            assert list(out) == [intensity] * 3 + [alpha], (hex(entry), list(out))
            lib.decode(32768, entry, out)
            expected = [((entry >> shift) & 31) * 255 // 31 for shift in (11, 6, 1)]
            expected += [255 if entry & 1 else 0]
            assert list(out) == expected, (hex(entry), list(out))
        if os.name == "nt":
            import _ctypes
            _ctypes.FreeLibrary(lib._handle)
    print("PASS: all 65,536 IA16 and 65,536 RGBA16 entries")


if __name__ == "__main__":
    main()
