"""What the native tests' compilers can do, shared by the tests that need it.

sanitize(*flags): the given -fsanitize flags where the compiler links them,
else none. Linux's and Apple's compilers carry AddressSanitizer and
UndefinedBehaviorSanitizer; MinGW's gcc (Strawberry) has no libasan or
libubsan, so a test built there runs without them rather than failing to
link. A test always run with them keeps them wherever they work.

Each check runs once per run of a test: it builds an empty program with the
flags.

unused_stubs(directory, functions, data): see below; for tests that rely on
the linker dropping code they never run. load_library(path): ctypes.CDLL,
finding a Windows build's dependencies. host_libc(): the define for a test
that includes the game's ultra64.h beside the C library's string.h.
"""
from functools import lru_cache
from pathlib import Path
import os
import shutil
import subprocess
import tempfile


@lru_cache(maxsize=None)
def links(cc, flags):
    """cc builds and links an empty C program with flags (a tuple)"""
    with tempfile.TemporaryDirectory(prefix="gevr-cc-probe-") as temp:
        source, exe = Path(temp) / "probe.c", Path(temp) / "probe.exe"
        source.write_text("int main(void) { return 0; }\n", encoding="utf-8")
        result = subprocess.run([cc, *flags, str(source), "-o", str(exe)], capture_output=True)
        return result.returncode == 0


def sanitize(*flags, cc=None):
    """flags as given where cc (gcc by default) links them, else []"""
    cc = cc or shutil.which("gcc") or "cc"
    return list(flags) if links(cc, tuple(flags)) else []


def unused_stubs(directory, functions=(), data=()):
    """Callees and data of production code a test builds but never runs.

    A test that links with -ffunction-sections and -Wl,--gc-sections relies
    on the linker dropping that code with its references. GNU ld on ELF
    does; MinGW's ld for PE still fails on the undefined names. There, this
    writes a C file defining each name (a function that aborts if it is ever
    reached, or 256 zeroed bytes of data) and returns it, as a source to
    add to the build; elsewhere it returns []. Compiled as its own unit, it
    never meets the game's prototypes.
    """
    if os.name != "nt":
        return []
    path = Path(directory) / "unused_stubs.c"
    lines = ["#include <stdlib.h>", "/* gevr_testcc.unused_stubs: never reached in this test */"]
    lines += ["void %s(void) { abort(); }" % name for name in functions]
    lines += ["char %s[256];" % name for name in data]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return [str(path)]


_dll_directories = []


def load_library(path):
    """ctypes.CDLL(path) for a library a test built. On Windows, Python 3.8
    and later no longer find a DLL's own dependencies on PATH, so the
    compiler's folder (MinGW's runtime, zlib) is added to the search first."""
    import ctypes
    if os.name == "nt" and not _dll_directories:
        cc = shutil.which("gcc")
        if cc:
            _dll_directories.append(os.add_dll_directory(str(Path(cc).resolve().parent)))
    return ctypes.CDLL(str(path))


def host_libc():
    """-DGEVR_HOST_LIBC=1 where the C library declares bcopy, bcmp and bzero
    (glibc, Apple's), so include/PR/os.h leaves out its N64 ones, whose types
    differ; [] on Windows, whose C library has none."""
    return [] if os.name == "nt" else ["-DGEVR_HOST_LIBC=1"]
