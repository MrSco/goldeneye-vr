"""
Load a model exported by tools/gevr_model_export.py into Blender, find its
holes and render views of them (issue #9).

Run headless:

    blender -b --factory-startup --python tools/blender/gevr_hands_import.py -- \
        build/handmodels/Csuit_lf_handZ.json build/handmodels/Csuit_lf_handZ \
        [--show 0x1c0,0x1f0,0x2f8,0x328] [--save]

Each DL node becomes one object named after its node offset. Vertices the N64
duplicated for texture seams are welded by position, so a boundary edge
(an edge with one face) is a real opening in the surface, not a UV seam.
Every mesh vertex keeps the ROM vertex it came from in the integer attributes
rom_node / rom_idx / rom_mtx, which is what a patch refers to.

Writes, into the output folder:
  holes.json        every boundary loop per part: vertex count, length, centre
  view_<name>.png   the parts in --show from six sides and two 3/4 views, the
                    boundary edges drawn as red tubes; --frame 0x2f8 frames the
                    camera on those parts only (a close-up among the rest)
  <model>.blend     with --save, for opening in Blender by hand

All output is ROM-derived and stays under build/.
"""

import json
import math
import os
import sys

import bmesh
import bpy
from mathutils import Vector

WELD = 0.5  # model units; the N64 duplicates seam vertices at identical positions


def args():
    a = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    src, out = a[0], a[1]
    show, frame, save = None, None, False
    i = 2
    while i < len(a):
        if a[i] == "--show":
            show = [int(x, 16) for x in a[i + 1].split(",")]
            i += 2
        elif a[i] == "--frame":
            frame = [int(x, 16) for x in a[i + 1].split(",")]
            i += 2
        elif a[i] == "--save":
            save = True
            i += 1
        else:
            raise SystemExit("unknown argument " + a[i])
    return src, out, show, frame, save


def texture_colour(tex):
    """A stable, readable colour per texture id, so parts and regions tell apart."""
    if tex is None:
        return (0.6, 0.6, 0.6, 1)
    h = ((tex * 2654435761) & 0xFFFFFFFF) / 0xFFFFFFFF
    s, v = 0.45, 0.85
    i = int(h * 6)
    f = h * 6 - i
    p, q, t = v * (1 - s), v * (1 - s * f), v * (1 - s * (1 - f))
    r, g, b = [(v, t, p), (q, v, p), (p, v, t), (p, q, v), (t, p, v), (v, p, q)][i % 6]
    return (r, g, b, 1)


def material_for(tex, cache):
    if tex in cache:
        return cache[tex]
    name = "tex_%x" % tex if tex is not None else "tex_none"
    m = bpy.data.materials.new(name)
    m.diffuse_color = texture_colour(tex)
    cache[tex] = m
    return m


def build_part(model, node, mats):
    verts = model["verts"]
    tris = [t for t in model["tris"] if t["node"] == node]
    if not tris:
        return None
    me = bpy.data.meshes.new("node_%04x" % node)
    ob = bpy.data.objects.new("node_%04x" % node, me)
    bpy.context.collection.objects.link(ob)
    bm = bmesh.new()
    lay_node = bm.verts.layers.int.new("rom_node")
    lay_idx = bm.verts.layers.int.new("rom_idx")
    lay_mtx = bm.verts.layers.int.new("rom_mtx")
    uv = bm.loops.layers.uv.new("UVMap")
    welded = {}
    slot = {}
    for t in tris:
        vs = []
        for vi in t["v"]:
            v = verts[vi]
            key = tuple(round(c / WELD) for c in v["pos"])
            if key not in welded:
                bv = bm.verts.new(v["pos"])
                bv[lay_node], bv[lay_idx], bv[lay_mtx] = v["node"], v["idx"], v["mtx"]
                welded[key] = bv
            vs.append((welded[key], v))
        if len({id(b) for b, _ in vs}) < 3:
            continue
        try:
            f = bm.faces.new([b for b, _ in vs])
        except ValueError:
            continue  # the same triangle twice (drawn in both lists)
        tex = t["tex"]
        if tex not in slot:
            slot[tex] = len(slot)
            me.materials.append(material_for(tex, mats))
        f.material_index = slot[tex]
        w = model["textures"].get("0x%x" % tex, {"w": 32, "h": 32}) if tex is not None else {"w": 32, "h": 32}
        for loop, (_, v) in zip(f.loops, vs):
            loop[uv].uv = (v["st"][0] / 32.0 / max(w["w"], 1), 1 - v["st"][1] / 32.0 / max(w["h"], 1))
    bm.normal_update()
    bm.to_mesh(me)
    bm.free()
    return ob


def boundary_loops(ob):
    """Boundary edges of a mesh, chained into loops (or open chains)."""
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bm.verts.ensure_lookup_table()
    edges = [e for e in bm.edges if len(e.link_faces) == 1]
    nonmanifold = sum(1 for e in bm.edges if len(e.link_faces) > 2)
    adj = {}
    for e in edges:
        for v in e.verts:
            adj.setdefault(v.index, []).append(e)
    seen = set()
    loops = []
    for e in edges:
        if e.index in seen:
            continue
        chain = [e]
        seen.add(e.index)
        stack = [e]
        while stack:
            cur = stack.pop()
            for v in cur.verts:
                for n in adj[v.index]:
                    if n.index not in seen:
                        seen.add(n.index)
                        chain.append(n)
                        stack.append(n)
        vids = sorted({v.index for c in chain for v in c.verts})
        pts = [bm.verts[i].co.copy() for i in vids]
        centre = sum(pts, Vector()) / len(pts)
        length = sum((c.verts[0].co - c.verts[1].co).length for c in chain)
        loops.append({"edges": len(chain), "verts": len(vids), "length": round(length, 1),
                      "centre": [round(c, 1) for c in centre],
                      "edge_list": [[c.verts[0].index, c.verts[1].index] for c in chain]})
    loops.sort(key=lambda l: -l["length"])
    info = {"faces": len(bm.faces), "verts": len(bm.verts), "boundary_edges": len(edges),
            "nonmanifold_edges": nonmanifold, "loops": loops}
    bm.free()
    return info


