# Bean performance: first implementation pass

2026-10-13. Branch `codex/bean-performance`, based on v0.4.23 / `e59049d`.
Research and upstream source references: [Bean performance research](bean-performance-research.md).

## Changes, in priority order

1. **`5d7025c`: renderer compiler optimization.** Restore function inlining for the eight Fast3D C++ translation units in optimized Android builds, following Dab's scoped compiler override. Keep the existing `-O2`, aliasing, integer wrapping and floating-point settings. Game/decomp and Bean C files retain their existing flags. Debug builds retain their previous flags. CMake option `GEVR_FAST3D_INLINE=OFF` supplies a compiler-only control.
2. **`5886849`: scope/redraw submission overhead.** Reuse VAO, enabled mesh attributes, clip/cull state and palette-buffer bindings within one controlled pass. Scope texture and filtering setup now follows the existing redraw approach: bind changed textures and refresh program-local filtering after every shader switch, including A → B → A. Each hand starts a fresh cache. Outside replay, each mesh call still explicitly establishes its palette binding; `glBindBufferBase` supplies both indexed and generic UBO bindings without an extra `glBindBuffer`.

Draw order, index ranges, visibility rejection, saved palettes, both scope masks and mirrored winding are retained. Program changes invalidate the attribute cache because unloading a CPU program can disable attributes on the destination mesh VAO. Caches end at the pass boundary; overlays and other renderers cannot leave them stale across passes.

## Verified effects

Production functions execute under mocked GL in the regression tests. These are synthetic submission counts, **not Quest frame times or an FPS prediction**.

| Workload | Before | After |
| --- | ---: | ---: |
| 100 identical mesh draws: mesh VAO binds, excluding pass setup/restoration | 100 | 1 |
| Same workload: rigid attribute enables | 300 | 3 |
| Same workload: skinned attribute enables | 1,500 | 15 |
| Same workload: total palette UBO binding calls | 200 | 1 |
| Two scopes, 100 identical draws each: texture binds, including final restoration | 402 | 6 |
| Same two-scope workload: filtering uniform writes | 400 | 4 |
| Same two-scope workload: skinned attribute enables | 3,000 | 30 |

Actual Android `RelWithDebInfo` compile commands were checked with `GEVR_FAST3D_INLINE=OFF` and `ON`: only the eight Fast3D files gain the final `-finline-functions` override. The option was restored to `ON`. Debug commands have no override. Paired ARM64 builds of unchanged `gfx_pc.cpp` confirmed different code generation (`llvm-size` text column 167,914 → 82,947 bytes); neither object imports `sinf`, `cosf` or `sincosf`. Object size is not a runtime benchmark.

## Validation

- Android `assembleDebug testDebugUnitTest externalNativeBuildRelease` completed successfully with NDK 25.1.8937393 / CMake 3.22.1. The release native target uses `RelWithDebInfo` / `-O2` with inlining enabled; the debug APK is a correctness build and does not include the inlining override.
- Debug APK signature verified. All 30 JVM tests executed with zero failures/errors/skips.
- Nine renderer/profiling suites passed: `test_mesh_dispatch.py`, `test_mesh_visibility.py`, `test_skin_shader.py`, `test_mesh_chunks.py`, `test_mesh_uniforms.py`, `test_frame_timing.py`, `test_scope_mesh.py`, `tools/perf/test_analyze.py`, `tools/perf/test_quest_profile.py`.
- Shader checks compile/link and execute 22 production variants on Mesa GLES. Palette and scope tests also passed with AddressSanitizer/UndefinedBehaviorSanitizer.
- New cases cover rigid/skinned repetition, CPU/mesh and shader transitions, different VAOs/culling, both scopes and the left scope alone, filtering/texture changes, unused samplers, owned palette changes, external UBO mutation and frontend restoration.

Cloud logs and compiler checks: `/workspace/scratch/bean-performance/build.log`, `implementation-checks/`, `optimized-compiler-flags.json`, `compiler-ab.json`, `cmake-inline-{off,on}.log`.

## Next decision requires Quest measurements

No headset or ROM is attached here. No installation, gameplay validation or new Quest speedup is claimed.

Compare optimized builds at `e59049d` → `5d7025c` → `5886849`, using the same build configuration, assets, render scale, headset rate and thermal conditions. Repeat the warmed XBLA-room turning route and the separate gate/sniper view three times each. Keep fresh and redraw frame statistics separate; compare 120 Hz and 90 Hz separately. Include one and two scopes, complete lens submission, compositor drops and CPU/GPU timings. Verify bodies/guns, both eyes, HUD/watch, transparency, decals, reflection winding and tracking in headset. The research report gives the profiling acceptance criteria.

Visible-run coalescing and frontend state-derivation caching remain the next candidates. Coalescing needs measured adjacent visible runs: crossing a rejected chunk or combining different scope masks can increase GPU work, particularly in the reported GPU-heavy gate view. Do not accept those larger changes based on these synthetic state-call reductions.
