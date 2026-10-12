# PR 172: renderer repair and Quest validation

Updated October 12, 2026. This supersedes the earlier eight-fault checkpoint;
that historical count is not a current whole-game completion claim.

## Implemented

- GPU skinning, owned per-instance palettes and indexed scope/redraw capture.
  Uniform uploads are cached by program/context; the bone palette belongs to
  the draw that captured it.
- Float room positions and normals avoid signed-short quantization. Opaque
  world materials are batched into spatial chunks, with conservative view/eye/
  scope culling. Transparent geometry keeps its original room ordering.
- Character bodies batch materials across limbs while preserving the native
  joint walker, heads, hats, weapons, hit geometry and visibility switches.
  Both US/JP and EU gameplay joint walkers emit the body once per instance.
- Guns use retained geometry. Small props stay in shared CPU batches; the
  measured default retained-prop threshold is 256 triangles.
- Watch glass selects the authored translucent black input. Native green text
  and coloured gauges survive lighting, the two light faces draw, and inventory
  previews have their fitted transform and separate depth clear.
- Authored RGB, UV scrolling/rotation and running texture frames survive both
  CPU/GPU rendering, scopes and XR redraws. Dam reservoir UVs move at the
  authored 0.5 repeats per game second; the Complex beacon changes frames.
- HD linear fog covers all nineteen retail environment states, CE overrides,
  Dam's height-dependent range and the native gas/sky transition. Sky lists
  explicitly clearing fog stay clear. This is source validation, not a visual
  sign-off of haze in every scene.
- Normal-dependent prop reflections combine the spot/environment inputs.
  Scratch/glass reflections retain diffuse alpha on soft and cutout draws.
  CCTV's reflection also includes diffuse RGB. Helmet materials interpolate
  toward c13 grey and compute alpha separately using c12/c13/c14.

The reflection families are derived from supplied material bindings/constants
and compiled shader instructions, compared with the MIT DAB reference pinned
at `adef1dc2d4eca1a5c453a7f2bf03a3b6cdad99b8`. These are bounded shader forms,
not a general XBLA shader interpreter. Specular light and bump/distortion
remain incomplete.

## Whole-pack evidence

The current sweep reads all 987 CAFF files, 7,880 texture records and 25,769
material records with zero failures. No loaded mesh has a missing texture
binding; four empty dynamic-screen containers keep the native path. All 457
native registrations and 26 stage catalog rows pass ASan/UBSan checks.

The earlier reflection census identified 27 registered asset candidates.
The current loader emits 4,471 normal-reflection faces across 20 of those
families. This includes only supported materials, not every face/variant in
those twenty assets. Seven candidates have no such emitted faces: Bunker,
Frigate, Grant's head, ICBM, desk lamp 2, Seawolf and Jeep. Some shader constants
disable reflection intentionally. See [current census](pr172-reflection-census.json).
Four special level-surface families remain incomplete: Dam and Complex water,
Silo's polished surface and Control's marble. Coincident geometry remains a
candidate inventory, not permission to delete intentional two-sided sheets.

The 84 folder and 121 HUD/menu mappings pass at their authored dimensions.
Quest menu captures and binding logs show replacements in use, including
file-select paper/crest, Dam filmstrip and difficulty pages. This does not
certify every menu image or turn authored small images into new high-resolution
art.

## Valid Quest measurements

Quest 3 `2G0YC1ZF8Q0JQ6`; Bean pack `2026.10.11.3`, user's quality settings,
120 Hz display and 60 Hz simulation. Save/configuration files are restored
between runs. Benchmark controls must be acknowledged by the renderer.
Source `95dc1f2` is the first valid whole-body comparison; captures show full
bodies, hands and weapons. Pad 32 is a guard-post view. Pad 285 is the curved
road tunnel from the user's report, confirmed by a headset capture.

Fresh-frame app processing times, in ms:

| Test/phase | Batching off median / p99 | Batching on median / p99 |
| --- | --- | --- |
| Guard post, forward | 8.41 / 9.36 | 7.08 / 8.72 |
| Guard post, turning | 10.42 / 18.72 | 8.04 / 12.46 |
| Tunnel, stationary | 6.55 / 8.67 | 5.21 / 7.26 |
| Tunnel, forward | 6.01 / 8.24 | 4.90 / 7.00 |
| Tunnel, backward | 7.04 / 9.60 | 5.55 / 7.52 |
| Tunnel, turning | 6.05 / 18.09 | 6.32 / 12.33 |

