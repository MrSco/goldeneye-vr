# Camera clearance at low doorways

The Caverns report from PR #172 occurs at the top of a narrow opening in
flat/stereo gameplay with Bean enabled or disabled. The player was using Jaws,
the tallest character. The floor/prop movement tests do not constrain the final
eye against background lintels. Character eye height, animation and stereo
head rise can therefore put the camera above a walkable doorway.

`gevrBgClampCameraCeiling` probes native solid background in the current room
and its portal neighbours, upwards from 30 world units above the walking floor.
25 samples, every 7 units, cover a 14-unit horizontal footprint around the
eye midpoint: both eyes, and the near plane 10 units ahead of them (with 8
units the view still showed above a Caverns doorway for a moment going in
and out, user).
The eye is capped 20 units below the nearest hit, without changing character
height in open space, player movement, collision bounds or crouch controls.
It uses the asset scale for bounds and hits; visibility scaling is independent.
Native geometry remains the source of clearance when Bean draws the room.

The game camera applies clearance after its final height offsets in both
rendering modes. The stereo presentation checks again after current tracking
and locomotion interpolation, including cached-geometry redraws. Each retained
camera owns the amount removed by its original ceiling clamp. Presentation
restores that amount before applying the new clamp, so small ducks under a
lintel and leaving the doorway agree between fresh draws and redraws. Only the
local game camera updates the floor/room context; another player's render or
a scope pass cannot replace it. Redraws do not load room data.

Validation on the cloud workspace:

- `python3 port/tests/test_camera_ceiling.py`: production ceiling solver and
  game-camera context against synthetic triangles using the production
  ray/triangle function, with ASan/UBSan. Covers tall eyes already above a
  lintel, standing height outside it, ducking, neighbour-owned lintels,
  off-centre footprint hits, translated/scaled assets, both windings, distant
  ray hits, flat/stereo, local context and camera exclusions.
- `python3 port/tests/test_locomotion.py`: production XR integration, including
  late head rise, small/deep ducks, fresh/redraw continuity and height recovery.
- Existing edge-slide and full-room-buffer checks pass with Linux compiler
  compatibility flags (`-fms-extensions -D_POSIX_C_SOURCE=200809L`, plus `-lm`).
- Android `assembleDebug` passes; the debug APK signature verifies.

No Quest or game ROM is available here. Confirm the reported Caverns doorway
with Jaws in 2D/3D, Bean on/off, including looking up while walking through,
head rise/duck, turns, and leaving the opening. Check a normal-height character
as well. The probe uses native geometry; exact Bean mesh clearance and headset
performance still need that device check.
