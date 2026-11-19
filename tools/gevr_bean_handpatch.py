#!/usr/bin/env python3
"""Transfer the N64 gripping hand's sculpted fingers onto the Bean glove.

Usage: python tools/gevr_bean_handpatch.py ROM.z64 XBLA_FILES
Requires NumPy and a C compiler. Exports only added faces, references to
Bean triangle corners, offsets from those corners, UVs, and fingerprints.
The cartridge and decoded Bean geometry stay local.
"""
import argparse
from collections import defaultdict
import json
import math
import os
from pathlib import Path
import subprocess
import tempfile

import numpy as np

from gevr_model_export import Model, header_counts, probe

ROOT = Path(__file__).resolve().parents[1]
SCALE = 4.7
GUNS = ("ppk", "ppksilenced", "goldppk", "silverppk", "tt33", "ruger",
        "goldengun", "knife", "throwingknife")
# Insets within the existing skin islands, in atlas pixels.
SKIN = {
    "little": (449, 473, 477, 496),
    "ring": (448, 437, 477, 457),
    "middle": (446, 393, 476, 415),
    "index": (331, 56, 376, 101),
    "palm": (34, 427, 128, 468),
    "arm": (353, 416, 414, 455),
}
DUMP = r"""
#include "gevr_bean_int.h"
#include <stdio.h>
void videoFreeCachedTexture(const void *p) { (void)p; }
int main(int argc, char **argv)
{
    struct gevrBeanMesh *mesh;
    if (argc != 2 || !gevrBeanLoad(argv[1], 1, &mesh)) return 1;
    for (int i = 0; i < mesh->ntris; i++) {
        const struct gevrBeanTri *t = &mesh->tris[i];
        if (t->tex < 0 || mesh->texW[t->tex] != 512 || mesh->texH[t->tex] != 511) continue;
        for (int j = 0; j < 9; j++) printf("%.9g ", t->p[j]);
        for (int j = 0; j < 6; j++) printf("%.9g ", t->uv[j]);
        puts("");
    }
    gevrBeanMeshFree(mesh);
    return 0;
}
"""


def read_gloves(files):
    with tempfile.TemporaryDirectory() as temp:
        src, exe = Path(temp) / "dump.c", Path(temp) / "dump.exe"
        src.write_text(DUMP, encoding="utf8")
        subprocess.run([os.environ.get("CC", "cc" if os.name != "nt" else "gcc"),
                        "-std=c11", "-O2", "-include", "stdarg.h", "-Iport/include",
                        str(src), "port/src/gevr_bean_load.c", "port/src/gevr_bean_logic.c",
                        "port/src/gevr_bean_tex.c", "-lm", "-o", str(exe)], cwd=ROOT, check=True)
        for gun in GUNS:
            asset = files / "new" / "gun" / gun / "default.bin"
            out = subprocess.check_output([str(exe), asset.as_posix()], text=True)
            data = np.array([list(map(float, line.split())) for line in out.splitlines()],
                            dtype=np.float32)
            yield gun, data[:, :9].reshape(-1, 3), data[:, 9:]


def edges_of(xyz):
    # Weld only for boundary discovery. The output still references the exact
    # live corners so the patch and original round to the same rendered vertex.
    _, indices, weld = np.unique(np.round(xyz * 1000).astype(int), axis=0,
                                 return_index=True, return_inverse=True)
    edges = defaultdict(list)
    for i, face in enumerate(weld.reshape(-1, 3)):
        if len(set(face)) < 3:
            continue
        for c in range(3):
            a, b = face[c], face[(c + 1) % 3]
            edges[tuple(sorted((a, b)))].append((a, b, i * 3 + c))
    return xyz[indices], edges


def boundary_rings(xyz):
    _, edges = edges_of(xyz)
    link = {a: (b, ref) for edge in edges.values() if len(edge) == 1 for a, b, ref in edge}
    rings = []
    while link:
        start = point = next(iter(link))
        ring = []
        while True:
            point, ref = link.pop(point)
            ring.append((xyz[ref], ref))
            if point == start:
                break
        rings.append(ring)
    return sorted(rings, key=len, reverse=True)


