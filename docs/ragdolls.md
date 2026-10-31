# Optional ragdolls and limb dismemberment

Experimental implementation on `feature/ragdolls-dismemberment`. Both options
default to **off**: **Ragdolls** and **Dismemberment** on the launcher's
**Play → Game rules…** page, beside **Bodies stay**. Settings persist as
`Ragdolls` and `Dismemberment` in `goldeneye-vr.ini`.

Ragdolls work in solo, co-op and multiplayer, including bots, with native, HD
or Bean character visuals. Limb dismemberment requires ragdolls. A locally
resolved lethal hit, or a shot at a physics body, can detach a limb or the
head, by Perfect Dark CE's rules (below). There is no health or hitbox change
for living actors.

**Bodies stay controls retention in every mode when ragdolls are on:**

| Setting | Physics bodies |
| --- | --- |
| Off (fade) | Fade between four and 5.5 seconds after capture |
| 12 | Keep the newest 12 |
| 24 | Keep the newest 24 |

A body's detached limbs belong to that body and leave with it. Reducing the
count removes the oldest bodies first. With retention off, simultaneous fading
bodies use the same 24-slot pool. When ragdolls are off, the existing animated
body retention rules apply.

48 was an option until v0.5.14: a pile of 41 dismembered Bean ragdolls in view
held the Quest 3 GPU near its limit (89-109 fps of 120, where 25 held 120;
Bunker II, measured with `tools/perf/pile_drive.py`). A saved 48 now keeps 24.

## Integration

- Jolt v5.6.0, pinned to `e77f175595e64cb44218cc9d9d56fc365ad0e36a`.
  The C interface comes from Perfect Dark CE's Stable release,
  `e33826da88e929e782e22af1a62278e2de1d5cb6`. Notices are in `port/licenses/`
  and the APK's `assets/licenses/`.
- `port/external/pdphysics/` builds outside the game's N64 compiler flags.
  The backend and the stage's floors are built on the stage's first tick (as
  CE's `ragdollStageLoad`), use two worker threads when available, and shut
  down on stage change or when disabled. On Windows hosts Jolt is fetched
  under `build/_deps` (the 260-character path limit).
- `gevr_ragdoll.c` snapshots the guard rig's current world pose, private
  runtime data (including attached heads), vertex copies and display lists.
  Snapshots survive actor cleanup and player respawns. No actor slots or
  model-hit entries are retained for physics bodies. Existing AI/death clocks
  and network messages continue to operate on the original actors.
- Physics advances once with the game clock, at up to 60 steps per second.
  Pause and additional camera passes cannot advance it or duplicate deaths.
  World rendering builds fresh matrices per view before native float-to-packed
  conversion. Visibility is evaluated against that render view's camera and
  rooms, so simulation/extra trace cameras cannot hide a visible corpse.
  Captured native actors continue their death/cleanup clocks without rebuilding
  redundant native pose, attachment and hit-list data. Mesh storage is retired three frames after eviction.
- Native STAN floors provide collision for the whole stage. Loaded native
  room display lists add only triangles that don't face up (walls, overhangs,
  ceilings), at most four rooms a tick, with the correct room offset and level
  scale; up-facing render triangles are left to the floors, so stairs stay
  ramps (CE `ragdollAddRoomCeiling`, level.c:170-357). GoldenEye's floors cover
  only walkable ground (PD's cover far more), so an up-facing triangle with
  none of the room's own floors within 40 units above or 60 below its middle
  and corners is added too: cliffs, banks and terrain off the paths (two
  blasted Dam bodies fell out of the level without it). A called list in a
  room's list is stepped over. Jolt owns copies after
  room unload. Room filters exclude unrelated geometry occupying the same world
  space; a body's collision rooms are redone every tick near the camera or
  after its pelvis moves 12 units, otherwise every fourth tick (CE
  ragdoll.c:1645).
- `gevr_ragdoll_movers.c` adds nearby solid doors and props as kinematic
  colliders. Props are considered when their position is within 650 units of
  a body's pelvis or a cut piece (CE `RD_PROPRANGE` + 400, props.c:1112). Sliding, vertical and hinged doors follow native world transforms,
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
  Both cut ends are capped as CE caps them (dismember.c:590, 1636): the body's
  native lists are walked as drawn after the seams are patched, vertices welded
  by death-pose position; each hole's border loop (edges one piece shares with
  another, or open edges right at a cut) is closed by a fan whose rim vertices
  load with the mesh's own matrices and coordinates, so the cap sits on the
  edge of the hole. Bean draws its own skin, so the same border rule runs on
  the skin (`gevr_bean_caps.inc.h`): each cut triangle's piece, vertices
  welded by their source data, open edges counted on triangles across the
  cut; loops are cached per skin and cut set, and the posed rim is fanned by
  the ragdoll. CE has no Bean; this is its rule on Bean's mesh. Holes neither
  mesh describes get CE's disc at 0.7 of the cut radius.

