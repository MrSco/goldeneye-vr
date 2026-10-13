# VR tracking, body cues and Bean render work — 2026-10-13

Aim steadying previously advanced when the game took its controller snapshot,
at the game frame rate. Retained-geometry XR frames used that same filtered
play-space orientation. Position and head-relative conversion updated, but
weapon rotation could hold for an intervening frame and then jump. This path
is shared by original and Bean weapons.

The filter now advances once after each OpenXR controller sample. Fresh game
snapshots, retained gun redraws and scope placement read that filtered result.
Repeated getters do not advance it. The filter coefficient is time-scaled from
its existing 60 Hz strength; controller position remains unfiltered.

The tracked gun matrix already replaces flat-game sway, walking bob, crouch
offsets and crosshair lead. Walking head bob is suppressed in `bondhead.c`,
and the stereo camera uses the headset orientation rather than animated head
look/up. `viShake` now explicitly clears screen-shake intensity and its timer
in stereo. Optional weapon recoil and action animations still apply.

With Body Slots and Show enabled, the two hips, chest and gadget belt render
their grabbable item. The hovered choice overrides the default selection;
empty and holster-only selections have no model. Shoulders remain hidden.
Reload-ammo guides suppress only their corresponding hip preview. Only slots
in view build model matrices. The four existing body model instances suffice.
The shared model poser also bounds its switch-table and matrix accesses for
short item models.

Two Bean dispatch changes remove avoidable work without reducing texture or
mesh quality:

- Hidden runs, and runs with both faces culled, are consumed before GPU setup
  or CPU skin posing. Previously they could pose and transform every vertex
  only to discard every triangle.
- Mirrored skin runs can use the retained GPU mesh path with the same winding
  reversal as the CPU path. The render-target inversion is applied separately.
  Runs requiring the special first-person backface depth push or generated UVs
  keep their existing fallback.

## Local validation

Debug and optimized release Android builds completed. The debug APK signature
verified. These native regression scripts passed:

- `test_aim_steady.py`: rotation advances on every 120 Hz frame, immutable
  snapshots, repeated reads/updates, low-speed response at 60/90/120 Hz, scope
  grip steadying and either handedness.
- `test_grip_redraw.py`, `test_controller_tracking.py`, `test_two_hand.py`,
  `test_gadget_hand_pose.py`: tracking loss/recovery, inferred poses, controller
  roles, scope attachment, support-hand aim and gadget transforms.
- `test_body_slots.py`, `test_body_slot_interactions.py`,
  `test_body_slot_render.py`: torso/selection arithmetic, input arbitration,
  front-slot draws, empty/hidden slots, reload-guide isolation and screen shake.
- `test_slot_model_pose.py`: short switch tables and matrix arrays under
  ASan/UBSan.
- `test_mesh_dispatch.py`, `test_mesh_visibility.py`, `test_mesh_uniforms.py`,
  `test_skin_shader.py`, `test_frame_timing.py`: production mesh dispatch,
  winding, conservative visibility, GL state, generated GLES shaders on Mesa,
  and fresh/redraw timing.
- `test_heldgun_flash.py`: synthetic flash-depth checks; the cartridge-model
  portion was skipped because no ROM was supplied.

Existing Linux harness compatibility required a scoped compiler wrapper to
include the system `stddef.h`/`strings.h` and downgrade GCC 14's incompatible
pointer diagnostic in the original game math. Production compiler flags and
test assertions were unchanged.

## Quest 3 validation still needed

No headset is connected in this environment. No claim of improved Quest frame
times or sustained Bean rendering at 120 Hz is established by the local tests.
The older measurements in `vr-render-stalls.md` concern original meshes with
the HD pack; they do not identify the current Bean bottleneck.

At 120 Hz the frame budget is 8.33 ms. Compare baseline and candidate optimized
builds on the same Quest 3, scene, view direction and Bean settings. Keep stats
off during timing. Use the existing `tools/perf/quest_profile.py` backup/run/
restore workflow and `tools/perf/analyze.py` to compare fresh and redraw CPU
time, asynchronous GPU time, texture uploads and compositor drops. Preserve
the same diagnostic batching settings between builds. Device logs already
report GPU skin draws, CPU poses, mesh fallbacks and culled runs.

Also playtest steady hand rotation, head motion with a stationary hand,
one/two-handed scopes, both handedness modes, and chest/belt previews while
hip reload guides are visible. Check slot model scale, grab location and
occlusion using original and Bean models. Sustained fights and heavy level
views need separate checks from a stationary scene.
