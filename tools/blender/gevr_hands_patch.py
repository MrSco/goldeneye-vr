"""
Close the holes in a first-person hand/arm model and write the patch (issue #9).

Run headless:

    blender -b --factory-startup --python tools/blender/gevr_hands_patch.py -- \
        build/handmodels/Csuit_lf_handZ.json build/handmodels/Csuit_lf_handZ \
        [--list] [--render 0x1c0,0x1f0,0x2f8,0x328] [--frame 0x1c0] [--save]

The model comes from tools/gevr_model_export.py (ROM-derived, under build/).
The recipe - which holes to close and how - is ours and lives in
tools/handpatch/<model>.recipe.json. The patch this writes,
tools/handpatch/<model>.patch.json, holds only what we add:

  - new triangles, whose corners are ROM vertices named by DL node and index
    into that node's vertex block (never their coordinates), or points we
    authored;
  - our texture coordinates and shade for each corner;
  - a fingerprint of each patched node's vertex block (a hash, not the data),
    so the game applies a patch only to the model it was made for.

Operations (recipe "ops"):
  fill    close one boundary loop with Liepa's hole triangulation: dynamic
          programming over the loop that keeps the largest angle between
          neighbouring triangles smallest, then the area. It zips a finger's
          open rims together instead of webbing across fingers, and adds no
          vertices.
  earclip close one loop by clipping convex ears, shortest first: for a
          curled hand, where "fill" webs the fingers to the palm like a mitten.
  bridge  join two loops with a strip (the gap between a jacket cuff and the
          shirt cuff inside it).
Loops are named by any ROM vertex index on them ("loop": 57).

--list prints every part's loops with such an index. --render draws before
and after views of those parts (--frame: framed on these; --tint: patch faces
magenta); --compare my,q2 --tag hand also writes compare_hand.png, the
before row over the after row for those views.
"""

import json
import math
import os
import sys

import bmesh
import bpy
from mathutils import Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gevr_hands_import as H  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RECIPES = os.path.join(REPO, "tools", "handpatch")


def args():
    a = sys.argv[sys.argv.index("--") + 1:]
    o = {"src": a[0], "out": a[1], "list": False, "render": None, "frame": None, "save": False,
         "tint": False, "compare": None, "tag": "patch"}
    i = 2
    while i < len(a):
        k = a[i]
        if k in ("--list", "--save", "--tint"):
            o[k[2:]] = True
            i += 1
        elif k in ("--render", "--frame"):
            o[k[2:]] = [int(x, 16) for x in a[i + 1].split(",")]
            i += 2
        elif k == "--compare":
            o["compare"] = a[i + 1].split(",")
            i += 2
        elif k == "--tag":
            o["tag"] = a[i + 1]
            i += 2
        else:
            raise SystemExit("unknown argument " + k)
    return o


# ---------------------------------------------------------------------------
# Boundary loops, directed as the hole's own outline
# ---------------------------------------------------------------------------

def directed_loops(bm):
    """Each hole as a list of verts. Consecutive pairs (a, b) are the twins of
    the neighbouring faces' edges, so a triangle (v[i], v[j], v[k]) with
    i < j < k winds the same way as the faces around it."""
    bm.verts.index_update()
    bm.verts.ensure_lookup_table()
    out = {}
    face_of = {}
    for e in bm.edges:
        if len(e.link_faces) != 1:
            continue
        f = e.link_faces[0]
        # the face walks the edge a -> b; the hole walks it b -> a
        for lp in f.loops:
            if lp.edge == e:
                a, b = lp.vert, lp.link_loop_next.vert
                out.setdefault(b.index, []).append(a)
                face_of[(b.index, a.index)] = f
                break
    used = set()
    loops = []
    dropped = 0
    for start in list(out):
        for first in out[start]:
            if (start, first.index) in used:
                continue
            path = [bm.verts[start]]
            cur = bm.verts[start]
            nxt = first
            while True:
                used.add((cur.index, nxt.index))
                if nxt.index in [v.index for v in path]:
                    k = [v.index for v in path].index(nxt.index)
                    loops.append(path[k:])       # a simple cycle closes here
                    path = path[:k + 1]
                    if k == 0:
                        break
                else:
                    path.append(nxt)
                cur = nxt
                cands = [v for v in out.get(cur.index, []) if (cur.index, v.index) not in used]
                if not cands:
                    if len(path) > 1:
                        dropped += 1
                    break
                nxt = cands[0]
    if dropped and not getattr(directed_loops, "quiet", False):
        print("   note: %d boundary paths never close (faces wound both ways along them);"
              " they are not offered as loops" % dropped)
    return [lp for lp in loops if len(lp) >= 3], face_of


