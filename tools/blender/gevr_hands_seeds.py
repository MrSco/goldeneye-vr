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

import math

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
CSUIT_FINGER = 0.8    # the ring and little fingers' palm sides (lighter, they read as a bulge)
CSUIT_PALMSIDE = (0x702, (728, 865, 481, 594))   # the middle finger's own palm side: 0x702, s along,
                                                 # t across; the ring and little fingers' undersides
                                                 # wear the same (the top's skin mirrored under them
                                                 # read as puffy)
CSUIT_CREASES = (     # the index finger's bends: each crack's two sides, from the end they share
    ([167, 197, 213, 211, 190], [167, 179, 177, 173, 172]),
    ([127, 164, 178, 176, 162], [127, 141, 139, 137, 135]),
)
CSUIT_NEAR = 150.0    # how far from a piece's rim the skin it takes may lie (watch arm units)
CSUIT_FLAT = (0.05, 0.12)  # the ring and little fingers' undersides: this deep, in half-widths (the
                          # N64's shell already wraps most of the way round; a half-round bottom on
                          # it made them "a little chubby" next to the middle finger)
CSUIT_BAND = (0x703, 380, 712, 370, 250)   # a fingertip's skin: the 0x703 row the PP7's fingers run in
# The fingers the N64 cut short, curled on in towards the palm to close the
# fist (the user's ask). A curl starts from the finger's whole end - its
# last cross-section, the N64's top and our underside together (the N64's
# sloped end cap ends up inside) - so it carries on in line with the finger
# and as wide (hung from the underside alone it sat low, set back and thin:
# "misaligned"). The middle fingertip is closed but for its underside, so
# its curl leaves that opening at the fingertip's front. Per finger: its
# radius r there; the first knuckle (forward, down) in r from the end's
# centre, at that many r round; each further joint (down, back) in r from
# the one before, at that many r round (the last is the fingertip's last
# ring); how far the tip swells past it, in its radius. r None: the radius
# of the finger's end itself, so the curl is exactly as thick as the finger.
CSUIT_CURL = {
    "ring_finger": (None, (0.55, 0.0, 0.75), [(1.15, 0.3, 0.68), (0.5, 1.0, 0.58)], 0.5),
    "little_finger": (None, (0.55, 0.0, 0.7), [(1.1, 0.3, 0.62), (0.5, 0.95, 0.52)], 0.5),
    "middle_finger": (95.0, (0.45, 0.8, 0.95), [(0.95, 0.4, 0.85), (0.5, 1.0, 0.72)], 0.55),
}


