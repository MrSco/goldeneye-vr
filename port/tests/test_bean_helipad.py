"""Authored CAFF coverage for Bunker's shader-only helipad overlay.

Optional argument: a previous gevr_bean_load.c, to verify the failure before repair.
No game data is used.
"""
from pathlib import Path
import os
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def words(*values):
    return struct.pack(">" + "I" * len(values), *values)


def record(kind, payload):
    return words(((len(payload) + 4) << 16) | kind << 8) + payload


def helipad(stride, colour=0xFFFFFFFF, own=False, isolated=False, blended=False):
    points = [(0, 0, 0), (100, 0, 0), (0, 0, 100)]
    gpu = bytearray()
    for i, p in enumerate(points):
        gpu += struct.pack(">3fIhhI", *p, 0, 1024 if i == 1 else 0,
                           1024 if i == 2 else 0, 0xFF606060)
    overlay = len(gpu)
    for i, p in enumerate(points):
        gpu += struct.pack(">3f", p[0] + (200 if isolated else 0), p[1], p[2])
        if stride >= 16:
            gpu += words(0)  # normal
        if stride == 20:
            gpu += words(colour)
        if stride == 24:
            gpu += struct.pack(">hhI", 0, 0, colour)
    indices = len(gpu)
    gpu += struct.pack(">3H", 0, 1, 2)
    gpu += bytes(0x200 - len(gpu))
    gpu += bytes([0x40, 0x60, 0x80, 255]) * 1024
    data = bytearray(0x160)
    data[0x20:0x30] = words(24, 0, 0, 72)
    data[0x40:0x50] = words(stride, 0, overlay, stride * 3)
    data[0x60:0x70] = words(0x99, indices, 6, 1)
    texture = bytearray(0x60)
    texture[:8] = b"texture\0"
    texture[0x1B] = 6  # RGBA8, tiled pixels
    texture[0x1C:0x20] = words(4)
    texture[0x44:0x50] = words(0x50, 0x54, 0x58)
    texture[0x50:0x5C] = words(4, 8, 1)
    data[0x100:0x160] = texture
    draw = record(1, words(4, 3, 0x99))
    material = record(0x2D, words(0, 0, 0, 0))
    stream = bytearray(0x24)
    stream += record(2, words(0x1000)) + material + record(0x2E, words(0, 0x20)) + draw
    # A new vertex shader with no material of its own inherits the preceding
    # texture in the old importer. This is the helipad's shader-only draw.
    stream += record(2, words(0x3958))
    if own:
        stream += material
    if blended:
        stream += record(0x1A, words(2))
    stream += record(0x2E, words(0, 0x40)) + draw
    stream[4:8] = words(len(stream))
    sections = [(".data", data), (".gpu", gpu), (".stream", stream)]
    names, table = bytearray(), bytearray()
    for name, blob in sections:
        entry = bytearray(0x21)
        entry[:4], entry[9:13] = words(len(names)), words(len(blob))
        table += entry
        names += name.encode() + b"\0"
    files = words(7, 0, 2) + b"x\0tile\0" + words(0)
    for i, (_, blob) in enumerate(sections):
        files += words(1, 0, len(blob)) + bytes([i + 1, 0])
    files += words(2, 0x100, 0x60) + bytes([1, 0])
    files += words(2, 0x200, 4096) + bytes([2, 0])
    header = bytearray(0x68)
    header[:4] = b"CAFF"
    header[0x14:0x18], header[0x1C:0x24] = words(0x68), words(2, 5)
    header[0x48:0x4A] = bytes([1, 3])
    header[0x4C:0x54] = words(len(names), len(table) + len(names))
    header[0x64:0x68] = words(len(files))
    return header + table + names + files + b"".join(blob for _, blob in sections)


DRIVER = r'''
#include "gevr_bean_int.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
void videoFreeCachedTexture(const void *p) { (void)p; }
int main(int argc, char **argv) {
    assert(argc==4);
    struct gevrBeanMesh *m=NULL;
    assert(gevrBeanLoad(argv[1],1,&m) && m);
    assert(m->ntex==1 && m->standin[0] && !m->texCut[0]);
    assert(m->ntris==atoi(argv[2]));
    assert(m->tris[0].tex==0 && m->tris[0].rgba[0]==0x60);
    assert(!(m->tris[0].flags & GEVR_BEAN_F_DECAL));
    if(m->ntris>1) assert(m->tris[1].tex==atoi(argv[3]));
    gevrBeanMeshFree(m);gevrBeanTexReset();
    return 0;
}
'''


with tempfile.TemporaryDirectory(prefix="bean-helipad-") as temporary:
    directory = Path(temporary)
    files = directory / "new/background/bunker"
    files.mkdir(parents=True)
    source = directory / "test.c"
    source.write_text(DRIVER)
    exe = directory / "helipad"
    loader = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "port/src/gevr_bean_load.c"
    sanitize = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-no-pie"] if os.environ.get("BEAN_SANITIZE") else []
    subprocess.run([
        shutil.which("gcc") or "cc", "-std=c11", "-O2", "-include", "stdarg.h", *sanitize,
        "-I" + str(ROOT / "port/include"), "-I" + str(ROOT / "port/src"),
        str(source), str(loader),
        *[str(ROOT / "port/src" / (p + ".c")) for p in ("gevr_bean_tex", "gevr_bean_logic")],
        "-lm", "-o", str(exe)], check=True)
    cases = [
        ("white16", {"stride": 16}, 1, -1),
        ("white20", {"stride": 20}, 1, -1),
        ("white12", {"stride": 12}, 1, -1),
        ("coloured20", {"stride": 20, "colour": 0xFF123456}, 2, -1),
        ("isolated16", {"stride": 16, "isolated": True}, 2, -1),
        ("owned16", {"stride": 16, "own": True}, 2, 0),
        ("owned20", {"stride": 20, "own": True}, 2, 0),
        ("fading20", {"stride": 20, "colour": 0x80FFFFFF, "blended": True}, 2, -1),
        ("uv24", {"stride": 24}, 2, 0),
    ]
    for name, options, count, tex in cases:
        model = files / (name + ".bin")
        model.write_bytes(helipad(**options))
        run = subprocess.run([str(exe), str(model), str(count), str(tex)], capture_output=True, text=True)
        if run.returncode:
            raise SystemExit(f"{name}: {run.stdout}\n{run.stderr}")
    print(f"Helipad CAFF: {len(cases)} shader-only overlay/material/colour/coverage cases passed")
