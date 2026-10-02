"""
The modeller behind `gevr_hands_author.py seed` (issue #9): finger
undersides, fingertip pads, finger bridges and palms, built over the ROM hand
in one Blender bmesh and handed back as editable pieces.

A piece is a set of triangles drawn with one host node. Its corners are ROM
vertices (welded: exactly where the ROM has them, so no seam) or new points.
The modeller picks texture coordinates and a shade per new point (the vertex
colour the game multiplies the texture by); corners on ROM vertices take that
vertex's own colour. `gevr_hands_author.py commit` turns the pieces into patch
data: weights over ROM vertices, never coordinates.

Units are the model's own (the watch arm is about ten times the pistol hand),
so every size here is relative to the finger being built.
"""

import heapq
import math

import bmesh
from mathutils import Vector

import gevr_hands_import as H
import gevr_hands_patch as P


def lerp(a, b, t):
    return a + (b - a) * t


class Workspace:
    """The ROM hand (welded, refs per vertex) plus the pieces being built."""

    def __init__(self, model, nodes, host):
        self.model = model
        self.host = host
        ob = H.build_part(model, nodes, {}, name="ws_rom")
        self.ob = ob
        bm = bmesh.new()
        bm.from_mesh(ob.data)
        bm.verts.ensure_lookup_table()
        bm.verts.index_update()
        self.bm = bm
        self.R = P.Refs(bm, host, H.ALLREFS.get(ob.name, {}))
        self.n_orig = len(bm.verts)
        self.lay_piece = bm.faces.layers.int.new("piece")
        self.lay_tex = bm.faces.layers.int.new("tex")
        self.lay_bone = bm.verts.layers.int.new("bone")
        self.uv = bm.loops.layers.uv["UVMap"]
        self.shade = bm.loops.layers.color["shade"]
        for v in bm.verts:
            v[self.lay_bone] = self.R.primary(v)[2]
        bm.normal_update()
        self.pieces = {}
        self.textures = model["textures"]
        self.made = []   # every face built, in order (a primitive settles its own)
        self.alias = {}  # new vertex -> the ROM vertex (node, idx, mtx) it stands on (a face drawn twice)

    # -- ROM access ---------------------------------------------------------

    def vert(self, spec):
        """A ROM vertex by its name: 57 (in the host node) or "0x02b8:57"."""
        return P.vert_by_rom(self.bm, self.R, spec)

    def is_rom(self, v):
        return v.index < self.n_orig

    def bone_of(self, v):
        return v[self.lay_bone]

    def rom_faces(self, v):
        return [f for f in v.link_faces if f[self.lay_piece] == 0]

    def rom_tex(self, f):
        """A ROM face's texture number (its material, tex_<hex>)."""
        return int(self.ob.data.materials[f.material_index].name[4:].split(".")[0], 16)

    def rom_colour(self, v):
        """A ROM vertex's shade (0..255), averaged over its corners."""
        cs = [l[self.shade][0] for f in self.rom_faces(v) for l in f.loops if l.vert == v]
        return 255.0 * sum(cs) / len(cs) if cs else 255.0

    def outward(self, v):
        """The ROM surface's normal at v, area weighted (its winding decides)."""
        acc = Vector()
        for f in self.rom_faces(v):
            acc += f.normal * f.calc_area()
        return acc.normalized() if acc.length > 1e-9 else Vector()

    def path_over(self, a, b, limit, avoid):
        """Shortest way from a to b over ROM edges, not through avoid (except
        b), no longer than limit: a cross-section of the modelled surface."""
        dist, prev, heap, n = {a.index: 0.0}, {}, [(0.0, 0, a.index)], 1
        bm = self.bm
        bm.verts.ensure_lookup_table()
        avoid_ix = {v.index for v in avoid}
        while heap:
            du, _, ui = heapq.heappop(heap)
            if ui == b.index:
                break
            if du > dist.get(ui, 1e30) or du > limit:
                continue
            u = bm.verts[ui]
            for e in u.link_edges:
                if not any(f[self.lay_piece] == 0 for f in e.link_faces):
                    continue
                w = e.other_vert(u)
                if w.index != b.index and w.index in avoid_ix:
                    continue
                dw = du + e.calc_length()
                if dw < dist.get(w.index, 1e30):
                    dist[w.index], prev[w.index] = dw, ui
                    heapq.heappush(heap, (dw, n, w.index))
                    n += 1
        if b.index not in prev or dist[b.index] > limit:
            return []
        path, ui = [], prev[b.index]
        while ui != a.index:
            path.append(bm.verts[ui])
            ui = prev[ui]
        return path

    # -- building -----------------------------------------------------------

    def piece(self, name, why, host=None):
        pid = len(self.pieces) + 1
        self.pieces[pid] = {"name": name, "why": why, "host": host if host is not None else self.host}
        return pid

    def new_vert(self, co, bone):
        v = self.bm.verts.new(co)
        v[self.lay_bone] = bone
        return v

    def face(self, pid, vs, tex, uvs, shades):
        """One triangle (vs, per-corner (u, v) in 0..1 of tex and shade 0..255;
        None for a ROM vertex: its own colour)."""
        if vs[0] == vs[1] or vs[1] == vs[2] or vs[0] == vs[2]:
            return None
        if hasattr(tex, "tex_at"):
            # a skin of several textures (NearMap): this face takes the one
            # beside it, its corners that texture's coordinates (looked up
            # where the skin says, e.g. mirrored over a finger's top)
            skin = tex
            pts = [skin.where(v) if hasattr(skin, "where") else v.co for v in vs]
            tex = skin.tex_at(sum(pts, Vector()) / len(pts))
            uvs = [skin(q, tex=tex) for q in pts]
        try:
            f = self.bm.faces.new(vs)
        except ValueError:
            return None
        f[self.lay_piece] = pid
        f[self.lay_tex] = tex
        f.smooth = True
        self.made.append(f)
        for lp, uv, sh in zip(f.loops, uvs, shades):
            lp[self.uv].uv = (uv[0], 1.0 - uv[1])
            if sh is None:
                # a ROM vertex: its own colour (the game draws it so: inherit)
                s = self.rom_colour(lp.vert) / 255.0 if self.is_rom(lp.vert) else 1.0
                lp[self.shade] = (s, s, s, 1.0)
            else:
                s = max(0.0, min(1.0, sh / 255.0))
                lp[self.shade] = (s, s, s, 1.0)
        return f

    def quad(self, pid, vs, tex, uvs, shades):
        """Two triangles over a quad, split along its shorter diagonal."""
        a, b, c, d = vs
        if (a.co - c.co).length <= (b.co - d.co).length:
            tris = ((0, 1, 2), (0, 2, 3))
        else:
            tris = ((0, 1, 3), (1, 2, 3))
        for t in tris:
            self.face(pid, [vs[i] for i in t], tex, [uvs[i] for i in t], [shades[i] for i in t])

    def wind_like_rom(self, pid, faces=None):
        """Turn the piece's faces (or these, built together) to wind like the
        ROM faces they meet (the game pushes a face it would cull a hair
        back; a piece must agree)."""
        bm = self.bm
        mine = faces if faces is not None else [f for f in bm.faces if f[self.lay_piece] == pid]
        votes = 0
        for f in mine:
            for lp in f.loops:
                for g in lp.edge.link_faces:
                    if g is f or g[self.lay_piece] != 0:
                        continue
                    gl = next(x for x in g.loops if x.edge is lp.edge)
                    votes += 1 if gl.vert.index == lp.vert.index else -1
        if votes > 0:
            bmesh.ops.reverse_faces(bm, faces=mine)
        bm.normal_update()
        return votes

    def settle(self, pid, centre_of=None, faces=None):
        """Wind a piece (built consistently) the way the ROM does: like the
        ROM faces it shares edges with, else with its normals pointing away
        from centre_of(face), the inside of the finger (the ROM's faces point
        outward)."""
        bm = self.bm
        if self.wind_like_rom(pid, faces) != 0 or centre_of is None:
            return
        mine = faces if faces is not None else [f for f in bm.faces if f[self.lay_piece] == pid]
        out = sum(f.normal.dot(f.calc_center_median() - centre_of(f)) * f.calc_area() for f in mine)
        if out < 0:
            bmesh.ops.reverse_faces(bm, faces=mine)
        bm.normal_update()

    def rom_winding_outward(self, sample):
        """+1 if the ROM's faces wind with their normal outward from the hand
        (judged on faces near sample points against the hand's centre), else -1."""
        centre = sum((v.co for v in self.bm.verts[:self.n_orig]), Vector()) / self.n_orig
        score = 0.0
        for f in self.bm.faces:
            if f[self.lay_piece] == 0:
                score += f.normal.dot((f.calc_center_median() - centre).normalized()) * f.calc_area()
        return 1 if score >= 0 else -1