def loop_info(lp, lay_idx):
    pts = [v.co for v in lp]
    centre = sum(pts, Vector()) / len(pts)
    per = sum((lp[i].co - lp[(i + 1) % len(lp)].co).length for i in range(len(lp)))
    return centre, per, min(v[lay_idx] for v in lp)


def find_loop(loops, lay_idx, rom_idx):
    for lp in loops:
        if any(v[lay_idx] == rom_idx for v in lp):
            return lp
    raise SystemExit("no boundary loop through ROM vertex %d" % rom_idx)


# ---------------------------------------------------------------------------
# Liepa's hole triangulation
# ---------------------------------------------------------------------------

def tri_normal(a, b, c):
    n = (b - a).cross(c - a)
    return n.normalized() if n.length > 1e-9 else None


def fill_loop(lp, face_of):
    """Triangles (as vertex triples) closing the loop, by Liepa's DP."""
    n = len(lp)
    P = [v.co.copy() for v in lp]
    # the outside face across each loop edge (i, i+1)
    edge_normal = []
    for i in range(n):
        f = face_of[(lp[i].index, lp[(i + 1) % n].index)]
        edge_normal.append(f.normal.copy())
    INF = (1e9, 1e18)
    W = {}
    O = {}
    for i in range(n - 1):
        W[(i, i + 1)] = (0.0, 0.0)

    def nb_normal(i, j):
        """Normal of the triangle on the far side of edge (i, j), already chosen."""
        if j == i + 1:
            return edge_normal[i]
        m = O.get((i, j))
        if m is None:
            return None
        return tri_normal(P[i], P[m], P[j])

    def angle(n1, n2):
        if n1 is None or n2 is None:
            return math.pi
        return math.acos(max(-1.0, min(1.0, n1.dot(n2))))

    for span in range(2, n):
        for i in range(0, n - span):
            j = i + span
            best, bestm = INF, None
            for m in range(i + 1, j):
                a, b = W.get((i, m), INF), W.get((m, j), INF)
                if a[0] >= INF[0] or b[0] >= INF[0]:
                    continue
                if lp[i] is lp[m] or lp[m] is lp[j] or lp[i] is lp[j]:
                    continue
                t = tri_normal(P[i], P[m], P[j])
                if t is None:
                    continue
                d = max(a[0], b[0], angle(t, nb_normal(i, m)), angle(t, nb_normal(m, j)))
                if span == n - 1:
                    d = max(d, angle(t, edge_normal[n - 1]))
                area = a[1] + b[1] + 0.5 * (P[m] - P[i]).cross(P[j] - P[i]).length
                cand = (d, area)
                if bestm is None or (cand[0] < best[0] - 1e-3) or \
                        (abs(cand[0] - best[0]) <= 1e-3 and cand[1] < best[1]):
                    best, bestm = cand, m
            if bestm is not None:
                W[(i, j)] = best
                O[(i, j)] = bestm
    tris = []
    stack = [(0, n - 1)]
    while stack:
        i, j = stack.pop()
        if j - i < 2:
            continue
        m = O.get((i, j))
        if m is None:
            raise SystemExit("loop of %d could not be filled" % n)
        tris.append((lp[i], lp[m], lp[j]))
        stack += [(i, m), (m, j)]
    return tris, W[(0, n - 1)]


