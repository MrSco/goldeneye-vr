# VR locomotion interpolation

## Implementation

Stereo gameplay presents confirmed local-player translation and body yaw on an
eight-sample, logical 60 Hz timeline anchored to OpenXR predicted display time.
The delay covers the longest normal gap between fresh game frames: 16.7 ms at
120 Hz, 22.2 ms at 90 Hz, 25 ms at 80 Hz, and 27.8 ms at 72 Hz. Queries clamp to
confirmed history; they never predict movement beyond a collision or missing
sample. Independently moving actors retain their simulation cadence.

Room-scale displacement and physical ducking are removed from the locomotion
history. Current head tracking is composed once with the delayed locomotion.
Fresh eye draws and retained-geometry redraws share the same presentation-camera
calculation. Each fresh render records both its simulation camera and actual
presentation camera; redraws use an absolute transform from the retained raw
vertices, avoiding accumulated corrections. Controller-tagged geometry and HUD
captures retain current tracking. Scope captures retain the gameplay camera.
Simulation, collision, aiming, projectiles, multiplayer state, the frame pump,
and virtual-screen rendering retain their existing behavior. Fresh game frames
still have one eye pass. Triangle visibility conservatively includes both the
simulation and presentation views; existing portal widening remains in place.

History resets on player/stage changes, explicit relocations, recentering, snap
turns, pause/watch/menu transitions, tank transitions, invalid tracking/session
state, refresh changes, and gaps over 100 ms. After a reset, the current confirmed
pose seeds the history. Physical movement blocked by collision also resets it.

## Diagnostics

Enable `ShowStats` to display and log once-per-second cadence:

- `SIM`: gameplay simulation ticks per second; paused ticks are excluded.
- `RENDER`: fresh game eye frames per second.
- `WORST RENDER GAP`: largest interval between fresh renders in the reporting window.
- `XR`: successful OpenXR submissions per second.
- `WORST XR GAP`: largest interval between successful submissions.
- `REDRAW`: successful intermediate retained-geometry redraws per second.
- `LOCO`: interpolation delay in milliseconds.
- `CLAMP`: presentation queries outside confirmed history in that window.

Steady XR submission FPS alone does not prove smooth movement or that display
deadlines were met. Record timing alongside visual observations.

## Automated validation

Run `python port/tests/test_locomotion.py`. It compiles the production C module
as C11 with warnings treated as errors and links extracted production XR and GL
integration functions against mocked tracking/uniform calls. Coverage includes
72/80/90/120 Hz scheduling, forward/backward/strafe/stair motion, physical head
motion, smooth body yaw and wraparound, stops/collision bounds, stalls, refresh
changes, invalid input, resets, fresh/redraw continuity, current controller
tracking, shader cleanup, and operation without a retained-geometry ring.

Validated on 2026-10-05:

- Native locomotion, display, surface, co-op collision, and hand-reload checks
  passed; all 64 multiplayer tests passed.
- Android `assembleDebug` and `testDebugUnitTest` passed; 20 unit tests had no
  failures.
- The signed debug APK updated the connected Quest 3 successfully. Launch was
  blocked by the headset's controllers-required system dialog before the game
  process started. This is not a successful gameplay smoke test.
- The installed baseline APK, original settings, and save were backed up in
  ignored `android/app/build/locomotion-device`; original settings were restored.
  No automated baseline-versus-patched gameplay comparison was completed.

## Quest validation

On 2026-10-05, after installation of the patched APK, the user tested on Quest
and reported: "feels smooth. i just tested". This confirms subjective locomotion
smoothness in the tested setup. The scene, actual display rate, QGO/ASW settings,
and solo/co-op mode were not specified, and no timing capture accompanied the
report. The original stutter was not apparent in this test; the full comparison
matrix below remains unverified.

In follow-up testing of v0.4.9 (`6c1b304`), the user reported a tremendous
smoothness improvement but occasional stutters at 90 and 120 Hz. Screenshots
show clamp counts of 0/41 at 90 Hz and 51/120 at 120 Hz. The stutter report is
still open; see [clamp investigation](vr-locomotion-clamps.md).

## Remaining Quest acceptance

Use the same scene, resolution, settings, and saved position for baseline and
patched builds. Save the original configuration and save data before testing.
For each rate below, repeat with QGO enabled and disabled, and in solo and co-op.

| Rate | Expected delay | Gameplay comparison |
| --- | --- | --- |
| 72 Hz | 27.8 ms | Pending |
| 80 Hz | 25.0 ms | Pending |
| 90 Hz | 22.2 ms | Improved, intermittent stutters reported |
| 120 Hz | 16.7 ms | Improved, intermittent stutters reported |

Exercise forward/backward movement, strafing, smooth turning, combined movement
and head motion, abrupt stops at walls, stairs, doorways, scopes, current hands
and HUDs, snap turns, teleports/recentering, pause/resume, tank entry/exit, refresh
changes, and tracking/session loss. Check for geometry appearing or disappearing
at portal boundaries. Capture the cadence log and compositor timing with each
comparison; record the actual display rate and QGO/ASW settings.

Acceptance requires continuous locomotion across fresh/redraw boundaries,
unchanged speed and collisions, current head/hand tracking, and no additional
eye pass per fresh game frame. The user's smoothness report supplies the first
visual confirmation; broader acceptance still requires the comparison matrix
and checks above.