# ---------------------------------------------------------------------------
# Shade: what light the N64's painted textures imply, for the new surfaces
# ---------------------------------------------------------------------------

def shade_for(n, light, base=0.80, swing=0.20):
    """Grey 0..255 for a surface facing n: base plus swing times how much it
    faces the light (n and light unit vectors)."""
    k = n.dot(light)
    return 255.0 * max(0.35, min(1.0, base + swing * k))


# ---------------------------------------------------------------------------
# Rails and cross-sections
# ---------------------------------------------------------------------------

def shares(rail):
    d = [0.0]
    for a, b in zip(rail, rail[1:]):
        d.append(d[-1] + (b.co - a.co).length)
    return [x / d[-1] for x in d] if d[-1] > 0 else [0.0] * len(rail)


def pair_rails(A, B):
    """Cross-sections: each rail vertex paired with the vertex on the other
    rail at the nearest share of its length, in order along the finger."""
    sA, sB = shares(A), shares(B)
    rungs = set()
    for i, sv in enumerate(sA):
        j = min(range(len(sB)), key=lambda j: abs(sB[j] - sv))
        rungs.add((sv, i, j))
    for j, sv in enumerate(sB):
        i = min(range(len(sA)), key=lambda i: abs(sA[i] - sv))
        rungs.add((sv, i, j))
    out, seen = [], set()
    for sv, i, j in sorted(rungs, key=lambda r: (r[0], r[1], r[2])):
        if (i, j) not in seen:
            seen.add((i, j))
            out.append((sv, i, j))
    # keep the order monotone on both rails
    mono, li, lj = [], -1, -1
    for sv, i, j in out:
        if i >= li and j >= lj:
            mono.append((sv, i, j))
            li, lj = i, j
    return mono


def fit_centre(c, pts2d):
    """The centre (0, v0) on the chord's bisector of the circle through
    (-c, 0) and (c, 0) that best fits pts2d [(u, v)]: the modelled top's
    curve, completed underneath."""
    if not pts2d:
        return 0.0
    best = None
    for k in range(-60, 91):
        v0 = c * k / 50.0
        r = math.hypot(c, v0)
        err = sum((math.hypot(u, v - v0) - r) ** 2 for u, v in pts2d)
        if best is None or err < best[0]:
            best = (err, v0)
    return best[1]


def underside(ws, pid, rails, tip, tex, st_box, light, arc=2, roundness=(0.75, 1.15),
              tip_pad=0.35, bone=None, shade=(0.80, 0.20), debug=False, flat=1.0, skin=None, rom_shade=None,
              over=None, mirror=False, up_hint=None):
    """The missing palm side of a finger modelled as the top half of a tube.

    rails: two lists of ROM vertex names from the finger's base to its tip,
    one along each open rim; tip: the ROM vertex where they meet (or None for
    a cut end, left open). At each pair of rail vertices the modelled top's
    cross-section (the shortest way over it from rim to rim) is fitted with a
    circle through the two rims, and the circle is completed underneath with
    arc new points; a tip gets a rounded pad. roundness clamps the depth below
    the rims to that range of half-widths, so a flat-topped finger still comes
    out round and an overhanging one is not left a sliver. flat < 1 flattens
    the bottom (a pressed pad).

    Texture: st_box (s0, s1, t0, t1, in the texture's own st units) maps s
    along the finger, t across it, the way the ROM maps the undersides of the
    fingers it did model; or skin (a SkinMap) runs the shell's own mapping on
    underneath. Shade from the completed circle's normal, or with rom_shade
    (a factor) the rails' own colours across, times it. With mirror, a skin
    (NearMap) is read at each new point's mirror image over the top: the
    underside shows the top's own skin turned under, seamless at the rails.

    Which side is the top comes from the faces along the rails, then the
    path over the top; up_hint (a direction) says it outright, for shells
    whose faces at the rails lean the wrong way (the watch arm's fingers
    wrap below their open edges, so those faces point down).

    over (a fraction of the half-width) builds the arcs on the modelled side
    instead, that high above it: a rounded cover over a flat N64 roof (a
    knuckle flap seen from the side it was never meant to be seen from),
    which then sits inside the finger."""
    first = len(ws.made)
    A = [ws.vert(x) for x in rails[0]]
    B = [ws.vert(x) for x in rails[1]]
    T = ws.vert(tip) if tip is not None else None
    stations = pair_rails(A, B)
    mirrored = {}   # new point -> its mirror image over the top (mirror)
    rim = A + B + ([T] if T is not None else [])
    info = ws.textures["0x%x" % tex]
    W, Hh = info["w"] * 32.0, info["h"] * 32.0
    s0, s1, t0, t1 = st_box
    mids = [(A[i].co + B[j].co) / 2 for _, i, j in stations]
    arcs = []
    up_prev = None
    for k, (sv, i, j) in enumerate(stations):
        a, b = A[i], B[j]
        m = mids[k]
        chord = b.co - a.co
        c = max(chord.length / 2, 1e-3)
        xh = chord / (2 * c)
        # along the finger
        t_dir = (mids[min(k + 1, len(mids) - 1)] - mids[max(k - 1, 0)])
        if T is not None and k == len(stations) - 1:
            t_dir = T.co - mids[max(k - 1, 0)]
        t_dir = t_dir - xh * t_dir.dot(xh)
        t_dir = t_dir.normalized() if t_dir.length > 1e-9 else Vector((1, 0, 0))
        # towards the modelled top: the ROM faces at the rims, then the path
        up = ws.outward(a) + ws.outward(b) if up_hint is None else Vector(up_hint)
        up = up - xh * up.dot(xh) - t_dir * up.dot(t_dir)
        if up.length < 1e-9:
            up = up_prev if up_prev is not None else xh.cross(t_dir)
        up.normalize()
        top = ws.path_over(a, b, 6 * c, rim)
        top = [v for v in top if (v.co - m).length <= 2.4 * c]
        if up_hint is not None:
            top = [v for v in top if (v.co - m).dot(up) > 0]
        elif top:
            if sum((v.co - m).dot(up) for v in top) < 0:
                up = -up
        elif up_prev is not None and up.dot(up_prev) < 0:
            up = -up
        up_prev = up
        pts = [((v.co - m).dot(xh), (v.co - m).dot(up)) for v in top]
        v0 = fit_centre(c, pts)
        r = math.hypot(c, v0)
        depth = r - v0
        depth = max(roundness[0] * c, min(roundness[1] * c, depth))
        if over is not None:
            depth = -over * c   # above the roof, not under it
        # the arc under the chord: an ellipse through the rims, depth deep
        new = []
        for q in range(1, arc + 1):
            th = math.pi * q / (arc + 1)
            u = c * math.cos(th)
            v = -depth * math.sin(th) * (flat if 0 < q < arc + 1 else 1.0)
            p = m + xh * u + up * v
            nrm = (xh * (math.cos(th) / c) + up * (-math.sin(th) / max(depth, 1e-6))).normalized()
            new.append((ws.new_vert(p, bone if bone is not None else ws.bone_of(a)), nrm))
            mirrored[new[-1][0]] = m + xh * u - up * v
        if debug:
            print("   station %s-%s half-width %.1f, top %d verts, centre %+.2f c, depth %.2f c"
                  % (ws.R.name(a), ws.R.name(b), c, len(top), v0 / c, depth / c))
        arcs.append((sv, [b] + [p for p, _ in new] + [a], [None] + [n for _, n in new] + [None], c, up, m, xh))

    if skin is not None:
        tex = skin.tex if skin.tex is not None else skin   # several textures: picked per face
        if mirror and skin.tex is None:
            tex = Mirrored(skin, mirrored)
    colour = {}
    if rom_shade is not None:
        for _, ring, *_ in arcs:
            cb, ca = ws.rom_colour(ring[0]), ws.rom_colour(ring[-1])
            for q in range(1, arc + 1):
                if hasattr(skin, "colour"):
                    # the shade of the skin beside it, as the N64 shades that
                    colour[ring[q]] = rom_shade * skin.colour(ring[q].co)
                else:
                    colour[ring[q]] = rom_shade * lerp(cb, ca, q / (arc + 1))

    def st_(sv, q):
        return ((s0 + (s1 - s0) * sv) / W, (t0 + (t1 - t0) * q / (arc + 1)) / Hh)

    def st(sv, q, v=None):
        if skin is not None and getattr(skin, "parametric", False):
            # round the finger: the cover over the top (0 at the middle of
            # it), the underside below (0.5 at its middle)
            f = q / (arc + 1)
            around = (f - 0.5) * 0.5 if over is not None else 0.25 + 0.5 * f
            return skin(v.co if v is not None else None, along=sv, around=around % 1.0)
        if skin is not None and v is not None:
            return skin(mirrored.get(v, v.co)) if mirror else skin(v.co)
        return st_(sv, q)

    def sh(n, v=None):
        if v is not None and v in colour:
            return colour[v]
        return None if n is None else shade_for(n, light, *shade)

    for (svA, ra, na, *_), (svB, rb, nb, *_) in zip(arcs, arcs[1:]):
        for q in range(arc + 1):
            vs = [ra[q], ra[q + 1], rb[q + 1], rb[q]]
            uvs = [st(svA, q, vs[0]), st(svA, q + 1, vs[1]), st(svB, q + 1, vs[2]), st(svB, q, vs[3])]
            shs = [sh(na[q], vs[0]), sh(na[q + 1], vs[1]), sh(nb[q + 1], vs[2]), sh(nb[q], vs[3])]
            if vs[0] == vs[3] or vs[1] == vs[2]:
                # one rail vertex shared by two cross-sections: a triangle
                tri = [v for v in vs]
                keep = [0, 1, 2] if vs[0] == vs[3] else [0, 1, 3]
                ws.face(pid, [tri[x] for x in keep], tex, [uvs[x] for x in keep], [shs[x] for x in keep])
            else:
                ws.quad(pid, vs, tex, uvs, shs)
    if T is not None:
        # the fingertip pad: a ring halfway to the tip, swollen downwards,
        # then a fan to the tip
        sv, last, nl, c, up, m, xh = arcs[-1]
        ring = [last[0]]
        nr = [None]
        for q in range(1, arc + 1):
            p = lerp(last[q].co, T.co, 0.5) - up * (tip_pad * c)
            ring.append(ws.new_vert(p, ws.bone_of(last[q])))
            mirrored[ring[-1]] = lerp(mirrored.get(last[q], last[q].co), T.co, 0.5) + up * (tip_pad * c)
            nr.append((nl[q] * 0.6 - up * 0.4 + (T.co - m).normalized() * 0.3).normalized())
            if last[q] in colour:
                colour[ring[-1]] = colour[last[q]]
        ring.append(last[-1])
        nr.append(None)
        for q in range(arc + 1):
            vs = [last[q], last[q + 1], ring[q + 1], ring[q]]
            uvs = [st(sv, q, vs[0]), st(sv, q + 1, vs[1]), st(min(1.0, sv + 0.06), q + 1, vs[2]),
                   st(min(1.0, sv + 0.06), q, vs[3])]
            shs = [sh(nl[q], vs[0]), sh(nl[q + 1], vs[1]), sh(nr[q + 1], vs[2]), sh(nr[q], vs[3])]
            if vs[0] == vs[3] or vs[1] == vs[2]:
                keep = [0, 1, 2] if vs[0] == vs[3] else [0, 1, 3]
                ws.face(pid, [vs[x] for x in keep], tex, [uvs[x] for x in keep], [shs[x] for x in keep])
            else:
                ws.quad(pid, vs, tex, uvs, shs)
        for q in range(arc + 1):
            ws.face(pid, [ring[q], ring[q + 1], T], tex,
                    [st(1.0, q, ring[q]), st(1.0, q + 1, ring[q + 1]), st(1.0, (arc + 1) / 2, T)],
                    [sh(nr[q], ring[q]), sh(nr[q + 1], ring[q + 1]), None])
    ws.settle(pid, faces=ws.made[first:])
    if over is not None:
        # wound like the roof it shares its rails with, a cover faces into
        # the roof: it is the outside of the finger, turned out
        bmesh.ops.reverse_faces(ws.bm, faces=ws.made[first:])
        ws.bm.normal_update()
    return arcs