def earclip_loop(lp, maxdiag=None):
    """Close a loop by clipping ears, shortest diagonal first, never at a
    reflex corner. With maxdiag, stop once every ear left is wider than that
    (the fingers are zipped; what is left is the palm, for "fill"). Reflex is judged against the surface the loop cuts: the
    fill should face away from the faces around each corner. At a fingertip
    the ear closes the finger's channel; at the valley between two fingers
    the ear would span the gap and faces the wrong way, so fingers are zipped
    one by one from the tip and never webbed together or to the palm."""
    expect = {}
    for v in lp:
        acc = Vector()
        for f in v.link_faces:
            acc += f.normal * f.calc_area()
        expect[v.index] = -acc.normalized() if acc.length > 1e-9 else Vector()
    ring = list(lp)
    tris = []
    forced = 0
    while len(ring) > 3:
        best = None
        fallback = None
        for i in range(len(ring)):
            p, c, n = ring[i - 1], ring[i], ring[(i + 1) % len(ring)]
            t = (c.co - p.co).cross(n.co - p.co)
            if t.length < 1e-9 or p is n:
                continue
            conv = t.normalized().dot((expect[p.index] + expect[c.index] + expect[n.index]).normalized())
            diag = (n.co - p.co).length
            if fallback is None or conv > fallback[0]:
                fallback = (conv, i)
            if conv <= 0.05:
                continue
            if maxdiag is not None and diag > maxdiag:
                continue
            if best is None or diag < best[0]:
                best = (diag, i)
        if best is None:
            if maxdiag is not None:
                return tris, forced
            if fallback is None:
                break
            best = (0, fallback[1])
            forced += 1
        i = best[1]
        p, c, n = ring[i - 1], ring[i], ring[(i + 1) % len(ring)]
        tris.append((p, c, n))
        del ring[i]
    if len(ring) == 3:
        tris.append(tuple(ring))
    return tris, forced


def bridge_loops(la, lb):
    """Triangles joining loop la to loop lb (the ring between them)."""
    # lb runs the other way round the ring; walk it backwards from the vertex
    # nearest la[0]
    k = min(range(len(lb)), key=lambda j: (lb[j].co - la[0].co).length)
    B = [lb[(k - j) % len(lb)] for j in range(len(lb))]
    A = la
    tris = []
    i = j = 0
    na, nb = len(A), len(B)
    while i < na or j < nb:
        a0, a1 = A[i % na], A[(i + 1) % na]
        b0, b1 = B[j % nb], B[(j + 1) % nb]
        adv_a = j >= nb or (i < na and (a1.co - b0.co).length <= (a0.co - b1.co).length)
        if adv_a:
            tris.append((a0, a1, b0))
            i += 1
        else:
            tris.append((a0, b1, b0))
            j += 1
    return tris


# ---------------------------------------------------------------------------
# Refinement and fairing (the second half of Liepa's method)
# ---------------------------------------------------------------------------

def refine(bm, lay_patch, k):
    """Split every edge inside patch k once (edges on its rim stay whole, or
    the ROM faces beyond them would get T-junctions), then triangulate."""
    inner = [e for e in bm.edges
             if len(e.link_faces) == 2 and all(f[lay_patch] == k for f in e.link_faces)]
    if not inner:
        return
    bmesh.ops.subdivide_edges(bm, edges=inner, cuts=1, use_grid_fill=False)
    faces = [f for f in bm.faces if f[lay_patch] == k and len(f.verts) > 3]
    if faces:
        bmesh.ops.triangulate(bm, faces=faces)


def fair(bm, n_orig, lay_idx, lay_mtx, free):
    """Place every new point so the patch continues the curvature around it:
    the bi-Laplacian (uniform weights) is zero at each new point, with every
    ROM vertex held fixed. The system is linear, so each new point comes out
    as fixed weights over ROM vertices; the patch stores the weights and the
    game computes the point from the player's own ROM."""
    import numpy as np
    verts = list(bm.verts)
    n = len(verts)
    new = sorted(free)
    if not new:
        return {}
    L = np.zeros((n, n))
    for v in verts:
        nb = [e.other_vert(v).index for e in v.link_edges]
        L[v.index, v.index] = 1.0
        for j in nb:
            L[v.index, j] -= 1.0 / len(nb)
    M = L @ L
    U = np.array(new)
    F = np.array([i for i in range(n) if i < n_orig])  # ROM vertices only
    W = -np.linalg.solve(M[np.ix_(U, U)], M[np.ix_(U, F)])
    X = np.array([list(verts[i].co) for i in F])
    mixes = {}
    for r, u in enumerate(U):
        w = W[r]
        keep = np.nonzero(np.abs(w) > 1e-3)[0]
        wk = w[keep] / w[keep].sum()
        verts[u].co = Vector(wk @ X[keep])
        refs = [verts[F[j]] for j in keep]
        mtx = {v[lay_mtx] for v in refs}
        if len(mtx) > 1:
            print("   warning: new point %d mixes matrices %s" % (u, sorted(mtx)))
        mixes[int(u)] = {"mix": [[int(v[lay_idx]), round(float(x), 5)] for v, x in zip(refs, wk)],
                         "mtx": int(refs[0][lay_mtx])}
    return mixes


