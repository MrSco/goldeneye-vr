#!/usr/bin/env python3
"""
Oddjob's hat for the throwing knife (Mods > "Oddjob's hat"): the file
port/src/gevr_oddjobhat.c loads, made from the player's own 3DReaperDX
capture of 007 Nightfire (GameCube) with the hat in Oddjob's hands.

Nothing of Nightfire is in this repository or the APK. The player captures a
frame, runs this script, and copies the result to the headset:

    python tools/gevr_oddjob_hat.py <frame.obj> build/oddjob_hat [--calls 538,539,540]
    adb push build/oddjob_hat/oddjob_hat.bin /sdcard/Android/data/com.gevr.port/files/mods/oddjob_hat.bin

The capture's view model is three draw calls after the depth clear: the hat's
crown, its band and both forearms gripping the brim (Nightfire draws the arms
a second time in another pose, off screen; that copy is skipped). They are
found by their textures unless --calls names them.

3DReaperDX unprojected X with the window's 4:3 aspect while the game drew
16:9 (the hat band is round only with X x 4/3), so X is scaled back. Its
view space is Direct3D's (+Z forward); the output is OpenGL's (-Z forward).

Writes:
  oddjob_hat.bin   the hat: textures, triangles (lit into vertex colours),
                   brim radius and the right fist's grip on the brim
  oddjob_hat.obj   the hat and both arms, as captured, for Blender
"""

import argparse
import math
import os
import struct
import sys

import numpy as np
from PIL import Image

ASPECT_FIX = 4.0 / 3.0
HAT_TEX = ("755bdc3e", "c7ceefc1")   # crown, band
ARM_TEX = "5f54cbc7"
LIGHT = np.array([0.35, 1.0, 0.55])
MAGIC = b"GEVRHAT1"


def read_dump(path):
    verts, uvs, calls, cur = [], [], [], None
    with open(path, encoding="latin-1") as f:
        for line in f:
            if line.startswith("v "):
                verts.append([float(x) for x in line.split()[1:4]])
            elif line.startswith("vt "):
                uvs.append([float(x) for x in line.split()[1:3]])
            elif line.startswith("g DrawCall"):
                cur = {"name": line.split()[1], "mtl": None, "faces": []}
                calls.append(cur)
            elif line.startswith("usemtl") and cur:
                cur["mtl"] = line.split()[1]
            elif line.startswith("f ") and cur:
                tri = []
                for p in line.split()[1:]:
                    a = p.split("/")
                    vi, ti = int(a[0]), int(a[1])
                    tri.append((vi - 1 if vi > 0 else len(verts) + vi,
                                ti - 1 if ti > 0 else len(uvs) + ti))
                cur["faces"].append(tri)
    tex = {}
    with open(os.path.splitext(path)[0] + ".mtl", encoding="latin-1") as f:
        for line in f:
            if line.startswith("newmtl"):
                m = line.split()[1]
            elif line.startswith("map_Kd"):
                tex[m] = line.split()[1]
    # Direct3D view space -> OpenGL, aspect fix
    v = np.array(verts) * [ASPECT_FIX, 1.0, -1.0]
    for c in calls:
        c["faces"] = [t[::-1] for t in c["faces"]]   # the Z mirror flips winding
        c["tex"] = tex.get(c["mtl"], "")
    return v, np.array(uvs), [c for c in calls if c["faces"]]


def pick_calls(calls, v, names):
    byname = {c["name"]: c for c in calls}
    if names:
        crown, band, arms = (byname[n] for n in names)
        return crown, band, arms
    find = lambda stem: [c for c in calls if c["tex"].startswith(stem)]
    crown, band = find(HAT_TEX[0])[0], find(HAT_TEX[1])[0]
    centre = v[[i for t in crown["faces"] for i, _ in t]].mean(0)
    arms = min(find(ARM_TEX), key=lambda c: np.linalg.norm(
        v[[i for t in c["faces"] for i, _ in t]].mean(0) - centre))
    return crown, band, arms