def cap(ws, pid, loop_spec, tex, st_box, light, height=0.45, rings=1, shade=(0.80, 0.20), mixed=False,
        direction=None):
    """Close an open ring (a fingertip hole, a stump) with a rounded cap:
    rings of points shrinking to an apex, raised along the ring's outward
    normal by height times its radius. direction overrides the normal."""
    bm = ws.bm
    first = len(ws.made)
    loops, face_of = P.directed_loops(bm)
    lp = P.find_loop(loops, ws.R, loop_spec, bm if mixed else None)
    n = len(lp)
    centre = sum((v.co for v in lp), Vector()) / n
    # the ring's own normal (Newell), turned away from the ROM faces round it
    nrm = Vector()
    for i in range(n):
        a, b = lp[i].co, lp[(i + 1) % n].co
        nrm += Vector(((a.y - b.y) * (a.z + b.z), (a.z - b.z) * (a.x + b.x), (a.x - b.x) * (a.y + b.y)))
    nrm = nrm.normalized() if nrm.length > 1e-9 else Vector((0, 0, 1))
    body = Vector()
    for v in lp:
        for f in ws.rom_faces(v):
            body += f.calc_center_median() * f.calc_area()
    wsum = sum(f.calc_area() for v in lp for f in ws.rom_faces(v))
    if wsum > 0 and nrm.dot(centre - body / wsum) < 0:
        nrm = -nrm
    if direction is not None:
        nrm = Vector(direction).normalized()
    radius = sum((v.co - centre).length for v in lp) / n
    info = ws.textures["0x%x" % tex]
    W, Hh = info["w"] * 32.0, info["h"] * 32.0
    s0, s1, t0, t1 = st_box
    # the cap's texture: the ring unrolled round the box's middle
    ax = (nrm.cross(Vector((0, 0, 1))) if abs(nrm.z) < 0.9 else nrm.cross(Vector((1, 0, 0)))).normalized()
    ay = nrm.cross(ax)

    def st(p):
        d = p - centre
        u = 0.5 + 0.5 * d.dot(ax) / (radius * 1.2)
        v = 0.5 + 0.5 * d.dot(ay) / (radius * 1.2)
        return ((s0 + (s1 - s0) * u) / W, (t0 + (t1 - t0) * v) / Hh)

    prev = list(lp)
    prev_n = [None] * n
    bone = max({ws.bone_of(v) for v in lp}, key=lambda b_: sum(1 for v in lp if ws.bone_of(v) == b_))
    for r in range(1, rings + 1):
        f_ = r / (rings + 1)
        lift = height * radius * math.sin(f_ * math.pi / 2)
        ring = []
        ring_n = []
        for v in lp:
            p = lerp(v.co, centre, f_) + nrm * lift
            ring.append(ws.new_vert(p, bone))
            ring_n.append(((p - (centre - nrm * radius * 0.6))).normalized())
        for i in range(n):
            j = (i + 1) % n
            vs = [prev[i], prev[j], ring[j], ring[i]]
            shs = [None if prev_n[i] is None else shade_for(prev_n[i], light, *shade),
                   None if prev_n[j] is None else shade_for(prev_n[j], light, *shade),
                   shade_for(ring_n[j], light, *shade), shade_for(ring_n[i], light, *shade)]
            ws.quad(pid, vs, tex, [st(x.co) for x in vs], shs)
        prev, prev_n = ring, ring_n
    apex = ws.new_vert(centre + nrm * height * radius, bone)
    an = nrm
    for i in range(n):
        j = (i + 1) % n
        shs = [None if prev_n[i] is None else shade_for(prev_n[i], light, *shade),
               None if prev_n[j] is None else shade_for(prev_n[j], light, *shade), shade_for(an, light, *shade)]
        ws.face(pid, [prev[i], prev[j], apex], tex, [st(prev[i].co), st(prev[j].co), st(apex.co)], shs)
    ws.settle(pid, faces=ws.made[first:])
    return lp


def catmull(points, per=1):
    """Points on a Catmull-Rom curve through points, per new points between
    each pair (per=0: the points themselves)."""
    P_ = [Vector(p) for p in points]
    out = []
    for i in range(len(P_) - 1):
        p0 = P_[i - 1] if i > 0 else P_[i] * 2 - P_[i + 1]
        p1, p2 = P_[i], P_[i + 1]
        p3 = P_[i + 2] if i + 2 < len(P_) else P_[i + 1] * 2 - P_[i]
        out.append(p1.copy())
        for k in range(1, per + 1):
            t = k / (per + 1)
            t2, t3 = t * t, t * t * t
            out.append(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2
                              + (-p0 + 3 * p1 - 3 * p2 + p3) * t3))
    out.append(P_[-1].copy())
    return out


def loop_frame(lp):
    """A ROM loop's centre, Newell normal and mean radius."""
    n = len(lp)
    centre = sum((v.co for v in lp), Vector()) / n
    nrm = Vector()
    for i in range(n):
        a, b = lp[i].co, lp[(i + 1) % n].co
        nrm += Vector(((a.y - b.y) * (a.z + b.z), (a.z - b.z) * (a.x + b.x), (a.x - b.x) * (a.y + b.y)))
    nrm = nrm.normalized() if nrm.length > 1e-9 else Vector((0, 0, 1))
    return centre, nrm, sum((v.co - centre).length for v in lp) / n


