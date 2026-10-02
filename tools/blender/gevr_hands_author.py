"""
Model the missing parts of a first-person hand in Blender and keep them as
patch data (issue #9).

The N64 modelled Bond's hands as shells seen from one camera: half fingers,
no palms, an index finger that floats beside the pistol's grip. The recipe
ops in gevr_hands_patch.py close holes; whole fingers need modelling, and
this script keeps that modelling editable in Blender.

Run with Blender 5.2, headless:

    blender -b --factory-startup --python tools/blender/gevr_hands_author.py -- <command> <model> [options]

  seed    model the hand's missing pieces (gevr_hands_seeds.py, built with the
          primitives in gevr_hands_model.py) and commit them; names after the
          model reseed only those pieces, the rest stay as committed.
  open    build/handmodels/<model>/<model>_author.blend: the ROM hand (locked),
          the recipe's procedural patch for reference, and the committed
          pieces as editable objects in the "authored" collection.
  commit  the "authored" objects of that .blend -> tools/handpatch/
          <model>.authored.json. Run it from Blender's text editor too: the
          .blend carries a "gevr_commit" text block (Run Script).
  render  views in the game's look (texture times vertex shade, unlit) of the
          ROM hand and its patch (tools/handpatch/<model>.patch.json), as the
          game builds it from the ROM: --tint shows the patch magenta,
          --parts each node in a colour, --groups each patch group too,
          --no-patch the bare ROM, --views a,b picks views, --tag names files.

After seed or commit, run gevr_hands_patch.py for the model (its recipe's
"authored" op takes the pieces) and tools/gevr_handpatch_gen.py.

An authored piece is one object: a mesh in the model's rest-pose units, one
material per texture (tex_<hex>), UVs, a corner colour "shade" (the vertex
colour the game multiplies in), and per vertex rom_node/rom_idx/rom_mtx (the
ROM vertex it is welded to, -1 for a new point) and "mtx" (a new point's
bone). Object properties: "host" (the node it draws with), "why", "inherit"
(corners on ROM vertices keep the ROM's colour). Moving a welded vertex away
from its ROM vertex makes it a new point; a new point dropped onto a ROM
vertex (within the weld distance) is welded to it.

authored.json holds what patch.json holds: corners on ROM vertices (node,
index), new points as weights over four ROM vertices on their bone, our UVs
and shade. No coordinates, no ROM data. The .blend is ROM-derived and stays
in build/.

Output goes to build/handmodels/<model>/ (ROM-derived, gitignored).
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
import gevr_hp_common as C  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HANDPATCH = os.path.join(REPO, "tools", "handpatch")
EXPORTS = os.path.join(REPO, "build", "handmodels")

# The hand's nodes, what it holds (drawn for context, never patched), and
# views named for where the camera sits: (direction from the hand, up).
MODELS = {
    "Csuit_lf_handZ": {
        "hand": [0x01c0],
        "host": 0x01c0,
        "context": [0x01f0, 0x02f8, 0x0328],
        "views": {
            "back": ((0, 1, 0), (1, 0, 0)),
            "palm": ((0, -1, 0), (1, 0, 0)),
            "thumbside": ((0, 0, 1), (0, 1, 0)),
            "pinkyside": ((0, 0, -1), (0, 1, 0)),
            "tips": ((1, 0, 0), (0, 1, 0)),
            "palm_q1": ((0.45, -1, 0.5), (1, 0, 0)),
            "palm_q2": ((0.45, -1, -0.5), (1, 0, 0)),
            "palm_wrist": ((-0.6, -1, 0.2), (1, 0, 0)),
        },
    },
    "GwppkZ": {
        "hand": [0x0318, 0x0390, 0x03a8, 0x03c0, 0x0408, 0x0468, 0x04b0, 0x02b8],
        "assembly": [0x0318, 0x0390, 0x03a8, 0x03c0, 0x0408, 0x0468, 0x04b0],   # bone 0 (0x02b8 is on 4)
        "host": 0x0408,
        "context": [0x0270, 0x02e8, 0x0330, 0x0420, 0x04c8, 0x0510, 0x0558, 0x0570, 0x0588],
        "views": {
            "right": ((-1, 0, 0), (0, 1, 0)),
            "left": ((1, 0, 0), (0, 1, 0)),
            "muzzle": ((0, 0, 1), (0, 1, 0)),
            "muzzle_low": ((0, -0.8, 1), (0, 1, 0)),
            "below": ((0, -1, 0), (0, 0, 1)),
            "front_left": ((0.8, -0.3, 0.8), (0, 1, 0)),
            "front_right": ((-0.8, -0.3, 0.8), (0, 1, 0)),
            "behind": ((0.3, 0.4, -1), (0, 1, 0)),
        },
    },
    "GtaserZ": {
        "hand": [0x01a0, 0x0200, 0x0440, 0x0470],
        "host": 0x0200,
        "context": [0x0290, 0x02d8, 0x0320, 0x0338, 0x0350, 0x0368, 0x0380, 0x0398, 0x03c8, 0x03e0],
        "views": {
            "thumbside": ((1, 0, 0), (0, 1, 0)),
            "pinkyside": ((-1, 0, 0), (0, 1, 0)),
            "top": ((0, 1, 0), (0, 0, 1)),
            "under": ((0, -1, 0), (0, 0, 1)),
            "tips": ((0, 0, 1), (0, 1, 0)),
            "elbow": ((0.2, -0.3, -1), (0, 1, 0)),
            "palm_q1": ((-0.7, -0.6, 0.5), (0, 1, 0)),
            "palm_q2": ((0.7, -0.6, 0.5), (0, 1, 0)),
        },
    },
    # the grenade the taser's hand holds (#41): its own model, one node
    "GgrenadeZ": {
        "hand": [0x00e4],
        "host": 0x00e4,
        "context": [],
        "views": {
            "below": ((0, -1, 0.1), (0, 0, 1)),
            "below_q1": ((0.6, -0.8, 0.3), (0, 1, 0)),
            "below_q2": ((-0.6, -0.8, -0.3), (0, 1, 0)),
            "side": ((1, 0, 0), (0, 1, 0)),
        },
    },
}
SAME_HAND = {"GwppksilZ": "GwppkZ", "GgoldwppkZ": "GwppkZ", "GsilverwppkZ": "GwppkZ"}


def args():
    a = sys.argv[sys.argv.index("--") + 1:]
    o = {"cmd": a[0], "model": a[1], "tint": False, "patch": True, "views": None, "tag": None,
         "names": [], "context": True, "frame": None, "parts": False, "size": 720}
    i = 2
    while i < len(a):
        k = a[i]
        if k == "--backfaces":
            C.BACKFACES = (0.1, 0.9, 0.2, 1)
            i += 1
        elif k in ("--tint", "--parts", "--rims", "--groups"):
            o[k[2:]] = True
            i += 1
        elif k == "--no-patch":
            o["patch"] = False
            i += 1
        elif k == "--no-context":
            o["context"] = False
            i += 1
        elif k == "--here":
            o["here"] = True
            i += 1
        elif k == "--views":
            o["views"] = a[i + 1].split(",")
            i += 2
        elif k == "--view":
            name, d = a[i + 1].split("=")
            vals = [float(x) for x in d.split(",")]
            o.setdefault("extra", {})[name] = (vals[:3], vals[3:6] if len(vals) >= 6 else None)
            i += 2
        elif k == "--frame":
            o["frame"] = [int(x, 16) for x in a[i + 1].split(",")]
            i += 2
        elif k == "--box":
            b = [float(x) for x in a[i + 1].split(",")]
            o["box"] = (Vector(b[:3]), Vector(b[3:]))
            i += 2
        elif k == "--patch-file":
            o["patch_file"] = a[i + 1]
            i += 2
        elif k == "--size":
            o["size"] = int(a[i + 1])
            i += 2
        elif k == "--tag":
            o["tag"] = a[i + 1]
            i += 2
        else:
            o["names"].append(k)
            i += 1
    return o


def cfg_of(name):
    return MODELS[SAME_HAND.get(name, name)]


def texpng(tex):
    return os.path.join(EXPORTS, "tex", "0x%03x.png" % tex)


def game_materials():
    """Every tex_<hex> material the importer made, in the game's look."""
    for m in list(bpy.data.materials):
        if m.name.startswith("tex_") and m.name != "tex_none":
            C.game_material(m.name, texpng(int(m.name[4:].split(".")[0], 16)))


