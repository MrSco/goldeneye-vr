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

## Follow-up report 22ffdb80

Quest 3 build `c220561` no longer crashed in the reported test, but showed
missing Dam surfaces, multiple arms, stretched necks, incomplete characters,
native-resolution menu textures and poor performance. The screenshot and
exported log confirm the renderer was still incorrect. The log loaded Dam's
110,448 triangles and 3,400 sky triangles, so missing mesh downloads were not
the main cause.

Subsequent source fixes (headset verification still outstanding):

- DAB's `gen_stagetable.py` explicitly targets geometry divided by GoldenEye's
  level scale. `bgSetScale()` stores that reciprocal in `room_data_float2`.
  The native adapter must therefore use `DAB scale / room_data_float2` before
  room assignment and subtract the native room position. Applying the DAB
  table directly made Dam 4.28 times too large. The former file comment was
  wrong. With the correction all 1,985 sampled Bean original Dam vertices are
  within two units of native geometry. All 26 stage entries now test their
  original Bean geometry against the actual cartridge's room surfaces.
- Rigid stride-32 room vertices store their lit colour at +28, not +20; the
  latter is their second UV set. An asymmetric layout fixture catches this.
- Bean solid rooms now follow DAB's two-sided default. Special overlap and
  outside-camera culling remain unimplemented.
- GoldenEye retains a pelvis GROUP (joint 1) below its HEADER at the same rest
  origin but with a separate rotation matrix. DAB's converted root folds it
  away. Classifying that extra pelvis as BACK shifted the true back into a
  leg slot and dropped an ankle. It now remains BASE. Native body probes
  check actual joint identities and every torso/limb section's triangle count.
- Triangle section selection now follows DAB's total influence weight across
  all three corners rather than the strongest weight at one corner.
- Bean honours native list hiding, and GE-X owns the whole watch-arm path
  while enabled. Its native watch pieces and switches must stay available;
  Bean combines those pieces into its wrist list, so hiding that list alone
  would also hide the dial. The original arm is no longer resurrected during
  GE-X's watch draw.
- Transparent-only model passes no longer compute an unused Bean CPU pose.
- Launcher Mods combines selection and downloads into one row per pack. All
  download states, update actions and errors fit at Quest font size in a
  1280x960 ImGui layout check; Done is fixed at the bottom.

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