def finger_tube(ws, pid, points, radii, tex, st_box, light, sides=6, per=1, end_loop=None, start_cap=True,
                bone=0, up=(0, 1, 0), squash=1.0, shade=(0.80, 0.20), end_into=0.0, mixed=False, end_verts=None):
    """A round finger segment through points (its centreline, my modelling
    choices), radius per point (interpolated along), sides round: low-poly
    rings like the N64's own fingers. end_loop: a ROM loop (the open end of a
    fingertip the N64 did model) that the last ring is zipped to; the path
    is ended at the loop's centre. start_cap closes the first ring (it starts
    hidden inside the hand). squash < 1 flattens the rings across "up" (a
    finger is wider than it is deep). Texture: s along the segment, t round
    it, in st_box (texture st units)."""
    bm = ws.bm
    first = len(ws.made)
    pts = [Vector(p) for p in points]
    end = None
    if end_loop is not None or end_verts is not None:
        # end_verts: the ring's ROM vertices named in order round it, for an
        # open end that shares its loop with something else
        if end_verts is not None:
            lp = [ws.vert(x) for x in end_verts]
        else:
            loops, _ = P.directed_loops(bm)
            lp = P.find_loop(loops, ws.R, end_loop, bm if mixed else None)
        c, n, r = loop_frame(lp)
        # the ring's normal points out of the fingertip, towards where this
        # segment comes from
        if n.dot(pts[-1] - c) < 0:
            n = -n
        pts = pts + [c + n * (r * 0.4), c - n * end_into * r]
        radii = list(radii) + [r, r]
        end = (lp, c, n, r)
    path = catmull(pts, per)
    # radius along the path: by arc length share between the control points
    seg = [0.0]
    for a, b in zip(pts, pts[1:]):
        seg.append(seg[-1] + (b - a).length)
    plen = [0.0]
    for a, b in zip(path, path[1:]):
        plen.append(plen[-1] + (b - a).length)

    def radius_at(i):
        # the path's i-th point lies on control segment j at fraction f
        idx = i / (per + 1)
        j = min(int(idx), len(radii) - 2)
        f = idx - j
        return radii[j] * (1 - f) + radii[j + 1] * f

    # parallel-transported frames
    ups = Vector(up).normalized()
    rings, ring_n, centres = [], [], []
    prev_u = None
    for i, p in enumerate(path):
        t = (path[min(i + 1, len(path) - 1)] - path[max(i - 1, 0)]).normalized()
        if prev_u is None:
            u = ups - t * ups.dot(t)
            if u.length < 1e-6:
                u = Vector((1, 0, 0)) - t * t.x
        else:
            u = prev_u - t * prev_u.dot(t)
        u.normalize()
        w = t.cross(u)
        prev_u = u
        r = radius_at(i)
        ring, nr = [], []
        for k in range(sides):
            th = 2 * math.pi * k / sides
            d = u * (math.cos(th) * squash) + w * math.sin(th)
            ring.append(ws.new_vert(p + d * r, bone))
            nr.append((u * (math.cos(th) / squash) + w * math.sin(th)).normalized())
        rings.append(ring)
        ring_n.append(nr)
        centres.append((p, t, u, w))
    info = ws.textures["0x%x" % tex]
    W, Hh = info["w"] * 32.0, info["h"] * 32.0
    s0, s1, t0, t1 = st_box
    total = plen[-1] if plen[-1] > 0 else 1.0

    def st(i, k):
        return ((s0 + (s1 - s0) * plen[i] / total) / W, (t0 + (t1 - t0) * k / sides) / Hh)

    def sh(n):
        return None if n is None else shade_for(n, light, *shade)

    last = len(rings) - 1 if end is None else len(rings) - 2   # the very last point is the loop's centre
    for i in range(last):
        for k in range(sides):
            k2 = (k + 1) % sides
            vs = [rings[i][k], rings[i][k2], rings[i + 1][k2], rings[i + 1][k]]
            uvs = [st(i, k), st(i, k + 1), st(i + 1, k + 1), st(i + 1, k)]
            ws.quad(pid, vs, tex, uvs,
                    [sh(ring_n[i][k]), sh(ring_n[i][k2]), sh(ring_n[i + 1][k2]), sh(ring_n[i + 1][k])])
    if start_cap:
        p, t, u, w = centres[0]
        apex = ws.new_vert(p - t * radius_at(0) * 0.5, bone)
        for k in range(sides):
            k2 = (k + 1) % sides
            ws.face(pid, [rings[0][k2], rings[0][k], apex], tex, [st(0, k + 1), st(0, k), st(0, k + 0.5)],
                    [sh(ring_n[0][k2]), sh(ring_n[0][k]), sh(-t)])
    if end is not None:
        lp, c, n, r = end
        # zip the ring before the loop's centre to the loop, both ordered by
        # angle round the end's axis
        ring = rings[last]
        p, t, u, w = centres[last]

        def ang(co):
            d = co - c
            return math.atan2(d.dot(w), d.dot(u)) % (2 * math.pi)
        A = sorted(range(sides), key=lambda k: ang(ring[k].co))
        B = sorted(range(len(lp)), key=lambda k: ang(lp[k].co))
        a_ang = [ang(ring[k].co) for k in A]
        b_ang = [ang(lp[k].co) for k in B]
        i = j = 0
        na, nb = len(A), len(B)
        while i < na or j < nb:
            a0, a1 = ring[A[i % na]], ring[A[(i + 1) % na]]
            b0, b1 = lp[B[j % nb]], lp[B[(j + 1) % nb]]
            nxt_a = a_ang[(i + 1) % na] + (2 * math.pi if i + 1 >= na else 0)
            nxt_b = b_ang[(j + 1) % nb] + (2 * math.pi if j + 1 >= nb else 0)
            if j >= nb or (i < na and nxt_a <= nxt_b):
                ws.face(pid, [a0, a1, b0], tex, [st(last, A[i % na]), st(last, A[(i + 1) % na]), st(last + 1, 0)],
                        [sh(ring_n[last][A[i % na]]), sh(ring_n[last][A[(i + 1) % na]]), None])
                i += 1
            else:
                ws.face(pid, [a0, b1, b0], tex, [st(last, A[i % na]), st(last + 1, 0), st(last + 1, 0)],
                        [sh(ring_n[last][A[i % na]]), None, None])
                j += 1
    # wind the whole piece outward from its centreline
    def centre_of(f):
        c_ = f.calc_center_median()
        return min((pp for pp, *_ in centres), key=lambda q: (q - c_).length)
    ws.settle(pid, centre_of, faces=ws.made[first:])
    return rings


# ---------------------------------------------------------------------------
# Growing out of the ROM's own skin
# ---------------------------------------------------------------------------

class SkinMap:
    """The ROM's texture mapping continued past its edge: an affine map from
    position to (u, v) fitted on the corners of the ROM faces that touch
    verts (and their neighbours, steps rings out), in texture tex (by default
    the one covering most of them). A new surface textured with it carries
    the skin across the seam instead of starting a texture of its own."""

    def __init__(self, ws, verts, tex=None, steps=1, skip=()):
        import numpy as np
        faces = set()
        front = set(verts)
        skip = set(skip)   # ROM vertices whose faces do not count (a flap the map is to replace)
        for _ in range(steps + 1):
            nxt = set()
            for v in front:
                for f in ws.rom_faces(v):
                    if f not in faces and not all(x in skip for x in f.verts):
                        faces.add(f)
                        nxt.update(f.verts)
            front = nxt
        if tex is None:
            area = {}
            for f in faces:
                area[ws.rom_tex(f)] = area.get(ws.rom_tex(f), 0.0) + f.calc_area()
            tex = max(area, key=area.get)
        X, Y = [], []
        for f in faces:
            if ws.rom_tex(f) != tex:
                continue
            for l in f.loops:
                X.append([l.vert.co.x, l.vert.co.y, l.vert.co.z, 1.0])
                Y.append([l[ws.uv].uv.x, 1.0 - l[ws.uv].uv.y])
        if len(X) < 3:
            raise SystemExit("SkinMap: no ROM faces in texture 0x%x round %s" % (tex, [ws.R.name(v) for v in verts]))
        self.tex = tex
        self.A, *_ = np.linalg.lstsq(np.array(X), np.array(Y), rcond=None)
        # carried far past its faces the map would run into other parts of
        # the texture (the white round a hand's image): keep to what they use
        us = [y[0] for y in Y]
        vs = [y[1] for y in Y]
        self.box = (min(us), max(us), min(vs), max(vs))

    def __call__(self, p, along=None, around=None):
        u = p.x * self.A[0][0] + p.y * self.A[1][0] + p.z * self.A[2][0] + self.A[3][0]
        v = p.x * self.A[0][1] + p.y * self.A[1][1] + p.z * self.A[2][1] + self.A[3][1]
        u0, u1, v0, v1 = self.box
        return (min(max(u, u0), u1), min(max(v, v0), v1))