## Ported from Perfect Dark CE

The tuning follows CE's human ragdoll (Stable, `e33826d`), not new numbers:

- **Skeleton** (`g_RdSkelHuman`, ragdoll.c:205): PD's chr skeleton is
  GoldenEye's guard skeleton less its HEADER joint, so CE part *p* is the guard
  GROUP with JointID *p*+1 (1 pelvis, 2 chest, 3 head, 4/5 upper arms, 6/7
  forearms, 8/9 hands, 10/11 thighs, 12/13 shins, 14/15 feet;
  `gevr_bean_mapping_test.c` checks these joints against the cartridge). Eleven
  parts have bodies; hands and feet ride their forearms and shins. Any other rig
  layout logs once and keeps its death animation (CE create.c:116). Hitparts map
  to parts through the model's BBOX nodes, as CE's do.
- **Shapes, masses, joints** (`ragdollCreatePhysics`, create.c:455): torso
  length scales everything; segment masses after Winter / de Leva; pelvis
  capsule across the hips, head sphere up the spine, foot capsules on the
  shins; swing-twist limits per joint; elbows and knees are one-way hinges
  widened to hold the death pose, never more than ~15 degrees backwards.
- **Launch** (`ragdollOnChrDeath`, ragdoll.c:1063; `ragdollGetPushSpeeds`,
  :656): 525 units/s on the hit part plus 180 on the whole body and 60 upward,
  scaled by the weapon's damage and force of impact. Explosion deaths keep the
  native launch velocity. CE also carries the walking speed; GoldenEye keeps no
  previous root position, so that part is left out.
- **Shots at bodies** (`ragdollOnChrShot`, ragdoll.c:858): 45% of the hit push
  and 15% of the whole-body push (only on the hit piece once cut), at most once
  every four ticks per body.
- **Dismemberment** (dismember.c:1268, 1375, 1478): CE's two settings,
  **Damage needed** (0.5-10) and **Chance** (0-100%), are sliders on the Game
  rules page (`DismemberDamage`, `DismemberChance`). CE's default damage 2.0 is
  its magnum; GoldenEye's guns are 1.0 or less (KF7 1.0, Skorpion 0.6), so the
  default here is 1.0 with CE's 40% (user, 2026-10-16). Never punches; the shotgun cuts
  only when a whole volley lands on one limb of one body, at the cut nearest the
  body. Chest hits split the waist only at damage 50 or more. The head can come
  off; hands and feet go with their forearm or shin. The chance applies to
  volleys and blasts too.
- **Wounds on bodies**: a shot body bleeds where it is hit, as a living guard
  does (`chrHandleBulletHit` -> `chrCreateBloodStain`, chr.c:3876): the
  nearest hit box of that part, the game's own stain on the body's private
  vertices, and Bean's wound through it (Dab's Mod). The game's random
  sequence is restored after the stain. Stains survive later cuts.
