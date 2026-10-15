# Bean performance: upstream comparison and next experiments

Researched 2026-10-13 against GoldenEye VR `e59049d68d47f16914d5d36b9de86dae3f1c3aa4` (v0.4.23).

This records the pre-change research snapshot. The first two candidates have since been implemented; see [changes and validation](bean-performance-improvements.md).

## Findings

The large architectural optimizations are already here: retained indexed room/gun meshes, GPU skinning, whole-body batching, opaque material batching across rooms, spatial chunk rejection, cached uniforms, presentation-correct redraws, and early rejection of hidden meshes. Recommending these as new work would repeat completed work.

The clearest missed upstream optimization is **a renderer-specific compiler override**: our CMake disables function inlining globally, while Dab explicitly restores it for Fast3D. The next concrete candidates are **less GL setup in scope/redraw passes**, **fewer submissions for adjacent visible chunks**, and **Dab's cached frontend batch-state derivation**. These need separate Quest A/B measurements; none has a newly measured Quest speedup in this research.

The remaining performance problem has at least two different observed shapes: CPU/driver-heavy turning with XBLA rooms, and a reported gate/sniper view with high GPU time. They should not be collapsed into one explanation.

## Sources actually checked

| Reference | Snapshot | What was inspected |
| --- | --- | --- |
| [GoldenEye VR](https://github.com/MrSco/goldeneye-vr/tree/e59049d68d47f16914d5d36b9de86dae3f1c3aa4) | `e59049d`, v0.4.23 | Current renderer, Bean adapter, build flags, frame pump, profiling tools and measurement notes. |
| [Dab's Perfect Dark mod](https://github.com/DabDavis/perfect-dark-dabs-mod/tree/aae28c8c2176f50d76959955363757cfb4705d24) | `aae28c8`, Oct 13 | Fast3D, GL mesh backend, `gebean.c`, `gebeanstage.c`, room meshes, CMake, `CLAUDE-notes/performance.md` and `gpu-vertices.md`; diff from our pinned `adef1dc2`. |
| [Perfect Dark VR](https://github.com/Alex-LeTux/perfect_dark_VR/tree/7739e3b05fc19049535565bb18d5f71865366c1f) | `7739e3b`, Oct 11 | Current scope implementation; texture/ring changes `f080291`, sampling fix `f758361`, GL restoration fix `b39b12b`, OpenXR frame lifecycle and filter-change guard. |
| [GEVR](https://github.com/no6969el/GEVR/tree/a4315530ad535c67a4df65071929d02a7a581aae) | `a431553`, vr456.8 public docs | Current player/release documents, HD memoization, XR catch-up, and source-track provenance. The shipping renderer's source is not in this public tree; several engineering notes are replaced by removal notices. |
| [GEVR's separate native-host project](https://github.com/no6969el/goldeneye-native/tree/3c26fb29993ecd1f4ba8d1d833c3332f2d6583f6) | `3c26fb2`, Sept 20 | `README.md`, bridge/XR lifetime, vertex-pipeline culling and architecture. This is a separate incomplete host, not proof of the shipping vr456.8 renderer's implementation or speed. |
| [Original GoldenEye decomp](https://github.com/n64decomp/007/tree/c4356466796c697dfd298010b9bed261f9ed8c6a) | `c435646`, Aug 17 | `src/game/bg.c` room/portal visibility and opaque/translucent order; `lv.c` timer/update semantics. |

Important provenance: PDVR's newest scope commit explicitly credits and ports **this project's scope**. It is not an independent faster scope implementation we have overlooked. Dab's current GL mesh backend explicitly excludes GLES, so its desktop results and backend code cannot be transplanted unchanged to Quest.

## What current measurements establish

- [October 13 comparison](vr-tracking-bean-validation.md): three runs/build, Dam pad 32, Quest 3, 120 Hz. With XBLA rooms on, the latest dispatch changes improve some phases by approximately 0.3–0.9 ms but do not improve smooth turning; compositor drops remain approximately 10/s median in that tested condition. CPU skin poses were already zero.
- With XBLA visuals off in that configuration, the tested mesh/skin paths have zero counters. An optimization of these paths cannot explain a speedup there.
- [October 12 tunnel runs](pr172-handoff.md): 2048-triangle chunks and retained mesh GL state improve smooth-turn fresh median from 6.5 to 5.2 ms and p99 from 12.4 to 11.1 ms. The 120 Hz budget is 8.33 ms. At 90 Hz, turn misses and compositor drops are substantially lower. The launcher already advises 90 Hz.
- That same handoff records an actual gate/sniper session with approximately 8–10 ms GPU time and 64–76 fps, not reproduced by the earlier route. It needs its own reproduction, not an assumed CPU-only fix.
- The earlier complete scope probe includes world replay **and lens submission**. Its turning p99 was 14.49 ms; the earlier probe that omitted lens submission is not a complete scope-cost measurement.

These numbers belong to their listed builds/scenes. The older HD/N64-mesh draw-call correlation in `vr-render-stalls.md` is context, not a quantitative model of current Bean performance. Raw Quest traces referenced by the notes are absent from this cloud checkout; these measurements were read, not re-run or independently reanalysed here. ADB reports no attached headset.

## 1. First A/B: enable inlining for the renderer only

**Confirmed difference.** [Our CMake](../CMakeLists.txt) applies `-fno-inline-functions` at directory scope and contains no Fast3D source override. [Dab's CMake](https://github.com/DabDavis/perfect-dark-dabs-mod/blob/aae28c8c2176f50d76959955363757cfb4705d24/CMakeLists.txt#L515) restores `-O2 -finline-functions` specifically for `port/fast3d/*.cpp` and separately for its XBLA mesh adapter.

Dab's [performance account](https://github.com/DabDavis/perfect-dark-dabs-mod/blob/aae28c8c2176f50d76959955363757cfb4705d24/CLAUDE-notes/performance.md) records a substantial main-thread benefit from optimizing the interpreter, along with smaller benefits from frontend input/layout caching. Its reported percentage is from its desktop build/workload and includes changing its previous renderer optimization level; our release is not established to have that same starting point. It is not a predicted Quest gain.

**Experiment:** override inlining on the Fast3D translation units only, after the global option. Verify the actual Android compile commands and instruction attribution, then compare clean baseline/candidate APKs at identical settings. Test Bean C adapter units separately only if profiles justify it.

Keep the decomp/game flags, aliasing assumptions, wrapping arithmetic and floating-point semantics unchanged. Dab documents a real optimization regression where game-defined `sinf`/`cosf` became libc `sincosf`, changing the simulation, and another caused by an uninitialized pad flag. A global optimization/fast-math switch is not the experiment proposed here. Inspect undefined math symbols and preserve gameplay/visual comparisons.

## 2. Remove redundant setup within each scope/redraw pass

**Confirmed current behavior.** In `gfx_opengl.cpp`:

- `gevr_eye_replay_vao()` binds the mesh VAO for every captured draw.
- `gevr_eye_replay_mesh()` re-enables 3 rigid or 15 skin attributes and reapplies culling for every draw.
- `gevr_mesh_uniforms()` already avoids repeated unchanged palette uploads, but still issues both `glBindBuffer` and `glBindBufferBase` every time, including rigid draws.
- `gfx_vr_scope_render()` unconditionally binds each used texture and uploads its filtering uniform on every draw. Main-eye redraw already has pass-local texture/filter comparisons; the scope loop does not.

A research probe extracts the actual production helper functions and scope inner loop unchanged, supplies identical synthetic draws and counts mocked GL calls:

| 100 identical draws | Rigid redraw | Skinned redraw | Rigid scope | Skinned scope |
| --- | ---: | ---: | ---: | ---: |
| VAO binds | 100 | 100 | 100 | 100 |
| Attribute enables | 300 | 1,500 | 300 | 1,500 |
| UBO binding calls in redraw helpers | 200 | 200 | — | — |
| Palette uploads in redraw helpers | 1 | 1 | — | — |
| Texture binds, two used textures | — | — | 200 | 200 |
| Active-texture calls | — | — | 200 | 200 |

The mock programs expose all tested uniforms. These are **submission counts, not timings, a live draw distribution or an FPS estimate**. They isolate avoidable repetition even with one program, one mesh and unchanged state. Existing caching is already preventing repeated palette upload; do not reimplement that as a new discovery.

**Experiment:** pass-local VAO/attribute/cull tracking and scope texture/filter tracking. Move UBO binding to a known ownership boundary, or maintain a binding cache that is invalidated at every external mutation. Shader-local filters must reset on program transitions, including A → B → A. Attributes must be repaired after `gfx_opengl_unload_shader()`, which currently disables attributes on the bound VAO. Preserve mixed CPU/mesh draws, both scopes, reflections, mirrored winding and GL restoration at pass end.

Both Dab's `gl_mesh_leave()` and PDVR's `b39b12b` reinforce the same ownership rule: cache inside a controlled pass and restore/invalidate at renderer boundaries. Our shelved global texture-cache experiment is explicitly unsafe across hub bindings. Do not apply that patch wholesale. Its introductory assertion that the scope already deduplicates is not true of the current scope loop.

This primarily targets scope/redraw submission. The effect on the heavy **fresh** turning frames must be measured separately.

## 3. Reduce draw submissions without undoing spatial rejection

**Already implemented:** opaque rooms are merged across room boundaries, material/spatial sorted, and split into 2048-triangle chunks. Transparent geometry keeps the original room order. Small props below the measured 256-triangle threshold remain on the CPU to share batches.

**Upstream warning worth retaining:** Dab's `gpu-vertices.md`, "It is not a win everywhere", records a GF64 view at 1,169 draws with GPU vertices versus 841 with CPU vertices, and higher game-thread cycles. It identifies 462 individual room runs plus per-draw parameter/palette/colour work. Moving triangles to the GPU can lose when it fragments shared CPU batches. This supports our small-prop threshold and argues against making every prop an independent retained draw.

**Experiment:** identify adjacent **visible** opaque runs with contiguous index ranges and identical material/render state, and coalesce their submissions. Never include a rejected gap or cross a state command, model transform, decal constraint or transparent-order boundary. Recompute/retain conservative scope bounds and ownership for any coalesced record. Count eligible pairs on the actual expensive view before implementing a complicated multi-draw path.

This is a candidate, not a missed feature already proven by Dab. Simply increasing the native triangle-buffer cap is not equivalent: PDVR uses 1024, our native path uses 256, while the retained opaque path has its own 2048 cap. Dab explicitly found its native cap was not the limiting flush cause.

## 4. Adopt Dab's frontend state caching where CPU work remains

**Confirmed difference:** Dab has `gfx_derive_batch_state()`, `gfx_mark_state_dirty()`, resolved emit-input kinds and per-batch vertex-layout templates. Our `gfx_sp_tri1()` still derives combiner/depth/texture inputs and handles input selection in its emission path. Retained meshes invoke it once as a dummy state probe per run; CPU fallbacks still invoke the triangle path normally.

Dab also documents a subtle trap: checking both texture-dirty bits blindly defeats caching because an unused texture unit's bit may stay set permanently. Only relevant used units should trigger derivation. Its `GFX_VERIFY_BATCH_STATE` mode re-derives the uncached state and checks it against the cache, counting both checks and failures.

**Experiment:** measure current fresh-frame time in state derivation/emit input work, then isolate a state-preparation cache with that verifier approach. Include all GoldenEye/Bean extensions, texture dimensions/animation, reflection inputs, fog transitions, scopes, captures and externally changed GL state. A zero mismatch count is meaningful only with a substantial executed-check count in gameplay. Do not port all of Dab's renderer or assume every desktop micro-optimization applies to AArch64.

## 5. Compact capture records and investigate palette streaming if measured

The native probe reports `sizeof(GfxMeshDrawParams) == 4032`, including a 3080-byte `GevrSkinPalette`. Both the eye mesh record and scope mesh record copy that struct, including its zero palette for rigid room runs. Palette upload is cached, but the capture copies remain.

**Candidate:** separate rigid material/transform state from an immutable per-instance/frame palette, and capture an owned stable handle/index rather than duplicating it per material run. A ring of palette allocations could later avoid repeatedly replacing one shared UBO when alternating instances. Measure capture copy bytes/time, UBO uploads and instance transitions first. Raw frame-arena pointers are not safe: retained redraws must outlive the producing frame, and different instances can reuse the same source address. Our value-owned palette is a correctness feature to preserve.

Dab's GL path uses per-draw parameter data and a palette reference with frame-aware caching; its Vulkan backend streams per-frame data. Neither is proof that the same streaming design wins on Quest GLES.

## Other upstream lessons: what to keep and what not to repeat

| Topic | Cross-reference and consequence |
| --- | --- |
| Visibility versus AI | Original `bg.c` uses N64 portal visibility for gameplay and draws transparent rooms in reverse order. Dab documents HD geometry extending beyond those portals. Our separate native visibility flags and added Bean room bounds already address this. Do not restore native portal-only rejection or use all rendered HD rooms to make every guard "on screen". |
| Eyes, late poses and scopes | PDVR and the native-host bridge both account for eye frusta. Our chunk tests include simulation/presentation views, stereo margins and scopes; existing tests pass. Preserve the union rather than making culling cheaper by excluding an eye or the scope. |
| Backfaces | Dab preserves two-sided decks, roofs and stairs, with special backed/overlap handling and camera-dependent outside culling. Our Bean room path is deliberately two-sided. A blanket backface-culling toggle can remove valid geometry; it is not a safe universal performance fix. |
| Seams | Dab explicitly avoids native seam sealing on welded HD rooms after observing a large CPU cost. This project's Bean path does not run Dab's seam-sealing mechanism, so there is no missing "disable sealing" fix to port. |
| Texture cache | Dab grows its cache when the working set would evict recently used textures; GEVR keeps decoded HD images in a bounded memo. Our cache is already 4096 entries; Bean's decoded table is bounded at 768, with separate native/repaint users. Confirm hot evictions and upload bytes before changing it, and use a Quest memory budget rather than GEVR's desktop 1 GB memo setting. |
| Texture loading | Bean already retains decoded RGBA and GPU cache entries, but first GPU texture/mesh upload and shader variant creation still occur on demand. Our async PNG-pack preload path is separate from Bean's own texture import. Warm-turn stalls must be separated from first-visit compilation/upload hitches before adding preload work. |
| Mipmaps/filtering | Bean takes `upload_texture`, not the PNG-pack `upload_texture_hd` path. The existing handoff says a mipmap experiment did not improve the measured GPU route. Do not promise it fixes CPU turn spikes or apply the pack's anisotropy blindly. A differently reproduced GPU-bound gate view can be measured on its own. |
| GPU lifetime | PDVR's streaming ring is not a replacement for our retained-eye lifetime contract. Our fenced ring and GPU drain only on failure are already tested. Keep storage alive through eye/scope/replay consumers; do not remove fences to improve a timer. |
| Frame lifecycle | PDVR handles `shouldRender=false` and frame closure; the separate GEVR host says simulate once/render per eye. Our multiview and 60 Hz simulation with new-pose redraws are already implemented. GEVR's "XR catch-up" is a useful timebase check, not authority to run GoldenEye simulation once per eye or once per headset frame. |
| Engine/backend replacement | Dab's GL GPU-mesh support explicitly requires desktop GL; its Vulkan metrics and GEVR's RT64/native-track docs are not a benchmark of our GLES adapter. No evidence here justifies replacing the engine/backend before testing the isolated candidates above. |

## Measurement plan for accepting changes

1. Reproduce **two** baselines: warm XBLA-room turning (Dam pads 32/285) and the reported gate/sniper view. Include original/Bean-only controls, one/two scopes, fixed yaw/pose and acknowledged runtime settings. Use the same owned assets and quality settings.
2. Build one change per candidate with a clean identity. Use the existing profiler's backup/run/restore flow; no change to player saves or calibration. Run three paired repetitions initially, increasing only if variance leaves a real decision unresolved.
3. Keep the headset rate fixed within each comparison; compare at 120 Hz and separately at 90 Hz. Do not average fresh and redraw frames together. Record CPU self/inclusive sections, asynchronous eye/scope GPU work, real draw submissions, state calls, uploads/compiles, and compositor drops. Existing aggregate GPU measurement includes scope work; it does not independently attribute the lens cost. A separate asynchronous scope GPU query would resolve that attribution.
4. Separate cold first visits from warmed steady scenes. Keep detailed per-triangle profiling frames out of headline timings, but report their counts. Keep ShowStats off; thermal/device performance state and render resolution must be comparable.
5. Accept only a measured improvement beyond scene/run variation with complete bodies, guns, both eyes, scope image/lens, transparency, decals and tracking intact. Check rendering and gameplay independently: a screenshot/pixel comparison does not prove simulation identity, and a state/replay test does not prove pixels.

Recommended implementation order: renderer-only inlining A/B; scope/redraw state setup; visible-run coalescing; then state-derivation caching or capture compaction according to the next profile. Hot-cache growth and shader/texture prewarming are conditional on their counters showing the problem.

## Validation performed during this research

Eight existing suites passed: `test_mesh_uniforms.py`, `test_mesh_dispatch.py`, `test_mesh_visibility.py`, `test_skin_shader.py` (22 production shader variants compiled/linked and executed on Mesa GLES), `test_mesh_chunks.py`, `test_frame_timing.py`, `tools/perf/test_analyze.py` and `tools/perf/test_quest_profile.py`.

The additional research probe extracts the production mesh redraw helpers, uniform function and scope inner loop into a mocked-GL program. It confirms the call counts and struct sizes above; it does not emulate the Quest driver or supply GPU timings. Probe/source manifest/results are saved under `/workspace/scratch/bean-performance/` (`probe_mesh_replay.py`, `sources.json`, `mesh-replay-calls.json`, `checks/`).

No runtime code/build setting was changed, APK built/installed, save modified, release published or new headset performance result claimed by this research. The colored water-line and Bunker-ending issues are resolved per the user and are not reopened here.
