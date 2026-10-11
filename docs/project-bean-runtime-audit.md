# Bean runtime audit — 2026-10-11

Report `a2167162`, Quest 3, v0.4.20 build `0bf6179`, crashed at
2026-10-11 14:33:35 UTC. Native ELF build ID:
`b7baded40f59e182fbf6079feb4a3e8a1cef68c9`.

The matching archived symbols resolve the stack to
`bgTestHitOnObj` (`src/game/propobj.c`, reading a display-list command),
`propobjFindHit`, `gevrStereoAimChrDepth`, `gevrStereoAimTrace`, and
`gevrStereoAimPoint`. The Bean importer overwrote a model's segmented primary
with a heap display-list pointer. `propobjFindHit` reconstructs a native pointer
as `BaseAddr + (Primary & 0xffffff)`, which then points outside the model.
The replacement must stay outside native gameplay data; a null guard or a
pointer mask would not repair the model/vertex format mismatch.

The corrective implementation keeps native primaries/types unchanged and
selects replacements in the render hooks. Reloads invalidate registrations
by header and overwritten storage range, including GE-X and unmapped loads.
The actual production hit walkers are compiled into the regression harness;
only ray-intersection mathematics are stubbed. Both native list record kinds
must still produce a hit while Bean is on and off.

The DAB comparison uses the complete port at
[`adef1dc2`](https://github.com/DabDavis/perfect-dark-dabs-mod/tree/adef1dc2d4eca1a5c453a7f2bf03a3b6cdad99b8).
The newer `936ac305` changes netplay, not Bean rendering.

Other confirmed omissions corrected in this pass:

- `beanDecodeTextureFrame` reverses image rows before upload; its emitter's
  V reversal assumes this. The VR decoder lacked that row reversal. An
  asymmetric tiled RGBA fixture now checks row order and channel order.
- `beanTexIsPlaceholder`, `beanPlaceholderStandIn` and `beanPlaceholderColour`
  handle missing magenta art using neighbouring cutouts or a neighbouring
  colour, grey when no neighbour exists. Nintendo's one texture is such a
  placeholder. The adapted selection preserves vertex-shader boundaries.
- The opaque renderer ignores stray/fog vertex alpha. Nintendo's stream uses
  a blended pass with no material fade and includes vertex alpha zero. Those
  values must not make its geometry disappear. Material-alpha glass and
  blended level effects retain vertex fading.
- Asset diagnostics used stderr and were absent from exported game reports.
  They now use the persistent game logger.

## Remaining mapping acceptance failures

The user reported wrong/missing geometry and textures in addition to the
confirmed failures above. Finite vertices, valid matrix indexes and registered
filenames are insufficient evidence of correct mapping. The following DAB
paths still require faithful adaptation and rendered comparison:

- Level draws with multiple samplers and UV sets, including masks, blended
  materials, reflection surfaces and position-only/instanced tree geometry.
  The current single-picture triangle representation loses this information.
- Rigid prop screens and recesses (`beanFindScreens`, `beanScreenFit`,
  `beanScreenRecesses`) and dynamic monitor programmes.
- Actual skeleton poses, head seams, switch/part routing and moving prop fits.
- Room ownership under live portals, cameras, fog, water and overlapping rooms.
- Intro-specific render state. DAB's GoldenEye mode skips Nintendo/Rare boot
  logos, so it does not provide a visual acceptance baseline for those screens.
- Animated CAFF pictures and layered reflection shaders.

A release requires visual acceptance in the intro and Dam, then other model,
material and room categories. The `0bf6179` APK is withdrawn. The follow-up
source build is not evidence of completed mapping or DAB rendering parity.

## GE-X / hack ROM coordination

GE-X reserves IDs 3100–4095. `image.c` checks its texture lookup before the
native ROM table, so an overlapping hack table can return GE-X artwork for a
native texture. Converter/pseudo-command fields mask IDs with `0xfff`; simply
moving the reserved range above 4095 will truncate it. The hack-ROM PR needs a
coordinated namespace/provenance solution. Bean's CAFF tiles use a separate
pointer registry, and the crash above is independently established by its
native-pointer mutation.
