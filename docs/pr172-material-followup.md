# PR 172: watch palette, Dam materials and rigid weapon buffers

## Evidence from the user's next test

The user tested `25c1c1e` and confirmed Nintendo's chrome and the Dam tower
glass work. The attached tunnel photograph shows striped road paint; the watch
photograph shows an opaque pale dial and white gauges. Their XBLA reference
shows the intended dark green dial, green page indicators and coloured gauges.

The performance photograph reports XR CPU work up to 12.3 ms, DRAW/TICK 12.8 ms,
game CPU about 1.5 ms and fresh-eye GPU work about 1.9 ms. The available ADB log
window starts **after** that photograph, at 21:32:40. Per 72 frames it records
936 room-mesh draws / 517,248 GPU triangles and 432 skin draws / 272,448 GPU
triangles, with zero CPU skin poses. It does not include a moving tunnel run or
scope transition. These counts confirm GPU skinning is active in that retained
view; they do not prove the tunnel's remaining bottleneck or an FPS improvement.

Read the Quest's Bean marker over ADB: `2026.10.11.3`. Its walletbond CAFF was
also pulled read-only and matches the tested local asset, SHA-256
`74f0b2273d6eec2c44b7399f92d4cede6cb81f2a2cdfb48e2e8a61b6474369e0`.
Logs and private asset diagnostics remain in ignored `build/headset-round3/`.
No APK was installed and no pack was changed on the device.

## Confirmed integration faults and fixes

- **Watch glass:** material input 10 is opaque grey; input 11 is black with
  approximately three-eighths alpha. Follow DAB's `beanArmGlass` selection:
  the real suitlfhand file now emits 135 translucent glass faces on texture 11
  and zero on texture 10. Outfit switches still draw the glass once.
- **Watch palette:** the lit Bean arm left `G_LIGHTING` enabled for native
  RGB menu vertices, interpreting their colours as normals. The watch page
  setup explicitly clears lighting, texture generation, fog and depth and
  disables texturing before drawing the existing green background and gauges.
  The cartridge's authored gauge and page colours remain in use.
- **Road paint:** material sorting could draw paint before its base; both
  translucent and cutout branches ignored decal flags. Sort base surfaces
  before decals, preserve equal-key triangle order, use decal depth modes for
  translucent/cutout paint and write depth for solid decals. Dam's actual
  four vertex-alpha paint faces retain their decal flags. This addresses
  depth fighting; headset confirmation is still required.
- **Room fog:** Bean lists bypass the native room render-mode conversion.
  They now use two cycles, preserve picture/vertex alpha, and choose fog versus
  a plain pass at draw time. Static GPU meshes accept fog as well as skins.
  Dam additionally uses the verified retail XBLA linear fog, from -100 to
  15,000 world units on the plateau, colour `85adca`, extending by up to 65,000
  units lower down. Its depth includes the level visibility scale; its height
  excludes Perfect Dark's conversion offset. Other stages retain native fog
  ranges and colours, including their environment transitions. Full HD fog
  tables, Community Edition overrides and object fog are not ported here.
- **Water motion:** read rendergraph scroll entries, resolve the unique
  transparency input in a multi-input material, and apply normalized offsets
  on both CPU triangles and GPU meshes. Dam texture 71 moves at the file's
  0.5 repeats/second; emitted T is reversed. Game time drives it, and captured
  scope/redraw parameters own that frame's offset. This implements texture
  scrolling, not the release's full multi-input water shader.
- **Inventory preview:** replacement lists load their own part matrices, while
  the native preview initializes only selected joints. Initialize each rigid
  replacement under its fitted rest pivot and the preview root. The Bean dial
  also writes depth, so the preview clears its depth before drawing with depth
  enabled. GE-X and disabled-Bean paths keep their existing poses.
- **Rigid weapon CPU cost:** use the existing retained indexed mesh path for
  rigid Bean gun/glove lists, retaining material commands and CPU fallback.
  Their two-sided lists have no cull bits, so the first-person cull override
  does not require CPU emission. Cull overrides on lit/skinned arms still
  fall back; the existing back-face depth push is preserved there. Buffers are
  released with the level's replacement allocations.
- **Repaint capacity:** an already-bound address can replace its picture even
  when the repaint table is full. New addresses still obey the capacity limit.
  This is a confirmed boundary defect, not proof of the user's thumbnail fault.

The missing/low-resolution menu report is still a runtime question. All 84
folder repaints and 121 mapped image repaints pass on the user's assets. Added
bounded image-load messages and periodic `bean-paint` binding/hit/upload counts
so the next headset log can distinguish missing files, registration failures
and successful uploads. Those checks do not prove every menu element looks
correct or that all material selections throughout the game are correct.

## Validation

- Android release builds with JDK 20; existing 36 JVM tests pass.
- Real-file registration audit: 457/457 model mappings and all 26 stage rows;
  address/undefined sanitizers enabled. Includes actual rigid weapon buffers,
  fitted preview transforms and matrix ownership.
- Real watch material, logo opacity and Dam alpha/scroll/paint checks pass.
- Production watch setup test preserves green RGB after a lit model; Dam fog
  endpoint/height/visibility-scale checks pass.
- Production emitter test preserves base-before-paint order, cutout/translucent
  decal depth and alpha under fog.
- All 16 production mesh shader combinations compile/link on Mesa GLES;
  execution checks weighted poses, native and linear HD fog, and scope position.
- Scope capture/replay retains owned fog parameters and palettes. Chunk bounds,
  visibility and the nine existing renderer/gameplay regression scripts pass.

These are offline validations. The new build needs a headset test of watch
pages and guns, Dam tunnel/road paint, reservoir motion, fog and frame times.

## Build

The signed APK and matching native symbol archive are recorded below after the
clean source build. No headset installation is authorized by this task.
