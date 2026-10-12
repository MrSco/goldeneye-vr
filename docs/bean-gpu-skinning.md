# Bean character GPU skinning (PR #172)

Bean character lists now use persistent vertex/index buffers and a per-instance
bone palette. The game still evaluates its animation matrices on the CPU;
weighted vertex positions and normals, Fast3D lighting, fog and back-face
culling run on the GPU. This follows Dab's `xblaMeshGpuPalette` design at
`adef1dc2d4eca1a5c453a7f2bf03a3b6cdad99b8` (MIT attribution in
`port/licenses/dabs-mod.txt`).

The adapter's existing rig fitting is preserved: each of four influences keeps
its own bone-local position and normal. A palette has up to 64 affine transforms
of 12 floats, relative to the list's anchor, plus validity bits. Neck/back and
deferred-anchor routing, missing influences, weight renormalization, signed
normal bytes and position quantization follow the existing CPU pose. Combining
the matrices can differ from the old arithmetic by one quantized unit; the
regression comparison allows one position/normal unit for this floating-point
reordering.

Each instance gets a palette in the frame arena. Draw records copy it, together
with lighting, fog and culling, so a 90/120 Hz redraw cannot borrow another
guard's pose. GPU buffers are shared by instances and retired through the
existing mesh lifecycle on reset, Bean toggling and GL context replacement.
Static rooms do not stream a palette on each draw.

The original CPU implementation remains for palettes that cannot be built.
For GPU-capable palettes, individual unsupported runs lazily pose vertices
once per instance using the same palette: desktop rendering,
menus, generated UVs, colour tables, grayscale, line mode and first-person
culling overrides retain the CPU path. A list can mix GPU and CPU runs without
replaying its materials or drawing previous runs twice. First-person arms and
the watch can therefore still cost CPU time. Rigid guns, props and heads are
outside this change.

## Offline validation

- Android `assembleRelease` and all 36 `testReleaseUnitTest` tests, with
  `JAVA_HOME=C:/Program Files/Java/jdk-20`.
- `BEAN_SANITIZE=1 python3 port/tests/test_bean.py`: emitter/registry, weighted
  pose isolation, lazy fallback, comparison with the legacy CPU pose under a
  scaled/rotated anchor, neck routing and missing bones, and room regressions.
- `BEAN_SANITIZE=1 python3 port/tests/test_bean_mapping.py ROM FILES`: all 457
  real-asset registrations and all 26 stage checks. Every registered body skin
  also builds valid GPU runs and indices.
- `python3 port/tests/test_skin_shader.py` under WSL/Linux and Mesa: 16 actual
  production shader variants compile/link; transform feedback executes weighted
  posing, rounding/clamping, signed/zero normals, lighting, fog, invalid bones,
  palette slot 63, instance changes and saved-palette replay. Occlusion queries
  check front/back culling and inverted-target winding. Mesa replaces only the
  OVR view declarations/selector; this is not an Adreno multiview test.
- Existing surface, grip-redraw, VR-display, timing, locomotion, multiplayer,
  profiling, dynamic-arena and Bean-mode regressions.
- The shader check exposed implicit integer-to-float conversion in the existing
  three-point texture-filter `mix`; its two calls now cast the filter flag to
  float, so they compile under strict GLSL ES 3.00.

## Headset validation still needed

No APK was installed by this work. No headset frame-time or visual improvement
has been measured yet. Compare the same guard-heavy view at 90/120 Hz, with
Show stats enabled for the `skin:` log line:

1. With Bean enabled, ordinary third-person guards should produce nonzero
   `skin: ... draws, ... tris on GPU`. `CPU poses` should rise for
   first-person fallback draws, not ordinary guards or scope capture.
2. Add an empty `files/gevr_noskin.txt` to turn only GPU skinning off; remove it
   to re-enable it. It is polled with the existing mesh switch every 240 frames.
   `files/gevr_nomesh.txt` still disables all GPU meshes, including rooms.
3. Check walking/shooting/dying guards, neck seams, fogged distant characters,
   cutouts, guard-held weapons, the wrist watch and controller redraws. Scope
   aiming should show the same pose as ordinary rendering.
4. Compare DRAW/TICK, frame time and the `skin:` counts between the two modes.

Retain the broader visual checklist in `pr172-handoff.md`: Dam holes, water,
the logo, menus and Goldfinger's Cartel remain unverified on the headset.
