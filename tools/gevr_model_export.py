#!/usr/bin/env python3
"""
Export a GoldenEye cartridge model's geometry for Blender (issue #9).

Why
---
The first-person hands and arms were modelled for one view on the N64, so they
are open shells: no palm, no inside to the sleeve, no back to the watch band.
To patch them we need the real geometry in a 3D editor. This script walks a
model file out of the ROM the same way tools/gevr_model_probe.py does, then
runs every display list through a small F3D interpreter (G_MTX, G_VTX, G_TRI1,
Rare's G_TRI4, the 0xC0 texture marker) and writes what it drew as JSON.

Every vertex keeps where it came from - the DL node, its index in that node's
vertex block and the matrix it was loaded under - so a patch made in Blender
can refer to the ROM's own vertices by index instead of copying them.

The output is ROM-derived. It goes under build/ (gitignored) and must never be
committed. tools/blender/gevr_hands_import.py loads it into Blender.

Usage
-----
    python tools/gevr_model_export.py <rom.z64> <FILENAME> [outdir]

e.g.  python tools/gevr_model_export.py "007 - GoldenEye.z64" Csuit_lf_handZ

The switch and texture counts are read from the model's MODELFILEHEADER under
assets/obseg. The rest pose places each matrix at the sum of its groups'
origins (no animation), which is enough to see the shape and find the holes.
"""

import glob
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import gevr_model_probe as probe  # noqa: E402

NODE_SIZE = 24
VTX_SIZE = 16


_HEADERS = None


def header_counts(name):
    """(numSwitches, numMatrices, numTextures) from the model's MODELFILEHEADER line."""
    global _HEADERS
    if _HEADERS is None:
        _HEADERS = {}
        rx = re.compile(r"MODELFILEHEADER\(\s*(\w+)\s*,(.*)\)")
        for path in glob.glob(os.path.join(REPO, "assets", "obseg", "**", "*.inc.c"), recursive=True):
            with open(path, encoding="utf-8", errors="replace") as f:
                for line in f:
                    m = rx.search(line)
                    if m:
                        # ROOTNODE, SKELETON, SWITCHES, NUMSWITCHES, NUMMATRICES,
                        # BOUNDINGRADIUS, NUMRECORDS, NUMTEXTURES
                        a = [x.strip() for x in re.sub(r"\([^)]*\)", "", m.group(2)).split(",")]
                        _HEADERS[m.group(1).lower()] = (int(a[3], 0), int(a[4], 0), int(a[7], 0))
    stem = re.sub(r"^[A-Z]", "", name)
    stem = re.sub(r"[Zz]$", "", stem).lower()
    if stem not in _HEADERS:
        raise SystemExit("%s: no MODELFILEHEADER for '%s' under assets/obseg" % (name, stem))
    return _HEADERS[stem]


def f32(b, o):
    return struct.unpack_from(">f", b, o)[0]


def s16(b, o):
    return struct.unpack_from(">h", b, o)[0]


