#!/usr/bin/env python3
"""
Meta Quest Touch controllers for the watch's Controls page: the C data in
port/src/gevr_touch_model_data.c, from the geometry that
tools/blender/gevr_touch_export.py exports and Meta's textures.

  1. Extract Meta's "Oculus controller art" zip (v1.8) to <art>.
  2. For each kind (quest1 quest2 plus pro):
       blender -b --factory-startup -P tools/blender/gevr_touch_export.py -- <art> build/touch/<kind>.json <kind>
  3. python tools/gevr_touch_model_gen.py --art <art> --json build/touch
         [--preview build/touch/preview]

docs/touch-controllers.md has the background.

Each controller's colour comes from its base colour texture (blurred, so a
vertex takes the colour of the area round it, not of one texel), sampled at
the corners and the triangle centre. Both controllers of a pair share one
model space, laid out side by side as the page shows them; the runtime
(port/src/gevr_touch_model.c) lights them and moves their parts each frame.
Triangles are packed into GoldenEye's 16-vertex loads (G_VTX + G_TRI4).

Data stays one item per line: a vertex, or a batch (its vertex indices and
its triangles), so a regenerated file diffs by what changed.
"""

import argparse
import json
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

KINDS = ["quest1", "quest2", "plus", "pro"]
PARTS = ["body", "trigger", "grip", "stick", "btn_lower", "btn_upper", "btn_menu"]
SIDES = ["L", "R"]

# Textures per kind and side, relative to the art folder (the zip's layout).
TEXTURES = {
    "quest1": {"L": "Oculus Touch for Quest & Rift S/Export/controller_bc.tga",
               "R": "Oculus Touch for Quest & Rift S/Export/controller_bc.tga"},
    "quest2": {"L": "Meta Quest 2 Touch/quest2Textures1k/nextControllerBoth_color1k.png",
               "R": "Meta Quest 2 Touch/quest2Textures1k/nextControllerBoth_color1k.png"},
    "plus": {"L": "Meta Quest Touch Plus/textures/MetaQuestTouchPlus_Left_BaseColor_AO.png",
             "R": "Meta Quest Touch Plus/textures/MetaQuestTouchPlus_right_BaseColor_AO.png"},
    "pro": {"L": "Meta Quest Touch Pro/textures/controller_l_lo_BaseColor.png",
            "R": "Meta Quest Touch Pro/textures/controller_r_lo_BaseColor.png"},
}
# Ambient occlusion where the base colour has none (ORM: occlusion in red).
OCCLUSION = {
    "pro": {"L": "Meta Quest Touch Pro/textures/controller_l_lo_ORM.png",
            "R": "Meta Quest Touch Pro/textures/controller_r_lo_ORM.png"},
}

# Layout, in the watch controller's model space (options.c draw_watch_controller:
# x right, +y toward the camera before the page's 45 degree tilt, -z up the
# screen; at the page's depth 7.07 units make a pixel of its 320 x 240). A
# metre is UNITS units; each controller's centre sits XOFFSET out from the
# middle and RAISE up the screen (each controller stands over its own list
# of controls, gevr_touch_model.c), tilted TILT toward the viewer from the
# page's view of the face and turned YAW about its up axis (outward +).
UNITS = 4000.0
XOFFSET = 420.0
RAISE = 170.0
YAW_DEG = 0.0
TILT_DEG = 35.0
LIFT = 0.12   # the darkest plastic's colour, of white

# Part animation (the runtime reads these): trigger and stick angles in
# radians, travel in model units.
TRIGGER_ANGLE = math.radians(16.0)
STICK_ANGLE = math.radians(16.0)
BUTTON_TRAVEL = 0.0016 * UNITS
GRIP_TRAVEL = 0.0022 * UNITS
STICK_TRAVEL = 0.0012 * UNITS

MAX_BATCH_VERTS = 16


def long_path(p):
    p = os.path.abspath(p)
    if os.name == "nt" and not p.startswith("\\\\?\\"):
        return "\\\\?\\" + p
    return p


def load_texture(art, rel, blur):
    with open(long_path(os.path.join(art, rel)), "rb") as f:
        im = Image.open(f)
        im.load()
    im = im.convert("RGB")
    size = 512
    im = im.resize((size, size), Image.BILINEAR).filter(ImageFilter.GaussianBlur(blur))
    return np.asarray(im, dtype=np.float32) / 255.0