def bary(p, a, b, c):
    """Barycentric weights of p (in or near triangle a b c)."""
    v0, v1, v2 = b - a, c - a, p - a
    d00, d01, d11 = v0.dot(v0), v0.dot(v1), v1.dot(v1)
    d20, d21 = v2.dot(v0), v2.dot(v1)
    den = d00 * d11 - d01 * d01
    if abs(den) < 1e-12:
        return (1 / 3, 1 / 3, 1 / 3)
    v = (d11 * d20 - d01 * d21) / den
    w = (d00 * d21 - d01 * d20) / den
    return (1.0 - v - w, v, w)


class NearMap:
    """The ROM's texture carried onto a new surface point by point: each
    point takes the texture coordinates of the nearest point on the ROM's
    faces in texture tex (not those all on skip's vertices: a flap being
    covered), interpolated across that face. A new surface next to the skin
    shows the skin that is right beside it.

    tex may be several textures, for skin that changes texture (a finger
    whose top is 0x702 at its base and 0x703 further on): each new face then
    takes the texture of the ROM face nearest to it (tex_at) and its corners
    that texture's coordinates. .tex is None then, and the primitives hand
    the map itself to Workspace.face, which picks per face."""

    def __init__(self, ws, tex, skip=(), near=None, reach=None, nodes=None):
        from mathutils.bvhtree import BVHTree
        skip = set(skip)
        texes = tuple(tex) if isinstance(tex, (tuple, list, set, frozenset)) else (tex,)
        self.ws = ws
        self.tex = texes[0] if len(texes) == 1 else None
        self.faces = []
        for f in ws.bm.faces:
            if f[ws.lay_piece] != 0 or ws.rom_tex(f) not in texes or all(v in skip for v in f.verts):
                continue
            if nodes is not None and not all(ws.R.primary(v)[0] in nodes for v in f.verts):
                continue
            if near is not None and reach is not None and \
                    min((f.calc_center_median() - Vector(n)).length for n in near) > reach:
                continue
            self.faces.append(f)
        if not self.faces:
            raise SystemExit("NearMap: no ROM faces in texture %s" % ", ".join("0x%x" % t for t in texes))

        def tree(faces):
            verts = []
            polys = []
            for f in faces:
                polys.append([len(verts) + k for k in range(len(f.verts))])
                verts.extend(v.co.copy() for v in f.verts)
            return BVHTree.FromPolygons(verts, polys)
        self.bvh = tree(self.faces)
        self.by_tex = {}
        if self.tex is None:
            for t in texes:
                fs = [f for f in self.faces if ws.rom_tex(f) == t]
                if fs:
                    self.by_tex[t] = (fs, tree(fs))

    def _at(self, p, tex=None):
        faces, bvh = self.by_tex[tex] if tex is not None and self.tex is None else (self.faces, self.bvh)
        loc, nrm, idx, dist = bvh.find_nearest(p)
        f = faces[idx]
        return f, bary(loc, *[l.vert.co for l in f.loops[:3]])

    def tex_at(self, p):
        """The texture of the ROM skin nearest to p."""
        return self.tex if self.tex is not None else self.ws.rom_tex(self._at(p)[0])

    def __call__(self, p, along=None, around=None, tex=None):
        f, w = self._at(p, tex)
        ls = f.loops[:3]
        u = sum(wk * l[self.ws.uv].uv.x for wk, l in zip(w, ls))
        v = sum(wk * (1.0 - l[self.ws.uv].uv.y) for wk, l in zip(w, ls))
        return (u, v)

    def colour(self, p):
        """The skin's shade (0..255) at the nearest point, as the N64 shades
        it there (255 outside, 190 on the side turned in)."""
        f, w = self._at(p)
        return 255.0 * max(0.0, min(1.0, sum(wk * l[self.ws.shade][0] for wk, l in zip(w, f.loops[:3]))))


class Mirrored:
    """A skin of several textures read at stand-in points (a point under a
    finger at its mirror image over the top): Workspace.face picks each
    face's texture and coordinates from where() instead of the corners'
    own positions."""

    tex = None

    def __init__(self, skin, stand_in):
        self.skin = skin
        self.stand_in = stand_in

    def where(self, v):
        return self.stand_in.get(v, v.co)

    def tex_at(self, p):
        return self.skin.tex_at(p)

    def __call__(self, p, along=None, around=None, tex=None):
        return self.skin(p, tex=tex)

    def colour(self, p):
        return self.skin.colour(p)


class Across:
    """A skin read straight across a part from the side being built: each
    point looks along up (into the part) to the far side's ROM faces in the
    skin's texture and takes the skin there. A missing side wears the skin
    of the side the N64 did model, mirrored, so both sides of a forearm look
    alike. A look that passes the far side's edge (a slanted cut end) takes
    the far side's point nearest to where it would have landed, depth on.
    Workspace.face reads each corner where() says."""

    tex = None

    def __init__(self, skin, up, depth):
        from mathutils.bvhtree import BVHTree
        self.skin = skin
        self.up = Vector(up).normalized()
        self.depth = depth
        verts, polys = [], []
        for f in skin.faces:
            polys.append([len(verts) + k for k in range(len(f.verts))])
            verts.extend(v.co.copy() for v in f.verts)
        self.bvh = BVHTree.FromPolygons(verts, polys)

    def where(self, v):
        p = v.co if hasattr(v, "co") else v
        hit = self.bvh.ray_cast(p + self.up * 1e-3, self.up)[0]
        return hit if hit is not None else self.bvh.find_nearest(p + self.up * self.depth)[0]

    def tex_at(self, p):
        return self.skin.tex_at(p)

    def __call__(self, p, along=None, around=None, tex=None):
        return self.skin(p, tex=tex)

    def colour(self, p):
        return self.skin.colour(p)


class Span:
    """A band map used over part of a finger: along (0..1 of this part) is
    carried to a0..a1 of the whole band, so pieces built one after another
    (a knuckle's cover, then the segment beyond) share one band."""

    parametric = True

    def __init__(self, band, a0, a1):
        self.band = band
        self.tex = band.tex
        self.a = (a0, a1)
        self.span = (0.0, 1.0)

    def __call__(self, p, along=0.5, around=0.5):
        return self.band(p, lerp(self.a[0], self.a[1], along), around)


class BandMap:
    """A finger's texture the way the N64 lays one along a finger: a band of
    tex, s from s0 to s1 along it, t from t0 to t1 round it and back (in the
    texture's st units; the middle finger's plate across the grip is 0x703
    s 499..787, t 500..598). along and around (0..1) come from the segment
    being built."""

    parametric = True

    def __init__(self, tex, w, h, s0, s1, t0, t1, span=None):
        self.tex = tex
        self.W, self.H = w * 32.0, h * 32.0
        self.s = (s0, s1)
        self.t = (t0, t1)
        self.span = span   # the part of the segment (along) the band runs over

    def __call__(self, p, along=0.5, around=0.5):
        a0, a1 = self.span or (0.0, 1.0)
        f = min(1.0, max(0.0, (along - a0) / (a1 - a0))) if a1 > a0 else 0.0
        k = 1.0 - abs(2.0 * (around % 1.0) - 1.0)   # 0 on top, 1 underneath, back to 0
        return (lerp(self.s[0], self.s[1], f) / self.W, lerp(self.t[0], self.t[1], k) / self.H)


def ring_frame(pts, axis, ref):
    """Centre and frame (u, w, n) of a ring of points round axis n; u is ref
    laid into the ring's plane."""
    c = sum(pts, Vector()) / len(pts)
    n = Vector(axis).normalized()
    u = Vector(ref) - n * Vector(ref).dot(n)
    if u.length < 1e-6:
        u = n.orthogonal()
    u.normalize()
    return c, u, n.cross(u), n


def ring_profile(pts, c, u, w, n, count):
    """The ring as radius and height (along n) at count even angles from u
    towards w, read off pts (in order or not) by angle, linearly between."""
    polar = []
    for p in pts:
        d = p - c
        polar.append((math.atan2(d.dot(w), d.dot(u)) % (2 * math.pi), (d - n * d.dot(n)).length, d.dot(n)))
    polar.sort()
    out = []
    for k in range(count):
        th = 2 * math.pi * k / count
        # the neighbours of th round the circle
        j = next((i for i, q in enumerate(polar) if q[0] >= th), len(polar))
        a = polar[j - 1] if j > 0 else (polar[-1][0] - 2 * math.pi,) + polar[-1][1:]
        b = polar[j] if j < len(polar) else (polar[0][0] + 2 * math.pi,) + polar[0][1:]
        f = 0.0 if b[0] - a[0] < 1e-9 else (th - a[0]) / (b[0] - a[0])
        out.append((lerp(a[1], b[1], f), lerp(a[2], b[2], f)))
    return out


