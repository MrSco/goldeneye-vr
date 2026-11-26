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



def test_input(cc, temp):
    """An interrupted stick click must not resume after the wheel releases it."""
    source = (ROOT / "src/game/gevr_modoptic.c").read_text(encoding="utf-8")
    start = source.index("int gevrModOpticInput(")
    end = source.index("\n}\n", start) + 3
    fixture = temp / "optic_input.c"
    fixture.write_text("""
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
typedef uint32_t u32;
enum { FALSE, TRUE };
static int s_focus, g_gevrStereo = TRUE, cycles, zooms;
static u32 s_focusFrame;
static struct { u32 frameCount; } os_scheduler;
static void *bindingFor(int hand) { static int binding; (void)hand; return &binding; }
static int gevrModAttachGunFrame(int hand, void *a, void *b) { (void)hand; (void)a; (void)b; return TRUE; }
static int canAdjust(int hand) { (void)hand; return TRUE; }
static void cycle(int hand) { (void)hand; ++cycles; }
static int adjustZoom(int hand, float y) { (void)hand; assert(y > 0); ++zooms; return TRUE; }
""" + source[start:end] + """
static int input(int enabled, int click, float y)
{
    s_focusFrame = ++os_scheduler.frameCount;
    return gevrModOpticInput(enabled, click, y);
}
int main(void)
{
    assert(input(1, 1, 0) && input(1, 0, 0) && cycles == 1);
    assert(input(1, 1, 0) && input(1, 1, 1) && zooms == 1);
    assert(!input(0, 1, 1)); /* the wheel opens while zooming */
    assert(!input(1, 1, 1) && !input(1, 0, 0) && cycles == 1 && zooms == 1);
    assert(!input(0, 1, 1)); /* a click starts inside the wheel */
    assert(!input(1, 1, 1) && !input(1, 0, 0) && cycles == 1 && zooms == 1);
    assert(input(1, 1, 0) && input(1, 0, 0) && cycles == 2);
    return 0;
}
""", encoding="ascii")
    executable = temp / ("optic_input.exe" if os.name == "nt" else "optic_input")
    subprocess.run([cc, "-std=c11", str(fixture), "-lm", "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
    print("PASS: optic click and zoom cancel when an input owner takes the stick")


def main():
    cc = shutil.which("gcc") or "cc"
    with tempfile.TemporaryDirectory(prefix="gevr-packoptic-parser-") as tmp:
        temp = Path(tmp)
        test_input(cc, temp)
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