def sample(tex, uv):
    """Bilinear, UV origin at the bottom left (Blender), wrapping."""
    h, w, _ = tex.shape
    x = (uv[:, 0] % 1.0) * w - 0.5
    y = (1.0 - (uv[:, 1] % 1.0)) * h - 0.5
    x0 = np.floor(x).astype(np.int64)
    y0 = np.floor(y).astype(np.int64)
    fx = (x - x0)[:, None]
    fy = (y - y0)[:, None]
    x0 %= w
    y0 %= h
    x1 = (x0 + 1) % w
    y1 = (y0 + 1) % h
    return (tex[y0, x0] * (1 - fx) * (1 - fy) + tex[y0, x1] * fx * (1 - fy)
            + tex[y1, x0] * (1 - fx) * fy + tex[y1, x1] * fx * fy)


def blender_to_model(p):
    """Blender (the ring toward -y, the face toward +z, a left controller's
    stick toward +x) -> the watch controller's model space: seen from above
    the face, as in the hand, the ring up the screen (-z), the face toward
    +y, and the screen's right +x."""
    p = np.asarray(p, dtype=np.float64)
    return np.stack([-p[..., 0], p[..., 2], p[..., 1]], axis=-1)


def rot_y(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])


def rot_x(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])


def rot_axis(axis, a):
    axis = np.asarray(axis, dtype=np.float64)
    axis = axis / np.linalg.norm(axis)
    x, y, z = axis
    c, s = math.cos(a), math.sin(a)
    C = 1 - c
    return np.array([[c + x * x * C, x * y * C - z * s, x * z * C + y * s],
                     [y * x * C + z * s, c + y * y * C, y * z * C - x * s],
                     [z * x * C - y * s, z * y * C + x * s, c + z * z * C]])


class Side:
    pass


def build_side(kind, side, data, art):
    tex = load_texture(art, TEXTURES[kind][side], 3.0)
    occ = load_texture(art, OCCLUSION[kind][side], 3.0)[..., 0:1] if kind in OCCLUSION else None
    pos, nrm, col, part = [], [], [], []
    for pi, pname in enumerate(PARTS):
        tris = data["parts"].get(pname, [])
        if not tris:
            continue
        t = np.asarray(tris, dtype=np.float64)          # (n, 3, 8)
        uv = t[:, :, 6:8]
        cuv = uv.mean(axis=1, keepdims=True).repeat(3, axis=1)
        c = 0.5 * sample(tex, uv.reshape(-1, 2)) + 0.5 * sample(tex, cuv.reshape(-1, 2))
        if occ is not None:
            c = c * (0.5 * sample(occ, uv.reshape(-1, 2)) + 0.5 * sample(occ, cuv.reshape(-1, 2)))
        pos.append(t[:, :, 0:3].reshape(-1, 3))
        nrm.append(t[:, :, 3:6].reshape(-1, 3))
        col.append(c)
        part.append(np.full(len(t) * 3, pi))
    pos = np.concatenate(pos)
    nrm = np.concatenate(nrm)
    col = np.concatenate(col)
    part = np.concatenate(part)
    # a vertex's corners (same place, normal and part) share their mean
    # colour, so triangles share the vertex
    key = np.concatenate([np.round(pos * 1e5), np.round(nrm * 100), part[:, None]], axis=1)
    _, group = np.unique(key, axis=0, return_inverse=True)
    group = group.reshape(-1)
    sums = np.zeros((group.max() + 1, 3))
    np.add.at(sums, group, col)
    col = (sums / np.bincount(group)[:, None])[group]
    # dark plastic stays readable on the watch's dark screen
    col = LIFT + (1.0 - LIFT) * col

    # place it: centred on its own bounds, turned, then out to its side
    lo, hi = pos.min(axis=0), pos.max(axis=0)
    centre = (lo + hi) / 2
    sgn = -1.0 if side == "L" else 1.0
    R = rot_y(sgn * math.radians(YAW_DEG)) @ rot_x(math.radians(TILT_DEG))
    # up the screen, before the page's 45 degree tilt: half toward the camera
    offset = np.array([sgn * XOFFSET, RAISE * math.sqrt(0.5), -RAISE * math.sqrt(0.5)])

    def place(p):
        return (blender_to_model(np.asarray(p) - centre) * UNITS) @ R.T + offset

    def turn(v):
        return blender_to_model(np.asarray(v)) @ R.T

    s = Side()
    s.pos = place(pos)
    s.nrm = turn(nrm)
    s.nrm /= np.linalg.norm(s.nrm, axis=1, keepdims=True)
    s.col = np.clip(col, 0, 1)
    s.part = part
    s.tris = np.arange(len(pos)).reshape(-1, 3)

    # the controller's own axes in model space, seen from above the face:
    # right, out of the face, and forward (toward the ring)
    right, up, fwd = turn([-1, 0, 0]), turn([0, 0, 1]), turn([0, -1, 0])
    s.axes = (right, up, fwd)
    pivots = {k: place(v) for k, v in data["pivots"].items()}
    s.anim = {}
    for pi, pname in enumerate(PARTS):
        sel = part == pi
        if pname == "body" or not sel.any():
            s.anim[pname] = None
            continue
        meann = s.nrm[sel].mean(axis=0)
        meann /= np.linalg.norm(meann)
        centroid = s.pos[sel].mean(axis=0)
        pv = pivots.get(pname, centroid)
        if pname == "trigger":
            # it swings about the controller's lateral axis, toward the grip
            s.anim[pname] = (pv, right, np.zeros(3))
        elif pname == "stick":
            s.anim[pname] = (pv, right, fwd)
        elif pname == "grip":
            s.anim[pname] = (pv, -meann, np.zeros(3))
        else:
            # buttons go in along their own facing
            s.anim[pname] = (pv, -meann, np.zeros(3))
    return s


