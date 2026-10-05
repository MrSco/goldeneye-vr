# VR render-stall profiling handoff — 2026-10-05

The user asked for this handoff because usage is nearly exhausted. Stop here and continue in the other agent. The rendering optimization task is **unfinished**. No release acceptance has been given.

## Authorization and objective

Implement the user's “Remove remaining VR rendering stalls with automated profiling” plan. Preserve the tested `346c909` locomotion/collision fixes, PR #127, resolution, visual quality, gameplay rate, head/hand tracking, and one eye pass per fresh frame. No repeated manual headset tests; request one final visual check only after automated validation. No merge, version bump, push, or GitHub release until final acceptance.

Use nested inclusive/exclusive frame profiling, bounded in-memory traces, detailed renderer sampling every 16th frame, and profiling-on/off overhead comparisons. Automate Dam stationary rendering, movement, angled curb contact, and smooth turning at 120 and 90 Hz: 15 seconds warm-up, three 60-second measured runs per build/rate. Find costs through measurements, retain only repeatably beneficial optimizations, and report median/p95/p99 processing and waits, GPU times, and runtime missed-frame counters where supported. Target steady 120 Hz app processing p99 below 6 ms excluding runtime pacing waits. A 21 ms gap dominated by `xrWaitFrame` alone is not grounds to change scheduling.

If unattended XR rendering is unsupported, the user explicitly authorized native/offscreen device benchmarks as fallback, with XR deadlines left unverified. No root, permanent power/quality changes, or QGO changes. Back up/restore saves, settings, and test markers.

## Workspace and build rules

- Worktree: `C:/Users/Occor/.codex/worktrees/7b5c/goldeneye-vr`
- Branch: `codex/fix-vr-locomotion-clamps`
- Primary repository: `C:/Users/Occor/Documents/other_projects/goldeneye-vr`
- Remote: `https://github.com/MrSco/goldeneye-vr.git`
- **Before EVERY Gradle invocation:** set `JAVA_HOME=C:/Program Files/Java/jdk-20`. Android Studio JBR is Java 25 and fails with class-file major version 69. The user explicitly supplied this rule and is frustrated by repeat mistakes.
- Also set `ANDROID_HOME=C:/Users/Occor/AppData/Local/Android/Sdk`; this worktree lacks `android/local.properties`. An initial invocation failed for missing SDK, then the JDK20+ANDROID_HOME invocation worked after a small C++ fix.
- NDK `25.1.8937393`, CMake `3.22.1`, build-tools `36.0.0`.
- Commands use PowerShell. Do not pass `sandbox_permissions` (approval policy never).
- Preserve mixed CRCRLF/CRLF/LF line endings. Byte edits are safer; strip trailing CR only on newly inserted/changed lines. Do not normalize whole old files. `git diff --check` currently reports new-line trailing CR issues described below.
- No subagents unless explicitly requested. No skills applied in this implementation turn.

## Commits and existing tested behavior

Current HEAD: `6462f42c45aa33a57a659ad0aa8b928677d72ec0`.

1. `346c909690bd225c28cee91fba4b5c31a78352da`: tested gameplay baseline. Collision-checked 0.01 cm outward precision retry on rounded curb slides; locomotion clock reseed after interrupted timing. User said it felt better.
2. `5d35f4c`: documentation only after that baseline.
3. `913c97c`: common profiling instrumentation and reversible Quest driver.
4. `6462f42`: completes asynchronous GPU CSV export and fixes metrics poll accounting placement. This is the latest built/signable profiling baseline, before any renderer optimization.

PR #127 is already merged locally via `303741b`; head `1e95f3b557b0ed840bd84917bbc4fdcd0bcfe7dc` is an ancestor. It fixes exhausted 600-model hit-list storage with heap chunks/stage cleanup. PR remains open upstream and attached to this chat. Keep it.

Published main/release remains v0.4.9, `6c1b304`, versionCode 62, protocol 18. Do not change version now.

Existing investigations and device evidence are documented in `docs/vr-locomotion-clamps.md` and `docs/releases/unreleased.md`. Ignored logs under `android/app/build/locomotion-device/`:

- `curb-live-completed.log`: old d6c1929, many stopped curb ticks; interrupted timeline stayed 40–67 ms late.
- `curb-slide-clock-completed.log`: tested 346c909, 2,349 ticks, 1,156 collision fallbacks, 36 precision retries/28 accepted, only five near-zero requested-movement ticks. Timeline phase stayed <=8.338 ms. Largest steady XR gaps ~21.4 ms were mostly `xrWaitFrame` (15.594 ms wait +5.787 ms work); these are not measured compositor dropped frames.
- The first stereo frame had a cold texture-upload/loading hitch and is excluded from steady conclusions.

