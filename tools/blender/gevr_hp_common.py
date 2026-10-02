"""
Shared helpers for the hand patch tools (issue #9), used by
gevr_hands_patch.py (the recipe ops) and gevr_hands_author.py (geometry
modelled in Blender).

  - reading a model export (tools/gevr_model_export.py, ROM-derived, build/);
  - where a patch corner lands: a ROM vertex, or affine weights over four;
  - the node fingerprint the game checks before patching;
  - the in-game look for renders: texture times vertex shade, unlit, as the
    gun render mode combines them (TEXEL0 * SHADE, model.c
    modelApplyRenderModeType3).
"""

import json
import math
import os

import bpy
from mathutils import Vector


def load_model(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def _depth(o):
    if isinstance(o, (dict, list)):
        return 1 + max((_depth(v) for v in (o.values() if isinstance(o, dict) else o)), default=0)
    return 0


def json_text(o, indent=0):
    """Patch data as JSON with one item per line: a corner (its weights
    included), a triangle, a node's fingerprint - whatever nests no deeper
    than a corner and fits in 200 characters - and everything bigger opened
    up round it. A diff then shows the corners that changed, not a column of
    single numbers."""
    flat = json.dumps(o)
    if not isinstance(o, (dict, list)) or (_depth(o) <= 3 and indent + len(flat) <= 200):
        return flat
    pad = " " * (indent + 1)
    if isinstance(o, dict):
        body = ",\n".join("%s%s: %s" % (pad, json.dumps(k), json_text(v, indent + 1)) for k, v in o.items())
        return "{\n%s\n%s}" % (body, " " * indent)
    return "[\n%s\n%s]" % (",\n".join(pad + json_text(v, indent + 1) for v in o), " " * indent)


def write_json(path, data):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(json_text(data) + "\n")


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


def translation(model, mtx):
    t = model["matrices"].get(str(mtx), [0.0, 0.0, 0.0])
    return Vector(t)


def rom_local(model, node, idx):
    """A ROM vertex's own coordinates (its bone's space), from the node's block."""
    blocks = model.setdefault("_blocks", {p["node"]: p["block_xyz"] for p in model["parts"]})
    return Vector(blocks[node][idx])


def corner_pos(model, c):
    """Where a patch corner lands in the rest pose (export "pos" space): its ROM
    vertex, or the weighted sum the game computes in the bone's space, moved
    by the bone's rest translation."""
    if "mix" in c:
        acc = Vector()
        for node, idx, w in c["mix"]:
            acc += w * rom_local(model, int(node, 16), int(idx))
        return acc + translation(model, c["mtx"])
    return rom_local(model, int(c["node"], 16), int(c["ref"])) + translation(model, c["mtx"])


def affine_weights(anchors, co):
    """co as an affine combination of four points (weights sum to 1)."""
    import numpy as np
    M = np.array([[a.x for a in anchors], [a.y for a in anchors], [a.z for a in anchors], [1, 1, 1, 1]])
    return [float(x) for x in np.linalg.solve(M, np.array([co.x, co.y, co.z, 1.0]))]


def mix_entry(refs, weights):
    """The patch form of a new point: [node, index, weight] over its anchors."""
    return {"mix": [["0x%04x" % r[0], int(r[1]), round(float(w), 6)] for r, w in zip(refs, weights)],
            "mtx": int(refs[0][2])}


# ---------------------------------------------------------------------------
# The in-game look
# ---------------------------------------------------------------------------

BACKFACES = None   # a colour to paint faces seen from behind with (an inspection aid), or None


def game_material(name, png):
    """Unlit: the texture (bilinear, wrapping) times the corner colour "shade".
    With BACKFACES set, faces seen from behind show that colour instead."""
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    em = nt.nodes.new("ShaderNodeEmission")
    attr = nt.nodes.new("ShaderNodeVertexColor")
    attr.layer_name = "shade"
    mul = nt.nodes.new("ShaderNodeMix")
    mul.data_type = "RGBA"
    mul.blend_type = "MULTIPLY"
    mul.inputs["Factor"].default_value = 1.0
    if png and os.path.exists(png):
        img = nt.nodes.new("ShaderNodeTexImage")
        img.image = bpy.data.images.load(png, check_existing=True)
        img.interpolation = "Linear"
        nt.links.new(img.outputs["Color"], mul.inputs["A"])
    else:
        mul.inputs["A"].default_value = (0.6, 0.6, 0.6, 1)
    nt.links.new(attr.outputs["Color"], mul.inputs["B"])
    colour = mul.outputs["Result"]
    if BACKFACES is not None:
        geo = nt.nodes.new("ShaderNodeNewGeometry")
        pick = nt.nodes.new("ShaderNodeMix")
        pick.data_type = "RGBA"
        pick.inputs["B"].default_value = BACKFACES
        nt.links.new(geo.outputs["Backfacing"], pick.inputs["Factor"])
        nt.links.new(colour, pick.inputs["A"])
        colour = pick.outputs["Result"]
    nt.links.new(colour, em.inputs["Color"])
    nt.links.new(em.outputs["Emission"], out.inputs["Surface"])
    m.use_backface_culling = False
    return m


def flat_material(name, rgba):
    """Unlit flat colour times the shade: tells parts apart in a render."""
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    em = nt.nodes.new("ShaderNodeEmission")
    em.inputs["Color"].default_value = rgba
    nt.links.new(em.outputs["Emission"], out.inputs["Surface"])
    return m


def tint_material():
    m = bpy.data.materials.get("patch_tint") or bpy.data.materials.new("patch_tint")
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    em = nt.nodes.new("ShaderNodeEmission")
    attr = nt.nodes.new("ShaderNodeVertexColor")
    attr.layer_name = "shade"
    mul = nt.nodes.new("ShaderNodeMix")
    mul.data_type = "RGBA"
    mul.blend_type = "MULTIPLY"
    mul.inputs["Factor"].default_value = 1.0
    mul.inputs["A"].default_value = (1.0, 0.15, 0.85, 1)
    nt.links.new(attr.outputs["Color"], mul.inputs["B"])
    nt.links.new(mul.outputs["Result"], em.inputs["Color"])
    nt.links.new(em.outputs["Emission"], out.inputs["Surface"])
    return m


def setup_game_render(width=720, height=720):
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x = width
    scene.render.resolution_y = height
    scene.render.film_transparent = False
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    world = scene.world or bpy.data.worlds.new("w")
    scene.world = world
    world.use_nodes = True
    bg = world.node_tree.nodes.get("Background")
    if bg is None:
        bg = world.node_tree.nodes.new("ShaderNodeBackground")
        world.node_tree.links.new(bg.outputs[0], world.node_tree.nodes.new("ShaderNodeOutputWorld").inputs[0])
    bg.inputs["Color"].default_value = (0.16, 0.17, 0.19, 1)
    bg.inputs["Strength"].default_value = 1.0
    return scene


def bounds(objs):
    lo = Vector((1e9, 1e9, 1e9))
    hi = Vector((-1e9, -1e9, -1e9))
    for o in objs:
        if o.type != "MESH":
            continue
        for v in o.data.vertices:
            p = o.matrix_world @ v.co
            lo = Vector(map(min, lo, p))
            hi = Vector(map(max, hi, p))
    return lo, hi


def look_from(cam, target, direction, distance, up=None):
    d = Vector(direction).normalized()
    cam.location = target + d * distance
    up = Vector(up) if up is not None else (Vector((0, 1, 0)) if abs(d.y) < 0.95 else Vector((0, 0, 1)))
    # camera looks down -Z with +Y up
    fwd = -d
    right = fwd.cross(up).normalized()
    upv = right.cross(fwd).normalized()
    from mathutils import Matrix
    m = Matrix((right, upv, -fwd)).transposed()
    cam.rotation_euler = m.to_euler()


def render_views(frame_objs, out, tag, views, fov=38.0, margin=1.08, width=720, height=720, box=None,
                 studio=False):
    """Perspective renders of whatever is visible, framed on frame_objs (or
    box, a (lo, hi) pair), one PNG per view: views is {name: (direction the
    camera sits in, up or None)}. studio: Workbench, lit, material colours
    (shape over looks)."""
    scene = setup_game_render(width, height)
    if studio:
        scene.render.engine = "BLENDER_WORKBENCH"
        scene.display.shading.light = "STUDIO"
        scene.display.shading.color_type = "MATERIAL"
        scene.display.shading.show_backface_culling = False
        scene.display.shading.show_cavity = False
    lo, hi = box if box is not None else bounds(frame_objs)
    centre = (lo + hi) / 2
    radius = (hi - lo).length / 2
    cam_data = bpy.data.cameras.new("cam")
    cam_data.type = "PERSP"
    cam_data.angle = math.radians(fov)
    dist = radius * margin / math.sin(math.radians(fov) / 2)
    cam_data.clip_start = dist * 0.01
    cam_data.clip_end = dist * 10
    cam = bpy.data.objects.new("cam", cam_data)
    bpy.context.scene.collection.objects.link(cam)
    scene.camera = cam
    written = []
    for name, (d, up) in views.items():
        look_from(cam, centre, d, dist, up)
        scene.render.filepath = os.path.join(out, "%s_%s.png" % (tag, name))
        bpy.ops.render.render(write_still=True)
        written.append(scene.render.filepath)
    bpy.data.objects.remove(cam)
    return written


def sheet(paths, out_path, cols, label_rows=None):
    """Tile rendered PNGs into one image (row-major, top row first)."""
    import numpy as np
    tiles = []
    for p in paths:
        img = bpy.data.images.load(p)
        w, h = img.size
        tiles.append(np.array(img.pixels[:], dtype=np.float32).reshape(h, w, 4))
        bpy.data.images.remove(img)
    h, w = tiles[0].shape[:2]
    rows = []
    for r in range(0, len(tiles), cols):
        row = tiles[r:r + cols]
        while len(row) < cols:
            row.append(np.zeros((h, w, 4), dtype=np.float32))
        rows.append(np.concatenate(row, axis=1))
    full = np.concatenate(rows[::-1], axis=0)   # pixels run bottom-up
    H, W = full.shape[:2]
    img = bpy.data.images.new("sheet", W, H, alpha=True)
    img.pixels = full.ravel()
    img.filepath_raw = out_path
    img.file_format = "PNG"
    img.save()
    bpy.data.images.remove(img)
    return out_path