def split_arms(faces, v, tol=0.004):
    """The arms are skinned pieces that share no indices: join touching ones."""
    parent = {}

    def find(a):
        while parent.setdefault(a, a) != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    cells = {}
    for t in faces:
        for vi, _ in t:
            parent[find(vi)] = find(t[0][0])
            k = tuple(np.round(v[vi] / tol).astype(int))
            for d in np.ndindex(3, 3, 3):
                o = cells.get((k[0] + d[0] - 1, k[1] + d[1] - 1, k[2] + d[2] - 1))
                if o is not None:
                    parent[find(vi)] = find(o)
            cells.setdefault(k, vi)
    groups = {}
    for t in faces:
        groups.setdefault(find(t[0][0]), []).append(t)
    groups = sorted(groups.values(), key=len, reverse=True)[:2]
    groups.sort(key=lambda g: v[[i for t in g for i, _ in t], 0].mean())
    return groups   # left, right


def hat_frame(v, hat_idx):
    """Origin the brim's centre, +Y the crown, unit the dump's."""
    p = v[hat_idx]
    centre = p.mean(0)
    axis = np.linalg.svd(p - centre)[2][0]
    if axis[1] < 0:
        axis = -axis
    t = (p - centre) @ axis
    r = np.linalg.norm((p - centre) - np.outer(t, axis), axis=1)
    brim = r > 0.9 * r.max()
    origin = centre + axis * np.median(t[brim])
    x = np.array([1.0, 0, 0]) - axis * axis[0]
    x /= np.linalg.norm(x)
    z = np.cross(x, axis)
    basis = np.array([x, axis, z])   # rows: hat-local axes in view space
    return origin, basis, float(np.median(r[brim]))


def grip(v, arm_faces, origin, basis, radius):
    """The right fist on the brim, in hat-local space: fist centre, forearm
    direction, and the fist-to-brim direction square to it."""
    local = lambda q: (q - origin) @ basis.T
    a = local(v[sorted({i for t in arm_faces for i, _ in t})])
    centre = a.mean(0)
    fore = np.linalg.svd(a - centre)[2][0]
    if (-centre) @ fore < 0:   # towards the hat
        fore = -fore
    along = (a - centre) @ fore
    fist = a[along > along.max() - 0.12].mean(0)
    radial = fist * [1, 0, 1]
    brim = radial / np.linalg.norm(radial) * radius
    up = brim - fist
    up -= fore * (up @ fore)
    up /= np.linalg.norm(up)
    return fist, fore, up


def vertex_light(p, faces):
    n = np.zeros_like(p)
    tri = np.array([[i for i, _ in t] for t in faces])
    fn = np.cross(p[tri[:, 1]] - p[tri[:, 0]], p[tri[:, 2]] - p[tri[:, 0]])
    # the crown's sides face out from the axis; flip the whole hat if they don't
    mid = p[tri].mean(1)
    side = np.abs(fn[:, 1]) < 0.5 * np.linalg.norm(fn, axis=1)
    if np.sum((fn[side] * (mid[side] * [1, 0, 1])).sum(1)) < 0:
        fn = -fn
    keys = {}
    for k, idx in enumerate(tri):
        for i in idx:
            keys.setdefault(tuple(np.round(p[i], 4)), []).append(k)
    l = LIGHT / np.linalg.norm(LIGHT)
    shade = {}
    for key, ks in keys.items():
        s = fn[ks].sum(0)
        s /= max(np.linalg.norm(s), 1e-9)
        shade[key] = 0.45 + 0.55 * max(0.0, float(s @ l))
    return {i: shade[tuple(np.round(p[i], 4))] for t in faces for i, _ in t}


