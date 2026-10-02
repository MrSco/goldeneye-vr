"""
The modelling of each hand's missing pieces (issue #9), run by
`gevr_hands_author.py seed <model> [pieces]`, with the primitives in
gevr_hands_model.py. Vertex names are ROM vertices: 57 is index 57 of the
host node's block, "0x02b8:57" names the node. All numbers are our modelling
choices; the result is committed as weights and texture coordinates.

Each seed builds every piece in order (a palm is filled round the fingers
built before it); the author tool commits all of them or only those named,
and the others stay as they were committed (edits made in Blender included).
"""

from mathutils import Vector

import gevr_hands_model as M
import gevr_hands_patch as P


def wanted(names, piece):
    return not names or piece in names


# ---------------------------------------------------------------------------
# Csuit_lf_handZ: the left watch arm
# ---------------------------------------------------------------------------
# One rigid mesh (node 0x01c0, bone 0), about ten times the pistol hand's
# units. Fingers run along +x, y is the back of the hand, z goes across
# (thumb at +z). The N64 camera saw the back of the hand, so the ring and
# little fingers are top halves, the middle fingertip is open, the index
# finger is open inside two of its bends, and the palm is missing between the
# heel and thumb skin it did model (0x705, facing down) and the finger roots.
# Every ROM vertex is lit 255 (the textures carry the light). The pieces
# take the skin beside them, each face in the texture of the N64 skin
# nearest to it (the finger tops are 0x702 at the root, 0x703 on), and are
# shaded a little down away from the N64's edges. The palm keeps to 0x705
# alone (faces switching between it and the finger roots' 0x702 made a
# ragged seam across it), mapped smoothly from the heel and thumb skin
# round it (nearest-point lookups folded near the finger roots).

CSUIT_LIGHT = Vector((0.15, 1.0, 0.3)).normalized()
CSUIT_SKIN = (0x702, 0x703, 0x704, 0x705)   # the back of the hand, fingers, index tip, heel and thumb
CSUIT_PALM = 0x705    # the palm side the N64 did model (heel, thumb's root): one texture, no seam across
CSUIT_UNDER = 0.88    # palm side, away from the N64's edges: that much of its 255
CSUIT_CREASE = 0.8    # inside a finger's bend
CSUIT_NEAR = 150.0    # how far from a piece's rim the skin it takes may lie (watch arm units)
CSUIT_BAND = (0x703, 380, 712, 370, 250)   # a fingertip's skin: the 0x703 row the PP7's fingers run in
# The fingers the N64 cut short, curled on in towards the palm to close the
# fist (the user's ask). Per finger: its radius r; the first knuckle that
# many r out of the open end along its normal, at 0.85 r round; then each
# further joint (down, back) in r from the one before, at that many r
# round (the last is the fingertip's last ring); how far the tip swells
# past it, in its radius.
CSUIT_CURL = {
    "ring_finger": (95.0, 0.5, [(1.2, 0.3, 0.8), (0.5, 1.0, 0.68)], 0.55),
    "little_finger": (85.0, 0.5, [(1.15, 0.3, 0.8), (0.5, 0.95, 0.68)], 0.55),
    "middle_finger": (100.0, 0.5, [(1.2, 0.35, 0.8), (0.5, 1.05, 0.68)], 0.55),
}


