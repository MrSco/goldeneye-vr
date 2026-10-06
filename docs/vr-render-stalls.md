# VR render stalls: automated Quest profiling (2026-10-05)

This continues Codex's handoff from 6462f42/d5d9bba (see git history of this
file). Everything here was measured unattended on Quest 3 `2G0YC1ZF8Q0JQ6`,
Horizon OS on Android 14, in Dam with the user's own save and quality
settings, including the HD texture pack. Only the refresh rate, smooth turning
and the stats readout differ.

## How to measure

```powershell
$env:JAVA_HOME='C:/Program Files/Java/jdk-20'
$env:ANDROID_HOME='C:/Users/Occor/AppData/Local/Android/Sdk'
./android/gradlew.bat -p android assembleBenchmark --console=plain
python tools/perf/archive_apk.py NAME --variant benchmark   # signed copy under outputs/symbols/<build id>/
python tools/perf/quest_profile.py backup  --out android/app/build/quest-profile   # once
python tools/perf/quest_profile.py run     --out ... --apk ... --label L --hz 120 --runs 3
python tools/perf/quest_profile.py restore --out ...
python tools/perf/analyze.py summary RUN_DIR
python tools/perf/analyze.py compare BASE_DIR CAND_DIR --fresh
```

- **Benchmark build type.** It is release plus `src/benchmark/AndroidManifest.xml`:
  - an optional hand-tracking declaration, so Quest launches the app with the
    controllers asleep
  - `profileable shell` for simpleperf

  Both are benchmark-only; the release manifest is unchanged (3c1080a3). The
  packaged `libgevr.so` `.text` is byte-identical to release; only the embedded
  "commit, built" string differs.
- **Runner.** Each run warps to pad 0, then takes 15 s of warm-up and a 60 s
  trace:
  - stationary 0–15 s
  - forward, backward and strafe (C buttons U, D, R), 5 s each
  - angled wall contact (U+R), 14 s
  - smooth turn, 14.5 s

  Stereo walking uses the C buttons that the left stick produces; the N64
  stick is ignored in stereo. Each frame records the injected input, so phases
  are classified from device data. The runner also:
  - writes markers atomically: the game's hooks delete a marker the moment
    they open it, and a half-written one lost a phase
  - dismisses a stale controllers-required dialog, which caches every later
    launch behind it
  - refuses to run if the headset's save or settings changed since the backup
  - always pushes them back on exit
- **Analyzer.** App processing is frame CPU minus `xrWaitFrame`, the swapchain
  image wait, the vertex-ring fence wait and the throttle; each wait is
  reported separately.
  - Headline numbers use camera-valid game frames, not within 0.5 s after an
    input change or reset, and never the 1-in-16 detailed frames, whose
    per-triangle timers inflate the tail.
  - Excluded frames are reported separately so stalls are not hidden.
  - Wall contact is counted from collision fallback paths.
  - Compositor dropped frames come from `XR_META_performance_metrics`, as
    per-second increments of the cumulative counter.
- **Sampling.** `--simpleperf` samples the measured minute (attribution only).
  `tools/perf/perf_symbols.py` symbolizes the APK-embedded library against the
  archived unstripped one.

## Baseline (441a062: profiling fixes, no renderer change)

The game ticks at 60 Hz. At 90/120 Hz every other XR frame is a *fresh* frame
(a game tick plus a full Fast3D render). The ones in between are *redraws*,
which replay the recorded draws with new poses.

App-processing p99 (ms), 3 × 60 s per rate:

| phase | 120 Hz fresh | 120 Hz redraw | 90 Hz fresh | 90 Hz redraw |
|---|---|---|---|---|
| stationary | 9.60 (5.7–9.7 per run) | 3.05 | 6.38 | 1.92 |
| forward | 5.68 | 1.92 | 5.85 | 1.73 |
| backward | 8.07 | 2.46 | 7.86 | 2.46 |
| strafe | 7.17 | 2.10 | 7.13 | 2.34 |
| angled contact | 5.80 | 1.88 | 6.49 | 2.19 |
| smooth turn | **9.95** | 2.95 | **10.22** | 3.26 |

- **120 Hz:**
  - The budget is 8.33 ms; fresh frames in heavy views exceed it.
  - The compositor counted 857 dropped frames in 224 s (median 1–2/s, worst
    second 30).
  - App-side skipped predictions follow fresh frames above about 7.5–8 ms.
- **90 Hz:** the budget is 11.1 ms and there were no app-side missed frames.
- **GPU:** eye passes run 1.8 ms median, 3.5 ms p99; the runtime reports app
  GPU time of about 3 ms and GPU utilization of 55%. The GPU is not the limit.