def quantize(s):
    """Unique vertices (position, normal, colour, part) and triangles over them."""
    qp = np.round(s.pos).astype(np.int64)
    qn = np.clip(np.round(s.nrm * 127), -127, 127).astype(np.int64)
    qc = np.clip(np.round(s.col * 255), 0, 255).astype(np.int64)
    keys = {}
    verts = []
    remap = np.zeros(len(qp), dtype=np.int64)
    for i in range(len(qp)):
        k = (tuple(qp[i]), tuple(qn[i]), tuple(qc[i]), int(s.part[i]))
        j = keys.get(k)
        if j is None:
            j = len(verts)
            keys[k] = j
            verts.append(k)
        remap[i] = j
    tris = remap[s.tris]
    tris = tris[(tris[:, 0] != tris[:, 1]) & (tris[:, 1] != tris[:, 2]) & (tris[:, 0] != tris[:, 2])]
    return verts, tris


def make_batches(tris):
    """Greedy: grow each 16-vertex load from a seed triangle, taking the
    unused neighbour that adds the fewest new vertices."""
    vtris = {}
    for ti, t in enumerate(tris):
        for v in t:
            vtris.setdefault(int(v), []).append(ti)
    used = np.zeros(len(tris), dtype=bool)
    batches = []
    seed = 0
    while True:
        while seed < len(tris) and used[seed]:
            seed += 1
        if seed >= len(tris):
            break
        bv = []
        bt = []

        def add(ti):
            used[ti] = True
            bt.append(ti)
            for v in tris[ti]:
                if int(v) not in bv:
                    bv.append(int(v))

        add(seed)
        while True:
            best, bestcost = None, 99
            seen = set()
            for v in bv:
                for ti in vtris[v]:
                    if used[ti] or ti in seen:
                        continue
                    seen.add(ti)
                    cost = sum(1 for x in tris[ti] if int(x) not in bv)
                    if cost < bestcost or (cost == bestcost and ti < best):
                        best, bestcost = ti, cost
            if best is None or len(bv) + bestcost > MAX_BATCH_VERTS:
                break
            add(best)
        local = [[bv.index(int(x)) for x in tris[ti]] for ti in bt]
        batches.append((bv, local))
    return batches


# ------------------------------------------------------------- the runtime's maths
# Kept in step with port/src/gevr_touch_model.c (gevrTouchLight and the part moves).

LIGHT = np.array([-0.35, 0.55, 0.76])
LIGHT = LIGHT / np.linalg.norm(LIGHT)
HALF = (LIGHT + np.array([0.0, 0.0, 1.0]))
HALF = HALF / np.linalg.norm(HALF)
AMBIENT, DIFFUSE, SPECULAR, RIM = 0.40, 0.78, 0.30, 0.30
LIT = np.array([0x50, 0xF0, 0x50]) / 255.0