def seed_csuit(ws, names):
    P.directed_loops.quiet = True

    def skin(textures, names_, reach=CSUIT_NEAR):
        return M.NearMap(ws, textures, near=[ws.vert(x).co for x in names_], reach=reach)

    def curl(pid, name, opening):
        """Grow the finger on from its open end into a fingertip curled in
        towards the palm (CSUIT_CURL)."""
        r, out, bends, tip_len = CSUIT_CURL[name]
        c0 = sum((v.co for v in opening), Vector()) / len(opening)
        # the opening's normal (Newell), turned away from the finger
        n = Vector()
        for a, b in zip(opening, opening[1:] + opening[:1]):
            n += (a.co - c0).cross(b.co - c0)
        n.normalize()
        inside = sum((f.calc_center_median() for v in opening for f in ws.rom_faces(v)), Vector())
        inside /= max(1, sum(len(ws.rom_faces(v)) for v in opening))
        if n.dot(c0 - inside) < 0:
            n = -n
        p = c0 + n * (out * r)
        joints = [(p, 0.85 * r)]
        for down, back, rad in bends:
            p = p + Vector((-back * r, -down * r, 0.0))
            joints.append((p, rad * r))
        M.extend(ws, pid, opening, joints, tip_len, 8, M.BandMap(CSUIT_BAND[0], 32, 32, *CSUIT_BAND[1:]),
                 (1, 1, 0), lambda t: 0, (0, 1, 0), shade=(0.95, 0.05))

    # the ring and little fingers: the palm side closed round under the N64's
    # top half, station by station along both open edges, the top's own skin
    # turned under (mirrored) so the seam along each edge does not show; then
    # the finger grown on past the N64's cut-off end, curled in
    # Each open edge runs base to tip but doubles back at the knuckle crease
    # (the ring's B edge goes 74, 39, 51, 40: 39 sticks out past 51), which
    # scrambles the cross-sections; the rails keep to the vertices that run
    # on, and the notches the skipped ones make are filled flat. The faces
    # along these edges lean down (the shell wraps below them), so "the top
    # is +y" is said outright.
    for name, rails, notches, tip in (
            ("ring_finger", [[71, 72, 47, 46], [73, 74, 40, 42]], [49, 39], 43),
            ("little_finger", [[24, 25, 8, 7], [26, 27, 1, 3]], [9, 0], 4)):
        pid = ws.piece(name, "the %s's palm side, closed round under the N64's top half, then grown on into a "
                       "fingertip curled in towards the palm" % name.replace("_", " "))
        finger = skin(CSUIT_SKIN, rails[0] + rails[1] + notches + [tip])
        M.underside(ws, pid, rails, None, 0x703, (0, 1, 0, 1), CSUIT_LIGHT, arc=2, skin=finger,
                    rom_shade=CSUIT_UNDER, mirror=True, up_hint=(0, 1, 0))
        for notch in notches:
            M.fill_palm(ws, pid, notch, 0x703, None, CSUIT_LIGHT, fair=0, dome=0, skin=finger, rom_shade=1.0)
        curl(pid, name, loop_verts(ws, tip))

    # the middle finger: grown on from its open fingertip, curled in
    pid = ws.piece("middle_finger", "the middle finger grown on from its open tip into a fingertip curled in "
                   "towards the palm")
    curl(pid, "middle_finger", loop_verts(ws, 87))

    # the index finger's two bends, open on their inside: closed nearly flat
    # (a crease), a little darker
    pid = ws.piece("index_creases", "the index finger's two bends, closed inside, in its own skin")
    for crease in ([167, 197, 213, 211, 190, 172, 173, 177, 179], [127, 164, 178, 176, 162, 135, 137, 139, 141]):
        M.fill_palm(ws, pid, crease[0], 0x703, None, CSUIT_LIGHT, fair=1, dome=0.1,
                    skin=skin(CSUIT_SKIN, crease), rom_shade=CSUIT_CREASE)

    # the palm: what is left between the heel, the thumb's root and the
    # finger roots (the fingers' palm sides above close their part), faired
    # into a cushion. (The 3-vertex "hole" at 398/399/400 is no hole: one
    # N64 triangle hanging off the heel by a corner.)
    rim = [0, 27, 26, 11, 67, 71, 73, 57, 109, 111, 115, 99, 210, 212, 214, 245, 243, 242, 241, 398, 424, 423,
           383, 382, 385, 30, 22, 24]
    pid = ws.piece("palm", "the palm between the heel, the thumb's root and the finger roots, in the skin "
                   "beside it")
    palm = skin(CSUIT_PALM, rim, reach=3 * CSUIT_NEAR)
    M.fill_palm(ws, pid, 241, 0x705, None, CSUIT_LIGHT, fair=1, dome=0.15, skin=palm, rom_shade=CSUIT_UNDER,
                smooth_uv=True)


