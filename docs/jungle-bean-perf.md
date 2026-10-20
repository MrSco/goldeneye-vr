# Jungle with Bean: frame drops (2026-10-20)

Quest 3 on the cable, 120 Hz, EyeMsaa 2, SpaceWarp off, Bean + Community
Edition on. Unattended route: the start view, then pads 20, 40 and 60, each
5 s still and a full turn (benchmark build, level hook `jungle 0`).

## What it is

Not the drone-gun turrets. With draw-skip switches at the start view, hiding
tracers, gunfire nodes, explosions and smoke, sparks, or the autoguns changed
nothing (93 fps, 382 draws). Hiding the Bean foliage props alone gave a
locked 120: 382 -> 112 draws, DRAW/TICK 10.3 -> 6.4 ms, app GPU 7.5 -> 5.6 ms.

The foliage is ~100+ instances of Bean props: PpalmZ (214 tris), Pplant1Z,
Pplant11Z (216), Pplant2bZ (200), PpalmtreeZ (64), Pjungle3_treeZ (60). Each is
under rigid_min (256 tris, gevr_bean.c wrapEmitted), so it is drawn through the
CPU vertex path: 51-61K CPU vertices and 17K CPU triangles a frame (PpalmZ
alone 32K vertices). They are drawn translucent, as GoldenEye draws them
(ZMODE_XLU, blended, depth test without depth write; DAB keeps the game's
node modes too), so their draw order decides what covers what.

## Fixed on this branch

- `gevrBeanCeApplied()` ran a `stat()` on every Bean room and prop list's
  fog (gevrBeanRoomFog -> communityEdition -> gevrBeanCeRoot): 12.8% of the
  render thread at the start view. Now looked up once; gevrBeanCeApply() resets it.
- `gevr_bean_transform()` (two Bean texture lookups) ran for every CPU
  triangle; now kept per record pass per tile and picture (~4%).

Route A/B, xr frames/s (main 2f69dd38 -> this branch): start still 98.5 ->
107.5, start turn 108.8 -> 114.4, pad 60 still 94.2 -> 103.8, pad 60 turn
104.8 -> 111.6, pads 20/40 ~120 either way. Screenshots at the start view and
pad 20 match.

## Tried, not kept

- rigid_min 0 (every prop a GPU mesh): CPU vertices 0, pads 60 106-114 fps,
  but the start view goes GPU-bound (app GPU 7.4 -> 9.1 ms, eye 5.4 -> 6.7 ms)
  and drops to ~91. Same draw count as the CPU path, so the mesh path costs
  the GPU more per draw (trunk and fronds share one program and alternate its
  uniforms every draw; unconfirmed).
- Instancing identical mesh draws in the replay (patch kept outside the repo):
  grouped only ~48 of ~380 draws, because the foliage is translucent and
  its order can't change; a matrix select in every mesh vertex shader also
  cost ~3 ms of GPU, so a separate instanced program variant is needed.

## 2D too (user, 2026-10-20)

"In jungle the slowdown happens even in 2d mode": 2D (PlayMode=0) has no eye
replays, and it dropped screen frames at the start view too (112 xr/s, min
109; DRAW/TICK 8.3 ms; GPU idle at 3.6 ms), so the cost is the display-list
record of the foliage, not stereo. In the 2D profile gfx_sp_tri1 was 29% of
the render thread, 16 points of it the per-vertex emission: tile lookups,
divisions by 255 and the combiner-input switch, all the same for a
triangle's three vertices. They are now worked out once a triangle, with
bit-identical output (gevr_input_source; inputs whose value depends on the
case before them keep the old switch).

2D start view 112 -> 120.5 xr/s (min 120), DRAW/TICK 8.28 -> 7.38 ms; pad 60
120 either way. VR unchanged at ~108 (start) and ~102 (pad 60 still).
rigid_min 0 re-measured on main with the decal-band LRZ fix: still 92 fps at
the start (GPU 9.0 ms, DRAW/TICK 10.3), so the CPU path stays.

## 2D at the first drone gun (user, 2026-10-20)

The user still dropped to ~96 in 2D standing at the first drone gun with guards
firing. Their spot, logged with the new gevr_cheat.txt `where` word and stood on
again with gevr_warp.txt `at -726 436 -4765 274.0`: 2D 118.8 xr/s (min 115),
DRAW/TICK 7.3 ms, 46K CPU vertices, GPU 3.1 ms. The props on the CPU are the
cost; as GPU meshes (rigid_min 0) the same spot is a locked 121 at DRAW/TICK
3.75 ms. So on the screen (PlayMode 0, read as a level loads) every Bean prop
is now a GPU mesh; stereo keeps rigid_min 256. Facility and Dam in 2D: no change.

Why not in stereo too: with rigid_min 0 the foliage costs the eye pass 1.4 ms
more GPU (6.8 vs 5.4 ms at the start view; 0.8 ms more at EyeMsaa 1) for the
same pixels (screenshots match), and VR's start view drops to ~92-98. Ruled out:
uniform uploads (re-uploading every mesh uniform every replay draw changed
nothing), the far clip distance, alpha-to-coverage (blended draws never take it).
It needs a GPU capture (RenderDoc for Meta Quest, launched from its UI).

## What is left

VR's start view misses on its fresh frames: the early tick (game tick +
record) takes ~8 ms before a ~2 ms replay, against an 8.33 ms slot. The
record has to lose about 1.5 ms more: gfx_sp_tri1 is still 21% of the VR
render thread (half of it the float stores of ~80 floats a triangle) and
gfx_sp_vertex 13% (clip tests and the presentation transform, spread thin).
The other lever is the mesh path at CPU-path cost per draw: today 270 small
mesh draws cost more CPU and GPU than the same triangles batched on the CPU.