def zip_rings(ws, pid, A, B, angle_a, angle_b, make):
    """Triangles joining ring A to ring B (lists of verts, closed), walking
    both by angle (angle_x(v) in 0..2pi round a shared axis), as a strip
    whose faces all wind the same way. make(tri) builds one."""
    ia = sorted(range(len(A)), key=lambda k: angle_a(A[k]))
    ib = sorted(range(len(B)), key=lambda k: angle_b(B[k]))
    aa = [angle_a(A[k]) for k in ia]
    bb = [angle_b(B[k]) for k in ib]
    # start both at the vertex nearest angle 0 on each, so the walks line up
    i = j = 0
    na, nb = len(ia), len(ib)
    while i < na or j < nb:
        a0, a1 = A[ia[i % na]], A[ia[(i + 1) % na]]
        b0, b1 = B[ib[j % nb]], B[ib[(j + 1) % nb]]
        nxt_a = aa[(i + 1) % na] + (2 * math.pi if i + 1 >= na else 0)
        nxt_b = bb[(j + 1) % nb] + (2 * math.pi if j + 1 >= nb else 0)
        if j >= nb or (i < na and nxt_a <= nxt_b):
            make((a0, a1, b0))
            i += 1
        else:
            make((a0, b1, b0))
            j += 1


def zip_chains(ws, pid, A, B, skin, shade=1.0):
    """A strip between two open chains of existing vertices that start
    together and end together (a finger's bend: the phalanx's open end and
    the next one's, already joined across the top), walked by each chain's
    share of its own length, so the two cross-sections meet as a mitre.
    Texture and shade from skin (a NearMap: the skin beside it)."""
    first = len(ws.made)

    def shares(ch):
        d = [0.0]
        for a, b in zip(ch, ch[1:]):
            d.append(d[-1] + (b.co - a.co).length)
        return [x / d[-1] if d[-1] > 0 else 0.0 for x in d]
    sa, sb = shares(A), shares(B)
    i = j = 0

    def make(tri):
        shs = [None if ws.is_rom(v) else shade * skin.colour(v.co) for v in tri]
        ws.face(pid, list(tri), skin.tex if skin.tex is not None else skin, [skin(v.co) for v in tri], shs)
    while i < len(A) - 1 or j < len(B) - 1:
        if j >= len(B) - 1 or (i < len(A) - 1 and sa[i + 1] <= sb[j + 1]):
            make((A[i], A[i + 1], B[j]))
            i += 1
        else:
            make((A[i], B[j + 1], B[j]))
            j += 1
    ws.settle(pid, faces=ws.made[first:])


def adopt(ws, pid, faces, skin, shade=1.0):
    """Faces another tool built in the workspace (gevr_hands_patch.py's skirt)
    taken into piece pid: the skin's texture and coordinates and its shade
    beside them, wound like the ROM."""
    faces = list(faces)
    for f in faces:
        ft = skin.tex if skin.tex is not None else skin.tex_at(f.calc_center_median())
        f[ws.lay_piece] = pid
        f[ws.lay_tex] = ft
        f.smooth = True
        ws.made.append(f)
        for l in f.loops:
            v = l.vert
            u, w = skin(v.co) if skin.tex is not None else skin(v.co, tex=ft)
            l[ws.uv].uv = (u, 1.0 - w)
            s_ = (ws.rom_colour(v) if ws.is_rom(v) else shade * skin.colour(v.co)) / 255.0
            l[ws.shade] = (s_, s_, s_, 1)
    ws.settle(pid, faces=faces)
    return faces


def plug(ws, pid, ring):
    """Close a small opening a piece left (a knuckle cover's back end,
    standing open over the flap's back edge): ring is the opening's vertices
    in order round it. Each corner takes the texture and shade the piece
    already gives that vertex, and the plug winds like the piece's faces
    across the edges they share."""
    first = len(ws.made)

    def corner_of(v):
        for f in v.link_faces:
            if f[ws.lay_piece] == pid:
                lp = next(l for l in f.loops if l.vert == v)
                return f[ws.lay_tex], (lp[ws.uv].uv.x, 1.0 - lp[ws.uv].uv.y), 255.0 * lp[ws.shade][0]
        raise SystemExit("plug: %s is not on the piece" % (ws.R.name(v) if ws.is_rom(v) else v.index))
    data = [corner_of(v) for v in ring]
    tex = data[0][0]
    uvs = [d[1] for d in data]
    shs = [None if ws.is_rom(v) else d[2] for v, d in zip(ring, data)]
    if len(ring) == 4:
        ws.quad(pid, list(ring), tex, uvs, shs)
    else:
        for i in range(1, len(ring) - 1):
            ws.face(pid, [ring[0], ring[i], ring[i + 1]], tex, [uvs[0], uvs[i], uvs[i + 1]],
                    [shs[0], shs[i], shs[i + 1]])
    mine = set(ws.made[first:])
    votes = 0
    for f in mine:
        for lp in f.loops:
            for g in lp.edge.link_faces:
                if g in mine or g[ws.lay_piece] != pid:
                    continue
                gl = next(x for x in g.loops if x.edge is lp.edge)
                votes += 1 if gl.vert.index == lp.vert.index else -1
    if votes > 0:
        bmesh.ops.reverse_faces(ws.bm, faces=list(mine))
    ws.bm.normal_update()
    return list(mine)


def vert_colour(ws, v):
    """A vertex's shade (0..255): a ROM vertex's own, else what the piece
    that made it gave it (the average over its corners)."""
    if ws.is_rom(v):
        return ws.rom_colour(v)
    cs = [l[ws.shade][0] for l in v.link_loops]
    return 255.0 * sum(cs) / len(cs) if cs else 255.0