Previous complete native suite (64 tests), multiplayer suite (20), and Android unit tests passed before this profiling work. This turn has not yet rerun the whole suite.

## Built signed profiling baseline

Release assembly at current HEAD passed with JDK20 and ANDROID_HOME, log:
`android/app/build/profile-baseline-build.log`.

Signed APK:
`android/app/build/outputs/apk/release/GoldenEye-VR-profile-baseline.apk`

- SHA256 `7cfa9dfd2dcea07c3afc768a6ce34be91569a69d6a927974ad44e311851d74c6`
- ELF build ID `12ad448b00112d039dcc0ede269aac88aaf4deb8`
- Exact APK, unstripped `libgevr.so`, symbol ZIP, and `build.json` archived at `android/app/build/outputs/symbols/12ad448b00112d039dcc0ede269aac88aaf4deb8/`.
- Release certificate SHA256 `382ff89137e24be05a0eed4794ad8d6bd9e3b3011cca72f98a4885cdba875452` verified.
- APK was installed to Quest, but **no valid XR benchmark ran**. Archive's installed flag is currently false (helper default), so metadata needs updating if used as install provenance.
- This APK contains only common instrumentation; it does **not** contain the uncommitted fence or texture-binding changes below.

Original tested APK remains:
`android/app/build/outputs/apk/release/GoldenEye-VR-curb-slide-clock-pr127.apk`
SHA256 `92fd8db674a66204bb4f551b2cbf64f278f503607cc8a25d7f0b3011cc22d0a3`, ELF `a53a7a4493a4cf7ce224553476622c055d8193dc`, with matching symbols archive. Do not overwrite it.

`tools/perf/archive_apk.py NAME` signs an already-built release APK using ignored keystore properties (fallback to primary checkout), verifies certificate and packaged ELF identity, and archives symbols. No credentials are printed. It assumes this machine's SDK/JDK paths. Commit code before building for a clean embedded build ID.

Build command:

```powershell
$env:JAVA_HOME='C:/Program Files/Java/jdk-20'
$env:ANDROID_HOME='C:/Users/Occor/AppData/Local/Android/Sdk'
./android/gradlew.bat -p android assembleRelease --console=plain
python tools/perf/archive_apk.py GoldenEye-VR-profile-candidate
```

## Committed profiling implementation

`port/include/gevr_frame_timing.h`, `port/src/gevr_frame_timing.c`:

- Existing CPU stats maintained; nested token stack of 32 scopes adds exclusive `self[]` times.
- Sections include full frame/game/input/audio/finalize/metrics, display-list interpretation, vertex transforms, clipping, texture lookup/conversion, HUD captures, scope work, texture-pack ready work, and existing driver/eye/wait sections.
- Detailed sections from `GEVR_TIME_DL` onward run every 16th XR frame. Existing coarse driver sections run every frame. Counters include draws, vertices, known allocation events, uploads, cache hits/misses. **Allocation counter is not a comprehensive heap tracker**; currently one event per cache miss, so refine or clearly label it.
- Input/music fade work before the next `xrWaitFrame` is captured in `pre[]`, separate from in-frame CPU work. Metrics poll work after submission is assigned to next frame's pre-work.
- Bounded 8,192 frame rows and GPU rows in static memory. No per-frame trace allocation or disk writes during measurement. CSV export happens afterward.
- Opt-in marker `/sdcard/Android/data/com.gevr.port/files/gevr_profile.txt`:
  `record LABEL WARMUP_SECONDS MEASURE_SECONDS DETAIL`
  Example `record baseline120-1 15 60 1`; labels restricted to alphanumeric/_/-, maximum measure 60 s.
- Acknowledgment `<marker>.status`: label, CPU-monotonic measure start/end ns.
- Output `<marker>.<LABEL>.csv` plus `.csv.gpu.csv`; GPU rows have ready timestamp, fresh/redraw kind, GPU ns. Async arrival-time samples, not exact CPU-frame matching.
- Frame CSV contains CPU start/submit/XR predicted time/period, kind, camera/body validity, resets, sampled flag, attribution errors, work/pre/counters/image lifetime, plus inclusive/self/pre per section.
- `work_ns` is total frame minus xrWaitFrame; analysis should also distinguish swapchain waits, fence waits, submission waits, and pre-work when reporting app processing.
- Poll marker every 60 idle frame calls. Collector enabled when ShowStats or recording active. GPU timer still requires ShowStats (device config already has ShowStats=1).
- Failed CSV fopen currently clears recording without an explicit exported failure status; runner catches missing file. Improve error reporting if useful.
- Scope/HUD capture timers cover begin/end setup/restoration; intervening geometry is in DL/draw timing.
- Redraw span currently marks kind=redraw even if `gfx_vr_redraw_frame()` returns zero. Filter on valid gameplay camera and/or fix kind to count only successful redraws before relying on submissions.