# ---------------------------------------------------------------------------
# New geometry: the back half of the watch band
# ---------------------------------------------------------------------------

def vert_by_rom(bm, lay_idx, rom):
    for v in bm.verts:
        if v[lay_idx] == rom:
            return v
    raise SystemExit("no vertex with ROM index %d" % rom)


def ribbon(bm, lay_idx, op):
    """A strip from one edge to another round an axis along x (the wrist):
    the band's missing back half, from one strap end under the wrist to the
    other. It leaves each end at the strap's radius and tightens to
    bottom_radius halfway, so it sits on the cuff. Returns the new faces as
    (verts, (s, t) per corner) and the new points."""
    a0, a1 = (vert_by_rom(bm, lay_idx, r) for r in op["from"])
    b0, b1 = (vert_by_rom(bm, lay_idx, r) for r in op["to"])
    cy, cz = op["axis"]
    n = op.get("segments", 8)

    def polar(p):
        return math.hypot(p.y - cy, p.z - cz), math.atan2(p.y - cy, p.z - cz)

    rails = []
    for pa, pb in ((a0, b0), (a1, b1)):
        ra, ta = polar(pa.co)
        rb, tb = polar(pb.co)
        # go round the underside: through angle -90 degrees (y below the axis)
        while tb < ta:
            tb += 2 * math.pi
        if not (ta < 1.5 * math.pi < tb):
            ta, tb = ta + 2 * math.pi, tb
            while tb < ta:
                tb += 2 * math.pi
        rail = [pa]
        for i in range(1, n):
            t = i / n
            r_lin = (1 - t) * ra + t * rb
            r = r_lin - (r_lin - op["bottom_radius"]) * math.sin(math.pi * t) ** 0.8
            th = ta + t * (tb - ta)
            x = (1 - t) * pa.co.x + t * pb.co.x
            rail.append(bm.verts.new((x, cy + r * math.sin(th), cz + r * math.cos(th))))
        rail.append(pb)
        rails.append(rail)
    # texture: across the band like the strap's end (s from the "from" pair),
    # along it at the strap's rate, continuing past its end (the texture wraps)
    s0, s1 = op["s"]
    t_end, t_rate = op["t"]
    faces = []
    along = 0.0
    for i in range(n):
        seg = ((rails[0][i + 1].co - rails[0][i].co).length + (rails[1][i + 1].co - rails[1][i].co).length) / 2
        tA = t_end - along * t_rate
        tB = t_end - (along + seg) * t_rate
        along += seg
        quad = (rails[0][i], rails[1][i], rails[1][i + 1], rails[0][i + 1])
        st = ((s0, tA), (s1, tA), (s1, tB), (s0, tB))
        faces.append((quad, st))
    new = [v for rail in rails for v in rail[1:-1]]
    return faces, new


def affine_mix(bm, lay_idx, lay_mtx, v, anchors):
    """v as an affine combination of four ROM vertices (weights sum to 1), so
    the patch stores no coordinates."""
    import numpy as np
    A = [vert_by_rom(bm, lay_idx, r) for r in anchors]
    M = np.array([[a.co.x for a in A], [a.co.y for a in A], [a.co.z for a in A], [1, 1, 1, 1]])
    w = np.linalg.solve(M, np.array([v.co.x, v.co.y, v.co.z, 1.0]))
    return {"mix": [[int(a[lay_idx]), round(float(x), 6)] for a, x in zip(A, w)],
            "mtx": int(A[0][lay_mtx])}


# ---------------------------------------------------------------------------
# Texture coordinates for new faces
# ---------------------------------------------------------------------------