def collection(name, parent=None):
    col = bpy.data.collections.get(name)
    if col is None:
        col = bpy.data.collections.new(name)
        (parent or bpy.context.scene.collection).children.link(col)
    return col


def link(ob, col):
    for c in list(ob.users_collection):
        c.objects.unlink(ob)
    col.objects.link(ob)


def rom_objects(model, cfg, with_context=True):
    """The ROM hand as one welded mesh (rom_node/rom_idx/rom_mtx per vertex,
    UVs, the shade colour), and what it holds, each in its collection."""
    mats = {}
    rom = collection("ROM")
    ob = H.build_part(model, cfg["hand"], mats, name="rom_hand")
    link(ob, rom)
    objs = [ob]
    if with_context and cfg["context"]:
        ctx = collection("held")
        c = H.build_part(model, cfg["context"], mats, name="rom_held")
        if c is not None:
            link(c, ctx)
            objs.append(c)
    game_materials()
    return objs


def patch_objects(model, patch, name, col, tint=False):
    """A patch (patch.json or authored.json form) as the game builds it: each
    corner at its ROM vertex or weighted sum, our UVs and shade."""
    objs = []
    for part in patch["parts"]:
        for gi, g in enumerate(part["groups"]):
            tex = int(g["tex"], 16)
            info = model["textures"].get("0x%x" % tex, {"w": 32, "h": 32})
            me = bpy.data.meshes.new("%s_%s_%d" % (name, part["host"], gi))
            ob = bpy.data.objects.new(me.name, me)
            col.objects.link(ob)
            bm = bmesh.new()
            uv = bm.loops.layers.uv.new("UVMap")
            sh = bm.loops.layers.color.new("shade")
            vs = [bm.verts.new(C.corner_pos(model, c)) for c in g["verts"]]
            for t in g["tris"]:
                try:
                    f = bm.faces.new([vs[i] for i in t])
                except ValueError:
                    continue
                for lp, i in zip(f.loops, t):
                    c = g["verts"][i]
                    lp[uv].uv = (c["s"] / 32.0 / info["w"], 1 - c["t"] / 32.0 / info["h"])
                    col_ = c.get("c", g.get("shade", [255, 255, 255, 255]))
                    lp[sh] = [x / 255.0 for x in col_]
            bm.to_mesh(me)
            bm.free()
            if tint:
                me.materials.append(C.tint_material())
            else:
                me.materials.append(C.game_material("tex_%x" % tex, texpng(tex)))
            ob["host"] = part["host"]
            ob["tex"] = g["tex"]
            ob["why"] = g.get("why", "")
            objs.append(ob)
    return objs