def grow(ws, pid, ring_a, ring_b, joints, count, maps, up, bone, between=0, debug=False):
    """A finger segment grown between two rings of existing vertices (ROM
    edges, or the arcs another piece left): ring_a where it leaves the skin
    the N64 modelled, ring_b where it meets it again (a fingertip's open
    end). Between them the finger runs straight from joint to joint, the
    way the N64 builds its own fingers: joints is [(centre, radius), ...]
    (my modelling: the knuckles round the grip), and each joint gets a ring
    of count points in the bisecting plane of its two phalanges, shaped as
    a blend of the two end rings' own profiles (so the cross-section goes
    over from the skin it leaves to the skin it meets) and sized to radius.
    between adds that many rings along each phalanx.

    maps: [(t, SkinMap), ...] - from t (0 at ring_a, 1 at ring_b, by length)
    on, faces take that map's texture and coordinates, so the skin's mapping
    runs on from each end. The shade runs from ring_a's colours to ring_b's
    round the finger."""
    from mathutils import Matrix
    first = len(ws.made)
    pa = [v.co.copy() for v in ring_a]
    pb = [v.co.copy() for v in ring_b]
    ca = sum(pa, Vector()) / len(pa)
    cb = sum(pb, Vector()) / len(pb)
    knots = [ca] + [Vector(j[0]) for j in joints] + [cb]
    radii = [None] + [j[1] for j in joints] + [None]
    # ring positions: the joints, and between more along each phalanx
    centres, rads = [], []
    for i in range(len(knots) - 1):
        for k in range(between + 1):
            f = k / (between + 1)
            centres.append(lerp(knots[i], knots[i + 1], f))
            ra_, rb_ = radii[i], radii[i + 1]
            rads.append(ra_ if k == 0 else (lerp(ra_, rb_, f) if ra_ is not None and rb_ is not None else
                                            (ra_ if ra_ is not None else rb_)))
    centres.append(cb)
    rads.append(None)
    L = [0.0]
    for x, y in zip(centres, centres[1:]):
        L.append(L[-1] + (y - x).length)
    total = L[-1] if L[-1] > 0 else 1.0
    tvals = [x / total for x in L]
    # each ring's plane: bisecting its two phalanges (the ends: their own)
    tans = []
    for i in range(len(centres)):
        d_in = (centres[i] - centres[i - 1]).normalized() if i > 0 else None
        d_out = (centres[i + 1] - centres[i]).normalized() if i + 1 < len(centres) else None
        t_ = (d_in if d_in is not None else Vector()) + (d_out if d_out is not None else Vector())
        tans.append(t_.normalized())
    up = Vector(up).normalized()
    c0, u0, w0, n0 = ring_frame(pa, tans[0], up)
    c1, u1, w1, n1 = ring_frame(pb, tans[-1], up)
    prof_a = ring_profile(pa, c0, u0, w0, n0, count)
    prof_b = ring_profile(pb, c1, u1, w1, n1, count)
    mean_a = sum(r for r, _ in prof_a) / count
    mean_b = sum(r for r, _ in prof_b) / count
    # frames carried along (parallel transport), the twist left at ring_b
    # spread along the length
    frames = []
    u = u0.copy()
    for i, t_ in enumerate(tans):
        if i > 0:
            axis = tans[i - 1].cross(t_)
            if axis.length > 1e-9:
                u = Matrix.Rotation(tans[i - 1].angle(t_), 3, axis.normalized()) @ u
        u = (u - t_ * u.dot(t_)).normalized()
        frames.append((centres[i], u.copy(), t_.cross(u), t_))
    twist = math.atan2(frames[-1][2].dot(u1), frames[-1][1].dot(u1))

    def frame_of(i):
        p_, u_, w_, n_ = frames[i]
        rot = twist * tvals[i]
        uu = u_ * math.cos(rot) + w_ * math.sin(rot)
        return p_, uu, n_.cross(uu), n_

    col_a = [vert_colour(ws, v) for v in ring_a]
    col_b = [vert_colour(ws, v) for v in ring_b]

    def col_at(ring_pts, cols, c, u_, w_, th):
        best = min(range(len(ring_pts)), key=lambda k: abs(((math.atan2((ring_pts[k] - c).dot(w_),
                                                                         (ring_pts[k] - c).dot(u_)) - th + math.pi)
                                                            % (2 * math.pi)) - math.pi))
        return cols[best]

    rings = [list(ring_a)]
    gen_cols = {}
    # where each vertex sits on the finger: along (0..1) and round (0..1
    # from the top), for maps laid along it
    param = {}
    for v in ring_a:
        d = v.co - c0
        param[v] = (0.0, (math.atan2(d.dot(w0), d.dot(u0)) / (2 * math.pi)) % 1.0)
    for v in ring_b:
        d = v.co - c1
        param[v] = (1.0, (math.atan2(d.dot(w1), d.dot(u1)) / (2 * math.pi)) % 1.0)
    for i in range(1, len(centres) - 1):
        p_, uu, ww, n_ = frame_of(i)
        t = tvals[i]
        ring = []
        for q in range(count):
            th = 2 * math.pi * q / count
            # the blended profile, at the joint's size
            r = lerp(prof_a[q][0] / mean_a, prof_b[q][0] / mean_b, t)
            r *= rads[i] if rads[i] is not None else lerp(mean_a, mean_b, t)
            h = lerp(prof_a[q][1], prof_b[q][1], t) * 0.5
            v = ws.new_vert(p_ + uu * (r * math.cos(th)) + ww * (r * math.sin(th)) + n_ * h, bone(t))
            ring.append(v)
            gen_cols[v] = lerp(col_at(pa, col_a, c0, u0, w0, th), col_at(pb, col_b, c1, u1, w1, th), t)
            param[v] = (t, q / count)
        rings.append(ring)
    rings.append(list(ring_b))

    # a map may start at a joint ("j1": the first joint) instead of a t
    joint_t = {}
    for k in range(1, len(knots) - 1):
        joint_t["j%d" % k] = tvals[k * (between + 1)]
    starts = [(joint_t[tt] if isinstance(tt, str) else tt, mm) for tt, mm in maps]
    for tt, mm in starts:
        if isinstance(mm, BandMap) and mm.span is None:
            mm.span = (tt, 1.0)   # a band runs from where it starts to the end

    def map_for(t):
        m = starts[0][1]
        for tt, mm in starts:
            if t >= tt:
                m = mm
        return m

    for i in range(len(rings) - 1):
        p_, uu, ww, n_ = frame_of(i)
        c_i = sum((v.co for v in rings[i]), Vector()) / len(rings[i])
        c_j = sum((v.co for v in rings[i + 1]), Vector()) / len(rings[i + 1])
        m = map_for((tvals[i] + tvals[i + 1]) / 2)

        def ang(c_):
            return lambda v: math.atan2((v.co - c_).dot(ww), (v.co - c_).dot(uu)) % (2 * math.pi)

        def make(tri, m=m):
            ws.face(pid, list(tri), m.tex if m.tex is not None else m, [m(v.co, *param.get(v, (0.5, 0.5))) for v in tri],
                    [None if ws.is_rom(v) else (m.colour(v.co) if hasattr(m, "colour") else
                                                gen_cols.get(v, vert_colour(ws, v))) for v in tri])
        zip_rings(ws, pid, rings[i], rings[i + 1], ang(c_i), ang(c_j), make)
    ws.settle(pid, lambda f: min(centres, key=lambda q: (q - f.calc_center_median()).length),
              faces=ws.made[first:])
    if debug:
        print("   grow: %d rings, length %.1f, twist %.0f deg, ends r %.1f / %.1f"
              % (len(rings), total, math.degrees(twist), mean_a, mean_b))
    return rings


def extend(ws, pid, ring_a, joints, tip_len, count, band, up, bone, light, shade=(0.9, 0.1), debug=False):
    """A finger the N64 cut short, grown on into a rounded fingertip: from
    ring_a (the vertices round its open end, in order) through joints
    [(centre, radius), ...] (my modelling: the knuckles of the curl), the
    last of which is the fingertip's last ring, to a tip tip_len times that
    radius beyond it. The rings blend from the opening's shape to round
    (grow); the tip is a fan to one point. Texture: band (a BandMap) along
    the new part. Shade: each new point's normal against light, base +
    swing * n.light (the open end keeps the shade it has)."""
    *mid, (tc, tr) = joints
    tc = Vector(tc)
    c0 = sum((v.co for v in ring_a), Vector()) / len(ring_a)
    prev = Vector(mid[-1][0]) if mid else c0
    d = (tc - prev).normalized()
    upv = Vector(up)
    u = upv - d * upv.dot(d)
    u = u.normalized() if u.length > 1e-6 else d.orthogonal().normalized()
    w = d.cross(u)
    t_end = bone(1.0)
    ring_b = [ws.new_vert(tc + (u * math.cos(2 * math.pi * k / count) + w * math.sin(2 * math.pi * k / count)) * tr,
                          t_end) for k in range(count)]
    first = len(ws.made)
    rings = grow(ws, pid, ring_a, ring_b, mid, count, [(0.0, band)], up, bone, debug=debug)
    apex = ws.new_vert(tc + d * (tip_len * tr), t_end)
    cap = []
    for k in range(count):
        a, b = ring_b[k], ring_b[(k + 1) % count]
        uvs = [band(a.co, 1.0, k / count), band(b.co, 1.0, (k + 1) / count), band(apex.co, 1.0, (k + 0.5) / count)]
        f = ws.face(pid, [a, b, apex], band.tex, uvs, [255.0, 255.0, 255.0])
        if f is not None:
            cap.append(f)
    # turned out by the shape, not by the open end's faces (the N64 may
    # have wound a cut-off end it never showed either way)
    centres = [c0] + [Vector(j[0]) for j in joints]
    ws.bm.normal_update()
    for group in (ws.made[first:len(ws.made) - len(cap)], cap):
        out = sum(f.normal.dot(f.calc_center_median() - min(centres, key=lambda q: (q - f.calc_center_median()).length))
                  * f.calc_area() for f in group)
        if debug:
            print("   extend: %d faces, outward %.0f" % (len(group), out))
        if out < 0:
            bmesh.ops.reverse_faces(ws.bm, faces=list(group))
    ws.bm.normal_update()
    # shade the new points by the way they face (the ROM lights nothing)
    made = ws.made[first:]
    ws.bm.normal_update()
    new = {v for r in rings[1:] for v in r} | {apex}
    normal = {}
    for f in made:
        for v in f.verts:
            if v in new:
                normal[v] = normal.get(v, Vector()) + f.normal * f.calc_area()
    light = Vector(light).normalized()
    for f in made:
        for lp in f.loops:
            if lp.vert in new and normal[lp.vert].length > 1e-9:
                k = max(0.0, min(1.0, shade[0] + shade[1] * normal[lp.vert].normalized().dot(light)))
                lp[ws.shade] = (k, k, k, 1.0)
    return rings, apex