`port/vr/gevr_xr_metrics.cpp`, header, `vr_openxr.cpp`, CMake:

- Optional `XR_META_performance_metrics` discovery and counter enumeration (max256). Logs every discovered path, queries app/compositor counters once/sec only on successful shouldRender frames, with units and valid flags. Unsupported APIs/enable/state explicitly log unavailable. Saves/restores metrics-enable state at session shutdown.
- Metrics initialization is called after Android/Windows session creation; reset before destruction.
- Whole swapchain acquire-to-release CPU lifetime in trace alongside acquire/wait/release durations.
- No actual runtime counter availability verified yet because app never reached XR in this run.
- Native mocked metrics extension tests still needed.

`port/src/gevr_engine_shim.c`, `port/fast3d/gfx_pc.cpp`, `gfx_opengl.cpp`: game/render/audio/input spans, detailed renderer sections and counters. Coarse OpenGL RAII timers now use balanced nested collector scopes.

Tests run and passed this turn:

- `python port/tests/test_profile.py`: production nested timing/exclusive sums, counters, pre-work, 1/16 sampling, bounded CSV overflow.
- `python port/tests/test_frame_timing.py`: CPU stats, collision path equivalence across26,244 scenarios, async GPU queries, and production replay state/order tests. Rerun after future changes.
- `python port/tests/test_locomotion.py` earlier in this turn (before final profiling additions).
- `python port/tests/test_vr_display.py`: sizing/refresh/resources/settings; fixture adds metrics Init/Reset mocks.
- Android release compilation at6462f42 passed; debug and Android unit tests pending.

## Quest automation blocker and restored state

Quest3 serial `2G0YC1ZF8Q0JQ6`; adb on PATH. Package `com.gevr.port`, external files `/sdcard/Android/data/com.gevr.port/files`.

Created `tools/perf/quest_profile.py` with `backup`, `run`, `restore`. Backup persists between build/rate runs, logger streams full logcat to disk without clearing existing logs. Settings are derived from the backed-up INI, changing only display rate and smooth turning for the benchmark; preserves all other quality settings (HD pack, GEX guns/muzzle fits, resolution). Save restored before each launch. Commands inject input and warp pads, stationary/movement/angled-contact/turn phases in a60s trace. Runner checks enough active camera-valid XR rows, period matching requested Hz, and no attribution errors.

**First run failed before app startup.** Current Horizon OS intercepts even explicit `am start -n com.gevr.port/.MainActivity` with `com.oculus.vrshell/.systemdialog.launchcheck.LaunchCheckControllerRequiredDialogActivity` while controllers are asleep. No app PID or valid XR rows. `am start -a MAIN` and category DEFAULT did not help. Temporary prox_close wakes system but does not bypass controller-required launch. An attempted automation_enable broadcast returned result0 but did not bypass it. UIAutomator sees only1x1 shell surface, no actionable controls. Do not pretend that app never starting is a rendering result.

**Manifest already explains this deliberately:** hand-tracking declaration was removed because declaring it to bypass this prompt caused controller sleep/reconnect failures mid-game. Do not re-add it casually for automation. User doesn't want repeated headset tests. Use the approved native/offscreen fallback if no supported unattended launch is available.

Backups and failure evidence:

- `android/app/build/quest-profile/backup/manifest.json`
- Original `data/goldeneye-vr.ini`, `eeprom.bin`, power/package snapshots under that backup folder. All six automation markers were absent initially and are removed by restore; unrelated user markers (decal/itempose/zdebug) left untouched.
- `android/app/build/quest-profile/baseline120/device.log`: failed launch/system logs, **not a valid benchmark**.
- `.../baseline120/settings.ini`, `.../window.xml`: failure investigation.
- No measured CSV produced.

**Restore completed and verified byte-for-byte** using:
`python tools/perf/quest_profile.py restore --out android/app/build/quest-profile`
It force-stopped app, pushed original INI/save, removed absent markers, broadcast automation_disable/prox_open, and returned original asleep power state. Latest `dumpsys power` confirms `mWakefulness=Asleep`. QGO untouched. Temporary `/sdcard/gevr-window.xml` removed. No logcat logger remains (runner terminated it in finally). One `adb shell am start -W` host session77070 may still be waiting; inspect/terminate that specific process if necessary. Device is not currently running gameplay.

Runner limitations to fix before another attempt:

- Controller-required launch should emit a clear unavailable reason instead of `pidof` CalledProcessError.
- All waits are in the standalone runner, not assistant tool blocking sleeps. Use asynchronous exec sessions and keep progress updates.
- Input hook won't consume a new marker until prior injected frames expire; max600frames. Current phase timing has neutral gaps because a15s phase only injects10s. Document or fix the workload before comparisons.
- Warp pad0 selected but not yet verified to be near the actual Dam curb. Validate collision contact from logs, rather than calling it the exact user repro.
- Repeat runs currently reuse the loaded mission/actor state. For truly identical paired comparisons, reset mission/state/randomness consistently and record it. Host-time phase offsets approximate marker acknowledgment; use acknowledged CPU timestamp for tighter analysis.
- Runner currently requires enough fresh/redraw valid rows and period match, but needs overflow checks, missed-frame counter aggregation, proper phase classification, and safer resume logging.

## UNCOMMITTED work — important

Current git status:

```
 M port/fast3d/gfx_opengl.cpp
 M port/tests/replay_state_native.cpp
?? port/include/gevr_texture_bindings.h
?? port/include/gevr_vertex_ownership.h
?? port/tests/vertex_ownership_native.c
```

These changes are **drafts, not built, not measured, not accepted**. Preserve them for review; do not ship blindly.

1. Vertex ownership correctness draft:
   - Existing ring waited100ms with `glClientWaitSync` and ignored return, then deleted fence/reused storage regardless of timeout/failure.
   - New production helper `gevrVertexAwaitOwnership` in `gevr_vertex_ownership.h`: first nonblocking poll; up to four1ms timeout retries; if still timeout/failed/unknown, call a `glFinish` completion barrier before deletion/reuse. Signaled path stays cheap. Returns result; backend logs fallback.
   - Backend handles null `glFenceSync` by immediate `glFinish` and warning, instead of silently considering unwitnessed storage free.
   - `vertex_ownership_native.c` tests already-signaled, timeout-then-signal, persistent timeout, failure and unknown results; **not compiled/run yet**, nor integrated into test driver.
   - Need tests against extracted actual ring function (fence creation, deletion order, no reuse before completion), and context-loss behavior. glFinish fallback is correctness, not a claimed speed gain. Do not blindly replace persistent ring with glBufferSubData: past Quest experiment regressed DRAW12→35ms.
2. Texture-binding cache experiment:
   - New `GevrTextureBindings` header tracks actual unit and two2D bindings separately from frontend logical state.
   - Backend wrappers `gevr_gl_active_texture` / `gevr_gl_bind_texture` suppress redundant calls; all glActiveTexture/glBindTexture sites in that file changed to wrappers. Non-2D target binds always pass through.
   - Invalidates at gfx init/start/end frame and before backend texture deletion. Need verify **every cross-file external GL state boundary**, HUD/scope/replay/framebuffer resource creation, context recreation, object deletion/reused names. Unknown unit passthrough handled by helper.
   - Existing extracted replay test fixture defines passthrough wrappers so test_frame_timing still passes; this does **not** test the new real cache! Add production helper/state equivalence and actual GLES framebuffer output tests before keeping it.
   - No measured device gain. Do not retain it as an optimization until repeated paired offscreen/native driver benchmark shows gain and correctness; full XR timing remains unverified if launch blocked.
   - `git diff --check` currently reports trailing CR on changed backend lines (roughly40). Strip CR only for lines containing the new wrappers/invalidation and inserted fence closing brace. Do not normalize whole file.

## Next work

1. Review/fix draft cache boundary correctness and fence ownership. Integrate meaningful production native tests, plus optional metrics extension mocks and profiler overhead benchmark.
2. Build an Android native/EGL offscreen benchmark using NDK, run under `/data/local/tmp` with ADB. Exercise actual production binding helper plus real GLES driver calls, texture-cache/conversion paths, mixed draws/HUD/scope state and output hashes. No roots or power-level changes. Use repeated paired runs with allocation/upload/cache counters; compare detailed profiling on/off overhead. Avoid claiming these synthetic timings establish full-game XR p99.
3. If supported unattended launch can be achieved without the known hand-tracking/controller regression, run the existing driver at both rates for baseline/candidate. Otherwise clearly report XR validation unavailable; don't ask user to keep putting on headset.
4. Only optimize attributed/measured hotspots; don't guess which source function “costs milliseconds.” Current source shows redundant texture binding calls as a candidate, but no timing evidence yet. Keep scheduling unchanged absent deadline evidence.
5. Run complete native regressions (locomotion, collision, display, surface, HUD, multiplayer), Android unit tests, release/debug assembly with explicit JDK20 each time. Archive signed candidate and matching symbols only after code is clean/tested.
6. Write before/after report including supported/unavailable runtime counters, median/p95/p99, overhead, scope/loading/warm-up/transition exclusions, each optimization's contribution and remaining gap to6ms. State uncertainties honestly. Do not request final acceptance while required checks remain.

Latest user-facing status: profiling release compiles; unattended launch is blocked by controller-required system prompt; proceeding with allowed native/offscreen fallback and ownership safety tests. User then requested this immediate handoff.