def node_box(model, nodes):
    pts = [Vector(model["verts"][i]["pos"]) for t in model["tris"] if t["node"] in nodes for i in t["v"]]
    lo = Vector([min(p[k] for p in pts) for k in range(3)])
    hi = Vector([max(p[k] for p in pts) for k in range(3)])
    return lo, hi


def part_objects(model, nodes):
    """One object per node, each in its own colour (which node is which finger)."""
    objs = []
    rom = collection("ROM")
    palette = [(0.9, 0.25, 0.2, 1), (0.25, 0.75, 0.3, 1), (0.25, 0.45, 0.95, 1), (0.95, 0.8, 0.2, 1),
               (0.75, 0.3, 0.9, 1), (0.2, 0.85, 0.9, 1), (0.95, 0.55, 0.15, 1), (0.6, 0.6, 0.6, 1),
               (0.55, 0.35, 0.2, 1), (0.95, 0.95, 0.95, 1)]
    if len(nodes) == 8:
        palette[7] = (0.98, 0.98, 0.98, 1)
    for k, n in enumerate(nodes):
        ob = H.build_part(model, n, {}, name="node_%04x" % n)
        if ob is None:
            continue
        link(ob, rom)
        m = bpy.data.materials.new("part_%04x" % n)
        m.diffuse_color = palette[k % len(palette)]
        ob.data.materials.clear()
        ob.data.materials.append(m)
        objs.append(ob)
        print("part 0x%04x colour %s" % (n, tuple(round(c, 2) for c in m.diffuse_color[:3])))
    return objs