def plane_uv(tris, box):
    """Project the triangles onto their best-fit plane and fit them into box
    (u0, v0, u1, v1 in 0..1 of the texture), keeping the aspect ratio."""
    pts = [v.co for t in tris for v in t]
    c = sum(pts, Vector()) / len(pts)
    nrm = Vector()
    for t in tris:
        nn = (t[1].co - t[0].co).cross(t[2].co - t[0].co)
        nrm += nn
    nrm = nrm.normalized() if nrm.length > 1e-9 else Vector((0, 0, 1))
    ax = (Vector((1, 0, 0)) if abs(nrm.x) < 0.9 else Vector((0, 1, 0))).cross(nrm).normalized()
    ay = nrm.cross(ax)
    uv2 = {v.index: ((v.co - c).dot(ax), (v.co - c).dot(ay)) for t in tris for v in t}
    xs = [p[0] for p in uv2.values()]
    ys = [p[1] for p in uv2.values()]
    w = max(max(xs) - min(xs), 1e-6)
    h = max(max(ys) - min(ys), 1e-6)
    s = min((box[2] - box[0]) / w, (box[3] - box[1]) / h)
    return {k: (box[0] + (p[0] - min(xs)) * s, box[1] + (p[1] - min(ys)) * s) for k, p in uv2.items()}


# ---------------------------------------------------------------------------

def fnv_vertices(model, node):
    """FNV-1a over the node's vertex block, each x, y, z as a little-endian
    s16 (the game hashes its converted block the same way)."""
    h = 0x811C9DC5
    part = next(p for p in model["parts"] if p["node"] == node)
    for xyz in part["block_xyz"]:
        for c in xyz:
            for byte in (c & 0xFF, (c >> 8) & 0xFF):
                h = ((h ^ byte) * 0x01000193) & 0xFFFFFFFF
    return "0x%08x" % h


def compare_sheet(out, tag, views):
    """Before over after for each view, side by side, as compare_<tag>.png."""
    import numpy as np
    rows = []
    for when in ("before", "after"):
        tiles = []
        for v in views:
            name = v.replace("+", "p").replace("-", "m")
            img = bpy.data.images.load(os.path.join(out, "view_%s_%s_%s.png" % (tag, when, name)))
            w, h = img.size
            px = np.array(img.pixels[:], dtype=np.float32).reshape(h, w, 4)
            tiles.append(px)
            bpy.data.images.remove(img)
        rows.append(np.concatenate(tiles, axis=1))
    sheet = np.concatenate([rows[1], rows[0]], axis=0)  # pixels run bottom-up: before on top
    h, w = sheet.shape[:2]
    img = bpy.data.images.new("compare", w, h, alpha=True)
    img.pixels = sheet.ravel()
    path = os.path.join(out, "compare_%s.png" % tag)
    img.filepath_raw = path
    img.file_format = "PNG"
    img.save()
    return path


