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

Collision uses native static level geometry, including with Bean visuals.
Moving doors, lifts, vehicles and furniture do not yet have Jolt colliders.
This can produce clipping. Detached limbs can be shot, but existing corpses
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
cover stacked-room filtering, scaled room offsets and room unload. Bean's
pose test checks triangle separation and preservation of another instance's
GPU skin. The launcher test measures both pages at Quest's font scale.

In this cloud workspace, activate `/workspace/.gevr/env.sh` before the build.
The existing native fixtures need the local GCC compatibility wrapper for
glibc's partial `stddef.h` includes and GCC 14's pointer diagnostics. The new
ragdoll fixture supplies its own strict-C11 compiler setup. The Android debug
APK and JVM tests also build here; no headset or ROM is attached.

### Still to verify on headsets

1. Native and Bean deaths on Dam, Facility stairs/vents, and moving lifts;
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
