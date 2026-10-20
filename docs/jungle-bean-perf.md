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
3.75 ms. Facility and Dam in 2D: no change. That first went in for the screen
only (b863d0bd), because of stereo's GPU cost below.

Why not in stereo at first: with rigid_min 0 the foliage cost the eye pass
1.4 ms more GPU (6.8 vs 5.4 ms at the start view) for the same pixels. Not the
uniform uploads, the far clip distance or alpha-to-coverage: it was the mesh
vertex shader. Its skin, light, tint and wound code ran for every vertex of
every static mesh (rooms and props, which use none of it), in every bin and
both views. A probe build without that code took the start view to a locked
120 (eye 6.78 -> 4.20 ms). Static meshes now take a variant without it
(gfx_opengl.cpp s_buildStatic, static_program_pool); skins keep the full one.

With that, every Bean prop is retained in both modes (rigid_min 0):

| xr frames/s | before | after |
|---|---|---|
| Jungle VR start view | 100 | 120.7 (min 120) |
| Jungle VR turret spot | 102 | 121 (min 120) |
| Jungle 2D turret spot | 118.8 (min 115) | 121 (min 120) |
| Facility / Dam / Frigate VR start + pad 20 | 120 | 120 |

## Dead guards in pieces

The user's next spot (`at -668 363 -4969 188.9`) still fell 20-30 fps in VR,
only while a shot-off guard lay in view; 2D and whole bodies were fine. A
dismembered Bean body skipped the GPU skin: every frame gevrBeanRenderGdl
posed all its vertices on the CPU (CPU vertices 16K -> 30K, gevrBeanRenderGdl
12% of the profile, gfx_sp_vertex + tri1 18%), in both eye passes' worth of
CPU draws.

The CPU pose only gives each triangle one piece and carries its influences
across the cut into that piece's frame at the bind pose (s_cuts.remap). That
depends on the body and its cuts, not the frame, so it is now made once per
body and cut set into a skin of its own, and the skin shader draws it with a
palette as a whole body; only the cut rims are posed on the CPU, for the caps
(gevr_bean_pose.inc.h beanCutGpuDraw). Moving and settled pieces held 120 in
VR with ten bodies down, several cut ("skin: ... 0 CPU poses"). Cut bodies
still draw without wounds, as before.

## What is left

Re-check Jungle in 2D where the user saw 109 on b863d0bd (spot unknown; mark it
with gevr_cheat.txt `where`).