def cmd_render(o):
    name = o["model"]
    cfg = cfg_of(name)
    model = C.load_model(os.path.join(EXPORTS, name + ".json"))
    bpy.ops.wm.read_factory_settings(use_empty=True)
    if o.get("rims"):
        # the open edges of the bare hand, as red tubes (welded as one mesh)
        whole = H.build_part(model, cfg.get("assembly", cfg["hand"]), {}, name="rims_of")
        info = H.boundary_loops(whole)
        lo, hi = node_box(model, cfg["hand"])
        H.red_tubes(whole, info, (hi - lo).length * 0.0025)
        whole.hide_render = True
    if o.get("groups"):
        o["parts"] = True
    if o["parts"]:
        objs = part_objects(model, cfg["hand"])
        if o["context"] and cfg["context"]:
            c = H.build_part(model, cfg["context"], {}, name="rom_held")
            link(c, collection("held"))
            c.data.materials.clear()
            hm = bpy.data.materials.new("held_studio")
            hm.diffuse_color = (0.12, 0.12, 0.13, 1)
            c.data.materials.append(hm)
    else:
        objs = rom_objects(model, cfg, o["context"])
    if o["patch"]:
        with open(o.get("patch_file") or os.path.join(HANDPATCH, name + ".patch.json"), encoding="utf-8") as f:
            patch = json.load(f)
        pobjs = patch_objects(model, patch, "patch", collection("patch"), o["tint"] or o["parts"])
        if o.get("groups"):
            # each patch group in its own colour (which piece is which)
            shades = [(1.0, 0.2, 0.85, 1), (0.6, 0.9, 0.0, 1), (1.0, 1.0, 0.5, 1), (0.3, 1.0, 0.6, 1),
                      (0.55, 0.1, 0.1, 1), (0.2, 0.2, 0.7, 1), (1.0, 0.6, 0.6, 1), (0.5, 0.8, 1.0, 1),
                      (0.5, 0.5, 0.0, 1), (0.0, 0.5, 0.5, 1)]
            for k, p in enumerate(pobjs):
                pm = bpy.data.materials.new("group_%d" % k)
                pm.diffuse_color = shades[k % len(shades)]
                p.data.materials.clear()
                p.data.materials.append(pm)
                print("group %d %s tex %s colour %s: %s" % (k, p["host"], p["tex"],
                                                         tuple(round(c, 2) for c in pm.diffuse_color[:3]),
                                                         p["why"][:60]))
        elif o["parts"]:
            pm = bpy.data.materials.new("patch_studio")
            pm.diffuse_color = (1.0, 0.2, 0.85, 1)
            for p in pobjs:
                p.data.materials.clear()
                p.data.materials.append(pm)
    out = os.path.join(EXPORTS, name)
    os.makedirs(out, exist_ok=True)
    views = {k: v for k, v in cfg["views"].items() if not o["views"] or k in o["views"]}
    views.update(o.get("extra", {}))
    tag = o["tag"] or ("parts" if o["parts"] else "rom" if not o["patch"] else "tint" if o["tint"] else "patch")
    box = o.get("box") or (node_box(model, o["frame"]) if o["frame"] else node_box(model, cfg["hand"]))
    paths = C.render_views(objs, out, "view_" + tag, views, box=box, studio=o["parts"],
                           width=o["size"], height=o["size"])
    print("wrote " + C.sheet(paths, os.path.join(out, "sheet_%s.png" % tag), 4))


# ---------------------------------------------------------------------------
# authored.json <-> objects
# ---------------------------------------------------------------------------

WELD = {"Csuit_lf_handZ": 4.0}   # model units; the watch arm is ~10x a gun hand
WELD_DEFAULT = 0.5


def authored_path(name):
    return os.path.join(HANDPATCH, name + ".authored.json")


def blend_path(name):
    return os.path.join(EXPORTS, name, name + "_author.blend")


def rom_table(model, nodes):
    """Every vertex the hand's lists draw: (node, idx, mtx) -> rest position."""
    out = {}
    for v in model["verts"]:
        if v["node"] in nodes:
            out[(v["node"], v["idx"], v["mtx"])] = Vector(v["pos"])
    return out