def light(albedo, n_eye, pressed):
    d = np.clip(n_eye @ LIGHT, 0, None)[:, None]
    h = np.clip(n_eye @ HALF, 0, None)[:, None] ** 16
    r = (1.0 - np.clip(n_eye[:, 2], 0, None))[:, None] ** 3
    c = albedo * (AMBIENT + DIFFUSE * d) + SPECULAR * h + RIM * r * np.array([0.55, 0.75, 0.55])
    c = np.where(pressed[:, None], c * 0.45 + LIT * 0.55, c)
    return np.clip(c, 0, 1)


def watch_matrix(spin, pitch):
    """options.c draw_watch_controller, single controller: lookat * T(pos) * Rz * Rx."""
    def rz(a):
        c, s = math.cos(a), math.sin(a)
        return np.array([[c, -s, 0, 0], [s, c, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]])

    def rx(a):
        c, s = math.cos(a), math.sin(a)
        return np.array([[1, 0, 0, 0], [0, c, -s, 0], [0, s, c, 0], [0, 0, 0, 1]])

    T = np.eye(4)
    T[:3, 3] = [0.0, 200.0, -200.0]
    eye = np.array([-5.0, 2000.0, -168.0])
    L = np.eye(4)
    L[0, :3] = [1, 0, 0]
    L[1, :3] = [0, 0, -1]
    L[2, :3] = [0, 1, 0]
    L[:3, 3] = -L[:3, :3] @ eye
    return L @ T @ rz(spin) @ rx(-pitch - 0.78539819)


def perspective(fovy_deg, aspect, near, far):
    f = 1.0 / math.tan(math.radians(fovy_deg) / 2)
    return np.array([[f / aspect, 0, 0, 0], [0, f, 0, 0],
                     [0, 0, (near + far) / (near - far), 2 * near * far / (near - far)],
                     [0, 0, -1, 0]])


def animate(s, pressed_parts, trigger=0.0, grip=0.0, stick=(0.0, 0.0)):
    p = s.pos.copy()
    n = s.nrm.copy()
    for pi, pname in enumerate(PARTS):
        a = s.anim.get(pname)
        if a is None:
            continue
        sel = s.part == pi
        pv, ax, ax2 = a
        if pname == "trigger" and trigger:
            M = rot_axis(ax, -trigger * TRIGGER_ANGLE)
            p[sel] = (p[sel] - pv) @ M.T + pv
            n[sel] = n[sel] @ M.T
        elif pname == "stick" and any(stick):
            M = rot_axis(ax2, stick[0] * STICK_ANGLE) @ rot_axis(ax, -stick[1] * STICK_ANGLE)
            p[sel] = (p[sel] - pv) @ M.T + pv
            n[sel] = n[sel] @ M.T
        elif pname == "grip" and grip:
            p[sel] = p[sel] + ax * GRIP_TRAVEL * grip
        elif pname in pressed_parts and pname.startswith("btn"):
            p[sel] = p[sel] + ax * BUTTON_TRAVEL
    return p, n