def align_rings(rings, reference):
    assert [len(rim) for rim in rings] == [92, 18, 18, 16]
    translation = np.mean([p for p, _ in rings[1]], axis=0) - np.mean(
        [p for p, _ in reference[1]], axis=0)
    target = reference[0][28][0] + translation
    start = min(range(92), key=lambda i: np.linalg.norm(target - rings[0][i][0]))
    shift = (start - 28) % 92
    return rings[0][shift:] + rings[0][:shift], *rings[1:]


def authored_fingers(rom, name):
    switches, _, textures = header_counts(name)
    offset, size = probe.manifest_lookup(name)
    model = Model(probe.decompress_1172(rom[offset:offset + size]), switches, textures).export(name)
    patch = json.loads((ROOT / "tools/handpatch" / f"{name}.authored.json").read_text())
    blocks = {p["node"]: np.array(p["block_xyz"]) for p in model["parts"]}

    def rest_position(v):
        if "mix" in v:
            p = sum(blocks[int(node, 16)][i] * weight for node, i, weight in v["mix"])
        else:
            p = blocks[int(v["node"], 16)][v["ref"]]
        return (p + model["matrices"][str(v["mtx"])]) * SCALE

    return {g["name"]: (np.array([rest_position(v) for v in g["verts"]]), g["tris"])
            for part in patch["parts"] for g in part["groups"]
            if g["name"] in ("little_finger", "ring_finger")}


def atlas_uv(coords, skin):
    bounds = np.array(SKIN[skin]).reshape(2, 2)
    span = np.maximum(np.ptp(coords, axis=0), .01)
    pixels = (coords - coords.min(0)) / span * (bounds[1] - bounds[0]) + bounds[0]
    return pixels / (512, 511)


def finger(sculpt, palm, tip, knuckle, skin):
    original, faces = sculpt
    assert len(original) == 43
    # Keep the original sculpted joints and seat them between Bean's knuckle
    # and fingertip. Bean doubled the number of edges around the fingertip.
    start_delta = (palm[knuckle][0] - original[0] + palm[knuckle + 8][0] - original[6]) * .5
    tip_delta = np.mean([p for p, _ in tip], axis=0) - original[34:].mean(0)
    blend = np.clip((original[:, 0] - original[0, 0]) / (original[34, 0] - original[0, 0]), 0, 1)
    points = list(original + (1 - blend[:, None]) * start_delta + blend[:, None] * tip_delta)
    refs = [None] * len(points)
    points[0], refs[0] = palm[knuckle]
    points[6], refs[6] = palm[knuckle + 8]
    old_tip = [34, 35, 42, 41, 40, 39, 38, 37, 36]
    start = min(range(18), key=lambda i: np.linalg.norm(tip[i][0] - points[34]))
    anchors = [(start - i * 2) % 18 for i in range(9)]
    assert max(np.linalg.norm(tip[a][0] - points[b]) for a, b in zip(anchors, old_tip)) < 8
    for a, b in zip(anchors, old_tip):
        points[b], refs[b] = tip[a]
    splits = {(6, 0): list(reversed(palm[knuckle + 1:knuckle + 8]))}
    for i, a in enumerate(old_tip):
        splits[a, old_tip[(i + 1) % 9]] = [tip[(anchors[i] - 1) % 18]]
    refined = []
    for face in faces:
        for i in range(3):
            a, b, c = face[i:] + face[:i]
            if (a, b) not in splits:
                continue
            walk = [a]
            for p, ref in splits.pop((a, b)):
                walk.append(len(points))
                points.append(p)
                refs.append(ref)
            walk.append(b)
            refined.extend((u, v, c) for u, v in zip(walk, walk[1:]))
            break
        else:
            refined.append(face)
    assert not splits
    points = np.array(points)
    return (points, refs, refined, atlas_uv(points[:, :2], skin), [255] * len(points)), [
        (points[1], None), (points[4], None)]


