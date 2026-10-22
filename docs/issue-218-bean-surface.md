# Bean Surface rendering investigation (#218)

The report shows purple pine spikes, a line over the forest wall, a floating
radar dish and a hard circle in the sky with Bean and Community Edition v6.
Three missing renderer behaviours match the upstream implementation and the
reported foliage/sky symptoms. The actual scene has not been run here.

## Changes

- **Pines:** Surface and the Bunkers instance a shared vertex buffer. Its first
  copy has no instance record. Branches (stride 20) have no stored UVs; trunks
  (stride 28) contain a tangent and binormal where the normal rigid reader
  expects UVs and colour. The binormal was used as the trunk's tint. Mark all
  copies of instanced buffers, read their actual final colour word, and generate
  branch-card and cylindrical bark UVs in local space before the instance
  matrix. Trunk seams and cone tips retain continuous UVs.
- **Forest outline:** The clear top of a forest-wall texture sampled the snowy
  bottom through repeating V. Clamp the vertical axis of alpha textures whose
  complete geometry occupies one repeat, allowing the upstream 1/32-repeat
  slack. Keep U repeating. Clamped triangles share a texture origin, including
  the decoder's V reversal; rebasing each triangle would flip picture edges.
  Textures spanning multiple V repeats keep wrapping.
- **Sky circle:** CE cloud-cap photographs lack the retail cap's alpha. Blend
  the cap over the dome with the upstream radial fade (whole through radius
  0.33, gone at 0.475). Preserve texture-authored alpha without applying a second
  fade. Clamp both cap axes and dome V; the dome's U still wraps at the horizon.

Algorithms adapted from Dab's Mod at
[`aae28c8`](https://github.com/DabDavis/perfect-dark-dabs-mod/tree/aae28c8c2176f50d76959955363757cfb4705d24):
`gebean.c` `beanTreeMeasure`/`beanTreeUvs`, `gebeanstage.c` `clampCutouts`, and
`gebeansky.c` `skyCapFade`/`skyBuild`. License: `port/licenses/dabs-mod.txt`.

## Dish/support follow-up and existing fixes

Following the Runway path further reproduced another missing-part failure.
The Runway fix `b6164e1d` excludes listless groups from bone ownership, but
`emitRigid()` still rejected a drawable list node with no native vertices.
A fitted Bean replacement already supplies its vertices, scale and local
origin, so this rejected its support/body after binding succeeded, while the
moving assembly continued to draw.

The emitter now accepts these native carrier nodes for fitted replacements.
Unfitted replacements still require native vertices for bounds-based fitting.
The change also avoids reading native vertex bounds on the fitted path.

The weighted support fixture now covers both native display-list node kinds,
with and without original support geometry. The empty-support case fails the
nonblank-support assertion before the emitter change and passes afterwards.
It executes the actual model dispatchers with a null native list, verifies
that the replacement support draws once, checks separate support/moving-part
matrices, and retains native primaries and Bean-off behaviour. The earlier
plane fixture still checks near/far hull lists and native propellers. The
listless-root binding guard, body selection, LOD, hit geometry and vertex-ring
rules are preserved.

This is a reproduced missing-support renderer defect and a candidate repair
for the reported dish. Matching the screenshot to this exact native model
layout remains a visual/real-asset acceptance check; the authored fixture is
not a dump of `PsevdishZ`. Check both Surface missions with CE enabled and
disabled, including dish support alignment and yaw/tilt while moving through
its LOD distances.

## Validation

- `test_bean_surface.py` sends authored CAFF files through the full production
  loader: stride-20 branches, stride-28 bark, the original copy, a rotated and
  translated instance, and the identity restored by record 0x25. The unchanged
  loader fails the generated branch UV assertion; the patch passes. ASan/UBSan
  also passes.
- `test_bean.py` checks sky blending/alpha, sampler clamp axes and common UV
  origins, static-mesh capture, empty-native support carriers and production
  dispatch, weighted support/part ownership, plane LODs and
  propellers, native aiming/hit walkers, room mapping and the Facility decal
  regressions. Also exercised under ASan/UBSan.
- Node-state, US/EU joint walkers, camera culling and Mirror Mode checks pass.
- Android `assembleDebug testDebugUnitTest`: build succeeds, 31 JVM tests pass;
  `apksigner verify` succeeds. Version/protocol unchanged.

The host GCC fixtures need an external compatibility shim preincluding
`stddef.h`/`stdarg.h` and making the existing incompatible-pointer diagnostic a
warning. This does not change production hit walkers or weaken assertions.

No ROM, extracted retail/CE assets or connected Quest is available here. No
real-asset sweep, headset visual acceptance or performance measurement is
claimed. Keep #218 open pending visual acceptance of the support repair and
both Surface missions, retail Bean, CE Bean, Original, and the earlier Runway,
Jungle, Frigate and Facility scenes.