def main():
    o = args()
    with open(o["src"], encoding="utf-8") as f:
        model = json.load(f)
    name = model["model"]
    tex = os.path.join(os.path.dirname(os.path.abspath(o["src"])), "tex")
    H.TEXDIR = tex if os.path.isdir(tex) else None
    bpy.ops.wm.read_factory_settings(use_empty=True)
    mats = {}
    objs = {}
    for p in model["parts"]:
        ob = H.build_part(model, p["node"], mats)
        if ob is not None:
            objs[p["node"]] = ob

    if o["list"]:
        for node, ob in objs.items():
            bm = bmesh.new()
            bm.from_mesh(ob.data)
            bm.verts.ensure_lookup_table()
            lay = bm.verts.layers.int["rom_idx"]
            loops, _ = directed_loops(bm)
            print("part 0x%04x: %d loops" % (node, len(loops)))
            for lp in sorted(loops, key=lambda l: -len(l)):
                c, per, idx = loop_info(lp, lay)
                lo = [min(v.co[a] for v in lp) for a in range(3)]
                hi = [max(v.co[a] for v in lp) for a in range(3)]
                print("   loop %3d verts  perimeter %7.0f  centre (%6.0f %6.0f %6.0f)  rom %-4d"
                      "  x %.0f..%.0f  y %.0f..%.0f  z %.0f..%.0f"
                      % (len(lp), per, c.x, c.y, c.z, idx, lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]))
            bm.free()
        return

    recipe_path = os.path.join(RECIPES, name + ".recipe.json")
    with open(recipe_path, encoding="utf-8") as f:
        recipe = json.load(f)

    before = None
    if o["render"]:
        before = H.render_views([objs[n] for n in (o["frame"] or o["render"]) if n in objs],
                                o["out"], o["tag"] + "_before",
                                hide=[ob for n, ob in objs.items() if n not in o["render"]])

    patch = {"model": name, "note": "Ours: triangles over ROM vertex indices, our UVs. No ROM data.",
             "parts": []}
    patch_mat = bpy.data.materials.new("patch_tint")
    patch_mat.diffuse_color = (1.0, 0.1, 0.8, 1)
    for node_hex, ops in recipe["parts"].items():
        node = int(node_hex, 16)
        ob = objs[node]
        bm = bmesh.new()
        bm.from_mesh(ob.data)
        bm.verts.ensure_lookup_table()
        n_orig = len(bm.verts)
        lay_idx = bm.verts.layers.int["rom_idx"]
        lay_mtx = bm.verts.layers.int["rom_mtx"]
        lay_patch = bm.faces.layers.int.new("patch")
        uvl = bm.loops.layers.uv["UVMap"]
        fixed_st = {}   # corners whose texture coordinates the op chose itself (s, t)
        anchored = {}   # new points placed by an op, stored as affine weights
        for k, op in enumerate(ops, 1):
            loops, face_of = directed_loops(bm)
            if op["op"] == "fill":
                lp = find_loop(loops, lay_idx, op["loop"])
                tris, w = fill_loop(lp, face_of)
                print("0x%04x fill loop %d (%d verts): %d triangles, max angle %.0f deg"
                      % (node, op["loop"], len(lp), len(tris), math.degrees(w[0])))
            elif op["op"] == "earclip":
                lp = find_loop(loops, lay_idx, op["loop"])
                tris, forced = earclip_loop(lp, op.get("maxdiag"))
                print("0x%04x earclip loop %d (%d verts): %d triangles, %d forced at reflex corners"
                      % (node, op["loop"], len(lp), len(tris), forced))
            elif op["op"] == "zipfill":
                # zip the narrow channels (fingers), then Liepa-fill what is left
                lp = find_loop(loops, lay_idx, op["loop"])
                tris, _ = earclip_loop(lp, op["maxdiag"])
                zipped = 0
                for t in tris:
                    try:
                        f = bm.faces.new(t)
                        f[lay_patch] = k
                        f.smooth = False
                        zipped += 1
                    except ValueError:
                        pass
                loops, face_of = directed_loops(bm)
                rest = find_loop(loops, lay_idx, op["palm"])
                tris, w = fill_loop(rest, face_of)
                print("0x%04x zipfill loop %d (%d verts): %d zipped, palm %d verts -> %d triangles, max angle %.0f deg"
                      % (node, op["loop"], len(lp), zipped, len(rest), len(tris), math.degrees(w[0])))
            elif op["op"] == "bridge":
                la = find_loop(loops, lay_idx, op["loops"][0])
                lb = find_loop(loops, lay_idx, op["loops"][1])
                tris = bridge_loops(la, lb)
                print("0x%04x bridge loops %s: %d triangles" % (node, op["loops"], len(tris)))
            elif op["op"] == "ribbon":
                quads, new_pts = ribbon(bm, lay_idx, op)
                tris = []
                made = []
                for quad, st in quads:
                    for tri in ((0, 1, 2), (0, 2, 3)):
                        f = bm.faces.new([quad[i] for i in tri])
                        f[lay_patch] = k
                        f.smooth = False
                        made.append(f)
                        for i in tri:
                            fixed_st[quad[i]] = st[i]
                # the first quad starts on the strap's end edge a0 -> a1; the
                # strap must walk that edge the other way
                a0, a1 = quads[0][0][0], quads[0][0][1]
                e = next(e for e in a0.link_edges if e.other_vert(a0) is a1)
                strap = [f for f in e.link_faces if f[lay_patch] == 0]
                if strap:
                    lp_ = next(l for l in strap[0].loops if l.edge is e)
                    if lp_.vert is a0:
                        bmesh.ops.reverse_faces(bm, faces=made)
                bm.normal_update()
                for v in new_pts:
                    anchored[v] = affine_mix(bm, lay_idx, lay_mtx, v, op["anchors"])
                print("0x%04x ribbon %s -> %s: %d triangles, %d new points"
                      % (node, op["from"], op["to"], 2 * len(quads), len(new_pts)))
            else:
                raise SystemExit("unknown op " + op["op"])
            for t in tris:
                try:
                    f = bm.faces.new(t)
                except ValueError:
                    print("   skipped a triangle that already exists")
                    continue
                f[lay_patch] = k
                f.smooth = False
            for _ in range(op.get("fair", 0)):
                refine(bm, lay_patch, k)
        bm.verts.index_update()
        bm.verts.ensure_lookup_table()
        faired = {v.index for f in bm.faces if f[lay_patch] and ops[f[lay_patch] - 1].get("fair")
                  for v in f.verts if v.index >= n_orig}
        mixes = fair(bm, n_orig, lay_idx, lay_mtx, faired)
        for v, m in anchored.items():
            mixes[v.index] = m

        groups = []
        for k, op in enumerate(ops, 1):
            faces = [f for f in bm.faces if f[lay_patch] == k]
            texnum = int(op["tex"], 16)
            info = model["textures"]["0x%x" % texnum]
            if op["op"] == "ribbon":
                uv = {v.index: (fixed_st[v][0] / 32.0 / info["w"], fixed_st[v][1] / 32.0 / info["h"])
                      for f in faces for v in f.verts}
            else:
                uv = plane_uv([tuple(f.verts) for f in faces], op.get("uvbox", [0, 0, 1, 1]))
            slot = None
            for i, m in enumerate(ob.data.materials):
                if m.name.startswith("tex_%x" % texnum):
                    slot = i
            if slot is None:
                ob.data.materials.append(H.material_for(texnum, mats))
                slot = len(ob.data.materials) - 1
            if o["tint"]:
                if patch_mat.name not in [m.name for m in ob.data.materials]:
                    ob.data.materials.append(patch_mat)
                slot = [m.name for m in ob.data.materials].index(patch_mat.name)
            verts, index, out_tris = [], {}, []
            for f in faces:
                f.material_index = slot
                tri_ids = []
                for lp_ in f.loops:
                    v = lp_.vert
                    u, vv = uv[v.index]
                    lp_[uvl].uv = (u, 1 - vv)
                    if v.index not in index:
                        index[v.index] = len(verts)
                        e = {"s": round(u * info["w"] * 32), "t": round(vv * info["h"] * 32)}
                        if v.index < n_orig:
                            e.update({"ref": v[lay_idx], "mtx": v[lay_mtx]})
                        else:
                            e.update(mixes[v.index])
                        verts.append(e)
                    tri_ids.append(index[v.index])
                out_tris.append(tri_ids)
            groups.append({"op": op["op"], "why": op.get("why", ""), "tex": "0x%03x" % texnum,
                           "shade": op.get("shade", [255, 255, 255, 255]),
                           "verts": verts, "tris": out_tris})
            print("0x%04x op %d %s: %d triangles, %d corners (%d new points)"
                  % (node, k, op["op"], len(out_tris), len(verts),
                     sum(1 for e in verts if "mix" in e)))
        # winding check: every edge with two faces must be walked both ways
        bad = 0
        for e in bm.edges:
            if len(e.link_faces) == 2:
                dirs = []
                for f in e.link_faces:
                    for lp_ in f.loops:
                        if lp_.edge == e:
                            dirs.append((lp_.vert.index, lp_.link_loop_next.vert.index))
                if dirs[0] == dirs[1]:
                    bad += 1
        remaining = sum(1 for e in bm.edges if len(e.link_faces) == 1)
        over = sum(1 for e in bm.edges if len(e.link_faces) > 2)
        print("0x%04x: %d open edges left, %d wound inconsistently, %d shared by 3+ faces"
              % (node, remaining, bad, over))
        bm.normal_update()
        bm.to_mesh(ob.data)
        bm.free()
        part = next(p for p in model["parts"] if p["node"] == node)
        patch["parts"].append({"node": node_hex, "numvtx": part["numvtx"],
                               "fingerprint": fnv_vertices(model, node), "groups": groups})

    out_patch = os.path.join(RECIPES, name + ".patch.json")
    with open(out_patch, "w", encoding="utf-8", newline="\n") as f:
        json.dump(patch, f, indent=1)
        f.write("\n")
    print("wrote " + out_patch)

    if o["render"]:
        for p in H.render_views([objs[n] for n in (o["frame"] or o["render"]) if n in objs],
                                o["out"], o["tag"] + "_after",
                                hide=[ob for n, ob in objs.items() if n not in o["render"]]):
            print("wrote " + p)
        if o["compare"]:
            print("wrote " + compare_sheet(o["out"], o["tag"], o["compare"]))
    if o["save"]:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(o["out"], name + "_patched.blend"))


if __name__ == "__main__":
    main()
