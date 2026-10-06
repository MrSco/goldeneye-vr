"""Inspect GE-X 6a weapon rigs and scripts; no ROM-derived files are saved.

Usage: python tools/gex/gex_extract.py ROM.z64 [slot ...]
JSON reports separate animation joints from matrix bindings in display lists.
Semantic roles and interaction points still require review.
"""
import argparse
import atexit
import ctypes
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

from pdrom import PdRom
from pdmodel import PdModel, seg
from gexguns import GexCode, G_WEAPONS

ROOT = Path(__file__).resolve().parents[2]


class AnimReader:
    """Use the production decoder, rather than a second implementation."""
    def __init__(self, rom):
        self.temp = tempfile.TemporaryDirectory(prefix="gex-anim-", ignore_cleanup_errors=True)
        temp = Path(self.temp.name)
        source = temp / "rom.c"
        source.write_text('''#include "gevr_gex.h"
static const u8 *rom; static u32 size;
void setRom(const u8 *r, u32 s) { rom=r; size=s; }
const u8 *gevrGexRom(u32 *s) { *s=size; return rom; }
''')
        output = temp / ("anim.dll" if __import__('os').name == "nt" else "anim.so")
        subprocess.run([shutil.which("gcc") or "cc", "-shared", "-O2", "-fPIC",
                        "-D_LANGUAGE_C", "-I"+str(ROOT/"include"),
                        "-I"+str(ROOT/"port/include"), str(source),
                        str(ROOT/"port/src/gevr_pdanim.c"), "-lm", "-o", str(output)], check=True)
        self.lib = ctypes.CDLL(str(output))
        self.bytes = ctypes.create_string_buffer(rom)
        self.lib.setRom.argtypes = [ctypes.c_void_p, ctypes.c_uint]
        self.lib.setRom(self.bytes, len(rom))
        self.lib.gevrPdAnimPart.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                          ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
        self.lib.gevrPdMtxRotTrans.argtypes = [ctypes.c_void_p]*3
        atexit.register(self.close)

    def close(self):
        if self.lib is not None:
            if __import__('os').name == "nt":
                from _ctypes import FreeLibrary
                FreeLibrary(self.lib._handle)
            else:
                from _ctypes import dlclose
                dlclose(self.lib._handle)
            self.lib = None
        self.temp.cleanup()

    def matrix(self, anim, frame, joint, origin, root=False):
        rot = (ctypes.c_float*3)()
        pos = (ctypes.c_float*3)()
        scale = (ctypes.c_float*3)()
        self.lib.gevrPdAnimPart(anim, frame, joint, rot, pos, scale)
        if not root:
            for a in range(3): pos[a] += origin[a]
        matrix = (ctypes.c_float*16)()
        self.lib.gevrPdMtxRotTrans(rot, pos, matrix)
        return [list(matrix[a*4:a*4+4]) for a in range(4)]


def multiply(parent, child):
    return [[sum(child[j][k]*parent[k][i] for k in range(4))
             for i in range(4)] for j in range(4)]


def point(matrix, vertex):
    return [sum(vertex[k]*matrix[k][a] for k in range(3))+matrix[3][a] for a in range(3)]


def pose(model, reader, anim, frame):
    matrices = {}
    groups = {}
    for _, node, kind, record in model.walk():
        if kind != 2: continue
        joint, matrix = struct.unpack_from(">hh", model.d, record+12)
        parent = seg(struct.unpack_from(">I", model.d, node+8)[0])
        while parent is not None and parent not in groups:
            parent = seg(struct.unpack_from(">I", model.d, parent+8)[0])
        origin = struct.unpack_from(">fff", model.d, record)
        local = reader.matrix(anim, frame, joint, origin, root=parent is None)
        matrices[matrix] = multiply(matrices[groups[parent]], local) if parent is not None else local
        groups[node] = matrix
    return matrices


def part_geometry(model, node):
    """Read only this subtree, not the part's siblings. Bind each loaded
    vertex to the matrix in force in its display list."""
    result = []
    kind, record, _, child = model.node(node)
    branch = [(0,node,kind,record)] if kind == 4 else (model.walk(child) if child is not None else [])
    for _, n, kind, record in branch:
        if kind != 4: continue
        matrix = None
        dl = seg(struct.unpack_from(">I",model.d,record)[0])
        steps = 0
        while dl is not None:
            if steps >= 4096: raise ValueError("Unterminated display list")
            w0,w1 = struct.unpack_from(">II",model.d,dl)
            if w0>>24 == 1 and w1>>24 == 3:
                matrix = (w1 & 0xffffff)//64
            if w0>>24 == 4:
                offset = w1 & 0xffffff
                count = (w0 & 0xffff)//12
                if count == 0: raise ValueError("Invalid vertex load")
                for k in range(count):
                    result.append((matrix, struct.unpack_from(">hhh", model.d, offset+12*k)))
            if w0>>24 == 0xb8: break
            dl += 8
            steps += 1
    return result


def inspect(rom, slot):
    code = GexCode(rom)
    weapon = code.u32(G_WEAPONS+4*slot)
    name = next(f[1] for f in rom.files if f[0] == code.u16(weapon))
    model = PdModel(rom.load(name))
    joints = {}
    for _, _, kind, record in model.walk():
        if kind == 2:
            joint,matrix = struct.unpack_from(">hh",model.d,record+12)
            joints[matrix] = joint
    parts = []
    for i in range(model.numparts):
        part = struct.unpack_from(">h",model.d,model.parts+4*model.numparts+2*i)[0]
        node = seg(struct.unpack_from(">I",model.d,model.parts+4*i)[0])
        geometry = part_geometry(model,node)
        bounds = {}
        for matrix in sorted({m for m,v in geometry if m is not None}):
            vertices = [v for m,v in geometry if m == matrix]
            bounds[matrix] = {"joint":joints.get(matrix),
                "min":[min(v[a] for v in vertices) for a in range(3)],
                "max":[max(v[a] for v in vertices) for a in range(3)]}
        parts.append({"part":part,"node":node,"bindings":bounds})
    textures = [struct.unpack_from(">I",model.d,model.texconfigs+12*i)[0]
                for i in range(model.numtexconfigs)]
    return {"slot":slot,"model":name,"matrices":model.nummatrices,
            "nodes":len(model.walk()),"parts":parts,"textures":textures,
            "scripts":code.weapon(slot),"review_required":["semantic parts", "interaction points",
            "held mesh alignment", "screen anchor", "muzzle origin"]}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom")
    parser.add_argument("slots",nargs="*",type=int,default=[3,4,7])
    args = parser.parse_args()
    rom = PdRom(args.rom)
    if len(rom.rom) != 33554432 or rom.title != "GoldenEye X":
        parser.error("Expected a GE-X 6a big-endian ROM")
    print(json.dumps([inspect(rom,s) for s in args.slots],indent=2))
