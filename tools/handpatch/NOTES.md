# #9 hand shells: working notes

Notes for the hand patch work (branches feature/9-hand-shells, then
claude/hand-models-missing-fingers-15c48f); STATUS.md gets a line when a
branch merges. Keep the main checkout (`goldeneye-vr`) off a feature branch:
other agents merge there. The ROM and the ROM-derived exports stay outside
git: pass `../goldeneye-vr/007 - GoldenEye.z64` to the tools; exports go to
`build/handmodels/` (gitignored).

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
  The PPK family is modelled in Blender now (last section).
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

## The rebuild (2026-09-26, after the first headset test)
The first patches were spanning fills: the smallest surface across each
hole. On flat cut ends that is right; on hands it is wrong, because the
N64 modelled fingers and forearms as the top half of a tube and the palm as
nothing at all. A spanning fill leaves flat finger undersides, fingertip
flaps and a palm that dips. The user saw craters and missing fingers.
What was tried and dropped:
- Lofted arcs across the ear-clip's rungs: the rungs come from the hole's
  outline, which on a curled low-poly hand is a row of knuckle notches, so
  the channels fragment and the arcs flip.
- Mirroring each finger's own top faces across its rim, rung by rung: the
  same fragmentation, plus an unreliable pick of "the top faces", so spikes.
- Reading cross-sections from the mesh's own cross edges: these tops are not
  strips, so only two or three edges per finger exist, all at the tip.
What works:
- "tube": the recipe names each half-finger's two rails (rim vertices, base
  to tip) and its tip; the tool pairs rail vertices by their share of the
  rail's length and builds a half-round arc under each pair, as deep as the
  modelled top is high and never shallower than 0.7 of the half-width,
  joined into the tube's other half and fanned to the tip. A cut end (the
  taser forearm's elbow, "tip": null) leaves a ring that a flat fill caps.
  Deterministic; it follows the curl because the rails do.
- Palms and caps: fill, one refinement, bi-Laplacian fairing for the
  in-plane spread, then "dome" pushes the new points out along the fill's
  normal by x times the loop's radius in the middle (0.2 palms, 0.3 caps):
  a convex cushion, never a crater. The ray-cast "inflate" is gone.
- Rails so far: watch hand ring finger [72 49 47 46] / [74 39 51 40 42]
  tip 43, pinky [25 9 8 7] / [27 0 10 1 3] tip 4 (indices in the hand
  node); taser forearm [0x01a0:63, :13] / [:69, :14], open both ends.
  Read off the loop coordinates: the fingers run along +x, z across the
  hand, the tip is the vertex with the largest x in its protrusion.
- The other hands only needed rounded caps (fingers already full tubes);
  the watch laser's two palms are cushions.
Round 4 (2026-09-27, after the headset test of ccd015b, main v0.1.17
merged as 66ec1d3):
- The user traced a hollow in the grip/taser/grenade hand's forearm, seen
  from the elbow end in a two-handed hold (GtaserZ). Cause: tube() took
  "up" from the rail vertices' face normals, and this forearm's top is more
  than half the tube (widest 18 units above the rails, 45 high), so those
  faces lean down: "up" pointed at the missing side and the rebuilt half
  went up inside the arch; the elbow cap spanned a sliver.
- tube() now finds the top's cross-section as the shortest way over the
  modelled faces from rail to rail that keeps off the rails (no longer than
  6 half-widths, within 2.4 of the pair; a finger whose top is a strip
  straight between its rails has none), turns "up" round when that lies
  below it, and keeps a pathless pair on the last pair's side. The depth
  is the top's height less twice the height of its widest point: 8.2 and
  8.7 for this forearm ("round" 0.25). The watch hand's tubes keep their
  direction; two mid-finger sections come out a little rounder (the path
  finds the top a little higher than the rails' own faces did).
- The forearm underside and elbow cap: 0x705's plain lower middle (the
  forearm's own texture; 0x704 is a knuckle texture and banded), cap shade
  170 instead of 60 (dark read as a hole from behind).
- --view name=x,y,z adds a render direction (elbow-end views for this).
Left alone, worth a look in the headset: on the PPK the index finger is not
seen at all in the user's screenshots - the socket patch shows where it
should enter the hand. The finger is its own part on bone 4 (switch table
entry 6, a position node the game rotates for trigger pulls), so it is
either drawn inside the gun's grip since the gun fit moved the gun, or not
placed in stereo. Not a shell problem; test with gevr_handpatch.txt = 0.

## Modelled in Blender: the PP7 hand (2026-10-02, merged)
Caps and tubes left the PP7's fingers as shells with holes; the user asked
for the missing fingers, blended into the skin round them, one hand at a
time. The PPK family (GwppkZ; GwppksilZ, GgoldwppkZ, GsilverwppkZ by
same_as) is done and headset-checked ("much improved"). The watch arm and
the taser/grenade hand are next, the same way.
- tools/blender/gevr_hands_author.py: `seed <model> [pieces]` builds the
  pieces (gevr_hands_seeds.py, with the primitives in gevr_hands_model.py)
  and writes tools/handpatch/<model>.authored.json; `open` makes
  build/handmodels/<m>/<m>_author.blend (ROM locked, pieces editable) and
  `commit` writes its pieces back; `render` shows the game's look (--groups
  colours each patch group, --backfaces paints back faces green: holes and
  pieces turned inside out). gevr_hp_common.py holds what the tools share.