# ---------------------------------------------------------------------------
# GwppkZ: the pistol hand (and the silenced, gold and silver PPK by same_as)
# ---------------------------------------------------------------------------
# The gun points along +z, y is up, +x is the gun's left (the thumb's side).
# The fingers wrap the grip (x -11..11, front strap z -18 at y -75 to -24 at
# y -112). The N64 camera sat behind, left and above: it saw the knuckles on
# the right, the thumb and fingertips on the left, never the front strap.
# So each finger stops at an open edge on the right and starts again, open,
# at the left:
#   middle  a half shell over its first joint (open underneath, its front end
#           open), then a flat plate across the front (0x0318:25..29), welded
#           at its left end to the open fingertip 0x0390;
#   ring    a knuckle flap (0x0318:0..4, a roof open underneath), then
#           nothing until its open fingertip 0x03a8;
#   little  the same: flap 0x0468:20/22/39/40/42, open fingertip 0x03c0.
# Each finger is grown out of those edges: the open undersides closed round
# under the shells (their own texture and colours carried on), then a
# segment whose first ring is the shell's own front edge and whose last is
# the fingertip's, the rings between morphing from one cross-section into
# the other along the way round the grip.

PPK_LIGHT = Vector((-0.3, 0.8, 0.5)).normalized()
PPK_UNDER = 1.0       # undersides: the rails' own shade (the N64 shades with 255 and 190)
PPK_DEEP = (1.0, 1.45)  # under a flap, deeper than half its width: a finger is thicker than its knuckle roof
PPK_KNUCKLE = 0.35      # a flap's rounded cover: that much of its half-width above it
PPK_HEEL = 1.0          # the pad below the butt, between the heel and the little finger: its dome (x its radius)


def PPK_BAND(tex, s0, s1, t_top, t_bottom):
    """From its middle knuckle on, a finger runs on in its own fingertip's
    texture row: s along it up to where the tip's own mapping starts at its
    open end, t from the finger's top to its underside as the tip has them,
    so segment and tip read as one finger."""
    return M.BandMap(tex, 32, 32, s0, s1, t_top, t_bottom)


def front_ring(ws, arc_ring, *rest):
    """A segment's first ring: an underside's last arc (rail B, new points,
    rail A) and the shell's front edge vertices from rail A back round to B."""
    return list(arc_ring) + [ws.vert(x) for x in rest]


def near_skin(ws, names, skip=False, reach=40.0, nodes=None):
    """The skin beside some ROM vertices, carried on point by point (the
    texture they mostly use, the faces within reach of them, of nodes if
    given); with skip, not their own faces (a flap being covered)."""
    vs = [ws.vert(x) if not hasattr(x, "co") else x for x in names]
    area = {}
    for v in vs:
        for f in ws.rom_faces(v):
            if not (skip and all(x in vs for x in f.verts)):
                area[ws.rom_tex(f)] = area.get(ws.rom_tex(f), 0.0) + f.calc_area()
    tex = max(area, key=area.get)
    return M.NearMap(ws, tex, skip=vs if skip else (), near=[v.co for v in vs], reach=reach, nodes=nodes)


def knuckle_ring(under, cover):
    """The ring a finger grows from at a covered knuckle flap: the
    underside's last arc (rail B, new points, rail A) and back over the
    cover's (the flap's own front edge is inside)."""
    u = under[-1][1]
    c = cover[-1][1]
    return list(u) + list(reversed(c[1:-1]))


def left_out(chain, proj, skipped):
    """Each stretch of a skirt's rim it left out (skipped vertex indices),
    as a hole to fill: from the strip's last point on the grip before the
    stretch, along it, to the first point after - wound as directed_loops
    winds a hole (against the faces along its rim); the side along the grip
    is the fill's own."""
    out, i, n = [], 0, len(chain)
    while i < n:
        if chain[i].index not in skipped:
            i += 1
            continue
        j = i
        while j + 1 < n and chain[j + 1].index in skipped:
            j += 1
        if i > 0 and j + 1 < n and proj[i - 1] is not None and proj[j + 1] is not None:
            cycle = [proj[i - 1]] + chain[i:j + 1] + [proj[j + 1]]
            a, b = chain[i], chain[i + 1] if i < j else proj[j + 1]
            e = next(e for e in a.link_edges if e.other_vert(a) is b)
            f = e.link_faces[0]
            if any(l.vert is a and l.link_loop_next.vert is b for l in f.loops):
                cycle.reverse()
            out.append(cycle)
        i = j + 1
    return out


def loop_verts(ws, spec):
    loops, _ = P.directed_loops(ws.bm)
    return list(P.find_loop(loops, ws.R, spec))


