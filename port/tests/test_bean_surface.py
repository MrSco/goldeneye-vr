"""Authored CAFF pines through the full loader, without a ROM or XBLA data.

An optional loader C path allows the same checks to run against an older loader.
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
    return words(((len(payload) + 4) << 16) | (kind << 8)) + payload


def pine(stride):
    # Opposite cards/cones share a local axis. Every value is authored here.
    points = [(1, 0, 0), (1, 10, 0), (5, 0, 0), (5, 10, 0),
              (-5, 0, 0), (-5, 10, 0), (-1, 0, 0), (-1, 10, 0)]
    gpu = bytearray()
    for point in points:
        gpu += struct.pack(">3fI", *point, 0)
        if stride == 28:
            gpu += words(0x12345678, 0xFF00FF00)  # tangent/binormal, not UV/colour
        gpu += words(0xFF123456)
    indices = struct.pack(">6H", 0, 1, 2, 4, 6, 7)
    data = bytearray(0x80)
    data[0x20:0x30] = words(stride, 0, 0, len(gpu))
    data[0x40:0x50] = words(0x99, len(gpu), len(indices), 1)
    gpu += indices
    matrix = (0, 0, -1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 100, 20, 300, 1)
    draw = record(1, words(4, 6, 0x99))
    stream = bytearray(0x24)
    stream += record(0x2E, words(0, 0x20)) + draw
    stream += record(0x21, words(0, 0, 0) + struct.pack(">16f", *matrix)) + draw
    stream += record(0x25, bytes(76)) + draw  # identity restored
    stream[4:8] = words(len(stream))
    sections = [(".data", data), (".gpu", gpu), (".stream", stream)]
    names = bytearray()
    table = bytearray()
    for name, blob in sections:
        entry = bytearray(0x21)
        entry[:4] = words(len(names))
        entry[9:13] = words(len(blob))
        table += entry
        names += name.encode() + b"\0"
    file_table = words(2, 0) + b"x\0" + words(0)
    for i, (_, blob) in enumerate(sections):
        file_table += words(1, 0, len(blob)) + bytes([i + 1, 0])
    header = bytearray(0x68)
    header[:4] = b"CAFF"
    header[0x14:0x18] = words(len(header))
    header[0x1C:0x24] = words(1, len(sections))
    header[0x48:0x4A] = bytes([1, len(sections)])
    header[0x4C:0x54] = words(len(names), len(table) + len(names))
    header[0x64:0x68] = words(len(file_table))
    return header + table + names + file_table + b"".join(blob for _, blob in sections)


DRIVER = r'''
#include "gevr_bean_int.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
void videoFreeCachedTexture(const void *p) { (void)p; }
int main(int argc, char **argv) {
    assert(argc==3);
    for(int f=1;f<argc;f++) {
        struct gevrBeanMesh *m=NULL; assert(gevrBeanLoad(argv[f],1,&m));
        assert(m && m->ntris==6);
        for(int t=0;t<2;t++) {
            const struct gevrBeanTri *a=&m->tris[t], *b=&m->tris[t+2], *c=&m->tris[t+4];
            assert(!memcmp(a->uv,b->uv,sizeof(a->uv)) && !memcmp(a->uv,c->uv,sizeof(a->uv)));
            for(int v=0;v<3;v++) {
                assert(fabsf(b->p[v*3]-(a->p[v*3+2]+100))<1e-5f);
                assert(fabsf(b->p[v*3+1]-(a->p[v*3+1]+20))<1e-5f);
                assert(fabsf(b->p[v*3+2]-(-a->p[v*3]+300))<1e-5f);
                assert(a->rgba[v*4]==0x56 && a->rgba[v*4+1]==0x34 && a->rgba[v*4+2]==0x12);
            }
        }
        if(f==1) {
            assert(m->tris[0].uv[0]==1 && m->tris[0].uv[2]==1 && m->tris[0].uv[4]==0);
            assert(m->tris[0].uv[1]==1 && m->tris[0].uv[3]==0);
        } else {
            assert(fabsf(m->tris[0].uv[0]-.5f)<1e-5f);
            assert(m->tris[0].uv[3]<0);
        }
        gevrBeanMeshFree(m);
    }
    puts("Full CAFF pine loading: local/instanced/reset geometry, generated UVs and authored colour passed");
}
'''


with tempfile.TemporaryDirectory(prefix="bean-surface-") as temporary:
    directory = Path(temporary)
    files = directory / "new/background/surface"
    files.mkdir(parents=True)
    models = [files / "branch.bin", files / "trunk.bin"]
    for model, stride in zip(models, (20, 28)):
        model.write_bytes(pine(stride))
    source = directory / "test.c"
    source.write_text(DRIVER)
    exe = directory / "surface"
    loader = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "port/src/gevr_bean_load.c"
    sanitize = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-no-pie"] if os.environ.get("BEAN_SANITIZE") else []
    subprocess.run([
        shutil.which("gcc") or "cc", "-std=c11", "-O2", "-include", "stdarg.h", *sanitize,
        "-I" + str(ROOT / "port/include"), "-I" + str(ROOT / "port/src"),
        str(source), str(loader),
        *[str(ROOT / "port/src" / (p + ".c")) for p in ("gevr_bean_tex", "gevr_bean_logic")],
        "-lm", "-o", str(exe)], check=True)
    subprocess.run([str(exe), *map(str, models)], check=True)