def red_tubes(ob, info, radius):
    """A curve along every boundary edge of ob, thick enough to see in a render."""
    cu = bpy.data.curves.new(ob.name + "_holes", "CURVE")
    cu.dimensions = "3D"
    cu.bevel_depth = radius
    cu.bevel_resolution = 1
    me = ob.data
    for lp in info["loops"]:
        for a, b in lp["edge_list"]:
            sp = cu.splines.new("POLY")
            sp.points.add(1)
            sp.points[0].co = (*me.vertices[a].co, 1)
            sp.points[1].co = (*me.vertices[b].co, 1)
    o = bpy.data.objects.new(ob.name + "_holes", cu)
    bpy.context.collection.objects.link(o)
    m = bpy.data.materials.new("hole_red")
    m.diffuse_color = (1, 0.05, 0.05, 1)
    cu.materials.append(m)
    return o


def render_views(objs, out, tag):
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.color_type = "MATERIAL"
    scene.display.shading.show_backface_culling = False
    scene.display.shading.show_cavity = False
    scene.render.resolution_x = 640
    scene.render.resolution_y = 480
    scene.render.film_transparent = False
    scene.world = scene.world or bpy.data.worlds.new("w")
    lo = Vector((1e9, 1e9, 1e9))
    hi = Vector((-1e9, -1e9, -1e9))
    for o in objs:
        if o.type != "MESH":
            continue
        for v in o.data.vertices:
            p = o.matrix_world @ v.co
            lo = Vector(map(min, lo, p))
            hi = Vector(map(max, hi, p))
    centre = (lo + hi) / 2
    size = (hi - lo).length
    cam_data = bpy.data.cameras.new("cam")
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = size * 1.05
    cam_data.clip_end = size * 10
    cam = bpy.data.objects.new("cam", cam_data)
    bpy.context.collection.objects.link(cam)
    scene.camera = cam
    views = {
        "+x": Vector((1, 0, 0)), "-x": Vector((-1, 0, 0)),
        "+y": Vector((0, 1, 0)), "-y": Vector((0, -1, 0)),
        "+z": Vector((0, 0, 1)), "-z": Vector((0, 0, -1)),
        "q1": Vector((1, 1, 1)).normalized(), "q2": Vector((-1, -1, -1)).normalized(),
    }
    written = []
    for name, d in views.items():
        cam.location = centre + d * size * 2
        up = Vector((0, 0, 1)) if abs(d.z) < 0.9 else Vector((0, 1, 0))
        cam.rotation_euler = (-d).to_track_quat("-Z", "Y" if up.y else "Z").to_euler()
        scene.render.filepath = os.path.join(out, "view_%s_%s.png" % (tag, name.replace("+", "p").replace("-", "m")))
        bpy.ops.render.render(write_still=True)
        written.append(scene.render.filepath)
    bpy.data.objects.remove(cam)
    return written


def main():
    src, out, show, frame, save = args()
    os.makedirs(out, exist_ok=True)
    with open(src, encoding="utf-8") as f:
        model = json.load(f)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    mats = {}
    objs = {}
    for p in model["parts"]:
        ob = build_part(model, p["node"], mats)
        if ob is not None:
            ob["switch"] = -1 if p["switch"] is None else p["switch"]
            objs[p["node"]] = ob
    report = {"model": model["model"], "parts": {}}
    tubes = {}
    for node, ob in objs.items():
        info = boundary_loops(ob)
        dims = ob.dimensions
        tubes[node] = red_tubes(ob, info, max(dims) * 0.004 + 1.0)
        report["parts"]["0x%04x" % node] = {
            "switch": ob["switch"], "faces": info["faces"], "verts": info["verts"],
            "boundary_edges": info["boundary_edges"], "nonmanifold_edges": info["nonmanifold_edges"],
            "loops": [{k: v for k, v in lp.items() if k != "edge_list"} for lp in info["loops"]],
        }
    with open(os.path.join(out, "holes.json"), "w", encoding="utf-8") as f:
        json.dump(report, f, indent=1)
    for node, part in report["parts"].items():
        print("part %s switch %s: %d faces, %d boundary edges in %d loops, %d non-manifold"
              % (node, part["switch"], part["faces"], part["boundary_edges"], len(part["loops"]),
                 part["nonmanifold_edges"]))
        for lp in part["loops"][:12]:
            print("    loop %3d edges  length %8.1f  centre %s" % (lp["edges"], lp["length"], lp["centre"]))
    if show:
        visible = set(show)
        for node, ob in objs.items():
            ob.hide_render = node not in visible
            tubes[node].hide_render = node not in visible
        tag = "_".join("%x" % n for n in show)
        if frame:
            tag += "_f" + "_".join("%x" % n for n in frame)
        for p in render_views([objs[n] for n in (frame or show) if n in objs], out, tag):
            print("wrote " + p)
    if save:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out, model["model"] + ".blend"))


main()
