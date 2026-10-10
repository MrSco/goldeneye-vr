"""
Meta Quest Touch controller geometry for tools/gevr_touch_model_gen.py.

  blender -b --factory-startup -P tools/blender/gevr_touch_export.py -- <art> <out.json> <kind> [triangles, 1600]

<art> is Meta's "Oculus controller art" zip (v1.8) extracted; <kind> is
quest1 (Oculus Touch for Quest and Rift S), quest2, plus (Touch Plus: Quest
3 and 3S) or pro (Touch Pro). Every model carries Meta's controller rig:
bones b_trigger_front, b_trigger_grip, b_thumbstick, b_button_x/y/a/b and
b_button_oculus/menu, skinned rigidly, so each vertex goes to the part of
its heaviest bone and each bone's head is that part's pivot.

Each controller is triangulated, split by part (a face goes with most of its
corners), its parts kept to PART_CAP triangles and the body decimated to the
rest of the budget, then written as corners - position, normal, UV - in
Blender's space (z up, metres), with the pivots.
"""

import json
import math
import os
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gevr_fbx_ascii as fa

PARTS = ["body", "trigger", "grip", "stick", "btn_lower", "btn_upper", "btn_menu"]
PART_CAP = {"trigger": 120, "grip": 90, "stick": 120, "btn_lower": 56, "btn_upper": 56, "btn_menu": 48}
SHARP = math.radians(60.0)   # an edge sharper than this keeps two normals

# The FBX per kind and side (the zip's layout), and for files holding more
# than one mesh, the meshes that make the controller.
MODELS = {
    "quest1": {"fbx": {"L": "Oculus Touch for Quest & Rift S/Export/controller_l.fbx",
                       "R": "Oculus Touch for Quest & Rift S/Export/controller_r.fbx"}},
    "quest2": {"fbx": {"L": "Meta Quest 2 Touch/quest2_controllers_div0.fbx",
                       "R": "Meta Quest 2 Touch/quest2_controllers_div0.fbx"},
               "mesh": {"L": ["left_quest2_mesh"], "R": ["right_quest2_mesh"]}},
    "plus": {"fbx": {"L": "Meta Quest Touch Plus/models/MetaQuestTouchPlus_Left.fbx",
                     "R": "Meta Quest Touch Plus/models/MetaQuestTouchPlus_Right.fbx"},
             "mesh": {"L": ["oculus_controller_l_MeshX"], "R": ["oculus_controller_r_MeshX"]}},
    "pro": {"fbx": {"L": "Meta Quest Touch Pro/models/questpro_controllers_left.fbx",
                    "R": "Meta Quest Touch Pro/models/questpro_controllers_right.fbx"},
            "mesh": {"L": ["left_oculus_controller_mesh", "left_nub"], "R": ["right_oculus_controller_mesh", "right_nub"]}},
}


def bone_part(name):
    n = name.lower()
    if "trigger_front" in n:
        return "trigger"
    if "trigger_grip" in n:
        return "grip"
    if "thumbstick" in n:
        return "stick"
    if n.endswith("button_x") or n.endswith("button_a"):
        return "btn_lower"
    if n.endswith("button_y") or n.endswith("button_b"):
        return "btn_upper"
    if "button_menu" in n or "button_oculus" in n:
        return "btn_menu"
    return "body"


def fbx_to_blender(p, unit):
    """FBX y up, in file units -> Blender z up, metres."""
    return Vector((p[0] * unit, -p[2] * unit, p[1] * unit))


def load_ascii(art, kind, side):
    m = MODELS[kind]
    root, objs, conns, settings = fa.load(os.path.join(art, m["fbx"][side]))
    unit = float(settings.get("UnitScaleFactor", [1])[0]) / 100.0   # a file unit in metres
    by_name = {}
    for oid, o in objs.items():
        if o.name == "Model":
            by_name.setdefault(o.props[1].split("::", 1)[1], []).append(oid)
    children, parent_of = {}, {}
    for c in conns:
        children.setdefault(c[2], []).append(c[1])
        parent_of.setdefault(c[1], []).append(c[2])
    # a mesh under a scaled RootNode (Touch Plus) takes its scale; the
    # clusters' TransformLinks are global, so include it already
    rootids = set(by_name.get("RootNode", []))
    rootscale = 1.0
    for oid in rootids:
        s = fa.props70(objs[oid]).get("Lcl Scaling")
        if s:
            rootscale = float(s[0])

    verts_all, faces, uvs, part_all, pivots = [], [], [], [], {}
    for meshname in m["mesh"][side]:
        mid = by_name[meshname][0]
        geom = next(objs[c] for c in children.get(mid, []) if c in objs and objs[c].name == "Geometry")
        v = geom.find("Vertices").array.reshape(-1, 3)
        if any(p in rootids for p in parent_of.get(mid, [])):
            v = v * rootscale
        pvi = geom.find("PolygonVertexIndex").array.astype(np.int64)
        polys, cur = [], []
        for k in pvi:
            if k < 0:
                cur.append(-k - 1)
                polys.append(cur)
                cur = []
            else:
                cur.append(k)
        lay = sorted(geom.findall("LayerElementUV"), key=lambda c: c.props[0])[0]
        uv = lay.find("UV").array.reshape(-1, 2)
        if lay.find("ReferenceInformationType").props[0] == "IndexToDirect":
            uv = uv[lay.find("UVIndex").array.astype(np.int64)]
        weight = np.zeros(len(v))
        part = np.zeros(len(v), dtype=np.int64)
        for skin in [objs[c] for c in children.get(geom.props[0], []) if c in objs and objs[c].name == "Deformer"]:
            for cl in [objs[c] for c in children.get(skin.props[0], []) if c in objs]:
                bone = next(objs[c] for c in children.get(cl.props[0], []) if c in objs and objs[c].name == "Model")
                pname = bone_part(bone.props[1].split("::", 1)[1])
                if pname != "body":
                    pivots[pname] = fbx_to_blender(cl.find("TransformLink").array.reshape(4, 4)[3, :3], unit)
                idx = cl.find("Indexes")
                if idx is None or idx.array is None:
                    continue
                ii = idx.array.astype(np.int64)
                ww = cl.find("Weights").array
                better = ww > weight[ii]
                weight[ii[better]] = ww[better]
                part[ii[better]] = PARTS.index(pname)
        base = sum(len(x) for x in verts_all)
        verts_all.append(v)
        part_all.append(part)
        corner = 0
        for p in polys:
            faces.append([base + k for k in p])
            uvs.append(uv[corner:corner + len(p)])
            corner += len(p)
    v = np.concatenate(verts_all)
    part = np.concatenate(part_all)

    me = bpy.data.meshes.new("%s_%s" % (kind, side))
    me.from_pydata([tuple(fbx_to_blender(p, unit)) for p in v], [], faces)
    me.validate(clean_customdata=False)
    me.uv_layers.new(name="uv").data.foreach_set("uv", np.concatenate(uvs).reshape(-1).astype(np.float32))
    ob = bpy.data.objects.new(me.name, me)
    bpy.context.scene.collection.objects.link(ob)
    return ob, part, pivots