class Model:
    def __init__(self, data, num_switches, num_textures):
        self.d = data
        self.num_switches = num_switches
        self.num_textures = num_textures
        tex = 4 * num_switches
        self.textures = {}
        for i in range(num_textures):
            o = tex + 12 * i
            self.textures[probe.be32(data, o) & 0xFFF] = {"w": data[o + 4], "h": data[o + 5]}
        self.root = tex + 12 * num_textures
        self.groups = {}      # node -> {origin, mtx, parent_group}
        self.parts = []       # DL nodes in walk order
        self.switch_of = {}   # node -> switch index controlling its subtree

    def ptr(self, o):
        return probe.ofs(probe.be32(self.d, o))

    def walk(self):
        d = self.d
        for i in range(self.num_switches):
            n = self.ptr(4 * i)
            if n is not None:
                self.switch_of[n] = i
        self.visit(self.root, None, None, set())

    def visit(self, n, group, switch, seen):
        """Depth first from n, children before siblings, as the renderer walks."""
        d = self.d
        while n is not None and n not in seen:
            seen.add(n)
            op = probe.be16(d, n) & 0xFF
            rod = self.ptr(n + 4)
            sw = self.switch_of.get(n, switch)
            child_group = group
            if op in (2, 21) and rod is not None:
                origin = [f32(d, rod), f32(d, rod + 4), f32(d, rod + 8)]
                mtx = s16(d, rod + 0xE) if op == 2 else s16(d, rod + 0xC)
                self.groups[n] = {"origin": origin, "mtx": mtx, "parent": group, "op": op}
                child_group = n
            elif op in (4, 22, 24) and rod is not None:
                self.parts.append(self.dl_part(n, op, rod, group, sw))
            self.visit(self.ptr(n + 20), child_group, sw, seen)
            n = self.ptr(n + 12)

    def dl_part(self, node, op, rod, group, switch):
        d = self.d
        if op == 4:
            prim, sec, verts, nv = self.ptr(rod), self.ptr(rod + 4), self.ptr(rod + 0xC), probe.be16(d, rod + 0x10)
            mtype = struct.unpack_from(">b", d, rod + 0x12)[0]
        elif op == 22:
            nv, verts, prim, sec, mtype = probe.be32(d, rod), self.ptr(rod + 4), self.ptr(rod + 8), None, None
        else:
            prim, sec, verts, nv = self.ptr(rod), self.ptr(rod + 4), self.ptr(rod + 8), s16(d, rod + 0xC)
            mtype = None
        return {"node": node, "op": op, "primary": prim, "secondary": sec, "vtxblock": verts,
                "numvtx": nv, "modeltype": mtype, "group": group, "switch": switch}

    def mtx_translation(self, mtx):
        """Rest-pose translation of a matrix: its group's origin plus every ancestor's."""
        for n, g in self.groups.items():
            if g["mtx"] == mtx:
                t = [0.0, 0.0, 0.0]
                while n is not None:
                    o = self.groups[n]["origin"]
                    t = [t[0] + o[0], t[1] + o[1], t[2] + o[2]]
                    n = self.groups[n]["parent"]
                return t
        return [0.0, 0.0, 0.0]

    def run_dl(self, part, start, which, out_verts, out_tris, vkey):
        """Interpret one display list; append the triangles it draws."""
        d = self.d
        cache = [None] * 16
        mtx = 0
        tex = None
        tscale = (1.0, 1.0)
        o = start
        count = 0
        while o is not None and o + 8 <= len(d) and count < 4096:
            w0, w1 = struct.unpack_from(">II", d, o)
            op = w0 >> 24
            count += 1
            o += 8
            if op == 0xB8:
                break
            if op == 0x01 and (w1 >> 24) == 0x03:
                mtx = (w1 & 0xFFFFFF) // 0x40
            elif op == 0xC0:
                tex = w1 & 0xFFF
            elif op == 0xBB:
                tscale = (((w1 >> 16) & 0xFFFF) / 65536.0, (w1 & 0xFFFF) / 65536.0)
            elif op == 0x04:
                n = (w0 & 0xFFFF) // VTX_SIZE
                v0 = (w0 >> 16) & 0xF
                seg, addr = w1 >> 24, w1 & 0xFFFFFF
                if seg != 0x05 or part["vtxblock"] is None:
                    for i in range(n):
                        cache[(v0 + i) & 15] = None
                    continue
                for i in range(n):
                    vo = addr + VTX_SIZE * i
                    idx = (vo - part["vtxblock"]) // VTX_SIZE
                    key = (part["node"], idx, mtx)
                    if key not in vkey:
                        x, y, z, flag, s, t = struct.unpack_from(">hhhhhh", d, vo)
                        rgba = list(d[vo + 12:vo + 16])
                        tr = self.mtx_translation(mtx)
                        vkey[key] = len(out_verts)
                        out_verts.append({"node": part["node"], "idx": idx, "mtx": mtx,
                                          "local": [x, y, z], "pos": [x + tr[0], y + tr[1], z + tr[2]],
                                          "flag": flag, "st": [s, t], "rgba": rgba})
                    cache[(v0 + i) & 15] = vkey[key]
            elif op == 0xBF:
                tri = [((w1 >> 16) & 0xFF) // 10, ((w1 >> 8) & 0xFF) // 10, (w1 & 0xFF) // 10]
                self.emit(part, which, cache, tri, tex, tscale, out_tris)
            elif op == 0xB1:
                for k in range(4):
                    x = (w1 >> (8 * k)) & 0xF
                    y = (w1 >> (8 * k + 4)) & 0xF
                    z = (w0 >> (4 * k)) & 0xF
                    if x or y or z:
                        self.emit(part, which, cache, [x, y, z], tex, tscale, out_tris)
            elif op == 0x06:
                # a branch or call into another list in this file
                if (w1 >> 24) == 0x05:
                    self.run_dl(part, w1 & 0xFFFFFF, which, out_verts, out_tris, vkey)
                    if (w0 >> 16) & 1:
                        break

    def emit(self, part, which, cache, tri, tex, tscale, out_tris):
        vs = [cache[i] for i in tri]
        if None in vs:
            return
        out_tris.append({"node": part["node"], "dl": which, "v": vs, "tex": tex,
                         "tscale": list(tscale)})

    def export(self, name):
        self.walk()
        verts, tris, vkey = [], [], {}
        for p in self.parts:
            for which in ("primary", "secondary"):
                if p[which] is not None:
                    self.run_dl(p, p[which], which, verts, tris, vkey)
        mtxs = sorted({g["mtx"] for g in self.groups.values()})
        return {
            "model": name,
            "note": "ROM-derived geometry. Never commit.",
            "textures": {"0x%x" % k: v for k, v in self.textures.items()},
            "groups": [{"node": n, "op": g["op"], "origin": g["origin"], "mtx": g["mtx"],
                        "parent": g["parent"]} for n, g in self.groups.items()],
            "matrices": {str(m): self.mtx_translation(m) for m in mtxs},
            "parts": self.parts,
            "verts": verts,
            "tris": tris,
        }


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    rom = probe.load_rom(sys.argv[1])
    name = sys.argv[2]
    outdir = sys.argv[3] if len(sys.argv) > 3 else os.path.join(REPO, "build", "handmodels")
    ns, _nm, nt = header_counts(name)
    off, size = probe.manifest_lookup(name)
    data = probe.decompress_1172(rom[off:off + size])
    m = Model(data, ns, nt)
    out = m.export(name)
    os.makedirs(outdir, exist_ok=True)
    path = os.path.join(outdir, name + ".json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(out, f, indent=1)
    print("%s: %d switches, %d textures, %d groups, %d DL nodes, %d vertices, %d triangles -> %s"
          % (name, ns, nt, len(out["groups"]), len(out["parts"]), len(out["verts"]),
             len(out["tris"]), path))
    for p in out["parts"]:
        ntri = sum(1 for t in out["tris"] if t["node"] == p["node"])
        texs = sorted({t["tex"] for t in out["tris"] if t["node"] == p["node"] and t["tex"] is not None})
        mt = sorted({out["verts"][t["v"][0]]["mtx"] for t in out["tris"] if t["node"] == p["node"]})
        print("  node 0x%04x op%-2d switch %-4s %4d vtx %4d tris  mtx %s  tex %s"
              % (p["node"], p["op"], p["switch"], p["numvtx"], ntri, mt,
                 " ".join("%x" % t for t in texs)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
