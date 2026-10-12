# PR 172: scope, Dam chunks, logo and glass follow-up

## Headset evidence

Read Quest logcat over ADB on 2026-10-11 and saved it under ignored
`build/headset-followup/`. The app identified itself as `a47c01a`. The available
stationary view had roughly 407 draws / 31K triangles, DRAW/TICK around 7–9 ms
with peaks around 10–11 ms, and fresh-eye GPU work around 2.4 ms. Per 72 frames
it logged 7056 mesh draws / 1,458,144 GPU triangles, 432 CPU runs, and 144 CPU
poses. There were no GPU skin draws in that view. This does not establish whether
guards were visible; tracked arms/watch still have CPU fallback.

The device's Bean marker was `2026.10.11.2`, while the launcher pins `.3`, which
includes the additional menu/HUD/effect texture exports. The user confirmed they
had forgotten to update the pack. No pack or APK was installed by this work.

The retained log window did not include the reported tunnel/scope transition.
There is no measured frame-time improvement for this follow-up yet.

## Changes

- Scope recording no longer forces static rooms or supported character skins
  through CPU vertex/triangle emission. It captures indexed VAOs and complete
  per-draw parameters, including owned palettes, and replays mixed CPU and GPU
  draws in order for each hand. Scope winding undoes the eye target's Y flip.
  Unbounded skins are retained conservatively for both active scopes.
- Large room material runs split at a vertex-load boundary after about 512
  triangles. Index/material order stays unchanged. Each chunk has bounds;
  rejection retains the union of original eyes, stereo margins, presented eyes
  and active scopes. Boxes crossing the camera plane or with nonfinite clip
  coordinates are retained. `mesh-cull:` reports rejected chunks/triangles.
- Nintendo's Bean geometry uses the cartridge logo's native reflection
  texture/tile/combiner setup. Fitted normals feed `G_TEXTURE_GEN` and lighting,
  making reflections move with rotation. Opaque alpha and depth writes prevent
  the earlier see-through regression. The chrome list remains on the CPU
  because generated UVs are outside the mesh shader's supported state.
- Native type-4 models submitted only in the transparent pass can now draw
  their self-contained Bean replacement. This fixes the missing breakable
  `PwindowZ` pane. The real Bean pane already has texture alpha 119 and material
  alpha 142; its alpha was not the reason it was absent. Hidden/cleared native
  lists remain hidden, and a combined primary/secondary call draws once.

## Validation

- Android release build and 36 JVM tests with JDK 20.
- ASan/UBSan Bean tests, including the actual model render dispatchers for both
  display-list node kinds, flags 1/2/3, cleared lists and chrome setup.
- All 457 actual-ROM/Bean model registrations and 26 stage checks, including
  the logo's real cartridge texture setup, generated UV state and normals.
- Real-asset material checks: logo opaque, Dam material alpha does not clear
  water, and decals retain their backing.
- ASan/UBSan production chunk builder: exact triangle/state order, bounds,
  deduplication, stereo margins and camera crossing.
- ASan/UBSan production scope capture/replay: mixed indexed/ring VAOs, saved
  palettes, both hand masks, upright scope winding and scope/CPU clip parity.
- Production chunk-visibility helper: original/presented eyes and scope union.
- Mesa GLES: 16 production shader variants compile/link; rigid and skinned
  scope positions match camera reconstruction with inverted eye Y, in addition
  to existing skinning/lighting/fog/palette/culling checks.
- Existing surface, grip redraw, display, timing, locomotion, multiplayer,
  profiling, dynamic arena and Bean mode regressions.

GL replay uses a state-recording fixture; shader execution uses single-view
Mesa with only OVR declarations/view selection substituted. Neither establishes
Adreno multiview visual correctness or headset performance.

## Next headset round

Use Bean pack `.3`. Check mission thumbnails and folder backgrounds, spinning
chrome logo, first Dam tower glass (including breaking it), tunnel approach and
sniper scope. Capture the same route with stats on. Scope-active views should
retain `mesh:` GPU draws and supported guard skins instead of making all runs
fall back. Compare DRAW/TICK, GPU times, `skin:` CPU poses and `mesh-cull:` counts.
Rigid guns/props and tracked first-person culling overrides still cost CPU work.

## Signed candidate

Release-signed `GoldenEye-VR-PR172-Render-Fixes-25c1c1e.apk` was built from
clean commit `25c1c1e`. SHA-256:
`7d9a787e93f6f621d76b35ff9cee8545dc0ff0d0c162715d4969102c999eea0b`.
The APK embeds that commit and is archived with its matching `libgevr.so`,
full native symbols and build metadata under
`android/app/build/outputs/symbols/617800c72e33febfe8427cd593f2a54bfeec2ab6/`.
It was not installed or published.