def load_binary(art, kind, side):
    """The Quest 1 controllers are binary FBX: Blender's importer, the rest
    pose baked in."""
    bpy.ops.import_scene.fbx(filepath=os.path.join(art, MODELS[kind]["fbx"][side]))
    arm = next(o for o in bpy.context.scene.objects if o.type == "ARMATURE")
    ob = next(o for o in bpy.context.scene.objects if o.type == "MESH")
    pivots = {}
    for bn in arm.data.bones:
        pn = bone_part(bn.name)
        if pn != "body":
            pivots[pn] = arm.matrix_world @ bn.head_local
    ob.modifiers.clear()
    mw = ob.matrix_world.copy()
    ob.parent = None
    ob.data.transform(mw)
    ob.matrix_world = Matrix.Identity(4)
    names = {g.index: bone_part(g.name) for g in ob.vertex_groups}
    part = np.zeros(len(ob.data.vertices), dtype=np.int64)
    for v in ob.data.vertices:
        if v.groups:
            part[v.index] = PARTS.index(names[max(v.groups, key=lambda g: g.weight).group])
    bpy.data.objects.remove(arm)
    while len(ob.data.uv_layers) > 1:
        ob.data.uv_layers.remove(ob.data.uv_layers[1])
    return ob, part, pivots


def export_side(art, kind, side, budget):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    ob, vpart, pivots = (load_binary if kind == "quest1" else load_ascii)(art, kind, side)
    me = ob.data
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    bm.to_mesh(me)
    bm.free()
    fpart = []
    for p in me.polygons:
        ps = [int(vpart[i]) for i in p.vertices]
        fpart.append(max(set(ps), key=ps.count))
    fpart = np.array(fpart)
    counts = {PARTS[i]: int((fpart == i).sum()) for i in range(len(PARTS))}
    small = sum(min(c, PART_CAP[k]) for k, c in counts.items() if k != "body")
    out = {"pivots": {k: list(v) for k, v in pivots.items()}, "parts": {}}
    for pi, pname in enumerate(PARTS):
        if counts[pname] == 0:
            continue
        bm = bmesh.new()
        bm.from_mesh(me)
        bmesh.ops.delete(bm, geom=[f for f in bm.faces if fpart[f.index] != pi], context="FACES")
        pme = bpy.data.meshes.new(pname)
        bm.to_mesh(pme)
        bm.free()
        po = bpy.data.objects.new(pname, pme)
        bpy.context.scene.collection.objects.link(po)
        target = max(budget - small, 200) if pname == "body" else PART_CAP[pname]
        if len(pme.polygons) > target:
            mod = po.modifiers.new("decimate", "DECIMATE")
            mod.decimate_type = "COLLAPSE"
            mod.ratio = target / len(pme.polygons)
            mod.use_collapse_triangulate = True
            bpy.context.view_layer.objects.active = po
            bpy.ops.object.modifier_apply(modifier="decimate")
        pme = po.data
        for p in pme.polygons:
            p.use_smooth = True
        pme.set_sharp_from_angle(angle=SHARP)
        cn = np.zeros(len(pme.loops) * 3, dtype=np.float32)
        pme.corner_normals.foreach_get("vector", cn)
        cn = cn.reshape(-1, 3)
        uv = np.zeros(len(pme.loops) * 2, dtype=np.float32)
        pme.uv_layers[0].data.foreach_get("uv", uv)
        uv = uv.reshape(-1, 2)
        tris = []
        for p in pme.polygons:
            if p.loop_total != 3:
                continue
            tris.append([[round(c, 6) for c in pme.vertices[pme.loops[li].vertex_index].co]
                         + [round(float(c), 4) for c in cn[li]]
                         + [round(float(c), 5) for c in uv[li]]
                         for li in range(p.loop_start, p.loop_start + 3)])
        out["parts"][pname] = tris
        print("touch export: %s %s %-9s %4d -> %4d triangles" % (kind, side, pname, counts[pname], len(tris)))
    return out


def main():
    args = sys.argv[sys.argv.index("--") + 1:]
    art, path, kind = args[0], args[1], args[2]
    budget = int(args[3]) if len(args) > 3 else 1600   # a controller is about 55 pixels of 320 wide
    result = {"kind": kind, "sides": {side: export_side(art, kind, side, budget) for side in ("L", "R")}}
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w") as f:
        json.dump(result, f)


main()