def write_bin(out, dumpdir, v, uv, origin, basis, radius, parts, fist, fore, up):
    texnames, corners = [], []
    allfaces = [t for _, faces in parts for t in faces]
    idx = sorted({i for t in allfaces for i, _ in t})
    local = np.zeros_like(v)
    local[idx] = (v[idx] - origin) @ basis.T
    light = vertex_light(local, allfaces)
    for tex, faces in parts:
        texnames.append(tex)
        for t in faces:
            for vi, ti in t:
                corners.append((local[vi], (uv[ti][0], 1.0 - uv[ti][1]), light[vi], len(texnames) - 1))
    with open(out, "wb") as f:
        f.write(MAGIC)
        f.write(struct.pack("<f9f", radius, *fist, *fore, *up))
        f.write(struct.pack("<I", len(texnames)))
        for name in texnames:
            im = Image.open(os.path.join(dumpdir, name)).convert("RGBA")
            f.write(struct.pack("<HH", *im.size))
            f.write(im.tobytes())
        f.write(struct.pack("<I", len(corners) // 3))
        for p, st, shade, tex in corners:
            c = int(round(255 * min(1.0, shade)))
            f.write(struct.pack("<5f4BB3x", *p, *st, c, c, c, 255, tex))
    return len(corners) // 3


def write_obj(out, dumpdir, v, uv, objects):
    base = os.path.splitext(os.path.basename(out))[0]
    with open(out, "w") as o, open(os.path.splitext(out)[0] + ".mtl", "w") as m:
        o.write("# Oddjob's hat view model (007 Nightfire, GameCube) from a 3DReaperDX capture\n")
        o.write("# game camera at the origin looking down -Z; X scaled by 4/3 (capture aspect)\n")
        o.write("mtllib %s.mtl\n" % base)
        vb = tb = 0
        for name, tex, faces in objects:
            vs = sorted({i for t in faces for i, _ in t})
            ts = sorted({j for t in faces for _, j in t})
            vm = {x: k + 1 + vb for k, x in enumerate(vs)}
            tm = {x: k + 1 + tb for k, x in enumerate(ts)}
            o.write("o %s\n" % name)
            o.writelines("v %.6f %.6f %.6f\n" % tuple(v[x]) for x in vs)
            o.writelines("vt %.6f %.6f\n" % tuple(uv[x]) for x in ts)
            o.write("usemtl %s\n" % name)
            o.writelines("f %s\n" % " ".join("%d/%d" % (vm[i], tm[j]) for i, j in t) for t in faces)
            vb += len(vs)
            tb += len(ts)
            png = os.path.splitext(tex)[0] + ".png"
            m.write("newmtl %s\nKd 1 1 1\nKs 0 0 0\nillum 1\nmap_Kd %s\n\n" % (name, png))
            target = os.path.join(os.path.dirname(out), png)
            if not os.path.exists(target):
                Image.open(os.path.join(dumpdir, tex)).save(target)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("frame", help="3DReaperDX frame .obj (its .mtl and .dds beside it)")
    ap.add_argument("outdir")
    ap.add_argument("--calls", help="crown,band,arms draw call numbers, e.g. 538,539,540")
    args = ap.parse_args()
    v, uv, calls = read_dump(args.frame)
    names = ["DrawCall_" + n for n in args.calls.split(",")] if args.calls else None
    crown, band, arms = pick_calls(calls, v, names)
    left, right = split_arms(arms["faces"], v)
    hat_idx = sorted({i for c in (crown, band) for t in c["faces"] for i, _ in t})
    origin, basis, radius = hat_frame(v, hat_idx)
    fist, fore, up = grip(v, right, origin, basis, radius)
    os.makedirs(args.outdir, exist_ok=True)
    dumpdir = os.path.dirname(os.path.abspath(args.frame))
    tris = write_bin(os.path.join(args.outdir, "oddjob_hat.bin"), dumpdir, v, uv, origin, basis, radius,
                     [(crown["tex"], crown["faces"]), (band["tex"], band["faces"])], fist, fore, up)
    write_obj(os.path.join(args.outdir, "oddjob_hat.obj"), dumpdir, v, uv,
              [("Hat", crown["tex"], crown["faces"]), ("HatBand", band["tex"], band["faces"]),
               ("ArmLeft", arms["tex"], left), ("ArmRight", arms["tex"], right)])
    print("%s %s %s: hat %d triangles, brim radius %.3f, arms %d + %d triangles"
          % (crown["name"], band["name"], arms["name"], tris, radius, len(left), len(right)))
    print("fist %s forearm %s up %s" % (np.round(fist, 3), np.round(fore, 3), np.round(up, 3)))


if __name__ == "__main__":
    sys.exit(main())