def render(pairs, out, spin=0.0, pitch=0.0, scale=4, pressed=(), trigger=0.0, grip=0.0, stick=(0.0, 0.0), labels=None):
    W, H = 320 * scale, 240 * scale
    img = np.zeros((H, W, 3), dtype=np.float32)
    img[:] = np.array([0x10, 0x30, 0x10]) / 255.0
    zb = np.full((H, W), np.inf, dtype=np.float32)
    M = watch_matrix(spin, pitch)
    P = perspective(50.5, 1.3333334, 1000.0, 3000.0)
    R = M[:3, :3]
    for s in pairs:
        p, n = animate(s, pressed, trigger, grip, stick)
        ph = np.concatenate([p, np.ones((len(p), 1))], axis=1)
        clip = (P @ M @ ph.T).T
        ndc = clip[:, :3] / clip[:, 3:4]
        sx = (ndc[:, 0] * 0.5 + 0.5) * W
        sy = (0.5 - ndc[:, 1] * 0.5) * H
        sz = ndc[:, 2]
        ne = n @ R.T
        isp = np.isin(s.part, [PARTS.index(x) for x in pressed])
        c = light(s.col, ne, isp)
        for t in s.tris:
            xs, ys, zs = sx[t], sy[t], sz[t]
            area = (xs[1] - xs[0]) * (ys[2] - ys[0]) - (xs[2] - xs[0]) * (ys[1] - ys[0])
            if area >= 0:     # back face (screen y down: front faces are negative)
                continue
            x0, x1 = int(max(min(xs.min(), W - 1), 0)), int(min(max(xs.max(), 0), W - 1))
            y0, y1 = int(max(min(ys.min(), H - 1), 0)), int(min(max(ys.max(), 0), H - 1))
            if x1 < x0 or y1 < y0:
                continue
            gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            w0 = ((xs[1] - gx) * (ys[2] - gy) - (xs[2] - gx) * (ys[1] - gy)) / area
            w1 = ((xs[2] - gx) * (ys[0] - gy) - (xs[0] - gx) * (ys[2] - gy)) / area
            w2 = 1 - w0 - w1
            inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
            if not inside.any():
                continue
            z = w0 * zs[0] + w1 * zs[1] + w2 * zs[2]
            sub = zb[y0:y1 + 1, x0:x1 + 1]
            ok = inside & (z < sub)
            sub[ok] = z[ok]
            col = (w0[..., None] * c[t[0]] + w1[..., None] * c[t[1]] + w2[..., None] * c[t[2]])
            img[y0:y1 + 1, x0:x1 + 1][ok] = col[ok]
    im = Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8))
    if labels:
        # the page's text, roughly: a stand-in font at about the watch font's size
        from PIL import ImageFont
        try:
            font = ImageFont.truetype("arialbd.ttf", 9 * scale)
        except OSError:
            font = ImageFont.load_default()
        d = ImageDraw.Draw(im)
        for x, y, text, colour in labels:
            d.text((x * scale, y * scale), text, fill=colour, font=font)
    im.save(out)


# The page's text as gevr_touch_model.c lays it out (for the previews).
PREVIEW_LABELS = [
    (64, 26, "CONTROL STYLE", (0xFF, 0x00, 0xB0)), (170, 26, "1.1 HONEY", (0xA0, 0xFF, 0xA0)),
    (64, 43, "CONTROLLER", (0xFF, 0x00, 0xB0)),
]
for _i, (_n, _a) in enumerate([("Trigger", "Aim"), ("Grip", "Grab"), ("Stick", "Move"), ("Y", "Action"), ("X", "Hand item"), ("Menu", "Pause")]):
    PREVIEW_LABELS += [(36, 133 + 13 * _i, _n, (0x00, 0xAA, 0x00)), (86, 133 + 13 * _i, _a, (0x00, 0xFF, 0x00))]
for _i, (_n, _a) in enumerate([("Trigger", "Fire"), ("Grip", "Aim"), ("Stick", "Turn"), ("B", "Action"), ("A", "Weapon")]):
    PREVIEW_LABELS += [(164, 133 + 13 * _i, _n, (0x00, 0xAA, 0x00)), (214, 133 + 13 * _i, _a, (0x00, 0xFF, 0x00))]


# ------------------------------------------------------------------ C output

def c_name(kind, side):
    return "s_%s%s" % (kind, side)


