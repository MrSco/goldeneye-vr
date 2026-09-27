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
          With "maxdiag" it only zips the narrow channels (fingers, an open
          forearm) and leaves the rest for a fill of "loop": "largest".
  tube    rebuild the missing bottom half of a finger or forearm modelled as
          the top half of a tube, from its two rails ("rails": [[base..tip],
          [base..tip]]) and "tip" (null for a cut end): a half-round arc under
          each cross-section, joined into the tube's other half.
  skirt   close the gap between a hand's open rim ("chain": a ROM vertex on
          it, or "largest") and the object it holds ("to": its nodes, or
          "rest": all the model's other parts): a strip from each rim
          vertex to the nearest point on the object, so a fist round a grip
          shows skin meeting metal, not the inside of the fingers.
  bridge  join two loops with a strip (the gap between a jacket cuff and the
          shirt cuff inside it).
Op options: "fair": N refines and fairs a fill's new points; "dome": x then
raises them by x times the loop's radius in the middle (a palm's cushion, a
fingertip). "loop": "largest" / "largest:2" names the largest loops left. Every new
point is stored as affine weights over four ROM vertices on one bone.
  ribbon  new geometry between two edges round an axis (the watch band).
Loops and points are named by any ROM vertex on them: 57 (an index in the
host node's vertex block) or "0x02b8:57" (node and index).

A recipe patches assemblies: {"host": node, "nodes": [...], "ops": [...]}.
The nodes are welded into one mesh (per position and matrix), so the seams
between a gun hand's parts are not holes; the patch is drawn with the host
node (for a gun hand, any node on switches 8-13: they switch together). The
older form "parts": {node: ops} is an assembly of one node. A recipe of
{"same_as": "GwppkZ"} borrows that model's recipe for a model with the same
hand, mapping its nodes by vertex-block fingerprint (both exports needed).
--list --assemble 0x2b8,0x318 lists the loops of such a welded set.

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
from mathutils.bvhtree import BVHTree

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
        elif k == "--assemble":
            o.setdefault("assemble", []).append([int(x, 16) for x in a[i + 1].split(",")])
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
    # half-edges that cannot lie on a cycle (faces wound both ways along a
    # rim leave a vertex with nothing coming in or going out) are pruned first
    edges = {(b, a.index) for b, lst in out.items() for a in lst}
    total = len(edges)
    while True:
        outdeg, indeg = {}, {}
        for a, b in edges:
            outdeg[a] = outdeg.get(a, 0) + 1
            indeg[b] = indeg.get(b, 0) + 1
        keep = {(a, b) for a, b in edges if indeg.get(a) and outdeg.get(b)}
        if keep == edges:
            break
        edges = keep
    dropped = total - len(edges)
    nexts = {}
    for a, b in sorted(edges):
        nexts.setdefault(a, []).append(b)
    used = set()   # half-edges already in a loop
    loops = []
    for h in sorted(edges):
        if h in used:
            continue
        path, walked = [h[0]], []   # the open path's vertices and half-edges
        cur, nxt = h
        while True:
            walked.append((cur, nxt))
            if nxt in path:
                k = path.index(nxt)
                loops.append([bm.verts[i] for i in path[k:]])   # a simple cycle closes here
                used.update(walked[k:])
                path, walked = path[:k + 1], walked[:k]
                if k == 0:
                    break
            else:
                path.append(nxt)
            cur = nxt
            cands = [b for b in nexts.get(cur, []) if (cur, b) not in used and (cur, b) not in walked]
            if not cands:
                # a dead end: the cycles found on the way are kept, the rest of
                # the path is released for other walks (it once swallowed a
                # whole good loop on the taser hand)
                dropped += 1
                break
            nxt = cands[0]
    if dropped and not getattr(directed_loops, "quiet", False):
        print("   note: %d boundary paths never close (faces wound both ways along them);"
              " they are not offered as loops" % dropped)
    return [lp for lp in loops if len(lp) >= 3], face_of


class Refs:
    """Where the vertices of one patched object came from. A welded vertex
    may stand for several ROM vertices (a seam between two parts); a recipe
    names a loop or a point by any of them: 57 (in the host node) or
    "0x02b8:57". Points the patch adds have none."""

    def __init__(self, bm, host, allrefs):
        self.host = host
        self.lay = (bm.verts.layers.int["rom_node"], bm.verts.layers.int["rom_idx"],
                    bm.verts.layers.int["rom_mtx"])
        self.all = allrefs
        self.n_orig = len(bm.verts)

    def parse(self, spec):
        if isinstance(spec, str) and ":" in spec:
            node, idx = spec.split(":")
            return int(node, 16), int(idx)
        return self.host, int(spec)

    def of(self, v):
        if v.index >= self.n_orig:
            return []
        return self.all.get(v.index) or [(v[self.lay[0]], v[self.lay[1]], v[self.lay[2]])]

    def matches(self, v, spec):
        node, idx = self.parse(spec)
        return any(r[0] == node and r[1] == idx for r in self.of(v))

    def primary(self, v):
        """The ROM vertex a corner loads: the first one welded here."""
        return self.of(v)[0]

    def name(self, v):
        node, idx, _ = self.primary(v)
        return str(idx) if node == self.host else "0x%04x:%d" % (node, idx)


def miswound(bm):
    """Edges whose two faces walk them the same way (wound inconsistently)."""
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
    return bad


def mixed_loops(bm, loops):
    """Boundary edges no directed loop took (faces wound both ways along
    them), chained into cycles ignoring direction. A fill over one of these
    winds as the chain runs; the game draws both sides in VR anyway."""
    taken = set()
    for lp in loops:
        for i in range(len(lp)):
            taken.add(frozenset((lp[i].index, lp[(i + 1) % len(lp)].index)))
    rest = [e for e in bm.edges if len(e.link_faces) == 1
            and frozenset((e.verts[0].index, e.verts[1].index)) not in taken]
    adj = {}
    for e in rest:
        for v in e.verts:
            adj.setdefault(v.index, []).append(e)
    seen = set()
    out = []
    for e0 in rest:
        if e0 in seen:
            continue
        seen.add(e0)
        chain = [e0.verts[0], e0.verts[1]]
        closed = False
        while True:
            nxt = [e for e in adj[chain[-1].index] if e not in seen]
            if not nxt:
                break
            e = nxt[0]
            seen.add(e)
            v = e.other_vert(chain[-1])
            if v is chain[0]:
                closed = True
                break
            chain.append(v)
        out.append((chain, closed))
    return out


def loop_info(lp, R):
    pts = [v.co for v in lp]
    centre = sum(pts, Vector()) / len(pts)
    per = sum((lp[i].co - lp[(i + 1) % len(lp)].co).length for i in range(len(lp)))
    rom = [v for v in lp if R.of(v)]
    if not rom:
        return centre, per, "(new points only)"
    first = min(rom, key=lambda v: (R.primary(v)[0] != R.host, R.primary(v)[0], R.primary(v)[1]))
    return centre, per, R.name(first)


def find_loop(loops, R, spec, bm=None):
    """The loop through ROM vertex spec; "largest" is the longest loop left
    (the palm, once the fingers are zipped). Loops whose faces are wound both
    ways are searched only when bm is given (the op says "mixed": true)."""
    if isinstance(spec, str) and spec.startswith("largest"):
        nth = int(spec.split(":")[1]) if ":" in spec else 1
        ranked = sorted(loops, key=lambda lp: -sum((lp[i].co - lp[i - 1].co).length for i in range(len(lp))))
        return ranked[nth - 1]
    for lp in loops:
        if any(R.matches(v, spec) for v in lp):
            return lp
    if bm is not None:
        for lp, closed in mixed_loops(bm, loops):
            if closed and any(R.matches(v, spec) for v in lp):
                print("   (loop %s has faces wound both ways)" % spec)
                return lp
    raise SystemExit("no boundary loop through ROM vertex %s" % spec)

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
    # the outside face across each loop edge (i, i+1), turned to match the
    # loop's direction (a face wound the other way counts as flipped)
    edge_normal = []
    for i in range(n):
        a, b = lp[i], lp[(i + 1) % n]
        e = next(e for e in a.link_edges if e.other_vert(a) is b)
        f = e.link_faces[0]
        walks_ab = any(l.vert is a and l.link_loop_next.vert is b for l in f.loops)
        edge_normal.append(-f.normal if walks_ab else f.normal.copy())
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


def earclip_loop(lp, maxdiag=None, make=None):
    """Close a loop by clipping ears, shortest diagonal first, never at a
    reflex corner. With maxdiag, stop once every ear left is wider than that
    (the fingers are zipped; what is left is the palm, for "fill"). Reflex is judged against the surface the loop cuts: the
    fill should face away from the faces around each corner. At a fingertip
    the ear closes the finger's channel; at the valley between two fingers
    the ear would span the gap and faces the wrong way, so fingers are zipped
    one by one from the tip and never webbed together or to the palm.

    With make, each ear becomes a face as soon as it is clipped, so the next
    ear sees it (make returns the face, or None if it already exists)."""
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
            # where two holes touch, an ear can repeat a face or run its
            # diagonal along an edge that already has two faces
            e = next((e for e in p.link_edges if e.other_vert(p) is n), None)
            if e is not None and (len(e.link_faces) >= 2 or
                                  any(c in f.verts for f in e.link_faces)):
                continue
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
        if make:
            make((p, c, n))
        del ring[i]
    if len(ring) == 3:
        tris.append(tuple(ring))
        if make:
            make(tuple(ring))
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


def fair_positions(bm, free):
    """Place the free points so the patch continues the curvature around it:
    the bi-Laplacian (uniform weights) is zero at each, every other point held
    where it is. Positions only; how a new point is stored comes later
    (op_anchors)."""
    import numpy as np
    bm.verts.index_update()
    bm.verts.ensure_lookup_table()
    verts = list(bm.verts)
    n = len(verts)
    U = np.array(sorted(v.index for v in free), dtype=np.int64)
    if len(U) == 0:
        return
    freeset = set(U.tolist())
    F = np.array([i for i in range(n) if i not in freeset], dtype=np.int64)
    L = np.zeros((n, n))
    for v in verts:
        nb = [e.other_vert(v).index for e in v.link_edges]
        L[v.index, v.index] = 1.0
        for j in nb:
            L[v.index, j] -= 1.0 / len(nb)
    M = L @ L
    X = np.array([list(verts[i].co) for i in F])
    XU = -np.linalg.solve(M[np.ix_(U, U)], M[np.ix_(U, F)] @ X)
    for r, u in enumerate(U):
        verts[int(u)].co = Vector(XU[r].tolist())


# ---------------------------------------------------------------------------
# Rebuilding the missing half: lofted channels, inflated palms, domed caps
# ---------------------------------------------------------------------------

def tube(bm, lp, op, tag, R):
    """Rebuild the missing bottom half of a finger or forearm that was
    modelled as the top half of a tube. The recipe names its two rails (rim
    vertices from the base to the tip) and the tip vertex where they meet,
    or "tip": null for an end that is cut (a forearm).

    Rail vertices are paired by their share of the rail's length; each pair
    is a cross-section, and gets a half-round arc underneath: as deep as the
    modelled top is high above the pair, and never shallower than "round"
    (0.7) of the half-width, so a flat-topped finger still comes out round.
    Consecutive arcs are joined by strips, the last is fanned to the tip.
    Rim vertices are shared, so the new half meets the old without a seam;
    the ends left open are rings for the fills that follow. Returns the new
    points and each vertex's (u, v): u across the arc, v along the rail."""
    rails = [[vert_by_rom(bm, R, x) for x in rail] for rail in op["rails"]]
    tip = vert_by_rom(bm, R, op["tip"]) if op.get("tip") is not None else None
    arcn = op.get("arc", 2)
    rnd = op.get("round", 0.7)

    def shares(rail):
        d = [0.0]
        for a, b in zip(rail, rail[1:]):
            d.append(d[-1] + (b.co - a.co).length)
        return [x / d[-1] for x in d] if d[-1] > 0 else [0.0] * len(rail)

    sA, sB = shares(rails[0]), shares(rails[1])
    # events along the tube: every rail vertex, paired with the nearest on
    # the other rail by share
    rungs = []
    for i, sv in enumerate(sA):
        j = min(range(len(sB)), key=lambda j: abs(sB[j] - sv))
        rungs.append((sv, i, j))
    for j, sv in enumerate(sB):
        i = min(range(len(sA)), key=lambda i: abs(sA[i] - sv))
        rungs.append((sv, i, j))
    rungs = sorted(set(rungs), key=lambda r: (r[0], r[1], r[2]))
    seen, order = set(), []
    for sv, i, j in rungs:
        if (i, j) not in seen:
            seen.add((i, j))
            order.append((sv, i, j))
    new, uv, arcs = [], {}, []
    up_prev = None
    for sv, i, j in order:
        a, b = rails[0][i], rails[1][j]
        m = (a.co + b.co) / 2
        chord = b.co - a.co
        c = max(chord.length / 2, 1e-3)
        xh = chord / (2 * c)
        acc = Vector()
        tops = set()
        for v in (a, b):
            for f in v.link_faces:
                if f[tag[0]] == 0:
                    acc += f.normal * f.calc_area()
                    tops.update(f.verts)
        up = acc - xh * acc.dot(xh)
        if up.length < 1e-9:
            up = up_prev if up_prev is not None else Vector((0, 1, 0))
        up.normalize()
        up_prev = up
        h = max([(v.co - m).dot(up) for v in tops] + [0.0])
        d = max(rnd * c, min(h, 1.5 * c))
        arc = [b]
        for k in range(1, arcn + 1):
            th = math.pi * k / (arcn + 1)
            v = bm.verts.new(m + xh * (c * math.cos(th)) - up * (d * math.sin(th)))
            new.append(v)
            uv[v] = (k / (arcn + 1), sv)
            arc.append(v)
        arc.append(a)
        uv.setdefault(b, (0.0, sv))
        uv.setdefault(a, (1.0, sv))
        arcs.append(arc)
    made = []

    def face(vs):
        out = []
        for v in vs:
            if not out or out[-1] is not v:
                out.append(v)
        if len(out) > 1 and out[0] is out[-1]:
            out.pop()
        if len(set(out)) < 3:
            return
        polys = [out] if len(out) == 3 else [[out[0], out[1], out[2]], [out[0], out[2], out[3]]]
        for t in polys:
            try:
                f = bm.faces.new(t)
            except ValueError:
                continue
            f[tag[0]] = tag[1]
            f.smooth = False
            made.append(f)

    for q, r in zip(arcs, arcs[1:]):
        for k in range(len(q) - 1):
            face([q[k], q[k + 1], r[k + 1], r[k]])
    if tip is not None:
        last = arcs[-1]
        for k in range(len(last) - 1):
            face([last[k], last[k + 1], tip])
        uv.setdefault(tip, (0.5, 1.0))
    # wind like the rim faces the tube grows from
    bm.normal_update()
    mine = set(made)
    for f in made:
        flip = None
        for l in f.loops:
            others = [g for g in l.edge.link_faces if g not in mine and g[tag[0]] == 0]
            if others:
                ol = next(x for x in others[0].loops if x.edge is l.edge)
                flip = ol.vert.index == l.vert.index
                break
        if flip is not None:
            if flip:
                bmesh.ops.reverse_faces(bm, faces=made)
            break
    return new, uv, len(order)


def boundary_chain(bm, R, spec):
    """The open-edge chain through ROM vertex spec, in order along it: a
    closed ring, or a path between two dead ends (where a T-junction breaks
    a rim wound both ways, as round a pistol grip)."""
    adj = {}
    for e in bm.edges:
        if len(e.link_faces) == 1:
            a, b = e.verts[0].index, e.verts[1].index
            adj.setdefault(a, set()).add(b)
            adj.setdefault(b, set()).add(a)
    if spec == "largest":
        # the biggest open-edge component: round a pistol grip, the fist's gutter
        best, seen0 = None, set()
        for s0 in adj:
            if s0 in seen0:
                continue
            c0, st0 = [], [s0]
            seen0.add(s0)
            while st0:
                u = st0.pop()
                c0.append(u)
                for w in adj[u]:
                    if w not in seen0:
                        seen0.add(w)
                        st0.append(w)
            if best is None or len(c0) > len(best):
                best = c0
        start = best[0]
    else:
        start = vert_by_rom(bm, R, spec).index
    comp, st, seen = [], [start], {start}
    while st:
        u = st.pop()
        comp.append(u)
        for w in adj.get(u, ()):
            if w not in seen:
                seen.add(w)
                st.append(w)
    ends = [u for u in comp if len(adj[u]) == 1]
    first = ends[0] if ends else start
    order, used, cur = [first], set(), first
    while True:
        nxt = [w for w in sorted(adj[cur]) if frozenset((cur, w)) not in used]
        if not nxt:
            break
        w = nxt[0]
        used.add(frozenset((cur, w)))
        if w == first:
            break
        order.append(w)
        cur = w
    closed = not ends
    return [bm.verts[u] for u in order], closed


def skirt(bm, R, op, tag, target_bvh):
    """Close the gap between a hand's open rim and the thing it holds: from
    each rim vertex to the nearest point on the held object's surface (just
    outside it), joined into a strip. Looking into the fist you then see skin
    meeting the grip instead of the inside of the fingers. Rim vertices
    further than "reach" from the object are left out."""
    chain, closed = boundary_chain(bm, R, op["chain"])
    gap = op.get("gap", 0.6)
    reach = op.get("reach", 60.0)
    proj = []
    for v in chain:
        loc, nrm, idx, dist = target_bvh.find_nearest(v.co, reach)
        if loc is None:
            proj.append(None)
            continue
        out = v.co - loc
        side = out.normalized() if out.length > 1e-6 else nrm
        proj.append(bm.verts.new(loc + side * gap))
    made, new = [], [p for p in proj if p is not None]
    n = len(chain)
    for i in range(n if closed else n - 1):
        j = (i + 1) % n
        a, b, pa, pb = chain[i], chain[j], proj[i], proj[j]
        quads = []
        if pa is not None and pb is not None:
            quads = [[a, b, pb], [a, pb, pa]]
        elif pa is not None:
            quads = [[a, b, pa]]
        elif pb is not None:
            quads = [[a, b, pb]]
        for t in quads:
            if len({x.index if x.index >= 0 else id(x) for x in t}) < 3:
                continue
            try:
                f = bm.faces.new(t)
            except ValueError:
                continue
            f[tag[0]] = tag[1]
            f.smooth = False
            made.append(f)
    # wind like the rim faces (the rim is wound both ways in places: follow
    # the majority)
    bm.normal_update()
    mine = set(made)
    votes = 0
    for f in made:
        for l in f.loops:
            others = [g for g in l.edge.link_faces if g not in mine and g[tag[0]] == 0]
            if others:
                ol = next(x for x in others[0].loops if x.edge is l.edge)
                votes += 1 if ol.vert.index == l.vert.index else -1
    if votes > 0:
        bmesh.ops.reverse_faces(bm, faces=made)
    return made, new, len(chain), closed


def dome(bm, pts, lp, lay_patch, k, amount):
    """Round a small cap (a fingertip, a socket): its new points rise by
    amount x the loop's radius at the middle, falling to nothing at the rim."""
    bm.normal_update()
    c = sum((v.co for v in lp), Vector()) / len(lp)
    n = Vector()
    for f in bm.faces:
        if f[lay_patch] == k:
            n += f.normal
    if n.length < 1e-9:
        return
    n.normalize()
    radius = sum(((v.co - c) - n * (v.co - c).dot(n)).length for v in lp) / len(lp)
    for v in pts:
        rho = ((v.co - c) - n * (v.co - c).dot(n)).length
        v.co += n * amount * radius * max(0.0, 1.0 - (rho / max(radius, 1e-6)) ** 2)


def op_anchors(bm, R, faces, n_orig):
    """Four ROM vertices on one bone, spread out, for storing an op's new
    points as affine weights: the op's own ROM corners, widened by a ring of
    neighbours if they lie too flat."""
    cand = {v for f in faces for v in f.verts if v.index < n_orig}
    if not cand:
        return None
    mtx = max({R.primary(v)[2] for v in cand}, key=lambda m: sum(1 for v in cand if R.primary(v)[2] == m))

    def pickset(cs):
        cs = sorted((v for v in cs if R.primary(v)[2] == mtx), key=lambda v: v.index)
        if len(cs) < 4:
            return None
        a0 = cs[0]
        a1 = max(cs, key=lambda v: (v.co - a0.co).length)
        ax = (a1.co - a0.co).normalized()
        a2 = max(cs, key=lambda v: ((v.co - a0.co) - ax * (v.co - a0.co).dot(ax)).length)
        nrm = (a1.co - a0.co).cross(a2.co - a0.co)
        if nrm.length < 1e-6:
            return None
        nrm.normalize()
        a3 = max(cs, key=lambda v: abs((v.co - a0.co).dot(nrm)))
        spread = (a1.co - a0.co).length
        if abs((a3.co - a0.co).dot(nrm)) < 0.05 * spread:
            return None
        return [a0, a1, a2, a3]

    got = pickset(cand)
    if got is None:
        ring = set(cand)
        for v in cand:
            for e in v.link_edges:
                w = e.other_vert(v)
                if w.index < n_orig:
                    ring.add(w)
        got = pickset(ring)
    return got


def anchor_mix(R, anchors, co):
    import numpy as np
    M = np.array([[a.co.x for a in anchors], [a.co.y for a in anchors], [a.co.z for a in anchors], [1, 1, 1, 1]])
    w = np.linalg.solve(M, np.array([co.x, co.y, co.z, 1.0]))
    refs = [R.primary(a) for a in anchors]
    return {"mix": [["0x%04x" % r[0], int(r[1]), round(float(x), 6)] for r, x in zip(refs, w)],
            "mtx": int(refs[0][2])}


# ---------------------------------------------------------------------------
# New geometry: the back half of the watch band
# ---------------------------------------------------------------------------

def vert_by_rom(bm, R, spec):
    for v in bm.verts:
        if R.matches(v, spec):
            return v
    raise SystemExit("no vertex for ROM vertex %s" % spec)

def ribbon(bm, R, op):
    """A strip from one edge to another round an axis along x (the wrist):
    the band's missing back half, from one strap end under the wrist to the
    other. It leaves each end at the strap's radius and tightens to
    bottom_radius halfway, so it sits on the cuff. Returns the new faces as
    (verts, (s, t) per corner) and the new points."""
    a0, a1 = (vert_by_rom(bm, R, r) for r in op["from"])
    b0, b1 = (vert_by_rom(bm, R, r) for r in op["to"])
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


def affine_mix(bm, R, v, anchors):
    """v as an affine combination of four ROM vertices (weights sum to 1), so
    the patch stores no coordinates."""
    import numpy as np
    A = [vert_by_rom(bm, R, r) for r in anchors]
    M = np.array([[a.co.x for a in A], [a.co.y for a in A], [a.co.z for a in A], [1, 1, 1, 1]])
    w = np.linalg.solve(M, np.array([v.co.x, v.co.y, v.co.z, 1.0]))
    refs = [R.primary(a) for a in A]
    return {"mix": [["0x%04x" % r[0], int(r[1]), round(float(x), 6)] for r, x in zip(refs, w)],
            "mtx": int(refs[0][2])}


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


def borrowed_recipe(model, other, export_dir):
    """A model whose hand is another's (the gold and silver PPKs share the
    PPK's hand, the throwing knife the knife's) uses that model's recipe,
    each node mapped to the node here with the same vertex block (same
    fingerprint). Every node the recipe names must have exactly one match."""
    with open(os.path.join(RECIPES, other + ".recipe.json"), encoding="utf-8") as f:
        src = json.load(f)
    with open(os.path.join(export_dir, other + ".json"), encoding="utf-8") as f:
        src_model = json.load(f)
    mine = {}
    for p in model["parts"]:
        mine.setdefault(fnv_vertices(model, p["node"]), []).append(p["node"])
    nodemap = {}
    for p in src_model["parts"]:
        fp = fnv_vertices(src_model, p["node"])
        if len(mine.get(fp, [])) == 1:
            nodemap["0x%04x" % p["node"]] = "0x%04x" % mine[fp][0]

    def m(x):
        if isinstance(x, str) and ":" in x:
            node, idx = x.split(":")
            return nodemap[node] + ":" + idx
        return x

    out = {"model": model["model"], "same_as": other, "assemblies": []}
    for a in assemblies_of(src):
        missing = [n for n in a["nodes"] if n not in nodemap]
        if missing:
            raise SystemExit("same_as %s: no single match here for node(s) %s" % (other, missing))
        ops = []
        for op in a["ops"]:
            op = dict(op)
            for key in ("loop", "palm"):
                if key in op and op[key] != "largest":
                    # a bare index names a vertex of the host node there
                    op[key] = m(op[key] if isinstance(op[key], str) else "%s:%d" % (a["host"], op[key]))
            for key in ("loops", "from", "to", "anchors"):
                if key in op and not isinstance(op[key], str):   # "to": "rest" stays as it is
                    op[key] = [m(x if isinstance(x, str) else "%s:%d" % (a["host"], x)) for x in op[key]]
            ops.append(op)
        out["assemblies"].append({"host": nodemap[a["host"]], "nodes": [nodemap[n] for n in a["nodes"]],
                                  "ops": ops})
    print("same_as %s: nodes %s" % (other, ", ".join("%s->%s" % kv for kv in sorted(nodemap.items())
                                                      if any(kv[0] in a["nodes"] for a in assemblies_of(src)))))
    return out


def assemblies_of(recipe):
    """The recipe's assemblies; the older {"parts": {node: ops}} as one-node ones."""
    out = [{"host": n, "nodes": [n], "ops": ops} for n, ops in recipe.get("parts", {}).items()]
    return out + list(recipe.get("assemblies", []))


def main():
    o = args()
    with open(o["src"], encoding="utf-8") as f:
        model = json.load(f)
    name = model["model"]
    tex = os.path.join(os.path.dirname(os.path.abspath(o["src"])), "tex")
    H.TEXDIR = tex if os.path.isdir(tex) else None
    bpy.ops.wm.read_factory_settings(use_empty=True)
    mats = {}
    recipe_path = os.path.join(RECIPES, name + ".recipe.json")
    recipe = None
    if os.path.exists(recipe_path):
        with open(recipe_path, encoding="utf-8") as f:
            recipe = json.load(f)
        if "same_as" in recipe:
            recipe = borrowed_recipe(model, recipe["same_as"], os.path.dirname(os.path.abspath(o["src"])))

    # one object per assembly, and one per node that is in none
    if o["list"] and o.get("assemble"):
        sets = o["assemble"]
    elif recipe:
        sets = [[int(n, 16) for n in a["nodes"]] for a in assemblies_of(recipe)]
    else:
        sets = []
    objs = {}
    for g in sets:
        ob = H.build_part(model, g, mats)
        if ob is not None:
            objs[tuple(g)] = ob
    taken = {n for g in sets for n in g}
    for p in model["parts"]:
        if p["node"] not in taken:
            ob = H.build_part(model, p["node"], mats)
            if ob is not None:
                objs[(p["node"],)] = ob

    def pick(nodes):
        return [ob for key, ob in objs.items() if set(key) & set(nodes)]

    if o["list"]:
        wanted = [tuple(g) for g in o.get("assemble", [])]
        for key, ob in objs.items():
            if wanted and key not in wanted:
                continue
            bm = bmesh.new()
            bm.from_mesh(ob.data)
            bm.verts.ensure_lookup_table()
            R = Refs(bm, key[0], H.ALLREFS.get(ob.name, {}))
            directed_loops.quiet = True
            loops, _ = directed_loops(bm)
            mixed = mixed_loops(bm, loops)
            print("part %s: %d loops, %d with faces wound both ways"
                  % (" ".join("0x%04x" % n for n in key), len(loops), len(mixed)))
            tagged = [(lp, "") for lp in loops] + [(lp, " MIXED" + ("" if c else " OPEN CHAIN")) for lp, c in mixed]
            for lp, tag in sorted(tagged, key=lambda t: -len(t[0])):
                c, per, nm = loop_info(lp, R)
                lo = [min(v.co[a] for v in lp) for a in range(3)]
                hi = [max(v.co[a] for v in lp) for a in range(3)]
                mt = sorted({R.primary(v)[2] for v in lp})
                print("   loop %3d verts  perimeter %7.0f  centre (%6.0f %6.0f %6.0f)  rom %-12s"
                      "  x %.0f..%.0f  y %.0f..%.0f  z %.0f..%.0f  mtx %s%s"
                      % (len(lp), per, c.x, c.y, c.z, nm, lo[0], hi[0], lo[1], hi[1], lo[2], hi[2], mt, tag))
            bm.free()
        return

    if recipe is None:
        raise SystemExit("no recipe: " + recipe_path)

    if o["render"]:
        show = pick(o["render"])
        H.render_views(pick(o["frame"] or o["render"]), o["out"], o["tag"] + "_before",
                       hide=[ob for ob in objs.values() if ob not in show])

    patch = {"model": name,
             "note": "Ours: triangles over ROM vertices (node, index), weights over them, our UVs. No ROM data.",
             "parts": []}
    patch_mat = bpy.data.materials.new("patch_tint")
    patch_mat.diffuse_color = (1.0, 0.1, 0.8, 1)
    for a in assemblies_of(recipe):
        nodes = [int(n, 16) for n in a["nodes"]]
        host = int(a["host"], 16)
        ops = a["ops"]
        ob = objs[tuple(nodes)]
        label = "0x%04x" % host + ("+%d" % (len(nodes) - 1) if len(nodes) > 1 else "")
        bm = bmesh.new()
        bm.from_mesh(ob.data)
        bm.verts.ensure_lookup_table()
        R = Refs(bm, host, H.ALLREFS.get(ob.name, {}))
        n_orig = len(bm.verts)
        lay_patch = bm.faces.layers.int.new("patch")
        uvl = bm.loops.layers.uv["UVMap"]
        bad_before = miswound(bm)
        over_before = sum(1 for e in bm.edges if len(e.link_faces) > 2)
        directed_loops.quiet = False
        fixed_st = {}   # corners whose texture coordinates the op chose itself (s, t)
        fixed_uv = {}   # tube corners: (u, v) in 0..1, mapped into the op's uvbox
        target_bvh = {} # skirt targets (the held object), by node set
        anchored = {}   # ribbon points: their own anchors
        born = {}       # new point -> the op that made it
        for k, op in enumerate(ops, 1):
            loops, face_of = directed_loops(bm)
            directed_loops.quiet = True   # said once per assembly is enough
            # bmesh hands out a new wrapper object at every access, so points
            # are kept by index (new ones are appended; nothing is removed)
            bm.verts.index_update()
            before = len(bm.verts)
            lp = None

            def make(t, k=k):
                try:
                    f = bm.faces.new(t)
                except ValueError:
                    return None
                f[lay_patch] = k
                f.smooth = False
                return f

            if op["op"] == "fill":
                lp = find_loop(loops, R, op["loop"], bm if op.get("mixed") else None)
                tris, w = fill_loop(lp, face_of)
                print("%s fill loop %s (%d verts): %d triangles, max angle %.0f deg"
                      % (label, op["loop"], len(lp), len(tris), math.degrees(w[0])))
            elif op["op"] == "earclip":
                lp = find_loop(loops, R, op["loop"], bm if op.get("mixed") else None)
                tris, forced = earclip_loop(lp, op.get("maxdiag"), make)
                print("%s earclip loop %s (%d verts): %d triangles, %d forced at reflex corners"
                      % (label, op["loop"], len(lp), len(tris), forced))
                tris = []   # made as they were clipped
            elif op["op"] == "tube":
                pts, uvs, nr = tube(bm, lp, op, (lay_patch, k), R)
                bm.verts.index_update()
                fixed_uv.update({v.index: t for v, t in uvs.items()})
                tris = []
                print("%s tube %s: %d cross-sections, %d new points"
                      % (label, op.get("why", ""), nr, len(pts)))
            elif op["op"] == "skirt":
                if op["to"] == "rest":
                    # everything in the model that is not the hand: what it holds
                    handnodes = {int(n, 16) for aa in assemblies_of(recipe) for n in aa["nodes"]}
                    op = dict(op, to=["0x%04x" % pp["node"] for pp in model["parts"]
                                      if pp["node"] not in handnodes
                                      and any(t["node"] == pp["node"] for t in model["tris"])])
                if tuple(int(x, 16) for x in op["to"]) not in target_bvh:
                    tob = H.build_part(model, [int(x, 16) for x in op["to"]], mats, name="skirt_target")
                    tbm = bmesh.new()
                    tbm.from_mesh(tob.data)
                    target_bvh[tuple(int(x, 16) for x in op["to"])] = BVHTree.FromBMesh(tbm)
                    tbm.free()
                    bpy.data.objects.remove(tob)
                made_s, pts, nchain, closed = skirt(bm, R, op, (lay_patch, k),
                                                    target_bvh[tuple(int(x, 16) for x in op["to"])])
                bm.verts.index_update()
                tris = []
                print("%s skirt from %s: chain of %d (%s), %d faces, %d new points"
                      % (label, op["chain"], nchain, "ring" if closed else "open", len(made_s), len(pts)))
            elif op["op"] == "bridge":
                la = find_loop(loops, R, op["loops"][0], bm if op.get("mixed") else None)
                lb = find_loop(loops, R, op["loops"][1], bm if op.get("mixed") else None)
                tris = bridge_loops(la, lb)
                print("%s bridge loops %s: %d triangles" % (label, op["loops"], len(tris)))
            elif op["op"] == "ribbon":
                quads, new_pts = ribbon(bm, R, op)
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
                    if lp_.vert.index == a0.index:
                        bmesh.ops.reverse_faces(bm, faces=made)
                bm.normal_update()
                bm.verts.index_update()
                for v in new_pts:
                    anchored[v.index] = affine_mix(bm, R, v, op["anchors"])
                print("%s ribbon %s -> %s: %d triangles, %d new points"
                      % (label, op["from"], op["to"], 2 * len(quads), len(new_pts)))
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
            lp_idx = [v.index for v in lp] if lp is not None else None
            for _ in range(op.get("fair", 0)):
                refine(bm, lay_patch, k)
            bm.verts.index_update()
            bm.verts.ensure_lookup_table()
            if lp_idx is not None:
                lp = [bm.verts[i] for i in lp_idx]   # wrappers taken before a bmesh op go stale
            created = [v for v in bm.verts if v.index >= before]
            for v in created:
                born[v.index] = k
            inner = [v for v in created if v.index not in fixed_uv and v.index not in anchored]
            if op.get("fair"):
                fair_positions(bm, inner)
            if op.get("dome") and lp is not None:
                dome(bm, inner, lp, lay_patch, k, op["dome"])
            if op.get("debug"):
                for l2 in sorted(directed_loops(bm)[0], key=len, reverse=True)[:6]:
                    print("      left: loop %d verts through %s" % (len(l2), loop_info(l2, R)[2]))
        bm.verts.index_update()
        bm.verts.ensure_lookup_table()
        # every new point as affine weights over four ROM vertices of its op
        mixes = {}
        for k, op in enumerate(ops, 1):
            pts = [bm.verts[i] for i, kk in born.items() if kk == k]
            if not pts:
                continue
            if op["op"] == "ribbon":
                for v in pts:
                    mixes[v.index] = anchored[v.index]
                continue
            anchors = op_anchors(bm, R, [f for f in bm.faces if f[lay_patch] == k], n_orig)
            if anchors is None:
                raise SystemExit("%s op %d: no four ROM vertices to anchor its new points to" % (label, k))
            for v in pts:
                mixes[v.index] = anchor_mix(R, anchors, v.co)

        groups = []
        used = set()

        def corner(v, s_, t_, verts, index):
            key = (v.index, s_, t_)
            if key not in index:
                index[key] = len(verts)
                e = {"s": s_, "t": t_}
                if v.index < n_orig:
                    node, idx, mtx = R.primary(v)
                    e.update({"node": "0x%04x" % node, "ref": idx, "mtx": mtx})
                    used.add(node)
                else:
                    e.update(mixes[v.index])
                    used.update(int(m[0], 16) for m in e["mix"])
                verts.append(e)
            return index[key]

        for k, op in enumerate(ops, 1):
            faces = [f for f in bm.faces if f[lay_patch] == k]
            texnum = int(op["tex"], 16)
            info = model["textures"]["0x%x" % texnum]
            if op["op"] == "ribbon":
                uv = {v.index: (fixed_st[v][0] / 32.0 / info["w"], fixed_st[v][1] / 32.0 / info["h"])
                      for f in faces for v in f.verts}
            elif op["op"] == "tube":
                b = op.get("uvbox", [0, 0, 1, 1])
                uv = {v.index: (b[0] + fixed_uv.get(v.index, (0.5, 0.5))[0] * (b[2] - b[0]),
                                b[1] + fixed_uv.get(v.index, (0.5, 0.5))[1] * (b[3] - b[1]))
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
                            node, idx, mtx = R.primary(v)
                            e.update({"node": "0x%04x" % node, "ref": idx, "mtx": mtx})
                            used.add(node)
                        else:
                            e.update(mixes[v.index])
                            used.update(int(m[0], 16) for m in e["mix"])
                        verts.append(e)
                    tri_ids.append(index[v.index])
                out_tris.append(tri_ids)
            groups.append({"op": op["op"], "why": op.get("why", ""), "tex": "0x%03x" % texnum,
                           "shade": op.get("shade", [255, 255, 255, 255]),
                           "verts": verts, "tris": out_tris})
            print("%s op %d %s: %d triangles, %d corners (%d new points)"
                  % (label, k, op["op"], len(out_tris), len(verts),
                     sum(1 for e in verts if "mix" in e)))
        remaining = sum(1 for e in bm.edges if len(e.link_faces) == 1)
        over = sum(1 for e in bm.edges if len(e.link_faces) > 2)
        print("%s: %d open edges left, %d wound inconsistently (%d in the model before), %d shared by 3+ faces (%d before)"
              % (label, remaining, miswound(bm), bad_before, over, over_before))
        bm.normal_update()
        bm.to_mesh(ob.data)
        bm.free()
        numvtx = {p["node"]: p["numvtx"] for p in model["parts"]}
        patch["parts"].append({
            "host": "0x%04x" % host,
            "nodes": {"0x%04x" % n: {"numvtx": numvtx[n], "fingerprint": fnv_vertices(model, n)}
                      for n in sorted(used)},
            "groups": groups})

    out_patch = os.path.join(RECIPES, name + ".patch.json")
    with open(out_patch, "w", encoding="utf-8", newline="\n") as f:
        json.dump(patch, f, indent=1)
        f.write("\n")
    print("wrote " + out_patch)

    if o["render"]:
        show = pick(o["render"])
        for p in H.render_views(pick(o["frame"] or o["render"]), o["out"], o["tag"] + "_after",
                                hide=[ob for ob in objs.values() if ob not in show]):
            print("wrote " + p)
        if o["compare"]:
            print("wrote " + compare_sheet(o["out"], o["tag"], o["compare"]))
    if o["save"]:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(o["out"], name + "_patched.blend"))


if __name__ == "__main__":
    main()