def pick_anchors(p, cands):
    """Four ROM vertices near p, spread round it, for p's weights: the
    nearest, then the furthest from it, from that line, from that plane,
    among the K nearest (K grows until the weights stay small)."""
    order = sorted(cands, key=lambda kv: ((kv[1] - p).length, kv[0]))
    best = None
    for K in (16, 32, 64, 128, len(order)):
        cs = order[:K]
        if len(cs) < 4:
            break
        a0 = cs[0]
        a1 = max(cs, key=lambda kv: ((kv[1] - a0[1]).length, kv[0]))
        ax = (a1[1] - a0[1]).normalized()
        a2 = max(cs, key=lambda kv: (((kv[1] - a0[1]) - ax * (kv[1] - a0[1]).dot(ax)).length, kv[0]))
        nrm = (a1[1] - a0[1]).cross(a2[1] - a0[1])
        if nrm.length < 1e-9:
            continue
        nrm.normalize()
        a3 = max(cs, key=lambda kv: (abs((kv[1] - a0[1]).dot(nrm)), kv[0]))
        quad = [a0, a1, a2, a3]
        if len({q[0] for q in quad}) < 4:
            continue
        try:
            w = C.affine_weights([q[1] for q in quad], p)
        except Exception:
            continue
        big = max(abs(x) for x in w)
        if best is None or big < best[0]:
            best = (big, quad, w)
        if big < 4.0:
            break
    if best is None:
        raise SystemExit("no four ROM vertices round %s to anchor it" % (tuple(round(c, 1) for c in p),))
    return best[1], best[2]


def objects_to_authored(model, cfg, objs):
    """Authored objects -> authored.json parts (see the module notes)."""
    name = model["model"]
    weld = WELD.get(SAME_HAND.get(name, name), WELD_DEFAULT)
    table = rom_table(model, cfg["hand"])
    by_bone = {}
    for key, pos in table.items():
        by_bone.setdefault(key[2], []).append((key, pos))
    from mathutils import kdtree
    trees = {}
    for bone, lst in by_bone.items():
        kd = kdtree.KDTree(len(lst))
        for i, (_, pos) in enumerate(lst):
            kd.insert(pos, i)
        kd.balance()
        trees[bone] = (kd, lst)
    parts = {}
    worst = 0.0
    for ob in sorted(objs, key=lambda o: o.name):
        if ob.type != "MESH":
            continue
        dg = bpy.context.evaluated_depsgraph_get()
        me = ob.evaluated_get(dg).to_mesh()
        bm = bmesh.new()
        bm.from_mesh(me)
        bm.transform(ob.matrix_world)
        bmesh.ops.triangulate(bm, faces=[f for f in bm.faces if len(f.verts) > 3])
        bm.verts.index_update()
        L = bm.verts.layers.int
        ln, li, lm = L.get("rom_node"), L.get("rom_idx"), L.get("rom_mtx")
        lb = L.get("mtx")
        uvl = bm.loops.layers.uv.get("UVMap") or (bm.loops.layers.uv[0] if len(bm.loops.layers.uv) else None)
        shl = bm.loops.layers.color.get("shade")
        default_bone = int(ob.get("mtx", 0))
        inherit = bool(ob.get("inherit", True))
        host = ob.get("host", "0x%04x" % cfg["host"])
        # what each vertex is: a ROM vertex (welded) or a new point (weights)
        kind = {}
        for v in bm.verts:
            ref = None
            if ln is not None and v[ln] >= 0:
                key = (v[ln], v[li], v[lm])
                if key in table and (table[key] - v.co).length <= weld:
                    ref = key
                else:
                    print("   %s: vertex %d left its ROM vertex %s; it is a new point now" % (ob.name, v.index, key))
            bone = v[lb] if lb is not None else default_bone
            if ref is None and bone in trees:
                kd, lst = trees[bone]
                co, i, dist = kd.find(v.co)
                if co is not None and dist <= weld:
                    ref = lst[i][0]
            if ref is not None:
                kind[v.index] = {"node": "0x%04x" % ref[0], "ref": int(ref[1]), "mtx": int(ref[2])}
            else:
                if bone not in by_bone:
                    raise SystemExit("%s: vertex %d is on bone %d, which the hand does not use"
                                     % (ob.name, v.index, bone))
                quad, w = pick_anchors(v.co, by_bone[bone])
                entry = C.mix_entry([q[0] for q in quad], w)
                back = C.corner_pos(model, entry)
                worst = max(worst, (back - v.co).length)
                if (back - v.co).length > 0.05 * (weld / WELD_DEFAULT):
                    raise SystemExit("%s: vertex %d comes back %.3f units away from its weights"
                                     % (ob.name, v.index, (back - v.co).length))
                kind[v.index] = entry
        groups = {}
        for f in bm.faces:
            mat = ob.data.materials[f.material_index] if f.material_index < len(ob.data.materials) else None
            if mat is None or not mat.name.startswith("tex_"):
                raise SystemExit("%s: a face without a tex_<hex> material" % ob.name)
            tex = int(mat.name[4:].split(".")[0], 16)
            info = model["textures"]["0x%x" % tex]
            g = groups.setdefault(tex, {"verts": [], "index": {}, "tris": []})
            ids = []
            for lp in f.loops:
                u, vv = lp[uvl].uv if uvl is not None else (0.5, 0.5)
                s_ = int(round(u * info["w"] * 32))
                t_ = int(round((1.0 - vv) * info["h"] * 32))
                k = kind[lp.vert.index]
                if "ref" in k and inherit:
                    col = None
                else:
                    c = lp[shl] if shl is not None else (1, 1, 1, 1)
                    col = tuple(int(round(max(0.0, min(1.0, x)) * 255)) for x in c)
                key = (lp.vert.index, s_, t_, col)
                if key not in g["index"]:
                    g["index"][key] = len(g["verts"])
                    e = {"s": s_, "t": t_}
                    e.update(k)
                    if col is None:
                        e["inherit"] = True
                    else:
                        e["c"] = list(col)
                    g["verts"].append(e)
                ids.append(g["index"][key])
            g["tris"].append(ids)
        bm.free()
        ob.evaluated_get(dg).to_mesh_clear()
        for tex, g in sorted(groups.items()):
            parts.setdefault(host, []).append({
                "name": ob.name, "why": ob.get("why", ""), "tex": "0x%03x" % tex,
                "verts": g["verts"], "tris": g["tris"]})
        print("   %s: %d triangles, %d corners, %d new points"
              % (ob.name, sum(len(g["tris"]) for g in groups.values()), sum(len(g["verts"]) for g in groups.values()),
                 sum(1 for k in kind.values() if "mix" in k)))
    print("   weights rebuild every new point within %.4f units" % worst)
    return [{"host": h, "groups": gs} for h, gs in sorted(parts.items())]