- authored.json is patch data, no coordinates: corners on ROM vertices
  (welded within 0.5 units, 4.0 on the watch arm) or new points as weights
  over four ROM vertices on their own bone (rebuilt within 0.0001), our UVs,
  a shade per corner. The recipe's "authored" op takes a host's pieces into
  the patch; same_as maps their nodes like everything else.
- Runtime: every corner carries its own colour, and HP_CORNER_INHERIT draws
  a corner on a ROM vertex in that vertex's colour, so a piece shades into
  the skin it meets. Groups are per texture.
- The pieces (254 triangles): the middle, ring and little fingers grown from
  the N64's knuckle flaps and shells into the fingertips (jointed segments
  round the grip's front, one texture band per finger, the flaps closed
  underneath and covered on top); the inside of the fist closed onto the
  grip (the old skirt, in the skin beside it); a domed heel pad below the
  butt (the skirt's fan there read as a bite out of the pinky's base); the
  trigger finger's first joint closed in its own skin and colours (it was
  0x706, the pale palm texture, at full shade).
- Traps: a cover wound like the roof it sits on faces into the roof (turned
  out now; no edge is wound both ways); a skin map carried past its faces
  picks up the texture's white surround (BandMap / NearMap instead); a pale
  patch in the headset is usually a piece on 0x706 at shade 255; to find
  what shows through a green pixel, cast a ray from the render's camera.
- patch.json and authored.json are written one corner or triangle per line
  (gevr_hp_common.write_json), so a diff shows the corners that changed.
- Totals: 13 models, 35 parts, 132 groups, 3938 triangles, 5832 weights.
- Next: GtaserZ by growing from the skin like this; then the pistols with
  their own hands (golden gun, Cougar, DD44, knives).

## The watch arm the same way (2026-10-02, built, awaiting the headset)
Csuit_lf_handZ's hand (0x01c0, one bone, ten times the pistol's units) had
tubes under the ring and little fingers, pale 0x706 fingertip caps and a
pale palm fill. Now (Csuit_lf_handZ.authored.json, 325 triangles):
- The ring and little fingers' palm sides: underside() station by station
  along their open edges, nearly flat (CSUIT_FLAT: the N64's shell already
  wraps most of the way round), in the middle finger's own palm-side skin
  (0x702 s 728..865 along, t 481..594 across) at 0.8. The headset said
  "a little chubby" with a half-round bottom and the top's skin mirrored
  under it, then "better but still too fat" with a shallower one: their
  width is the N64's shell and cannot shrink, so they now match the middle
  finger the user likes instead of reading as smooth light tubes. The
  faces along these edges lean down (the shell wraps below them), so
  up_hint says the top is +y; and the edges double back at the knuckle
  crease, so the rails skip those vertices and the notches they leave are
  filled flat. (The first build had half the cross-sections upside down,
  inside the fingers.)
- The middle, ring and little fingers grown on past the N64's cut-off ends
  and curled in towards the palm, a closing fist (the user: "slightly
  extended so they're closed like a fist"): extend() from each finger's
  whole end, a knuckle ahead on the finger's line, then down and back to a
  rounded tip (CSUIT_CURL), in the PP7's 0x703 finger band, shaded by which
  way each point faces. The ring and little fingers' whole end is the N64's
  top cross-section plus our underside's last arc (the N64's sloped end cap
  ends up inside the curl); hung from the bottom opening alone the curls sat
  low, set back and thin ("slightly misaligned"). The middle fingertip is
  closed but for its underside, so its curl leaves that opening. The frames
  use forward-and-up as their reference, which stays off every tangent of
  the curl (+y alone twisted them 180 degrees).