def ears(xy):
    """Triangulate a nonconvex rim in its best-fit plane without skipping seam vertices."""
    def area(a, b, c):
        u, v = xy[b] - xy[a], xy[c] - xy[a]
        return u[0] * v[1] - u[1] * v[0]

    xy = xy.copy()
    xy[:, 0] *= np.sign(sum(area(0, i, i + 1) for i in range(1, len(xy) - 1)))
    rim, faces = list(range(len(xy))), []
    while len(rim) > 3:
        choices = []
        for i, b in enumerate(rim):
            a, c = rim[i - 1], rim[(i + 1) % len(rim)]
            if area(a, b, c) < .001:
                continue
            if any(min(area(a, b, p), area(b, c, p), area(c, a, p)) >= -.001
                   for p in rim if p not in (a, b, c)):
                continue
            choices.append((np.sum((xy[a] - xy[c]) ** 2), i, a, b, c))
        if not choices:
            return None
        _, i, a, b, c = min(choices)
        faces.append((a, b, c))
        del rim[i]
    return faces + [rim]


def cap(rim, skin, recess=None, shade=255):
    points = np.array([p for p, _ in reversed(rim)])
    refs = [ref for _, ref in reversed(rim)]
    _, _, axes = np.linalg.svd(points - points.mean(0))
    faces = ears(points @ axes[:2].T) if recess is None else None
    if faces is None:
        centre = points.mean(0) + (recess if recess is not None else [0, 0, 0])
        points = np.vstack((points, centre))
        faces = [(i, (i + 1) % len(refs), len(refs)) for i in range(len(refs))]
        refs.append(None)
    shades = [255 if ref is not None else shade for ref in refs]
    return points, refs, faces, atlas_uv(points @ axes[:2].T, skin), shades


def patch_groups(rings, sculpt):
    palm, ring, little, cuff = rings
    pinky, pinky_root = finger(sculpt["little_finger"], palm, little, 28, "little")
    ring_finger, ring_root = finger(sculpt["ring_finger"], palm, ring, 38, "ring")
    yield pinky
    yield ring_finger
    yield cap(palm[18:29], "palm", [0, 0, -15])
    yield cap(palm[52:73], "middle")
    yield cap(palm[82:] + palm[:3], "index")
    yield cap(cuff, "arm", [0, 0, 12], 185)

    # Line the inside of the grip, leaving a recess for each weapon's handle.
    rim = (palm[2:19] + [palm[28]] + pinky_root + palm[36:39] + ring_root +
           palm[46:53] + palm[72:83])
    outer = np.array([p for p, _ in rim])
    points = np.concatenate((outer, outer * .82 + outer.mean(0) * .18))
    n = len(rim)
    faces = []
    for i in range(n):
        j = (i + 1) % n
        faces.extend(((j, i, i + n), (j, i + n, j + n)))
    yield (points, [ref for _, ref in rim] + [None] * n, faces,
           atlas_uv(points[:, 1:], "palm"), [255] * n + [208] * n)


def nearest_int(x):
    return math.floor(x + .5) if x >= 0 else -math.floor(-x + .5)


def fingerprint(xyz, uv):
    # Match gevr_bean_handpatch.inc.h. CAFF UVs are stored as fixed point.
    value = 2166136261
    for p, st in zip(xyz.reshape(-1, 9), uv):
        for q in [nearest_int(float(v) * 16) for v in p] + [
                nearest_int(float(v) * 32768) for v in st]:
            value = ((value ^ (q & 0xffffffff)) * 16777619) & 0xffffffff
    return value


def encode(xyz, groups):
    # Share identical positions even when two groups use different UV islands.
    known = {tuple(p): ref for points, refs, *_ in groups for p, ref in zip(points, refs)
             if ref is not None}
    refs = list(known.values())
    anchors = xyz[refs]
    point_ids, points, vertices, faces = {}, [], [], []
    for group, rim_refs, indices, uv, shades in groups:
        start = len(vertices)
        for p, ref, st, shade in zip(group, rim_refs, uv, shades):
            key = tuple(p)
            if key not in point_ids:
                anchor = known.get(key, ref)
                if anchor is None:
                    anchor = refs[np.argmin(np.linalg.norm(anchors - p, axis=1))]
                offset = tuple(nearest_int(v * 16) for v in (p - xyz[anchor]))
                assert all(-32768 <= v <= 32767 for v in offset)
                point_ids[key] = len(points)
                points.append((anchor, offset))
            vertices.append((point_ids[key], *(nearest_int(v * 32768) for v in st), shade))
        faces.extend(tuple(start + i for i in face) for face in indices)
    return tuple(points), tuple(vertices), tuple(faces)