def write_authored(name, parts, keep_others=None):
    data = {"model": name,
            "note": "Modelled in Blender (tools/blender/gevr_hands_author.py): corners on ROM vertices "
                    "(node, index), new points as weights over ROM vertices, our UVs and shade. No ROM data.",
            "parts": parts}
    if keep_others:
        mine = {(p["host"], g["name"]) for p in parts for g in p["groups"]}
        for p in keep_others["parts"]:
            for g in p["groups"]:
                if (p["host"], g["name"]) not in mine:
                    tgt = next((q for q in data["parts"] if q["host"] == p["host"]), None)
                    if tgt is None:
                        tgt = {"host": p["host"], "groups": []}
                        data["parts"].append(tgt)
                    tgt["groups"].append(g)
        for p in data["parts"]:
            p["groups"].sort(key=lambda g: (g["name"], g["tex"]))
        data["parts"].sort(key=lambda p: p["host"])
    C.write_json(authored_path(name), data)
    print("wrote " + authored_path(name))


def authored_objects(model, data, col):
    """authored.json -> editable objects, one per piece (its groups merged),
    vertices welded where corners share a ROM vertex or the same weights."""
    pieces = {}
    for part in data["parts"]:
        for g in part["groups"]:
            pieces.setdefault(g["name"], []).append((part["host"], g))
    objs = []
    for pname, groups in sorted(pieces.items()):
        me = bpy.data.meshes.new(pname)
        ob = bpy.data.objects.new(pname, me)
        col.objects.link(ob)
        bm = bmesh.new()
        on = bm.verts.layers.int.new("rom_node")
        oi = bm.verts.layers.int.new("rom_idx")
        om = bm.verts.layers.int.new("rom_mtx")
        ob_ = bm.verts.layers.int.new("mtx")
        uvl = bm.loops.layers.uv.new("UVMap")
        shl = bm.loops.layers.color.new("shade")
        vmap = {}
        inherit = True
        for host, g in groups:
            tex = int(g["tex"], 16)
            info = model["textures"]["0x%x" % tex]
            me.materials.append(C.game_material("tex_%x" % tex, texpng(tex)))
            slot = len(me.materials) - 1
            vs = []
            for c in g["verts"]:
                key = ("r", c["node"], c["ref"], c["mtx"]) if "mix" not in c else \
                    ("m", tuple(tuple(x) for x in c["mix"]), c["mtx"])
                if key not in vmap:
                    v = bm.verts.new(C.corner_pos(model, c))
                    if "mix" in c:
                        v[on], v[oi], v[om] = -1, -1, -1
                    else:
                        v[on], v[oi], v[om] = int(c["node"], 16), c["ref"], c["mtx"]
                    v[ob_] = c["mtx"]
                    vmap[key] = v
                vs.append(vmap[key])
                if "mix" not in c and not c.get("inherit"):
                    inherit = False
            for t in g["tris"]:
                try:
                    f = bm.faces.new([vs[i] for i in t])
                except ValueError:
                    continue
                f.material_index = slot
                f.smooth = True
                for lp, i in zip(f.loops, t):
                    c = g["verts"][i]
                    lp[uvl].uv = (c["s"] / 32.0 / info["w"], 1 - c["t"] / 32.0 / info["h"])
                    col_ = c.get("c", [255, 255, 255, 255])
                    lp[shl] = [x / 255.0 for x in col_]
        bm.to_mesh(me)
        bm.free()
        ob["host"] = groups[0][0]
        ob["why"] = groups[0][1].get("why", "")
        ob["inherit"] = inherit
        objs.append(ob)
    return objs