- The index finger's two bends, open inside: closed nearly flat, a little
  darker.
- The palm: the opening left between the heel, the thumb's root and the
  finger roots, faired into a cushion in 0x705 (the palm side the N64 did
  model) only, mapped with harmonic_uv: the heel and thumb corners keep
  their own coordinates and the rest is smoothed between them.
- The watch arm's ROM is lit 255 everywhere (the textures carry the light),
  so the palm side is shaded down to 0.88 (0.8 in the bends) away from the
  N64's edges.
- Tool changes: NearMap takes several textures (each new face takes the
  texture of the ROM skin nearest to it; the finger tops change from 0x702
  to 0x703); fill_palm takes smooth_uv. Tried and dropped for the palm:
  per-face textures (a ragged seam between 0x702 and 0x705), nearest-point
  coordinates (folds near the finger roots), an affine SkinMap (knuckle
  marks on the palm).
- The 3-vertex "hole" at 398/399/400 is one N64 triangle hanging off the
  heel by a corner, not a hole.

## The taser/grenade hand and the grenade (2026-10-02, headset-accepted, merged)
The user (watch arm "good for now"): the grenade's flat bottom is a hole,
the index finger is missing some volume, and the skin is a different
colour and texture. GtaserZ is also the #41 grenade hand and the off hand
in two-handed holds.
- The grenade's bottom (GgrenadeZ, a new patched model): a disk and the
  band round it, in the model's second list in 0x5e2, an 8-bit intensity
  texture (a clock face's ticks on black; run-length coded, which
  tools/gevr_tex_decode.py now decodes). First-person models draw that list
  blended, alpha-tested in stereo, and an intensity texture's alpha is its
  intensity, so all but the ticks dropped out. The authored piece draws the
  same 24 triangles with the first, opaque list: corners on the bottom's own
  vertices (Workspace.alias pins each to them; the band's top ring shares
  its position with the bevel's darker vertices), the N64's coordinates and
  colours. The blended copy, drawn after at the same depth, fails GL_LESS.
- The hand's -x side was never modelled (the N64 camera saw the palm): the
  back of the hand and the forearm's back. The old recipe closed them in the
  pale 0x706 at 235 and 0x705 at 215: the different skin. Now
  (GtaserZ.authored.json, 225 triangles): the forearm's back between the
  channel's two open edges, a little proud of them, wearing the 0x704 side
  across from it (M.Across: a look straight across the arm to the far side's
  skin; at the slanted elbow cut, the far side's nearest point); the back of
  the hand in 0x702 with harmonic_uv; the elbow's cut end in 0x704, nearly
  flat (faired twice it grew a lip), at 0.8.
- The finger "holes" are cracks: where two segments meet (middle joint and
  fingertip, first segment and knuckle) their ends run side by side 1-6
  units apart. The old domes over them stood out as pale flaps and dips,
  the index finger's on its end as seen from behind (the "missing volume").
  They are zipped shut (zip_chains) in the skin either side; three real
  openings beside the knuckles are filled.
- fill_palm's dome rises along the fill's own normal, which follows however
  the loop happens to wind: here it sank caps into the fingers. dome_out
  makes it rise out of the hand (as the ROM faces round the rim point); the
  taser's fills use it. Off by default, so the accepted hands re-seed as
  they are; a seed now prints the fills whose domes sink: the PP7's heel
  pad (0x0468:22) and the watch arm's two index creases. The recipe ops'
  fill (gevr_hands_patch.py) domes the old way too: check the other hands'
  domes before trusting them.
- The headset (f991772): "good except the dent in the index finger is still
  there". The index finger's middle joint (a tube, ring 159..173 to ring
  160..174) has 164 and 166 pulled into the finger at its end (2.9 units off
  its axis, the rest of the ring and the other fingers' rings 6-10) and 4-5
  units back along it: seen from the back of the hand its end is a notch
  with 162 and 168 standing up as two points. The faces round 164 and 166
  are drawn again with them put back on the ring between 162 and 168
  (index_knuckle); the notch's own faces end up inside. Accepted (6091470).
- Totals: 14 models, 36 parts, 132 groups, 3932 triangles, 5848 weights.

## Back to the PP7 and the watch arm with that (2026-10-02, headset-accepted, merged)
- The PP7's heel pad sank into the hand (dome 1.0 against the fill's own
  winding): it now rises out of it (PPK_HEEL_OUT), filling the hollow under
  the little finger's knuckle as it was meant to.
- The PP7's forearm cut end was the recipe's cap at shade 60, faired twice:
  from behind and the side a near-black band round the end of the arm, the
  taser elbow's trouble. Now an authored piece (forearm_end) in the
  forearm's 0x704, nearly flat, at 0.8 (PPK_STUMP); the recipe op is gone.
  The siblings follow by same_as.
- The watch arm's index creases are cracks (the two segments' ends 16-50
  units apart, a quarter of the finger's width at most): zipped shut
  instead of filled with a dome that sank (CSUIT_CREASES).
- A scan for single sunken vertices (dent_scan, below the mean of their
  neighbours along the surface normal) flagged nothing else that shows: the
  watch arm's 232 is the palm's own hollow. The taser's two-vertex notch
  scores low on it (each pulled vertex has the other as a neighbour); the
  ring-radius check found it.
- Totals: 14 models, 36 parts, 132 groups, 3748 triangles, 5440 weights.

## The other pistols and the knives: the PP7's hand (2026-10-02, built, awaiting the headset)
The golden gun (GgoldengunZ), Cougar (GrugerZ) and DD44 (Gtt33Z) were still
on the old generic recipe (pale skirt and fingertip caps, the forearm end at
60). Fitting each PP7 node onto their hands (a rigid motion per node:
piece_match in the session scratch) found them all to be the PP7's hand:
every piece the same mesh in the same pose, the whole hand moved by one
offset in the gun's model (golden gun (0, -8.5, -80.7), Cougar (0, -26.4,
-70.2), DD44 (0, -8.1, -182.6)); only the golden gun's forearm is its own.
The knives' hand (GknifeZ, GthrowknifeZ) is the PP7's middle, ring and
little fingers and heel 3.1 units higher, its index finger the PP7's trigger
finger curled round the handle on bone 0, its own thumb and forearm.
- seed_pistol is the PP7's seed with its vertex names carried over: each
  PP7 vertex it names is the gun's ROM vertex at its place (offset on,
  within 2 units: the meshes match to their rounding), its joints move with
  the hand. Per gun (PISTOLS): the offset, the trigger finger's node (its
  own bone; none on the knives), the forearm's cut end, the nodes whose 0x702
  lines the fist, and the ring and little fingers' knuckle shifts where the
  grip stands further forward than the PP7's (the golden gun's +z 7.8 and
  9.6, the Cougar's 6.2 and 2.6: measured by how deep the grown fingers went
  into the grip, kept 1 unit off). The inside of the fist and the pad below
  the butt are built against each gun's own grip (the Cougar's grip runs
  below the heel: no pad). The PP7 re-seeds byte-identical through it.
- zip_rings sorted each ring by angle round the finger; the Cougar's ring
  fingertip is the PP7's with its N64 integers rounded a unit differently,
  which swapped two of its vertices in that order and folded the strip (an
  edge wound both ways, a hole). ring_walk keeps a ring's own order unless
  sorting gives that order anyway, so every accepted hand re-seeds the same.
- The user asked whether the grenade hand's fingers could be reused instead
  of grown. They are Rare's own, complete, and in the same skin textures,
  so their shape and mapping could be copied into another hand's patch as
  weights; but they are curled round the taser body (thicker than a pistol
  grip) and the pistol hands already draw each finger's knuckle flap and
  tip, which a copied finger would overlap unless the runtime learned to
  hide those triangles. Offered as a prototype on the PP7 to compare.
- Totals: 14 models, 36 parts, 144 groups, 4046 triangles, 6176 weights.

Tool quirks
- Bash heredocs with Python inside break here: write edit scripts to the
  scratchpad and run them.
- Render: `blender -b --factory-startup --python tools/blender/gevr_hands_patch.py --
  <abs json> <abs outdir> --render 1c0 --tint` then the before/after views
  land in the outdir; bmesh index tables go stale after subdivide
  (directed_loops refreshes them).
