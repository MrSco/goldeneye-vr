# Optional ragdolls and limb dismemberment

Experimental implementation on `feature/ragdolls-dismemberment`. Both options
default to **off**. In the launcher, open **Play → Game rules… → Ragdolls and
limbs…**. Settings persist as `Ragdolls` and `Dismemberment` in
`goldeneye-vr.ini`.

Ragdolls work in solo, co-op and multiplayer, including bots, with native, HD
or Bean character visuals. Limb dismemberment requires ragdolls. A locally
resolved lethal limb hit, or a shot at a physics body, can detach that limb.
There is no health or hitbox change for living actors.

**Bodies stay controls retention in every mode when ragdolls are on:**

| Setting | Physics bodies |
| --- | --- |
| Off (fade) | Fade between four and 5.5 seconds after capture |
| 12 | Keep the newest 12 |
| 24 | Keep the newest 24 |
| 48 | Keep the newest 48 |

A body's detached limbs belong to that body and leave with it. Reducing the
count removes the oldest bodies first. With retention off, simultaneous fading
bodies use the same 48-slot pool. When ragdolls are off, the existing animated
body retention rules apply.

## Integration

- Jolt v5.6.0, pinned to `e77f175595e64cb44218cc9d9d56fc365ad0e36a`.
  The C interface comes from Perfect Dark CE's Stable release,
  `e33826da88e929e782e22af1a62278e2de1d5cb6`. Notices are in `port/licenses/`
  and the APK's `assets/licenses/`.
- `port/external/pdphysics/` builds outside the game's N64 compiler flags.
  The backend starts on the first eligible death, uses two worker threads
  when available, and shuts down on stage change or when disabled.
- `gevr_ragdoll.c` snapshots the guard rig's current world pose, private
  runtime data (including attached heads), vertex copies and display lists.
  Snapshots survive actor cleanup and player respawns. No actor slots or
  model-hit entries are retained for physics bodies. Existing AI/death clocks
  and network messages continue to operate on the original actors.
- Physics advances once with the game clock, at up to 60 steps per second.
  Pause and additional camera passes cannot advance it or duplicate deaths.
  World rendering builds fresh matrices per view before native float-to-packed
  conversion. Mesh storage is retired three frames after eviction.
- Native STAN floors provide collision in unvisited rooms. Loaded native
  room display lists add solid walls, ceilings and ledges, with the correct
  room offset and level scale. Jolt owns copies after room unload. Room
  filters exclude unrelated geometry occupying the same world space.
- `gevr_ragdoll_movers.c` adds nearby solid doors and props as kinematic
  colliders. Sliding, vertical and hinged doors follow native world transforms,
  scale, reflection and current clipped bounds. Open/disabled/destroyed doors
  lose collision with the native game. Caverns eye/iris doors use an enclosing
  box around their native collision prism, including its reduced ajar height.
  Colliders use simulation data even when a model is off screen or its render
  matrices have been packed. They are removed on prop delisting, slot reuse,
  stage change and option disable.
- Moving solid platforms carry bodies through ascent, descent, horizontal
  motion and stops. Contact tests transfer platform acceleration to connected
  pieces resting on top, with damping relative to platform speed. Detached
  pieces elsewhere receive no carry impulse. Nearby motion or support removal
  wakes sleeping bodies. Script/network warps teleport colliders with zero
  velocity instead of launching bodies. Room filters also apply to props.
  GoldenEye has no separate lift object type; a solid moving object or door
  panel supplies the platform. This adds no new playable elevators.
- Native cuts patch private seam vertices and matrix references. Bean cut
  triangles choose one piece for all their influences and use CPU posing;
  uncut bodies and other characters retain the shared GPU skinning path.
  Both cut ends receive simple dark-red caps.

## Current limits

Physics is cosmetic and **local to each headset**. Deaths are detected from the
existing authoritative game state, but trajectories, corpse impulses and cuts
are not replicated. Peers can choose different options and counts. Corpses
do not block bullets, players or mission progress. This does not add Perfect
Dark CE's grabbing, bodies as cover, physics death camera or witness AI.

Collision uses native level geometry, including with Bean visuals. Moving
props use their native root bounds; holes, articulated vehicle parts and
custom sheared transforms are not reproduced. Up to 256 nearby prop colliders
share the backend pool, separately from the configured body count. Detached
limbs can be shot, but existing corpses
do not receive explosion-area impulses. New explosion deaths preserve the
native launch velocity. The caps are approximate joint disks, not exact mesh
cross-sections. Models outside the native guard skeleton retain their original
death rendering; a failed snapshot also falls back to it.

Forty-eight bodies are supported by the pool and native stress test; **Quest
frame time has not been measured**. Cut Bean bodies use CPU skinning and are
the most expensive case. There is no automatic body-count reduction.

## Validation

The real Jolt/adapter test uses synthetic models and room geometry, without
cartridge data:

```sh
python3 port/tests/test_ragdolls.py --jolt-source /path/to/JoltPhysics-v5.6.0
python3 port/tests/test_ragdoll_ui.py
python3 port/tests/test_bean.py
python3 port/tests/test_bean_wounds.py
python3 port/tests/test_reinforcements.py
```

`test_ragdolls.py` runs the adapter and mesh/world code under ASan/UBSan. It
covers 1/2/4/8-player deaths, repeated slot reuse, duplicate camera passes,
private head/vertex ownership, 12/24/48 retention, 48-body simulation, pause,
native seams, remapping, per-view draw, fade and shutdown. Collision checks
cover stacked-room filtering, scaled room offsets and room unload. Moving
collision checks use the production door-transform and collision-eligibility
functions with synthetic setup data. They cover sliding/clipped/hinged doors,
eye/iris ajar height, reflection/nonuniform scale, warp handling, prop reuse
and delisting, sleeping riders, support removal, unrelated rooms, pause and
detached pieces. Forty-eight sphere riders follow lifts at 60/30/10 Hz; a
captured 15-part guard and its severed arm ride through the game-clock path.
These checks do not replace cartridge/Quest playtests. Bean's
pose test checks triangle separation and preservation of another instance's
GPU skin. The launcher test measures both pages at Quest's font scale.

In this cloud workspace, activate `/workspace/.gevr/env.sh` before the build.
The existing native fixtures need the local GCC compatibility wrapper for
glibc's partial `stddef.h` includes and GCC 14's pointer diagnostics. The new
ragdoll fixture supplies its own strict-C11 compiler setup. The Android debug
APK and JVM tests also build here; no headset or ROM is attached.

### Still to verify on headsets

1. Native and Bean deaths on Dam, Facility stairs/vents, moving doors and
   custom moving platforms; bodies under closing slabs, in hinged doorways,
   on ascending/descending platforms, and falling when support is removed;
   limb seams, caps, shadows, blood and hats in stereo and cinema mode.
2. Solo retention at Off/12/24/48; sustained 48-body combat on Quest 2 and
   Quest 3, especially multiple cut Bean bodies. Record CPU/GPU frame time.
3. Two/four-headset co-op: host/client kills, NPC removal, reinforcements,
   late join, host migration and stage restart. Each peer's count is local.
4. Deathmatch with bots and remote players: repeated deaths/respawns and
   disconnects with 48 bodies kept; no duplicate bodies or invisible live
   avatars. Confirm that physics does not affect damage or pickups.
5. Pause/unpause, scopes and extra barrel views, turning physics off, and
   repeated stage changes. Both-off behavior must match the current release.