- **Wall contact:** the angled phase hit collision fallbacks on 90% of its
  ticks, so curb and wall sliding was exercised.
- **Variation:** "stationary" varies by run because each run faces slightly
  different geometry.

## Where the fresh-frame time goes

- **Draw count.** Fresh-frame app time tracks draw count with correlation 0.91,
  at about 3.0 ms plus 10.6 µs per draw. Typical views issue 140 draws; heavy
  views about 570 (p99), at about 15 triangles each.
- **simpleperf, main thread.**
  - Our code takes 43% of samples and the Adreno GL driver 33%. Most driver
    time is inside `glDrawArrays` (`gevr_issue_draw`): 9.4% on fresh frames and
    7.3% replaying them on redraws.
  - The VR runtime takes 8%.
  - `gfx_sp_tri1` is 13% self. The texture lookup's apparent 12% is mostly the
    flush (draw) it triggers.
- **Flush causes per frame (average, heavy second).**

  | cause | average | heavy second |
  |---|---|---|
  | texture change | 125 (80%) | 329 |
  | room display-list boundary | 12.6 | 17 |
  | depth mode | 8.8 | 44 |
  | 64-triangle cap | 4.4 | — |
  | other | 4.6 | — |

## Changes

**Kept (correctness and measurement):**
- **Vertex ring.** It no longer reuses storage after a fence that timed out,
  failed or was never created. The 100 ms wait is unchanged, and `glFinish`
  drains only on failure.
- **Profiler fixes.**
  - Export status is reported as done or failed.
  - Columns are honest: `tex_cache_allocs`, `start_time_ns`/`submit_time_ns`
    (the old `submit_ns` collided with the submit section).
  - A redraw that drew nothing no longer marks its frame as a redraw.
  - The GPU timer also runs during traces, so benchmarks can turn ShowStats off.
  - Collision and input columns were added.
- **XR metrics.** Every discovered counter is now queried; the old filter
  matched none of Meta's paths.
- **Shelved.** Codex's unmeasured texture-binding cache, which is unsafe across
  `vr_hub_render`: `docs/experiments/texture-binding-cache.patch`.

**Candidate 6330bb0 (fewer draw calls):**
- Room boundaries flush only decal batches; `gevrRoomDl` only affects batches
  with `s_isDecal`, which is constant within a batch.
- The batch cap rises from 64 to 256 triangles.
- Measured draws per frame: 155.7 → 142.7 (−8.4%). Room flushes 12.6 → 2.8;
  cap flushes 4.4 → 0.

**Paired against a same-session baseline rerun (3 + 3 runs at 120 Hz):**
- Fresh-frame app-time deltas are within noise in every phase; all 95%
  intervals include 0.
- The expected effect, about 0.14 ms on average (13 draws × 10.6 µs), is below
  the run-to-run scene variance of this benchmark.
- In the same 224 s, the compositor dropped 459 frames vs 683, and app-side
  missed frames were 18 vs 53. That is suggestive, not significant.

## HD texture pack off vs on (same code, paired, 3 + 3 runs at 120 Hz)

- **App time:** with `ActiveTexturePack=` (none), fresh-frame deltas against
  `ge007-hd-ai` are within noise in every phase. The largest is smooth-turn p99
  at -0.61 ms [-1.25, +0.17]. Pack off still reached p99 9.68 ms while turning,
  so the stall is not caused by the pack.
- **GPU:** fresh eye passes ran 1.67 ms median without the pack and 1.80 ms
  with it. Both have ample headroom.
- **Draws and dropped frames:** 160 vs 203 draws per frame and 506 vs 715
  dropped frames, pack off vs on. Both are inconclusive: the same pack-on build
  measured 143 draws per frame and 459 dropped frames in an earlier session, so
  scene variation between sessions dominates.

## Remaining gap and options

- **120 Hz.** Fresh frames in heavy views still reach p99 about 9.5–9.8 ms
  against the 6 ms target and the 8.33 ms budget. 80% of draws are real texture
  switches, because Fast3D binds one texture per draw. Closing the gap needs
  multi-texture batching (texture arrays), or moving the game tick off the
  render frame; both are larger, riskier changes.
- **90 Hz** runs without app-side missed frames.
- **ShowStats=1** adds a once-a-second log burst on the render thread, which is
  why benchmarks turn it off. Turn it off for normal play.
- **Measurement limits:**
  - The benchmark's view direction varies between runs; a fixed yaw after each
    warp would tighten comparisons.
  - Dropped-frame counts come from the runtime and are not attributed to
    individual frames.
  - The hand-tracking declaration in benchmark builds may add a small,
    equal system load to both sides of a comparison.
