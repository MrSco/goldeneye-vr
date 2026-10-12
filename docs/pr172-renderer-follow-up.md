# PR 172: animation, colour and fog follow-up

The eight fault classes counted against `2057eb2` now have seven source fixes.
This follow-up repairs five of the previous six remaining classes. Reflection
and scratch passes are still an open implementation item; headset appearance,
Dam stutter and menu repaint use need verification on the updated APK.

## Changes

- Preserve authored vertex RGB separately from normals in both the GPU skin
  buffer and the CPU fallback. Unlit watch-light vertices retain their colour.
  Rigid lit heads retain RGB too. Tint loads stay inside each material's GPU
  run, so preserving colour does not multiply character draw calls.
- Restore the watch's two light triangles, retaining outfit/cuff selection.
- Rotate authored pictures about their centre using game time, alongside UV
  scroll. This covers the five Frigate bridge consoles, door console and
  helicopter. Frames are decoded once and kept in separate texture-cache
  entries: the Complex beacon alternates its two images every 0.5 seconds.
  The helicopter's explicitly stopped frame sequence remains stopped.
- Capture UV rotation, selected frame textures and tint state for scopes and
  XR redraws; CPU and GPU paths apply the same affine UV transform.
- Expand HD linear fog to all nineteen retail environment states. Include CE
  range overrides and Surface 2 colour, Dam's height-dependent range, and the
  native gas/sky transition. Bean models use the same fog line as rooms in
  gameplay and clear it for menu previews; depth and alpha modes are preserved.

## Validation

- Actual asset checks pass for the watch's 135 translucent black glass faces
  and two light faces, Dam's moving reservoir and four road-paint decal faces,
  Complex's four water faces and two distinct beacon images, helicopter UV
  motion, and Frigate radar rotation.
- All 457 native model registrations and 26 stage catalog rows pass against
  the local ROM and Bean pack with address/undefined sanitizers. GPU buffers
  retain tint values and no material gains additional runs from tint loads.
- Mesa GLES compiles/links all sixteen production mesh shader variants and
  executes weighted skinning, tint, unlit colour, rotated UVs, linear fog,
  scope projection, winding and palette replay checks.
- The scope recorder regression checks ownership of tint, UV rotation, fog
  and palette state after the caller changes its instance data.
- Nine broader native regressions pass on the Windows toolchain: surfaces,
  grip redraw, display/settings, frame timing, locomotion, multiplayer/audio,
  profiling, frame pools and Bean modes.
- Watch setup and all retail fog endpoints, CE overrides and a gas-transition
  endpoint pass. Android release compilation passes with JDK 20.

## Remaining evidence

Fresh ADB logs were read on October 12. Quest 3 `2G0YC1ZF8Q0JQ6` still reports
package update time `2026-10-11 21:23:46`; it has not received these changes.
The retained view reports about 8,856--8,876 flushes and 711,500 triangles per
72 game frames, with zero CPU skin poses. It does not demonstrate a moving
tunnel/scope regression on the latest code. All menu replacement mappings
previously decoded at authored dimensions, and the menu capture uses the
headset's internal render dimensions. That does not prove the correct images
are selected in every menu at runtime.

The reflection bindings are not interchangeable diffuse images. Shared prop
materials combine a normal-dependent spot map and environment map; water and
polished floors have other bump/distortion inputs. The existing single-picture
path still omits these extra passes. The earlier audit retains the affected
27 registered asset families and four level-surface families; none are counted
as fixed by this follow-up.

No APK was installed. Repository instructions require an explicit installation
request before installing a build on any device.

## Signed review build

- Clean source: `5c79082b4030d4d8f630bebbe022e07aa1974fd9`.
- APK: `android/app/build/outputs/apk/release/GoldenEye-VR-PR172-Animation-Colour-Fog-5c79082.apk`.
- SHA-256: `b7c8ebb077367bb26cea435eeaa91e8022ccc2eded1e30f35cfdb8b586031216`.
- Native build ID: `ffa05ee609d169d21af6bfd7ba57135772df4a50`.
- Exact symbols: `android/app/build/outputs/symbols/ffa05ee609d169d21af6bfd7ba57135772df4a50/`.
- Signing and packaged source stamp verified; archive metadata records
  `dirty: false`, `installed: false`, `published: false`.
- Release build and JVM test task pass with JDK 20; JVM tests are cached because
  this follow-up has no Java changes. Headset validation is pending installation.