def emit(kinds_out, path):
    lines = []
    w = lines.append
    w("/*")
    w(" * GENERATED by tools/gevr_touch_model_gen.py - do not edit by hand.")
    w(" *")
    w(" * Meta Quest Touch controllers for the watch's Controls page, from Meta's")
    w(" * controller art (v1.8), whose licence allows using it to refer to the")
    w(" * controllers in a VR game and its manuals (docs/touch-controllers.md).")
    w(" * Vertices: x, y, z, normal x, y, z, part, red, green, blue. Batches: the")
    w(" * vertex count, the triangle count, the vertex indices, then each triangle's")
    w(" * three batch-local indices packed a | b << 4 | c << 8.")
    w(" */")
    w("")
    w('#include "gevr_touch_model.h"')
    w("")
    for kind, sides in kinds_out:
        for side, (verts, batches, s) in zip(SIDES, sides):
            nm = c_name(kind, side)
            w("static const GevrTouchVert %sVerts[%d] = {" % (nm, len(verts)))
            for (p, n, c, part) in verts:
                w("    { %d, %d, %d, %d, %d, %d, %d, %d, %d, %d }," % (p[0], p[1], p[2], n[0], n[1], n[2], part, c[0], c[1], c[2]))
            w("};")
            w("")
            words = sum(2 + len(bv) + len(bt) for bv, bt in batches)
            w("static const u16 %sBatches[%d] = {" % (nm, words))
            for bv, bt in batches:
                tri = ["0x%03x" % (a | b << 4 | c << 8) for a, b, c in bt]
                w("    %d, %d, %s, %s," % (len(bv), len(bt), ", ".join(str(v) for v in bv), ", ".join(tri)))
            w("};")
            w("")
    w("const GevrTouchModel gevrTouchModels[GEVR_TOUCH_KINDS] = {")
    for kind, sides in kinds_out:
        w("    { \"%s\", {" % kind)
        for side, (verts, batches, s) in zip(SIDES, sides):
            nm = c_name(kind, side)
            nstream = sum(len(bv) for bv, bt in batches)
            ntris = sum(len(bt) for bv, bt in batches)
            w("        { %sVerts, %sBatches, %d, %d, %d, %d, {" % (nm, nm, len(verts), len(batches), nstream, ntris))
            for pname in PARTS:
                a = s.anim.get(pname)
                if a is None:
                    w("            { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } },   /* %s */" % pname)
                else:
                    pv, ax, ax2 = a
                    f = lambda v: "{ %.2ff, %.4ff, %.4ff }" % tuple(v) if False else "{ %s }" % ", ".join("%.4ff" % x for x in v)
                    w("            { %s, %s, %s },   /* %s */" % (f(pv), f(ax), f(ax2), pname))
            w("        } },")
        w("    } },")
    w("};")
    w("")
    w("const f32 gevrTouchTriggerAngle = %.5ff;" % TRIGGER_ANGLE)
    w("const f32 gevrTouchStickAngle = %.5ff;" % STICK_ANGLE)
    w("const f32 gevrTouchButtonTravel = %.3ff;" % BUTTON_TRAVEL)
    w("const f32 gevrTouchGripTravel = %.3ff;" % GRIP_TRAVEL)
    w("const f32 gevrTouchStickTravel = %.3ff;" % STICK_TRAVEL)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--art", required=True, help="the extracted controller-art folder")
    ap.add_argument("--json", required=True, help="the folder of <kind>.json exports")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "port", "src", "gevr_touch_model_data.c"))
    ap.add_argument("--preview", help="write preview renders of the page here")
    ap.add_argument("--kinds", default=",".join(KINDS))
    ap.add_argument("--layout", help="try a layout: units,xoffset,raise,yaw,tilt (previews only, no C)")
    args = ap.parse_args()
    if args.layout:
        global UNITS, XOFFSET, RAISE, YAW_DEG, TILT_DEG
        UNITS, XOFFSET, RAISE, YAW_DEG, TILT_DEG = (float(x) for x in args.layout.split(","))

    kinds_out = []
    for kind in args.kinds.split(","):
        with open(os.path.join(args.json, kind + ".json")) as f:
            data = json.load(f)
        sides = []
        pairs = []
        for side in SIDES:
            s = build_side(kind, side, data["sides"][side], args.art)
            verts, tris = quantize(s)
            batches = make_batches(tris)
            nstream = sum(len(bv) for bv, bt in batches)
            print("%s %s: %d triangles, %d vertices, %d batches, %d loaded (%.2f triangles a load vertex)"
                  % (kind, side, len(tris), len(verts), len(batches), nstream, len(tris) / max(nstream, 1)))
            sides.append((verts, batches, s))
            pairs.append(s)
        kinds_out.append((kind, sides))
        if args.preview:
            os.makedirs(args.preview, exist_ok=True)
            render(pairs, os.path.join(args.preview, kind + "_rest.png"), labels=PREVIEW_LABELS)
            render(pairs, os.path.join(args.preview, kind + "_pressed.png"), pressed=("trigger", "grip", "btn_lower", "stick"),
                   trigger=1.0, grip=1.0, stick=(1.0, 1.0))
            render(pairs, os.path.join(args.preview, kind + "_spin.png"), spin=2.2)
    if len(kinds_out) == len(KINDS) and not args.layout:
        emit(kinds_out, args.out)
        print("wrote", args.out)


if __name__ == "__main__":
    main()