def verify(xyz, patch):
    points, vertices, faces = patch
    new = np.array([xyz[ref] + np.array(offset) / 16 for ref, offset in points])
    tris = new[np.array(vertices)[np.array(faces), 0]]
    areas = np.linalg.norm(np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0]), axis=1)
    assert np.min(areas) > .01
    combined = np.vstack((xyz, tris.reshape(-1, 3)))
    assert [len(ring) for ring in boundary_rings(combined)] == [43]
    _, edges = edges_of(combined)
    for edge in edges.values():
        if any(ref >= len(xyz) for _, _, ref in edge):
            assert len(edge) <= 2
            if len(edge) == 2:
                assert edge[0][:2] == edge[1][:2][::-1]


def write_data(models, templates):
    out = ["/* Generated by tools/gevr_bean_handpatch.py. Contains added faces only. */",
           f"#define BEAN_HAND_MAX_TRIS {max(count for _, count, _, _ in models)}", ""]
    point_sets, vertex_sets, face_sets = [list(dict.fromkeys(items)) for items in zip(*templates)]
    for i, points in enumerate(point_sets):
        out.append(f"static const struct beanHandPoint s_beanHandPoints{i}[] = {{")
        out.extend("    { %d, { %d, %d, %d } }," % (ref, *delta) for ref, delta in points)
        out.extend(("};", ""))
    for i, vertices in enumerate(vertex_sets):
        out.append(f"static const struct beanHandVertex s_beanHandVertices{i}[] = {{")
        out.extend("    { %d, %d, %d, %d }," % v for v in vertices)
        out.extend(("};", ""))
    for i, faces in enumerate(face_sets):
        out.append(f"static const uint16_t s_beanHandFaces{i}[][3] = {{")
        for start in range(0, len(faces), 4):
            out.append("    " + " ".join("{ %d, %d, %d }," % f for f in faces[start:start + 4]))
        out.extend(("};", ""))
    out.append("static const struct beanHandPatch s_beanHandPatches[] = {")
    by_mesh = defaultdict(list)
    for name, count, sig, i in models:
        by_mesh[count, sig, i].append(name)
    for (count, sig, i), names in by_mesh.items():
        points, vertices, faces = templates[i]
        out.append(f"    {{ {count}, 0x{sig:08x}u, s_beanHandPoints{point_sets.index(points)}, "
                   f"s_beanHandVertices{vertex_sets.index(vertices)}, s_beanHandFaces{face_sets.index(faces)}, "
                   f"{len(faces)} }}, /* {', '.join(names)} */")
    out.extend(("};", ""))
    (ROOT / "port/src/gevr_bean_handpatch_data.inc.h").write_text("\n".join(out), encoding="utf8", newline="\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path)
    parser.add_argument("files", type=Path)
    args = parser.parse_args()
    rom = probe.load_rom(args.rom)
    pistol = authored_fingers(rom, "GwppkZ")
    knife = authored_fingers(rom, "GknifeZ")
    templates, models = [], []
    ref = None
    for name, xyz, uv in read_gloves(args.files.resolve()):
        rings = boundary_rings(xyz)
        if ref is None:
            ref = rings
        rings = align_rings(rings, ref)
        groups = list(patch_groups(rings, knife if "knife" in name else pistol))
        patch = encode(xyz, groups)
        verify(xyz, patch)
        if patch not in templates:
            templates.append(patch)
        sig = fingerprint(xyz, uv)
        models.append((name, len(uv), sig, templates.index(patch)))
        print(f"{name}: {len(patch[2])} triangles, {sig:08x}")
    write_data(models, templates)


if __name__ == "__main__":
    main()