def seed_csuit(ws, names):
    P.directed_loops.quiet = True

    def skin(textures, names_, reach=CSUIT_NEAR):
        return M.NearMap(ws, textures, near=[ws.vert(x).co for x in names_], reach=reach)

    def curl(pid, name, end):
        """Grow the finger on from end (the vertices round its end, in
        order) into a fingertip curled in towards the palm (CSUIT_CURL)."""
        r, (fwd, down0, rad0), bends, tip_len = CSUIT_CURL[name]
        c0 = sum((v.co for v in end), Vector()) / len(end)
        if r is None:
            r = sum((v.co - c0).length for v in end) / len(end)
        p = c0 + Vector((fwd * r, -down0 * r, 0.0))
        joints = [(p, rad0 * r)]
        for down, back, rad in bends:
            p = p + Vector((-back * r, -down * r, 0.0))
            joints.append((p, rad * r))
        M.extend(ws, pid, end, joints, tip_len, 8, M.BandMap(CSUIT_BAND[0], 32, 32, *CSUIT_BAND[1:]),
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
        under = M.underside(ws, pid, rails, None, CSUIT_PALMSIDE[0], CSUIT_PALMSIDE[1], CSUIT_LIGHT, arc=2,
                            rom_shade=CSUIT_FINGER, up_hint=(0, 1, 0), roundness=CSUIT_FLAT)
        for notch in notches:
            M.fill_palm(ws, pid, notch, 0x703, None, CSUIT_LIGHT, fair=0, dome=0, skin=finger, rom_shade=1.0)
        # the finger's whole end: rail A's last vertex, over the N64's top
        # (not through its fingertip), rail B's last, back under ours
        a, b = ws.vert(rails[0][-1]), ws.vert(rails[1][-1])
        top = ws.path_over(a, b, 1e9, [ws.vert(x) for x in rails[0] + rails[1] + notches + [tip]])
        curl(pid, name, [a] + list(reversed(top)) + [b] + list(under[-1][1][1:-1]))

    # the middle finger: grown on from its open fingertip, curled in
    pid = ws.piece("middle_finger", "the middle finger grown on from its open tip into a fingertip curled in "
                   "towards the palm")
    curl(pid, "middle_finger", loop_verts(ws, 87))

    # the index finger's two bends, open on their inside: cracks, not holes
    # (the two segments' ends run side by side 16-50 units apart, a quarter
    # of the finger's width at most), zipped shut in the skin either side.
    # (Filled and domed, as first, the dome sank into the bend.)
    pid = ws.piece("index_creases", "the index finger's two bends, closed inside, in its own skin")
    for a, b in CSUIT_CREASES:
        M.zip_chains(ws, pid, [ws.vert(x) for x in a], [ws.vert(x) for x in b], skin(CSUIT_SKIN, a + b))

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
PPK_HEEL_OUT = True     # ... rising out of the hand (fill_palm's dome_out: the first pad sank into it)
PPK_STUMP = (1, 0.1, 0.8)   # the forearm's cut end: fairing, dome, and a little darker than the skin round it


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


# The other pistols' hands (golden gun, Cougar, DD44) are the PP7's: fitted
# node by node (a rigid motion each), every piece is the same mesh in the
# same pose, the whole hand moved by one offset in the gun's model (the
# golden gun's forearm alone is its own). So they are modelled by the PP7's
# seed: each PP7 vertex it names is the gun's ROM vertex at the same place
# (offset on), its joints move with the hand, and what depends on the gun -
# the inside of the fist closed onto the grip, the pad below the butt - is
# built against the gun's own grip. Per gun: the hand's offset from the
# PP7's, the trigger finger's node (its own bone), the forearm's cut end
# (and the nodes whose skin it takes), the nodes whose 0x702 lines the fist,
# and how far the ring and little fingers' knuckles round the grip move
# forward where the gun's grip stands further forward than the PP7's (found
# by measuring how deep the grown fingers went into it; kept 1 unit off).
PISTOLS = {
    "GwppkZ": {"offset": (0.0, 0.0, 0.0), "trigger": 0x02b8, "forearm_end": ("0x04b0:108", {0x04b0}),
               "fist": {0x0318, 0x0468, 0x04b0}},
    "GgoldengunZ": {"offset": (0.0, -8.5, -80.7), "trigger": 0x021c, "forearm_end": ("0x0324:205", {0x0324}),
                    "fist": {0x024c, 0x0324},
                    "knuckles": {"ring_finger": (0.0, 0.0, 7.8), "little_finger": (0.0, 0.0, 9.6)}},
    "GrugerZ": {"offset": (0.0, -26.4, -70.2), "trigger": 0x02b8, "forearm_end": ("0x0468:55", {0x0468}),
                "fist": {0x0240, 0x02d0, 0x0450, 0x0468},
                "knuckles": {"ring_finger": (0.0, 0.0, 6.2), "little_finger": (0.0, 0.0, 2.6)}},
    "Gtt33Z": {"offset": (0.0, -8.1, -182.6), "trigger": 0x03b4, "forearm_end": ("0x0564:47", {0x0564}),
               "fist": {0x0384, 0x0414, 0x054c, 0x0564}},
    # the knives' hand: the PP7's middle, ring and little fingers and heel,
    # 3.1 units higher; its index finger (the PP7's trigger finger, curled
    # round the handle on bone 0), thumb and forearm are its own
    "GknifeZ": {"offset": (0.0, 3.1, 0.0), "trigger": None, "forearm_end": ("0x01a4:104", {0x01a4}),
                "fist": {0x01a4, 0x03b4}},
    "GthrowknifeZ": {"offset": (0.0, 3.1, 0.0), "trigger": None, "forearm_end": ("0x015c:92", {0x015c}),
                     "fist": {0x015c, 0x0384}},
}
SKIN_TEXTURES = (0x701, 0x702, 0x703, 0x704, 0x705, 0x706)


class PistolLayout:
    """The PP7's names carried over to another gun's hand: its ROM vertex at
    the PP7 vertex's place, offset on (within 2 units: the meshes match to
    their rounding); the PP7 itself maps to itself."""

    def __init__(self, ws, gun):
        import os
        import gevr_hp_common as C
        self.ws = ws
        self.cfg = PISTOLS[gun]
        self.offset = Vector(self.cfg["offset"])
        self.same = gun == "GwppkZ"
        self.ppk = None
        if not self.same:
            here = os.path.dirname(os.path.abspath(__file__))
            path = os.path.join(os.path.dirname(os.path.dirname(here)), "build", "handmodels", "GwppkZ.json")
            ppk = C.load_model(path)
            self.ppk = {(v["node"], v["idx"]): Vector(v["pos"]) for v in ppk["verts"]}
        self.memo = {}

    def __call__(self, name):
        """A PP7 vertex name ("0x0318:12") as this gun's."""
        if self.same:
            return name
        if name not in self.memo:
            node, idx = name.split(":")
            p = self.ppk[(int(node, 16), int(idx))] + self.offset
            v = min((v for v in self.ws.bm.verts[:self.ws.n_orig]), key=lambda v: (v.co - p).length)
            if (v.co - p).length > 2.0:
                raise SystemExit("%s: no ROM vertex at PP7 %s (nearest %.1f away)" % (self.ws.model["model"], name,
                                                                                  (v.co - p).length))
            self.memo[name] = "0x%04x:%d" % self.ws.R.primary(v)[:2]
        return self.memo[name]

    def at(self, p):
        """A PP7 position (a joint) in this gun's hand."""
        return tuple(Vector(p) + self.offset)


def grip_tree(model):
    """What a hand model holds (every part not in the skin textures), as a
    BVH, and its lowest point (the grip's butt)."""
    import bmesh
    from mathutils.bvhtree import BVHTree
    import gevr_hands_import as H
    hand = {t["node"] for t in model["tris"] if t["tex"] in SKIN_TEXTURES}
    held = [p_["node"] for p_ in model["parts"] if p_["node"] not in hand
            and any(t["node"] == p_["node"] for t in model["tris"])]
    tob = H.build_part(model, held, {}, name="fist_target")
    tbm = bmesh.new()
    tbm.from_mesh(tob.data)
    tree = BVHTree.FromBMesh(tbm)
    butt = min(v.co.y for v in tbm.verts)
    tbm.free()
    return tree, butt


def seed_ppk(ws, names):
    return seed_pistol(ws, names, "GwppkZ")


def seed_pistol(ws, names, gun):
    P.directed_loops.quiet = True
    bone0 = lambda t: 0   # noqa: E731 - every part here is on bone 0
    L = PistolLayout(ws, gun)
    N = L
    import bmesh
    # what the hand holds (the gun), for the inside of the fist and, on the
    # other guns, the knuckles round the grip
    target, butt = grip_tree(ws.model)

    def clear(name, joints):
        """The PP7's knuckles round its grip (given in the PP7's frame), on
        this gun: moved with the hand, and off this gun's grip by the
        finger's shift (PISTOLS "knuckles": the golden gun's and Cougar's
        grips stand further forward than the PP7's)."""
        shift = Vector(L.cfg.get("knuckles", {}).get(name, (0.0, 0.0, 0.0)))
        return [(tuple(Vector(L.at(p)) + shift), r) for p, r in joints]

    # the middle finger: under its first joint, under the plate, joined
    pid = ws.piece("middle_finger", "the middle finger: its first joint and the plate across the grip closed "
                   "underneath, joined to each other and into the fingertip")
    shell = near_skin(ws, (N("0x0318:12"), N("0x0318:19"), N("0x0318:15"), N("0x0318:11"), N("0x0318:5")))
    first = M.underside(ws, pid, [[N("0x0318:13"), N("0x0318:12"), N("0x0318:19")], [N("0x0318:14"), N("0x0318:18"), N("0x0318:15")]],
                        None, shell.tex, (0, 1, 0, 1), PPK_LIGHT, arc=2, skin=shell, rom_shade=PPK_UNDER)
    plate = near_skin(ws, (N("0x0318:29"), N("0x0318:28"), N("0x0318:26"), N("0x0318:27")))
    top = M.underside(ws, pid, [[N("0x0318:29"), N("0x0318:28")], [N("0x0318:26"), N("0x0318:27")]],
                      None, plate.tex, (0, 1, 0, 1), PPK_LIGHT, arc=2, skin=plate, rom_shade=PPK_UNDER,
                      roundness=PPK_DEEP)
    # the joint's front opening to the plate's right end (they nearly touch)
    a = front_ring(ws, first[-1][1], N("0x0318:11"), N("0x0318:5"), N("0x0318:6"))
    b = front_ring(ws, top[0][1], N("0x0318:25"))
    M.grow(ws, pid, a, b, [], 8, [(0.0, shell), (0.5, plate)], (0, 1, 0), bone0)
    # the bend into the fingertip: the plate's end (its left corner, then
    # under it back to front) zipped to the fingertip's open end (round its
    # inner side, underneath and its outer side): they start and end at the
    # corners the N64 already joins across the top, so the two
    # cross-sections meet as a mitre
    ring = [N("0x0390:1"), N("0x0390:20"), N("0x0390:18"), N("0x0390:14"), N("0x0390:12"), N("0x0390:10"), N("0x0390:8"), N("0x0390:3")]
    M.zip_chains(ws, pid, [ws.vert(N("0x0318:24"))] + list(top[-1][1]), [ws.vert(x) for x in ring],
                 near_skin(ws, ring))

    # the ring and little fingers: each grown from its knuckle flap (closed
    # underneath, a rounded cover over it: the flap's flat face and pale
    # highlight were never meant to be seen from this side), then its middle
    # joint round the grip's front - the knuckle at the grip's front right
    # corner, the phalanx straight across the front strap - into the
    # fingertip at the left corner. One texture band runs the whole finger,
    # in the row of 0x703 the N64's own ring fingertip continues.
    for name, flap_names, joints, tip_loop in (
            ("ring_finger", (N("0x0318:3"), N("0x0318:4"), N("0x0318:1"), N("0x0318:2"), N("0x0318:0")),
             [((-17.5, -94, -14), 8.8), ((0, -94, -11.5), 8.8)], N("0x03a8:0")),
            ("little_finger", (N("0x0468:20"), N("0x0468:42"), N("0x0468:40"), N("0x0468:22"), N("0x0468:39")),
             [((-18, -112.5, -17), 8.4), ((0, -112.5, -14.5), 8.4)], N("0x03c0:0"))):
        pid = ws.piece(name, "the %s: its knuckle flap closed underneath and rounded over, then its "
                       "middle joint round the grip's front, grown from the knuckle into the fingertip's "
                       "open end" % name.replace("_", " "))
        back_a, front_a, front_mid, back_b, front_b = flap_names
        rails = [[back_a, front_a], [back_b, front_b]]
        tipring = loop_verts(ws, tip_loop)
        joints = clear(name, joints)
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
    pid = ws.piece("fist_inside", "the inside of the fist round the fingers, closed onto the grip")
    work = 200000 + pid
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
    fist = M.NearMap(ws, 0x0702, nodes=L.cfg["fist"])
    M.adopt(ws, pid, made, fist)
    pid = ws.piece("heel_pad", "below the grip's butt, between the heel and the little finger's knuckle: "
                   "the palm's pad, domed out to fill the hollow under the knuckle")
    for cycle in left_out(rim["chain"], rim["proj"], below):
        M.fill_palm(ws, pid, cycle, fist.tex, None, PPK_LIGHT, fair=1, dome=PPK_HEEL, skin=fist, rom_shade=1.0,
                    dome_out=PPK_HEEL_OUT)

    # the forearm's cut end: in the forearm's own skin, nearly flat, a little
    # darker than the skin round it (the old recipe's cap at shade 60 read
    # as a dark band round the end of the arm, as the taser's elbow did)
    pid = ws.piece("forearm_end", "the forearm's cut end, in the forearm's skin")
    end, end_nodes = L.cfg["forearm_end"]
    M.fill_palm(ws, pid, end, 0x704, None, PPK_LIGHT, fair=PPK_STUMP[0], dome=PPK_STUMP[1],
                skin=M.NearMap(ws, 0x704, nodes=end_nodes), rom_shade=PPK_STUMP[2], smooth_uv=True, dome_out=True)

    # the trigger finger (0x02b8, its own bone): its first joint is a shell
    # open underneath, at its base and inside the bend into the next joint.
    # Closed as before (one fill of that opening, no new points), now in the
    # finger's own skin - its texture carried on from the faces beside each
    # corner and the N64's own colours at them (255 on top, 190 underneath)
    # instead of the pale palm texture at full brightness.
    trig = L.cfg["trigger"]   # the PP7's 0x02b8, the same mesh: the same indices
    if trig is None:
        return []             # no trigger finger of its own (the knife hand's index wraps the handle)
    wt = M.Workspace(ws.model, [trig], trig)
    pid = wt.piece("trigger_finger", "the trigger finger's first joint closed underneath, at its base and "
                   "inside the bend, in its own skin")
    skin = M.NearMap(wt, 0x0703, nodes={trig})
    M.fill_palm(wt, pid, "0x%04x:0" % trig, skin.tex, None, PPK_LIGHT, fair=0, dome=0, skin=skin, rom_shade=1.0)
    return [wt]


# ---------------------------------------------------------------------------
# GtaserZ: the taser's hand, also the grenade hand (#41)
# ---------------------------------------------------------------------------
# Four nodes on one bone: the forearm 0x01a0, the hand 0x0200, the thumb's
# tip 0x0440 and the fingertips 0x0470. The hand grips the taser upright:
# the palm faces +x (onto the grip), the fingers wrap its front (+z) and end
# on its +x side, the thumb is on top. The N64 camera saw the palm side, so
# everything facing -x is missing: the back of the hand and the forearm's
# back (the forearm is a channel open on -x: a 0x704 side and 0x701 walls).
# Every ROM vertex is lit 255; the textures carry the light. The old recipe
# closed these in the pale 0x706 and flat shades (the forearm's back 215,
# the back of the hand 235), which read as a different skin; the pieces now
# carry the skin beside them on at 255. The finger openings are cracks, not
# holes: where two segments meet (a finger's middle joint and its tip, its
# first segment and its knuckle) their ends run side by side 1-6 units
# apart. Domed over (the old recipe) they stood out as pale flaps and dips -
# the index finger's on the end of it as seen from behind ("missing some
# volume"). They are zipped shut.

TASER_LIGHT = Vector((-0.3, 0.8, 0.5)).normalized()
# each crack: one segment's end and the other's, from the same end of it
TASER_CRACKS = {
    "finger_joints": [   # middle joint (0x0200) to fingertip (0x0470): index, middle, ring, little
        ([168, 170, 172, 174], ["0x0470:9", "0x0470:82", "0x0470:84", "0x0470:86", "0x0470:0"]),
        ([206, 210, 212, 214], ["0x0470:46", "0x0470:47", "0x0470:50", "0x0470:52", "0x0470:40"]),
        ([237, 241, 243, 245, 233], ["0x0470:21", "0x0470:109", "0x0470:113", "0x0470:25"]),
        ([139, 141, 143, 147, 131], ["0x0470:136", "0x0470:138", "0x0470:140", "0x0470:142"]),
    ],
    "knuckles": [        # first segment to knuckle, index to little
        ([64, 68, 70, 72, 161], [167, 169, 171, 173, 161]),
        ([90, 92, 94, 96, 187], [196, 207, 211, 213, 187]),
        ([108, 112, 116, 118, 121], [226, 238, 242, 230, 121]),
        ([30, 146, 142, 140, 138], [46, 44, 42, 40]),
    ],
}
TASER_FOREARM = 0.3    # the forearm's back: this deep past its open edges, in half-widths
TASER_STUMP = (1, 0.1, 0.8)   # the elbow's cut end: fairing, dome, and a little darker than the skin round it
TASER_BACK = 0.15      # the back of the hand: its dome
# The index finger's middle joint is a tube from ring 159..173 (odd) to ring
# 160..174 (even); at its end the N64 pulled 164 and 166 into the finger
# (2.9 units off its axis, the rest of that ring and the other fingers' 6-10)
# and 4-5 units back along it: a notch on the back of the last knuckle
# between 162 and 168, which stand up either side of it as two points (the
# headset: "the dent in the index finger").
TASER_INDEX_RINGS = ((159, 161, 163, 165, 167, 169, 171, 173), (160, 162, 164, 166, 168, 170, 172, 174))
TASER_INDEX_DENT = ((162, 168), (164, 166))   # the corners either side, the points pulled in


def seed_taser(ws, names):
    P.directed_loops.quiet = True

    def skin(textures, names, reach=12.0):
        return M.NearMap(ws, textures, near=[ws.vert(x).co for x in names], reach=reach)

    # the small openings first: two of them touch the big one round the
    # back of the hand, and filling that first could close one of their edges
    for name, textures, why in (
            ("finger_joints", (0x703, 0x704), "the crack round the outside of each finger's last bend, zipped "
             "shut in the skin either side of it"),
            ("knuckles", (0x702, 0x703), "the crack between each finger's first segment and its knuckle, zipped "
             "shut, and the openings beside them, in the skin round them")):
        pid = ws.piece(name, why)
        for a, b in TASER_CRACKS[name]:
            M.zip_chains(ws, pid, [ws.vert(x) for x in a], [ws.vert(x) for x in b], skin(textures, a + b))
    # a gap between the back of the hand's edge and the middle knuckles
    # (mapped smoothly from the knuckle skin round it: nearest-point lookups
    # landed in the texture's dark creases), and two small ones under the
    # little finger: closed, a little rounded
    for spec, textures, rim in ((83, 0x702, (83, 87, 100, 91)), (12, (0x702, 0x705), (12, 41)),
                                (31, (0x702, 0x705), (31, 107))):
        M.fill_palm(ws, pid, spec, None, None, TASER_LIGHT, fair=1, dome=0.15, skin=skin(textures, rim),
                    rom_shade=1.0, smooth_uv=True, dome_out=True)

    # the forearm's back: closed between the channel's open edges (one ROM
    # edge each, elbow to wrist) by an arc at each end, a little proud of
    # them, and textured with the 0x704 side straight across from it
    pid = ws.piece("forearm", "the forearm's back, closed between its open edges, wearing the skin of "
                   "the side across from it")
    rails = [[ws.vert("0x01a0:63"), ws.vert("0x01a0:13")], [ws.vert("0x01a0:69"), ws.vert("0x01a0:14")]]
    arm = [v for v in ws.bm.verts if ws.is_rom(v) and ws.R.primary(v)[0] == 0x01a0]
    centre = sum((v.co for v in arm), Vector()) / len(arm)
    mids = [(a.co + b.co) / 2 for a, b in zip(*rails)]
    axis = (mids[1] - mids[0]).normalized()
    arcs = []
    for (a, b), m in zip(zip(*rails), mids):
        chord = b.co - a.co
        c = chord.length / 2
        xh = chord / (2 * c)
        up = (centre - m) - xh * (centre - m).dot(xh) - axis * (centre - m).dot(axis)
        up.normalize()
        ring = [b]
        for q in (1, 2, 3):
            th = math.pi * q / 4
            ring.append(ws.new_vert(m + xh * (c * math.cos(th)) - up * (TASER_FOREARM * c * math.sin(th)), 0))
        arcs.append(ring + [a])
    far = M.NearMap(ws, 0x704, nodes={0x01a0})
    m = sum(mids, Vector()) / 2
    across = M.Across(far, up, (far.bvh.ray_cast(m, up)[0] - m).dot(up))
    first = len(ws.made)
    for q in range(4):
        vs = [arcs[0][q], arcs[0][q + 1], arcs[1][q + 1], arcs[1][q]]
        ws.quad(pid, vs, across, [None] * 4, [None if ws.is_rom(v) else 255.0 for v in vs])
    ws.settle(pid, faces=ws.made[first:])

    # the elbow's cut end, a rounded stump (seen from behind in a two-handed
    # hold), and the back of the hand: what is left, in the skin round each
    pid = ws.piece("elbow", "the elbow's cut end, a rounded stump in the forearm's skin")
    M.fill_palm(ws, pid, "0x01a0:59", 0x704, None, TASER_LIGHT, fair=TASER_STUMP[0], dome=TASER_STUMP[1],
                skin=M.NearMap(ws, 0x704, nodes={0x01a0}), rom_shade=TASER_STUMP[2], smooth_uv=True,
                dome_out=True)
    pid = ws.piece("hand_back", "the back of the hand, between the wrist, the knuckles and the hand's "
                   "edges, in the back of the hand's skin")
    M.fill_palm(ws, pid, "largest", 0x702, None, TASER_LIGHT, fair=1, dome=TASER_BACK,
                skin=M.NearMap(ws, 0x702, nodes={0x0200}), rom_shade=1.0, smooth_uv=True, dome_out=True)

    # the index finger's last knuckle: the faces round the two points pulled
    # in, drawn again over the notch with those points put back on the ring
    # (between the corners either side, as far out and along as they are);
    # the notch's own faces end up inside
    pid = ws.piece("index_knuckle", "the back of the index finger's last knuckle, rounded over the notch "
                   "the N64 left in it")
    pip, dip = ([ws.vert(x) for x in ring] for ring in TASER_INDEX_RINGS)
    c1 = sum((v.co for v in pip), Vector()) / len(pip)
    c2 = sum((v.co for v in dip), Vector()) / len(dip)
    axis = (c2 - c1).normalized()

    def along_and_out(v):
        d = v.co - c2
        return d.dot(axis), d - axis * d.dot(axis)
    (ta, pa), (tb, pb) = (along_and_out(ws.vert(x)) for x in TASER_INDEX_DENT[0])
    e1 = pa.normalized()
    e2 = axis.cross(e1)

    def angle(p):
        return math.atan2(p.dot(e2), p.dot(e1))
    # from one corner to the other the way round the dent is, not the way
    # round the rest of the ring
    turn = angle(pb)
    others = [angle(along_and_out(v)[1]) for v in dip
              if ws.R.name(v) not in {str(x) for x in TASER_INDEX_DENT[0] + TASER_INDEX_DENT[1]}]
    if any(0 < a * math.copysign(1, turn) < abs(turn) for a in others):
        turn -= math.copysign(2 * math.pi, turn)
    moved = {}
    for k, name in enumerate(TASER_INDEX_DENT[1], 1):
        f = k / (len(TASER_INDEX_DENT[1]) + 1)
        a = turn * f
        r = M.lerp(pa.length, pb.length, f)
        p = c2 + axis * M.lerp(ta, tb, f) + (e1 * math.cos(a) + e2 * math.sin(a)) * r
        v = ws.vert(name)
        moved[v] = ws.new_vert(p, ws.bone_of(v))
    first = len(ws.made)
    for f in [f for f in ws.bm.faces if f[ws.lay_piece] == 0 and any(v in moved for v in f.verts)]:
        ws.face(pid, [moved.get(v, v) for v in f.verts], ws.rom_tex(f),
                [(l[ws.uv].uv.x, 1.0 - l[ws.uv].uv.y) for l in f.loops],
                [ws.rom_colour(l.vert) if l.vert in moved else None for l in f.loops])
    ws.settle(pid, faces=ws.made[first:])


# ---------------------------------------------------------------------------
# GgrenadeZ: the grenade in the taser's hand (#41)
# ---------------------------------------------------------------------------
# Its flat bottom (a disk and the short band round it) is in the model's
# second list, which first-person models draw blended (alpha-tested in
# stereo, so it writes depth), in 0x5e2: an intensity texture, a ring of
# clock-face ticks on black. Intensity is its alpha, so all but the ticks
# drops out and the bottom reads as a hole into the hand (the N64 camera
# never looked up at it). The same triangles are drawn once more with the
# model's first, opaque list: the disk and band solid, the ticks on them,
# the N64's own coordinates and colours. The blended copy, drawn after at
# the same depth, fails the depth test and adds nothing.

def seed_grenade(ws, names):
    pid = ws.piece("bottom", "the flat bottom and its band, drawn solid (blended, its black dropped out "
                   "and it read as a hole)")
    rom = [f for f in ws.bm.faces if f[ws.lay_piece] == 0 and ws.rom_tex(f) == 0x5e2]
    # the bottom's own vertices: the band's top ring lies on the body's
    # bevel, whose vertices there are darker (48 against 68)
    own = {(t["node"], ws.model["verts"][i]["idx"], ws.model["verts"][i]["mtx"])
           for t in ws.model["tris"] if t["tex"] == 0x5e2 for i in t["v"]}
    twin = {}
    for f in rom:
        # the same corners again: new points standing on the ROM vertices
        # (a bmesh holds one face per vertex triple), committed as them
        vs = []
        for v in f.verts:
            if v not in twin:
                twin[v] = ws.new_vert(v.co.copy(), ws.bone_of(v))
                ws.alias[twin[v]] = next(r for r in ws.R.of(v) if r in own)
            vs.append(twin[v])
        uvs = [(l[ws.uv].uv.x, 1.0 - l[ws.uv].uv.y) for l in f.loops]
        shs = [255.0 * l[ws.shade][0] for l in f.loops]
        ws.face(pid, vs, 0x5e2, uvs, shs)


SEEDS = {
    "Csuit_lf_handZ": seed_csuit,
    "GwppkZ": seed_ppk,
    "GgoldengunZ": lambda ws, names: seed_pistol(ws, names, "GgoldengunZ"),
    "GrugerZ": lambda ws, names: seed_pistol(ws, names, "GrugerZ"),
    "Gtt33Z": lambda ws, names: seed_pistol(ws, names, "Gtt33Z"),
    "GknifeZ": lambda ws, names: seed_pistol(ws, names, "GknifeZ"),
    "GthrowknifeZ": lambda ws, names: seed_pistol(ws, names, "GthrowknifeZ"),
    "GtaserZ": seed_taser,
    "GgrenadeZ": seed_grenade,
}
