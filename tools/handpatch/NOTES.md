# #9 hand shells: working notes (branch feature/9-hand-shells)

These notes stay on the branch. HANDOFF.md gets one numbered section when the
branch merges; a number taken earlier raced the gun-fit (112) and two-handed
hold (113) entries on main.

Worktree: `../gevr-hands`. Keep the main checkout (`goldeneye-vr`) off this
branch: other agents merge there and in `../gevr-wt`. The ROM and the
ROM-derived exports stay outside git: pass `../goldeneye-vr/007 - GoldenEye.z64`
to the tools; exports go to `build/handmodels/` (gitignored).

## Phase 0: export, Blender, hole survey (2026-09-26)
The user asked to patch the hand and arm models' missing walls with Blender.
Plan: ship only our own new triangles (existing ROM vertices referenced by node
and index, new points as weights over ROM vertices), merged in at model load
behind a fingerprint check; no ROM bytes in the repo. No reference port has
done this (GEVR PC parks its ghost fingers, PD VR hides fist bones).
- tools/gevr_model_export.py: walks a model file (gevr_model_probe's walk)
  and runs every DL through a small F3D interpreter (G_MTX seg 3, G_VTX seg 5,
  G_TRI1 /10, Rare's G_TRI4, the 0xC0 texture marker, G_TEXTURE scale).
  JSON to build/handmodels/. Each vertex keeps node / idx / mtx; each part
  keeps its whole vertex block (block_xyz) for the fingerprint. Counts come
  from MODELFILEHEADER (NUMTEXTURES is the 8th argument, not the 5th).
- tools/blender/gevr_hands_import.py (Blender 5.2, headless): one object per
  DL node, seam vertices welded, boundary loops to holes.json, Workbench
  renders from 8 sides with boundary edges as red tubes; --frame for a
  close-up, --save for a .blend. Run with absolute paths: a relative render
  path lands nowhere. Textured when build/handmodels/tex holds decoded PNGs.
- Csuit_lf_handZ (the left watch arm) is rigid: every DL is under matrix 0
  (the hand group at the end of a 5-group chain). Parts: hand 0x1c0 (338
  tris), sleeves on switches 4-9 (one per outfit; 0x1f0/0x2e0 share a mesh,
  0x220/0x250/0x280 share another, 0x2b0 is its own), watch 0x2f8 (370),
  face 0x328 (switch 3), watch hands 0x358/0x388/0x3b8.
- Holes found:
  - Hand: palm and the underside of the pinky and ring fingers are one
    46-vertex loop; the index and middle fingers are closed except their
    tips (three 9-vertex loops). The 8-edge wrist ring is shared exactly
    with the sleeve's cuff, so it closes in assembly.
  - Sleeve: open elbow end (octagon); the jacket cuff edge has no thickness,
    an open ring between it and the shirt cuff (jacket edge loop at x 5884,
    shirt cuff's inner end at x 5568, inside the jacket).
  - Watch: the band covers the top of the wrist only (no back half), and the
    case has no back. The other ~30 loops on the watch are layered trim and
    dial marks (overlays, not holes).
- Hands in gun models (survey of all G*Z): rifles have none. 12 models carry
  a hand (textures 0x701-0x706), in 4-10 parts each. The PPK family (wppk,
  wppksil, gold, silver) share one hand, knife/throwknife share one; with the
  fist, golden gun, ruger, tt33, taser, watchlaser and the watch arm that is 9
  unique hands to patch. Gun hands use more than one matrix (e.g. a trigger
  finger on its own bone): fairing must not mix matrices there.

## Phase 1: patching the watch arm (2026-09-26)
- tools/gevr_tex_decode.py ports texLoad's lookup (images.def sizes to
  running offsets from GEVR_SEG_IMAGES) and texInflateZlib. All the hand,
  sleeve and cuff textures decode; 0x706 has plain skin for the palm. The
  watch's 0x5dd/0x5e0/0x5e1/0x5e3 are I8/IA8 huffman-blur / RLE-lookup
  (texInflateNonZlib): not ported, drawn flat grey in previews.
- tools/blender/gevr_hands_patch.py applies tools/handpatch/<model>.recipe.json
  and writes <model>.patch.json (ours only: refs, weights, our UVs, a
  fingerprint = FNV-1a over the node's vertex block x,y,z as little-endian
  s16). Ops:
  - fill: Liepa's DP (max dihedral, then area). Good for caps. On the curled
    hand it webs the fingers to the palm like a mitten.
  - earclip: convex ears only (judged against the faces around each corner),
    shortest diagonal first; zips a finger channel from its tip, never
    across the valley between fingers. Alone it forces ears in the palm's
    saddle.
  - zipfill: earclip up to maxdiag (250 on the watch arm: the finger
    channels and the palm spots at the finger bases), then Liepa-fill the
    rest, found by a ROM vertex on it (the heel, 424).
  - bridge: zip two loops (the jacket cuff to the shirt cuff).
  - "fair": N refines the op's faces N times (inner edges only, so the ROM
    faces beyond the rim get no T-junctions), then one bi-Laplacian solve
    (numpy, uniform weights) for all new points of the part with every ROM
    vertex fixed. The solve is linear, so each new point is stored as
    weights over ROM vertices and the game rebuilds it from the player's ROM.
  - ribbon: new geometry between two edges round an axis along x (the watch
    band's back half, strap end to strap end under the wrist, tightening
    to bottom_radius halfway). Its new points are stored as affine weights
    over four ROM vertices ("anchors"), its texture coordinates continue the
    strap's (the strap texture wraps). Its winding follows the strap.
  - Checks printed per part: open edges left, winding (every shared edge
    walked both ways), edges on 3+ faces. directed_loops says when a
    boundary path never closes (faces wound both ways along it: the watch
    case's rim) - such a rim is not offered as a loop.
- --compare my,q2 --tag x writes compare_x.png (before row over after row).
  The previews do not apply the shade (vertex colour): linings and caps look
  brighter there than in the game.

## Phase 1 result: the watch arm (2026-09-26)
tools/handpatch/Csuit_lf_handZ.patch.json, from the recipe beside it:
- Hand 0x1c0 (151 triangles, 43 faired points): zipfill closes the pinky and
  ring undersides from their tips and the palm between the heel and the
  curled fingers (fair 1 bows it into the curl; fair 2 made 463 triangles
  and a membrane); three fingertip fills. Palm UVs on 0x706's plain skin.
  Left open on purpose: the wrist ring (the sleeve's cuff closes it) and a
  lone triangle (rom 398) that is its own piece.
- Sleeves, one entry per outfit node (each has its own fingerprint, even the
  ones that share a loop layout):
  - 0x1f0 / 0x2e0: elbow cap (shade 60) + jacket edge to shirt cuff (90).
  - 0x220 / 0x250 / 0x280 (tuxedo: jacket, white cuff band 0x645, bare
    wrist): elbow cap + jacket edge to cuff band inner end [56, 1] + cuff
    band edge to wrist tube inner end [0, 11] (shade 110).
  - 0x2b0 (rolled camo sleeve over the forearm): elbow cap + sleeve edge to
    forearm [18, 0], on the green half of 0x66c.
  - Every sleeve is closed but its wrist ring (shared with the hand).
- Watch 0x2f8: ribbon from strap end [23, 22] to [91, 92] under the wrist,
  axis (y 3758, z 31), bottom radius 288 (the cuffs are ~270-275 from the
  axis at the watch on every outfit, interpolated between their end rings;
  no sleeve has vertices under the watch). 16 triangles on 0x5e3.
- Not done, on purpose: the case back. The case is sunk into the cuff (face
  at y 4000, cuff top ~4030 at the watch), so its open bottom is not in
  view; its rim is also wound both ways, so it is not a clean loop.
- Most drawn at once in game: hand 151 + one sleeve <= 50 + band 16.
  Group by texture and shade at runtime (the hand's four groups are one
  texture) to keep draw calls down.
- Fairing weights run to 60 terms per point (threshold 1e-3); raise the
  threshold if load time matters.
- Next hands (Phase 3): gun hands use several matrices; fair per matrix or
  keep new points on one bone, and check the rest pose before trusting a
  render.
- Phase 2 (runtime: gevr_handpatch.c at the model-load hook after
  sub_GAME_7F0762E0, objecthandler_2.c) is ON HOLD by the user's call until
  the #35 / viewmodel work in that code has landed; branch it from the new
  main then.

## Phase 3: the gun hands (2026-09-26)
Every hand-carrying model has a recipe and a patch in tools/handpatch:
GwppkZ (+ GwppksilZ, GgoldwppkZ, GsilverwppkZ by "same_as"), GgoldengunZ,
GrugerZ, Gtt33Z, GknifeZ, GthrowknifeZ, GfistZ, GtaserZ, GwatchlaserZ.
Rifles have no hands. Each run printed no new mis-wound or 3-face edges
(every one left was in the model before).

How a gun model's hand is built:
- Switches 8-13 (and 35 when there are 36 switches) are the hand, all set
  together by sub_GAME_7F05E978; switch 6 is the trigger finger on its own
  bone (matrix 4 or 5); 29-34 are outfit cuffs (only the watch laser model
  has them). GfistZ has no switches: its four finger parts sit under BSP
  nodes (draw order only) and all six parts always draw.
- The hand is split into up to ten parts whose seams meet at the same points,
  so holes are judged on the parts welded together (an "assembly"; welding
  keys on position AND matrix). Per-part outlines are mostly seams.
- The pistol hand (PPK family, golden gun, Cougar, DD44, both knives) is one
  design: an open grip cavity (the gun's handle fills it; its rim is wound
  both ways, a "mixed open chain" - left alone), a 14-vertex socket where
  the trigger finger plugs in (open knuckle on the knives), two fingertip
  holes, and the forearm's cut end. The trigger finger's base (17 verts)
  matches the socket (both perimeter 177).
- T-junction slits along some part seams (single edges wound both ways) are
  original and have no area; left alone.

What the patches do:
- Pistols and knives: cap the forearm end (0x704, shade 60), close the
  socket (0x704), the two fingertips (0x706); the trigger finger's base on
  its own bone, drawn with the trigger finger. 47 triangles (32 knives).
- GfistZ (actually an open hand in a suit sleeve): jacket end cap, jacket
  edge -> shirt sleeve far end, shirt cuff -> forearm (like the tuxedo).
- GtaserZ (also the grenade hand): finger holes first (two touch the big
  loop), then earclip the open forearm underside (maxdiag 80, flat cut end),
  then fill + fair the palm (loop "largest"). 152 triangles.
- GwatchlaserZ: both palms (fair), arm ends, 12 finger holes, the pressing
  finger (spans bones 0 and 3), and all six sleeve troughs closed
  underneath (earclip). The watch's trim rings left alone.

Tool changes in this phase:
- Assemblies ({"host", "nodes", "ops"}), loops named "node:index"; the patch
  corners carry their node, weights are [node, index, weight]; each patch
  part lists a fingerprint for every node it references.
- directed_loops prunes half-edges that can be on no cycle and, on a dead
  end, keeps the cycles found and releases the rest of the path (the old
  walk threw away a whole good loop on the taser). mixed_loops lists the
  rims wound both ways; a fill takes one only with "mixed": true.
- Ear clipping makes each ear a face at once and refuses an ear that repeats
  a face or runs its diagonal along an edge with two faces (holes that touch).
- fill_loop takes edge normals from the mesh, turned to the loop direction.
- "same_as": borrow another model's recipe, nodes matched by vertex-block
  fingerprint; refuses when a node has no single match (the throwing knife's
  two main parts differ from the knife's, so it has its own recipe).
- The exporter follows a switch node's Controls pointer.

For Phase 2 (the runtime), from this format:
- A patch part draws with its host node, right after the host's own DL
  (same segment 5, same matrix state).
- Corners may come from other nodes of the same model file (the hand's
  parts) and from more than one matrix (the watch laser's pressing finger;
  pistol trigger fingers stay on their own bone). Load each corner under its
  own G_MTX, 16-vertex cache batches like the original DLs do.
- New points: sum of weight * ROM vertex position (same node:index refs),
  computed once at load. Group by texture + shade to keep draw calls down.
- Skip a whole patch part if any referenced node's fingerprint differs.

## Phase 2: the runtime (2026-09-26, built, awaiting the headset test)
Started once #35 and the stereo viewmodel wood had merged (main v0.1.16,
merged into the branch as 4aff811).
- tools/gevr_handpatch_gen.py: tools/handpatch/*.patch.json ->
  port/src/gevr_handpatch_data.c (generated, committed; 13 models, 34 parts,
  1384 triangles, 3418 weights). Re-run it after any patch changes.
- port/include/gevr_handpatch.h, port/src/gevr_handpatch.c:
  - gevrModelConvert() (gevr_model.c) asks gevrHandPatchWants(name) and, for
    a patched model, notes every node's cartridge -> host offset and each
    texture marker's w0 (0xC0 commands) before its block table goes.
  - load_object_fill_header() (objecthandler_2.c) calls gevrHandPatchApply()
    after sub_GAME_7F0762E0: every model load passes there, the private
    loaders too (fist, taser/grenade hand, watch laser, watch arm).
  - Per part: every referenced node must have its numvtx and FNV; corners
    from the node's Vertices (host pointers after promotion), new points as
    weight sums; per group the model's own marker (w0 noted, w1 = texture
    number) then G_MTX (segment 3 | 1 tag) + G_VTX per matrix run, 16-vertex
    batches, G_TRI1; texLoadFromGdl() expands the markers with the model's
    texture pool (it writes the G_TEXTURE too: writeTexFlag starts TRUE).
    The host's Primary becomes a wrapper [G_DL own list, G_DL patch, ENDDL].
  - Allocations are per model buffer (24 slots), freed when a model loads
    into that buffer again; two copies of a gun are two buffers.
  - files/gevr_handpatch.txt, read at each model load: 0 off, 2 magenta
    (shade), else on. A weapon switch or level load re-reads it.
  - Log: "handpatch <model>: N of M parts, T triangles" (LOG_NOTE), or a
    warning naming the node that did not match.
- A desk check (prox_close + am start) hit Guardian and the controllers
  dialog; the test is the user's.

Tool quirks
- Bash heredocs with Python inside break here: write edit scripts to the
  scratchpad and run them.
- Render: `blender -b --factory-startup --python tools/blender/gevr_hands_patch.py --
  <abs json> <abs outdir> --render 1c0 --tint` then the before/after views
  land in the outdir; bmesh index tables go stale after subdivide
  (directed_loops refreshes them).
