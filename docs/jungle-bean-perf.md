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

## What is left

The start view is CPU-bound in the record (gfx_sp_tri1 25% and gfx_sp_vertex
12% of the render thread on 17K foliage triangles). Levers: a gfx_sp_tri1
fast path for triangles under unchanged state, or the mesh path at CPU-path
GPU cost (then rigid_min 0 wins everywhere).