- **Explosions** (`ragdollOnExplosion`, ragdoll.c:1318;
  `ragdollTryDismemberBlast`, dismember.c:1593): a guard killed by a blast is
  thrown up and away from its centre (700 units/s, falloff over 1.5 times the
  explosion type's damage range) instead of taking the native launch; joints
  within 0.6 of that range (the waist 0.35) can come off, up to two, with a
  40% chance falling off with distance. Bodies already down are thrown and
  cut by an explosion's first damage pulse (GoldenEye repeats the pulse every
  quarter of the explosion's life; CE blasts once). A private random sequence
  makes the rolls, so the game's own (co-op and replay) sequence is untouched.
- **Kill floor and sleep** (ragdoll.c:1691, 1708): a body below y = -32000 is
  removed; a body still asleep after the step skips its pose read-back.
- **Drawing** (`ragdollIsAnyPieceOnScreen`, rooms.c:460): drawn when one of its
  pieces' rooms is drawn and any part is inside the view, culled at tick time
  as chrTick culls guards. This replaces the 100 m distance check.

Not ported: CE's animation blend, muscles and reflexes, grabbing, bodies as
cover, translucent-surface passability (GoldenEye's primary room lists are
opaque), prop triangle meshes, and the optional physical-body cap (CE's
default is unlimited).

**Hinge axes** (headset, 2026-10-16): CE's hinges turn about each joint's
local Y, which PD's animated frames put across the limb. GoldenEye's do too:
every body logged `hinge Y along bone 0000`. If a rig ever runs Y along the
bone, the port uses CE's own rule for rigs without known hinges (bend the way
it is bent, about that axis). The game's `atan2f` returns 0..2pi, so death-pose
angles are wrapped to -pi..pi before the limits are widened.

## Current limits

Physics is cosmetic and **local to each headset**. Deaths are detected from the
existing authoritative game state, but trajectories, corpse impulses and cuts
are not replicated. Peers can choose different options and counts. Corpses
do not block bullets, players or mission progress. This does not add Perfect
Dark CE's grabbing, bodies as cover, physics death camera or witness AI.

Collision uses native level geometry, including with Bean visuals. Moving
props use their native root bounds; holes, articulated vehicle parts and
custom sheared transforms are not reproduced. Very large doors or platforms
whose centre is more than 650 units from a body's pelvis are not picked up
until the body comes closer. Up to 256 nearby prop colliders
share the backend pool, separately from the configured body count. Detached
limbs can be shot, but existing corpses
do not receive explosion-area impulses. New explosion deaths preserve the
native launch velocity. Models outside the native guard skeleton retain their original
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

0. Headset, 2026-10-16 (Dam, GE-X weapons, Bean on and off): accepted.
   Shot bodies bleed and cut with damage-1.0 guns; caps follow the holes;
   GE-X's rocket launcher shows no far-side decal (decals facing away culled
   in the stereo gun pass, a 1 cm decal band for hand-held models) and holds
   its own rocket in the tube (GoldenEye's oversized round no longer drawn
   on it). Still unverified: Quest frame time with many bodies, two-headset
   co-op and deathmatch.
   Fifth round (2026-10-16): Bean bodies still showed discs; Bean skin caps
   now follow the hole too. A rocket launcher's barrel showed its far-side
   decal text through the near side (first-person gun, not ragdoll code).
   Fourth round (2026-10-16, Dam): no body fell out of the level and no room
   was floor-only after the uncovered-ground fix. The disc caps did not line up
   with the holes (screenshots of a torso and a head); caps now follow the mesh
   border. Check native caps line up, and Bean discs.
   Third headset round (2026-10-16, Dam): bodies bounce off walls; a blasted
   guard clipped into a cliff and two bodies fell out of the level (ground
   without floors, fixed above). Check the cliffs and banks again.
   First headset round (2026-10-16, Bean visuals): ragdolls fine, 11 parts and
   CE hinge axes on every body; no cuts, because Skorpion kills are damage 0.6
   and grenade-launcher kills had no blast rule yet (now ported). Next: grenade
   and rocket kills cut and throw bodies (`blast 1` in the log), a shotgun
   volley on one leg, a strong gun, with Bean and native visuals; room
   collision counts and no `floor-only` warnings. Note which GoldenEye weapons
   reach damage 2.0.

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
