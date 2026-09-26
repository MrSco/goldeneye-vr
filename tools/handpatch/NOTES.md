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

## Phase 1: patching the watch arm (in progress, 2026-09-26)
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
  - Checks printed per part: open edges left, winding (every shared edge
    walked both ways), edges on 3+ faces.
- State at the stop: hand = zipfill (130 triangles, 43 new points, fair 1)
  plus three fingertip fills; sleeve 0x1f0 = elbow cap (shade 60) and cuff
  bridge (shade 90). Winding consistent, no 3-face edges; the hand's 11 open
  edges are the wrist ring (8) and a lone triangle (398) that is its own
  piece. Renders of that last run not yet reviewed.
- Still to do: review the zipfill renders (the fairing still made a membrane
  between the curled fingertips and the heel on the fair 2 run), the other
  sleeve meshes (0x220 family has 6 loops, 0x2b0 has 4), the watch band back
  (a new strip under the wrist between the strap ends: loops rom 2 and 71 on
  0x2f8) and the case back, then before/after renders for the user.
- Phase 2 (runtime: gevr_handpatch.c at the model-load hook after
  sub_GAME_7F0762E0, objecthandler_2.c) is ON HOLD by the user's call until
  the #35 / viewmodel work in that code has landed; branch it from the new
  main then.

Tool quirks
- Bash heredocs with Python inside break here: write edit scripts to the
  scratchpad and run them.
- Render: `blender -b --factory-startup --python tools/blender/gevr_hands_patch.py --
  <abs json> <abs outdir> --render 1c0 --tint` then the before/after views
  land in the outdir; bmesh index tables go stale after subdivide
  (directed_loops refreshes them).