These compare world/body batching together at prop threshold 256. The guard
post candidate has two runs; tunnel comparisons have one run per setting.
NPC movement/contact and sampling variation limit interpretation. The first
tunnel candidate used detailed sampling; headline analysis excludes detailed
frames and transitions. Stationary guard-post p99 varied between candidate
runs. No whole-game FPS guarantee follows from these measurements.

Material build `eda055d` with batching on also completed a 60-second tunnel
trace. Fresh forward median/p99 was 4.82/6.47 ms and turning 6.66/13.24 ms.
GPU fresh median/p99 was 1.43/3.75 ms. Fast turns still miss 120 Hz frames;
the task is not complete. Captures show a green watch and complete sniper
preview. The automated tower-pad capture looked into a wall and does not
verify the new glass reflection. The user earlier verified the chrome logo
and visible tower glass in `25c1c1e`.

The profiling driver's explicit `--scope-probe` uses the existing
`gevr_fakegrip.txt` hook, equips the sniper and exercises both world replay
and XR lens submission. A capture shows the rendered tunnel through the lens.
Source `07f96dc`, same tunnel/settings, one moving minute: fresh forward
median/p99 6.43/7.98 ms; turning 7.70/14.49 ms; GPU fresh median/p99 2.60/4.52 ms.
This is a synthetic-pose scope test, not physical-controller tracking validation.
The earlier `d373dea` probe exercised image replay but omitted lens submission;
its timings must not be used as the cost of the complete scope path. That
redundant hook was removed in favour of the existing grip implementation.
Probe files are protected by the backup manifest and removed/restored afterward.

### Discarded comparisons

- Before `f169043`, relative marker paths did not read the driver's Android
  files-root controls. Intended A/B settings cannot be inferred from labels.
- `f169043` body-on tests skipped body limb lists but did not emit them in the
  gameplay joint walker, producing floating heads. Their timing gains are
  invalid and retracted. `95dc1f2` fixes both gameplay variants and verifies
  complete bodies. Do not reuse those old body-on traces as performance proof.
- Some old labels said tunnel for pad 32. Location comes from captured geometry,
  not the label.

Ignored local evidence is under `build/perf/pr172-follow-up/`, including CSV,
GPU timings, input events, screenshots, device logs and APK identities. Analysis
separates fresh/redraw frames and excludes reset/detailed-frame overhead.

## Validation and build

- 22 production GLES shader variants compile/link. Executed checks cover
  weighted skinning, lighting/tint, UV rotation, fog, scope projection, winding,
  saved-palette replay, reflection RGB and soft/coloured alpha on CPU/GPU paths.
- Real watch/water/decal/animation/prop material checks pass with ASan/UBSan.
  Native joint-walker, emitter, scope ownership/replay, program uniform and
  two-hand and lost-tracking/grip-redraw regression checks pass.
- JDK 20 Android release and benchmark builds pass. The existing 36 JVM tests
  remain up to date; no Java changed in this renderer follow-up.
- Clean signed material release checkpoint: `16f18ade0126b757a6eb060385618b0ac0d23722`.
  APK: `GoldenEye-VR-PR172-Coloured-Reflection-16f18ad.apk`.
  SHA-256: `37a4e1d5e34682bf1d9d273821ea0939af6333f2f7c8a79544d19351bf99849f`.
  Native build ID: `46d0e5ca26ca7f2373c61f1addf66b9217173421`.
  Subsequent clean build identities and installation receipts are kept beside
  the exact unstripped symbols and signed APKs, archived locally under
  `android/app/build/outputs/symbols/<build-id>/`.

Installed checkpoint: clean source `e4367b706173e0de6a5263382f5f3fececf585d0`,
`GoldenEye-VR-PR172-Renderer-Followup.apk`, SHA-256
`30c20debfd31c3a73c1a29409f29686c377be8c492f2467a62cb1aff9be4a6c2`,
native build ID `c56435e6df2a60cb6a17b1a38b779c851cd577ee`.
The installed APK was pulled back and its hash matched. Full backup restoration
verified save/settings and all protected markers byte for byte (or absence).

Installation of PR 172 test builds was explicitly authorized by the user.
No APK was published and this PR is not merged.

## Work still open

Reproduce the special water/floor shaders and remaining prop variants; verify
road-paint depth fighting and distant haze in moving headset views; reduce
the measured active-scope turn spikes; complete remaining material/menu
visual checks. A whole-pack decode pass is not a visual whole-game pass.