COMMIT_TEXT = """# Commit the pieces in the "authored" collection to %(json)s.
# Run Script (Alt+P). Then run gevr_hands_patch.py and gevr_handpatch_gen.py.
import runpy, sys
sys.argv = ["blender", "--", "commit", "%(model)s", "--here"]
runpy.run_path(r"%(script)s", run_name="__main__")
"""


def cmd_open(o):
    name = o["model"]
    cfg = cfg_of(name)
    model = C.load_model(os.path.join(EXPORTS, name + ".json"))
    bpy.ops.wm.read_factory_settings(use_empty=True)
    rom = rom_objects(model, cfg, True)
    for ob in rom:
        ob.hide_select = True
    pp = os.path.join(HANDPATCH, name + ".patch.json")
    if os.path.exists(pp):
        with open(pp, encoding="utf-8") as f:
            patch = json.load(f)
        for part in patch["parts"]:
            part["groups"] = [g for g in part["groups"] if g.get("op") != "authored"]
        rc = collection("recipe")
        for ob in patch_objects(model, patch, "recipe", rc):
            ob.hide_select = True
    col = collection("authored")
    if os.path.exists(authored_path(name)):
        with open(authored_path(name), encoding="utf-8") as f:
            authored_objects(model, json.load(f), col)
    txt = bpy.data.texts.new("gevr_commit")
    txt.write(COMMIT_TEXT % {"json": os.path.relpath(authored_path(name), REPO).replace("\\", "/"),
                             "model": name, "script": os.path.abspath(__file__)})
    os.makedirs(os.path.dirname(blend_path(name)), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=blend_path(name))
    print("wrote " + blend_path(name))


def cmd_commit(o):
    name = o["model"]
    cfg = cfg_of(name)
    model = C.load_model(os.path.join(EXPORTS, name + ".json"))
    if not o.get("here"):
        bpy.ops.wm.open_mainfile(filepath=blend_path(name))
    col = bpy.data.collections.get("authored")
    if col is None:
        raise SystemExit("no \"authored\" collection in this file")
    write_authored(name, objects_to_authored(model, cfg, list(col.objects)))
    if o.get("here") and bpy.data.filepath:
        bpy.ops.wm.save_mainfile()


def cmd_seed(o):
    import gevr_hands_model as M
    import gevr_hands_seeds as S
    name = o["model"]
    cfg = cfg_of(name)
    model = C.load_model(os.path.join(EXPORTS, name + ".json"))
    bpy.ops.wm.read_factory_settings(use_empty=True)
    ws = M.Workspace(model, cfg.get("assembly", cfg["hand"]), cfg["host"])
    # every piece is built (a palm is filled round the fingers made before
    # it); only the named ones are committed. A seed may model a part on
    # another bone in a workspace of its own and hand that back too.
    more = S.SEEDS[SAME_HAND.get(name, name)](ws, []) or []
    col = collection("authored")
    objs = []
    for w in [ws] + list(more):
        objs += M.extract(w, col, lambda tex: C.game_material("tex_%x" % tex, texpng(tex)), o["names"])
    keep = None
    if o["names"] and os.path.exists(authored_path(name)):
        with open(authored_path(name), encoding="utf-8") as f:
            keep = json.load(f)
    write_authored(name, objects_to_authored(model, cfg, objs), keep)


def main():
    o = args()
    {"render": cmd_render, "open": cmd_open, "commit": cmd_commit, "seed": cmd_seed}[o["cmd"]](o)


if __name__ == "__main__":
    main()