def harmonic_uv(ws, faces, tex):
    """Texture coordinates for a new surface in tex, smooth and unfolded: the
    corners on ROM vertices that tex's faces touch keep those faces'
    coordinates (the skin runs on without a seam there), and every other
    point sits at the average of its neighbours (a harmonic map: it stays
    inside the region those corners cover, so it never reaches the
    texture's pale surround, and nearest-point jumps cannot fold it).
    Returns {vertex index: (u, v)}, or None if no corner is held."""
    import numpy as np
    verts = sorted({v for f in faces for v in f.verts}, key=lambda v: v.index)
    at = {v.index: k for k, v in enumerate(verts)}
    held = {}
    for v in verts:
        if not ws.is_rom(v):
            continue
        for f in ws.rom_faces(v):
            if ws.rom_tex(f) == tex:
                lp = next(l for l in f.loops if l.vert == v)
                held[v.index] = (lp[ws.uv].uv.x, 1.0 - lp[ws.uv].uv.y)
                break
    if not held:
        return None
    nbrs = {v.index: set() for v in verts}
    for f in faces:
        for e in f.edges:
            a, b = e.verts
            nbrs[a.index].add(b.index)
            nbrs[b.index].add(a.index)
    n = len(verts)
    A = np.zeros((n, n))
    rhs = np.zeros((n, 2))
    for v in verts:
        i = at[v.index]
        if v.index in held:
            A[i, i] = 1.0
            rhs[i] = held[v.index]
        else:
            A[i, i] = len(nbrs[v.index])
            for w in nbrs[v.index]:
                A[i, at[w]] -= 1.0
    X = np.linalg.solve(A, rhs)
    return {v.index: (float(X[at[v.index]][0]), float(X[at[v.index]][1])) for v in verts}


def fill_palm(ws, pid, loop_spec, tex, uvbox, light, fair=1, dome=0.2, shade=(0.80, 0.20), skin=None,
              rom_shade=None, smooth_uv=False, dome_out=False):
    """A palm, or any opening left once the fingers are closed: Liepa's fill
    of the loop (gevr_hands_patch.py's fill), refined, faired and domed into
    a cushion. loop_spec names a vertex on the loop, or is the cycle itself
    (vertices in order, wound as directed_loops winds a hole; its last edge
    may be one the fill closes). Texture: uvbox (a box of tex, planar), or
    skin (a SkinMap) carrying the ROM's mapping on; with smooth_uv and a
    one-texture skin, harmonic_uv in its texture (shade still from the
    skin). Shade: by the faces' normals, or with rom_shade (a factor) the
    rim's own colours times it. The dome rises along the fill's own normal,
    which follows however the loop winds, and can sink into the hand; with
    dome_out it rises out of it (as the ROM faces round the rim point). The
    PP7 and watch arm pieces were accepted without it."""
    bm = ws.bm
    first = len(ws.made)
    if isinstance(loop_spec, (list, tuple)):
        lp = list(loop_spec)
    else:
        loops, _ = P.directed_loops(bm)
        lp = P.find_loop(loops, ws.R, loop_spec)
    tris, w = P.fill_loop(lp, None)
    bm.verts.index_update()
    before = len(bm.verts)
    lp_idx = [v.index for v in lp]
    work = 100000 + pid   # a tag of its own while refining (the piece may hold more)
    for t in tris:
        try:
            f = bm.faces.new(t)
        except ValueError:
            continue
        f[ws.lay_piece] = work
    for _ in range(fair):
        P.refine(bm, ws.lay_piece, work)
    bm.verts.index_update()
    bm.verts.ensure_lookup_table()
    lp = [bm.verts[i] for i in lp_idx]
    created = [v for v in bm.verts if v.index >= before]
    bone = ws.bone_of(lp[0])
    for v in created:
        v[ws.lay_bone] = bone
    if fair:
        P.fair_positions(bm, created)
    if dome:
        bm.normal_update()
        n = sum((f.normal for f in bm.faces if f[ws.lay_piece] == work), Vector())
        sinks = n.dot(sum((ws.outward(v) for v in lp), Vector())) < 0
        if sinks and not dome_out:
            at = next((ws.R.name(v) for v in lp if ws.is_rom(v)), "new points")
            print("   fill at %s: its dome sinks into the hand (dome_out turns it out)" % at)
        P.dome(bm, created, lp, ws.lay_piece, work, -dome if sinks and dome_out else dome)
    faces = [f for f in bm.faces if f[ws.lay_piece] == work]
    multi = skin is not None and getattr(skin, "tex", 0) is None   # several textures: per face
    if skin is not None:
        tex = skin.tex
        uv = {} if multi else {v.index: skin(v.co) for f in faces for v in f.verts}
        if smooth_uv and not multi:
            uv = harmonic_uv(ws, faces, tex) or uv
    else:
        uv = P.plane_uv([tuple(f.verts) for f in faces], uvbox)
    bm.normal_update()
    vn = {}
    for f in faces:
        for v in f.verts:
            vn[v.index] = vn.get(v.index, Vector()) + f.normal * f.calc_area()
    # the fill's faces point out of the hand? compare with the ROM round the rim
    out = sum((ws.outward(v) for v in lp), Vector())
    sign = 1.0
    if sum((vn[v.index] for v in lp if v.index in vn), Vector()).dot(out) < 0:
        sign = -1.0
    rim = [ws.rom_colour(v) for v in lp if ws.is_rom(v)]
    rim_shade = (sum(rim) / len(rim) if rim else 255.0) * (rom_shade or 1.0)
    for f in faces:
        ft = skin.tex_at(f.calc_center_median()) if multi else tex
        f[ws.lay_piece] = pid
        f[ws.lay_tex] = ft
        f.smooth = True
        ws.made.append(f)
        for l in f.loops:
            v = l.vert
            u, w = skin(v.co, tex=ft) if multi else uv[v.index]
            l[ws.uv].uv = (u, 1.0 - w)
            if ws.is_rom(v):
                s_ = ws.rom_colour(v) / 255.0
            elif rom_shade is not None:
                s_ = (rom_shade * skin.colour(v.co) if hasattr(skin, "colour") else rim_shade) / 255.0
            else:
                n = (vn[v.index] * sign).normalized()
                s_ = shade_for(n, light, *shade) / 255.0
            l[ws.shade] = (s_, s_, s_, 1)
    ws.settle(pid, faces=ws.made[first:])
    return faces


# ---------------------------------------------------------------------------
# Pieces out as editable objects
# ---------------------------------------------------------------------------

def extract(ws, collection, materials_for, names=None):
    """Each piece (or those named) as its own mesh object: ROM vertices keep
    their refs (rom_node, rom_idx, rom_mtx), new ones -1 and their bone in
    "mtx"; the corners keep UVs and shade; one material per texture."""
    import bpy
    bm = ws.bm
    bm.verts.index_update()
    lay_n = bm.verts.layers.int["rom_node"]
    lay_i = bm.verts.layers.int["rom_idx"]
    lay_m = bm.verts.layers.int["rom_mtx"]
    objs = []
    for pid, meta in ws.pieces.items():
        if names and meta["name"] not in names:
            continue
        faces = [f for f in bm.faces if f[ws.lay_piece] == pid]
        if not faces:
            print("   piece %s made no faces" % meta["name"])
            continue
        me = bpy.data.meshes.new(meta["name"])
        ob = bpy.data.objects.new(meta["name"], me)
        collection.objects.link(ob)
        out = bmesh.new()
        on = out.verts.layers.int.new("rom_node")
        oi = out.verts.layers.int.new("rom_idx")
        om = out.verts.layers.int.new("rom_mtx")
        ob_ = out.verts.layers.int.new("mtx")
        ouv = out.loops.layers.uv.new("UVMap")
        osh = out.loops.layers.color.new("shade")
        vmap = {}
        texslot = {}
        for f in faces:
            vs = []
            for v in f.verts:
                if v.index not in vmap:
                    nv = out.verts.new(v.co)
                    if ws.is_rom(v) or v in ws.alias:
                        node, idx, mtx = ws.alias[v] if v in ws.alias else ws.R.primary(v)
                        nv[on], nv[oi], nv[om] = node, idx, mtx
                        nv[ob_] = mtx
                    else:
                        nv[on], nv[oi], nv[om] = -1, -1, -1
                        nv[ob_] = v[ws.lay_bone]
                    vmap[v.index] = nv
                vs.append(vmap[v.index])
            try:
                nf = out.faces.new(vs)
            except ValueError:
                continue
            tex = f[ws.lay_tex]
            if tex not in texslot:
                texslot[tex] = len(texslot)
                me.materials.append(materials_for(tex))
            nf.material_index = texslot[tex]
            nf.smooth = True
            for nl, l in zip(nf.loops, f.loops):
                nl[ouv].uv = l[ws.uv].uv
                nl[osh] = l[ws.shade]
        out.to_mesh(me)
        out.free()
        ob["host"] = "0x%04x" % meta["host"]
        ob["why"] = meta["why"]
        ob["inherit"] = True
        objs.append(ob)
    return objs