def seed_ppk(ws, names):
    P.directed_loops.quiet = True
    bone0 = lambda t: 0   # noqa: E731 - every part here is on bone 0

    # the middle finger: under its first joint, under the plate, joined
    pid = ws.piece("middle_finger", "the middle finger: its first joint and the plate across the grip closed "
                   "underneath, joined to each other and into the fingertip")
    shell = near_skin(ws, ("0x0318:12", "0x0318:19", "0x0318:15", "0x0318:11", "0x0318:5"))
    first = M.underside(ws, pid, [["0x0318:13", "0x0318:12", "0x0318:19"], ["0x0318:14", "0x0318:18", "0x0318:15"]],
                        None, shell.tex, (0, 1, 0, 1), PPK_LIGHT, arc=2, skin=shell, rom_shade=PPK_UNDER)
    plate = near_skin(ws, ("0x0318:29", "0x0318:28", "0x0318:26", "0x0318:27"))
    top = M.underside(ws, pid, [["0x0318:29", "0x0318:28"], ["0x0318:26", "0x0318:27"]],
                      None, plate.tex, (0, 1, 0, 1), PPK_LIGHT, arc=2, skin=plate, rom_shade=PPK_UNDER,
                      roundness=PPK_DEEP)
    # the joint's front opening to the plate's right end (they nearly touch)
    a = front_ring(ws, first[-1][1], "0x0318:11", "0x0318:5", "0x0318:6")
    b = front_ring(ws, top[0][1], "0x0318:25")
    M.grow(ws, pid, a, b, [], 8, [(0.0, shell), (0.5, plate)], (0, 1, 0), bone0)
    # the bend into the fingertip: the plate's end (its left corner, then
    # under it back to front) zipped to the fingertip's open end (round its
    # inner side, underneath and its outer side): they start and end at the
    # corners the N64 already joins across the top, so the two
    # cross-sections meet as a mitre
    ring = ["0x0390:1", "0x0390:20", "0x0390:18", "0x0390:14", "0x0390:12", "0x0390:10", "0x0390:8", "0x0390:3"]
    M.zip_chains(ws, pid, [ws.vert("0x0318:24")] + list(top[-1][1]), [ws.vert(x) for x in ring],
                 near_skin(ws, ring))

    # the ring and little fingers: each grown from its knuckle flap (closed
    # underneath, a rounded cover over it: the flap's flat face and pale
    # highlight were never meant to be seen from this side), then its middle
    # joint round the grip's front - the knuckle at the grip's front right
    # corner, the phalanx straight across the front strap - into the
    # fingertip at the left corner. One texture band runs the whole finger,
    # in the row of 0x703 the N64's own ring fingertip continues.
    for name, flap_names, joints, tip_loop in (
            ("ring_finger", ("0x0318:3", "0x0318:4", "0x0318:1", "0x0318:2", "0x0318:0"),
             [((-17.5, -94, -14), 8.8), ((0, -94, -11.5), 8.8)], "0x03a8:0"),
            ("little_finger", ("0x0468:20", "0x0468:42", "0x0468:40", "0x0468:22", "0x0468:39"),
             [((-18, -112.5, -17), 8.4), ((0, -112.5, -14.5), 8.4)], "0x03c0:0")):
        pid = ws.piece(name, "the %s: its knuckle flap closed underneath and rounded over, then its "
                       "middle joint round the grip's front, grown from the knuckle into the fingertip's "
                       "open end" % name.replace("_", " "))
        back_a, front_a, front_mid, back_b, front_b = flap_names
        rails = [[back_a, front_a], [back_b, front_b]]
        tipring = loop_verts(ws, tip_loop)
        # the band's share for the knuckle, by length
        back = (ws.vert(back_a).co + ws.vert(back_b).co) / 2
        front = (ws.vert(front_a).co + ws.vert(front_b).co) / 2
        tip = sum((v.co for v in tipring), Vector()) / len(tipring)
        knots = [front] + [Vector(j[0]) for j in joints] + [tip]
        rest = sum((y - x).length for x, y in zip(knots, knots[1:]))
        f = (front - back).length / ((front - back).length + rest)
        band = PPK_BAND(0x703, 380, 712, 370, 250)
        under = M.underside(ws, pid, rails, None, band.tex, (0, 1, 0, 1), PPK_LIGHT, arc=2,
                            skin=M.Span(band, 0.0, f), rom_shade=PPK_UNDER, roundness=PPK_DEEP)
        cover = M.underside(ws, pid, rails, None, band.tex, (0, 1, 0, 1), PPK_LIGHT, arc=2,
                            skin=M.Span(band, 0.0, f), rom_shade=PPK_UNDER, over=PPK_KNUCKLE)
        # the cover's back end stands open over the flap's back edge (you
        # see into the knuckle there from behind): closed
        M.plug(ws, pid, cover[0][1])
        M.grow(ws, pid, knuckle_ring(under, cover), tipring, joints, 8, [(0.0, M.Span(band, f, 1.0))],
               (0, 1, 0), bone0, debug=True)

    # the inside of the fist: what is still open round the fingers' inner
    # rims, closed onto the grip the way gevr_hands_patch.py's skirt does
    # it (a strip from each rim vertex to just outside the grip), built
    # after the fingers so it only closes what they leave, and in the skin
    # beside it, not a texture of its own
    hand = {0x0318, 0x0390, 0x03a8, 0x03c0, 0x0408, 0x0468, 0x04b0, 0x02b8}
    held = [p_["node"] for p_ in ws.model["parts"] if p_["node"] not in hand
            and any(t["node"] == p_["node"] for t in ws.model["tris"])]
    pid = ws.piece("fist_inside", "the inside of the fist round the fingers, closed onto the grip")
    work = 200000 + pid
    import bmesh
    from mathutils.bvhtree import BVHTree
    import gevr_hands_import as H
    tob = H.build_part(ws.model, held, {}, name="fist_target")
    tbm = bmesh.new()
    tbm.from_mesh(tob.data)
    target = BVHTree.FromBMesh(tbm)
    butt = min(v.co.y for v in tbm.verts)
    tbm.free()
    ws.bm.verts.index_update()
    ws.bm.verts.ensure_lookup_table()
    # below the grip's butt the heel and the little finger's knuckle hang
    # free: a strip from there pulled up to the butt's edge is a flat fan
    # that reads as a bite out of the pinky's base. Those rim vertices are
    # left out of the strip, and what they leave is closed as a cushion.
    chain, _ = P.boundary_chain(ws.bm, ws.R, "largest")
    below = {v.index for v in chain if v.co.y < butt - 1.0}
    rim = {}
    made, pts, nchain, closed = P.skirt(ws.bm, ws.R, {"chain": "largest", "gap": 0.6, "reach": 60, "skip": below},
                                        (ws.lay_piece, work), target, record=rim)
    for v in pts:
        v[ws.lay_bone] = 0
    print("   fist inside: a rim of %d (%s), %d faces, %d below the butt"
          % (nchain, "ring" if closed else "open", len(made), len(below)))
    fist = M.NearMap(ws, 0x0702, nodes={0x0318, 0x0468, 0x04b0})
    M.adopt(ws, pid, made, fist)
    pid = ws.piece("heel_pad", "below the grip's butt, between the heel and the little finger's knuckle: "
                   "the palm's pad, domed out to fill the hollow under the knuckle")
    for cycle in left_out(rim["chain"], rim["proj"], below):
        M.fill_palm(ws, pid, cycle, fist.tex, None, PPK_LIGHT, fair=1, dome=PPK_HEEL, skin=fist, rom_shade=1.0)

    # the trigger finger (0x02b8, its own bone): its first joint is a shell
    # open underneath, at its base and inside the bend into the next joint.
    # Closed as before (one fill of that opening, no new points), now in the
    # finger's own skin - its texture carried on from the faces beside each
    # corner and the N64's own colours at them (255 on top, 190 underneath)
    # instead of the pale palm texture at full brightness.
    wt = M.Workspace(ws.model, [0x02b8], 0x02b8)
    pid = wt.piece("trigger_finger", "the trigger finger's first joint closed underneath, at its base and "
                   "inside the bend, in its own skin")
    skin = M.NearMap(wt, 0x0703, nodes={0x02b8})
    M.fill_palm(wt, pid, "0x02b8:0", skin.tex, None, PPK_LIGHT, fair=0, dome=0, skin=skin, rom_shade=1.0)
    return [wt]


SEEDS = {
    "Csuit_lf_handZ": seed_csuit,
    "GwppkZ": seed_ppk,
}
