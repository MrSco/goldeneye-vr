# Intermittent locomotion stutters after v0.4.9

## Evidence, 2026-10-05

The user says the interpolation improved smoothness tremendously, but random
stutters remain, possibly more frequent at 120 Hz. The following screenshots
were taken on the Dam with build `6c1b304`, at eye resolution 1680x1760:

| Screenshot time | Display | XR FPS | Worst render gap | Worst XR gap | CLAMP | DRAW |
| --- | --- | --- | --- | --- | --- | --- |
| 15:31:38 | 90 Hz | 90 | 22.4 ms | 15.7 ms | 0 | 4.1 ms |
| 15:33:23 | 90 Hz | 89 | 37.6 ms | 30.2 ms | 41 | 9.8 ms |
| 15:32:25 | 120 Hz | 122 | 25.6 ms | 14.3 ms | 51 | 4.1 ms |
| 15:32:24 | 120 Hz | 120 | 30.0 ms | 24.0 ms | 120 | 4.2 ms |

CLAMP counts presentation evaluations outside confirmed history, not dropped
frames. Its old aggregate cannot identify why history was unavailable. The old
DRAW value is average CPU time for fresh renders and redraws combined per game
tick. It is not GPU execution time. The headset's retained log begins after the
screenshots, so those windows cannot be diagnosed retrospectively from it.

## Changes under investigation

The collision compensation reset formerly accepted any nonzero head step when
combined joystick/head movement was blocked. Tiny tracking jitter while pushing
against a wall could therefore clear history every simulation tick. Presentation
now resets for a head step longer than 1 mm only when collision loss projects
more than 1 mm along that step. Parallel wall slides and head movement away from
an obstacle do not trigger it. Physical input and collision calculations are
unchanged; the gate controls presentation history only. Requested movement is
captured before collision may shorten it.

The collision-gate build kept interpolation delay, logical timeline, camera
composition and simulation pump unchanged. A native test demonstrates that a persistent
40 ms offset between regular snapshot arrivals and their logical timestamps
produces late clamps. That reproduces a candidate failure mechanism, not proof
that the headset encountered it. Timeline correction awaits measured evidence.

## New diagnostics

All counters cover the reporting window. Enable Show stats:

- `CLAMP ... E ... L ... S ...`: total, target before oldest history (E), target
  beyond newest history (L), and stale/backward display time (S).
- `RESET`: effective history resets, followed by the most frequent reason and
  its count. Repeated calls with already-empty history are excluded.
- `AHEAD`: maximum positive delayed-target minus newest logical timestamp, ms.
- `PHASE`: maximum positive predicted-display time minus newest logical
  timestamp measured at fresh snapshot arrivals, ms. Large phase values with
  late clamps indicate an anchor/scheduling mismatch; ordinary frame arrival
  jitter also contributes, so a nonzero value alone is not proof.
- `DRAW/TICK`: average combined CPU drawing duration per game tick; `PEAK` is
  the longest individual fresh-render/redraw CPU call, including driver waits.
  Neither is a GPU timer.

With Show stats enabled, logcat additionally reports the signed latest target
lead and phase, their window maxima, history length, seed count, predicted/newest
times, delay, and every nonzero reset reason. Reasons include physical collision,
stage/player context, teleport/respawn, recenter, snap turn, pause, tank,
tracking, session, refresh, long gap, backward clock/sequence and invalid input.
Capture the full log with `adb logcat -v time GoldenEye-VR:V GoldenEye:V '*:S'`.

## Validation and next headset comparison

Native locomotion tests cover the new clamp/reset reasons at all four rates,
persistent timeline offsets, effective-reset counting, wall jitter/sliding/
retreat, and genuine blocked head movement. Existing fresh/redraw, current
head/hand, display, surface, collision and reload regressions pass. The display
test runner now compiles production settings defaults as C, matching Android,
rather than C++ (which rejected the v0.4.9 designated initializer).

The Android ARM64 release variant builds using JDK 20. The diagnostic APK stays
at version 0.4.9 / versionCode 62 and uses the release signing certificate.
It is a local test build; it has not been published. The user tested it on Quest
at 90 and 120 Hz and reported that stutters seemed less frequent, but was unsure.

## Diagnostic Quest results, 2026-10-05

Two new screenshots were pulled directly from `/sdcard/Oculus/Screenshots`.
Both show build `f4548c4`, Dam, and eye resolution 1680x1760:

| Screenshot time | Display | XR FPS | Worst render gap | Worst XR gap | CLAMP | RESET | AHEAD | PHASE | CPU draw peak |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 16:07:41 | 90 Hz | 90 | 34.8 ms | 29.9 ms | 0 | 0 | 0.0 ms | 16.7 ms | 8.0 ms |
| 16:08:25 | 120 Hz | 121 | 30.1 ms | 17.7 ms | 0 | 0 | 0.0 ms | 0.5 ms | 16.2 ms |

The retained log covers 55 cadence windows at 120 Hz, from 16:08:47.840 through
16:09:42.056. All report zero clamps, zero effective resets, zero seeds and eight
history samples. Maximum positive target lead is zero; maximum positive phase
is 4.5 ms. Worst render gap reaches 34.5 ms, worst XR submission gap 22.6 ms,
and peak individual CPU drawing duration 16.6 ms. The captured log begins after
both screenshots and contains no 90 Hz cadence windows, so the 90 Hz result is
limited to its screenshot. Evidence is preserved in ignored
`android/app/build/locomotion-device/clamp-tests-90-120.log` and the two
`com.gevr.port-20261005-160741.jpg` / `...160825.jpg` files beside it.

These windows show healthy interpolation history despite submission/render
gaps. They do not establish that the collision gate caused the improvement,
exclude clamping elsewhere, or prove that stutters are resolved. There is no
measured sustained late-history condition here to justify changing the timeline
or increasing interpolation delay. Frame pacing, CPU rendering/driver waits,
swapchain waits and compositor timing are the next investigation targets.
CPU draw peak is not a GPU timer and cannot identify the cause of a particular
submission gap by itself; report windows are also independently timed.

Compare open-ground locomotion with pushing into the same barrier, keeping the
head relatively still, at 90 and 120 Hz with the same other settings. Then try
physical head movement parallel to, away from, and into the barrier. Record the
new fields during stutters. Rising PHYSICAL resets with early clamps identifies
collision-history churn; late clamps with large AHEAD/PHASE indicate timing;
large CPU PEAK/render/XR gaps also warrant rendering/driver investigation.

## Frame-gap investigation build

The follow-up keeps locomotion history, delay, camera math, game timing and eye
pass count unchanged. Cached eye redraws skip repeated texture bindings and
filtering uniform writes; filtering is invalidated on program changes, while
texture bindings remain valid across them. Draw order, geometry and restoration
of the frontend's GL state are preserved. A production replay test exercises
104 draws, mixed one/two-texture shaders, A/B/A program switches, filter changes
and texture changes: texture binds fall from 208 to 5 including restoration.
That is a mocked call-count result, not a measured headset frame-time gain.
Room-decal span scanning and repetitive logs now run only with a decal/depth
debug marker active; the actual decal rendering is unchanged.

With Show stats enabled, a new per-XR collector times `xrWaitFrame`,
`xrBeginFrame`, pose/input location, eye image acquisition/wait, eye setup,
fresh rendering, redraws, vertex-ring fence waits, layer preparation/copies,
image release, `xrEndFrame` and any SDL framerate throttle. Its `xr-gap` and
`xr-gap-prev` logs retain the actual two frames around the largest submission
gap; `xr-work` records the largest CPU duration excluding `xrWaitFrame`.
`between` includes work after the preceding submission and before the next wait
(including logging, events, input and frame finalization). Durations are ms.
Fresh/redraw durations include nested eye/driver sections: do not sum those
columns. Long active-frame stalls are retained; explicit session/empty-frame
aborts reset the pair. `predicted_skips` counts prediction-time steps that were
skipped, not measured compositor drops. No CPU/OpenXR clock conversion is used.

The former `END` field timed `videoEndFrame`, outside actual XR submission. It
is relabeled `FINISH`; actual `xrEndFrame` is now measured as `submit` in the
per-XR log. The HUD adds `XR CPU WORK MAX` and `GPU EYE MAX F/R`.

GPU eye timing uses an eight-query asynchronous pool when the driver supports
[EXT_disjoint_timer_query](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_disjoint_timer_query.txt).
Results are read only after availability, with disjoint checks; a full pool
skips measurement, and unavailable/invalid results show N/A. Queries are
balanced and destroyed at XR shutdown. F/R distinguish fresh/redraw eye passes;
the query starts after eye acquisition/setup/clear and ends before release,
excluding submission-time layer copies and compositor work. Results arrive in
later reporting windows and cannot be directly assigned to the window's worst
CPU gap. GPU timing adds no blocking wait, finish or extra render pass, but can
affect driver scheduling; compare perceived smoothness with Show stats off too.

`python port/tests/test_frame_timing.py` exercises real production CPU/GPU code,
unsupported/zero-bit GPU timers, unavailable results, full pools, disjoint
changes before/after collection, stats toggles, query cleanup, long stalls,
failed/empty frames, report rollover and predictions at all four display rates.
It also executes the extracted production eye replay against a GL state mock.
Locomotion, display, surface, reload and co-op collision regressions pass.
## Frame-gap Quest results and delay rounding correction

The user tested `d21b4dc` at 120 Hz with 45-degree snap turns and smooth turning
and reported that it seemed pretty smooth. The 16:33:31 screenshot shows Dam,
1680x1760 eyes, XR 117 FPS, worst render/XR gaps of 24.0/21.9 ms, no clamps or
resets, CPU work peak 15.3 ms, and GPU eye peaks of 2.7/2.4 ms fresh/redraw.
This confirms perceived improvement for that test; the full acceptance matrix
and a controlled baseline comparison remain incomplete.

The retained log, `android/app/build/locomotion-device/frame-pacing-tested-120.log`,
covers 14 windows from 16:33:34.504 through 16:33:47.574 at 120 Hz. Its first two
windows contain 11 clamps and one effective SESSION reset; the final 12 contain
no clamps or resets. Across the capture, GPU eye peaks reach 2.9/2.7 ms. Ordinary
worst submission gaps are roughly 17-22 ms, with fresh-frame CPU work reaching
18.3 ms and fresh rendering around 12-17 ms. Eye image waits on the paired frames
reach about 6 ms; vertex fence waits are negligible and XR submission stays below
0.5 ms. Eye GPU queries exclude layer copies/compositor work, so these numbers
do not exclude a runtime or compositor scheduling problem. Fresh rendering is
inclusive of nested waits; its duration and image wait must not be added.

A 51.7 ms submission gap coincides with the SESSION reset window. The current
frame has only 5.6 ms CPU work, while `between` is 46.1 ms. This is not evidence
of a 51.7 ms draw call. The underlying session event has not been identified.
After that reset, signed snapshot phase becomes about -33 to -42 ms: logical
snapshot timestamps are ahead of predicted display time. The positive-only HUD
does not expose this. Future captures now include exact runtime/history periods,
anchor time/sequence and newest sequence to investigate this separately before
changing the timeline. A read-only simpleperf attempt was denied access to perf
events by the device; no CPU call-stack profile was obtained.

The screenshot also shows an unexpected 25.0 ms locomotion delay at 120 Hz.
The old integer ceiling selected three display intervals when the runtime
period was just below its rounded nominal value (8,333,332 ns instead of
8,333,333 ns). The new regression failed on the old implementation. Near an
integer divisor of the 60 Hz tick, delay now uses the exact 16,666,667 ns tick
when the nearest display multiple differs by at most 1 microsecond. Other normal
display rates retain their existing delays. Production-code motion tests pass
at all four rates with period variations of -100, -2, -1, 0, 1, 2 and 100 ns.
Exact periods were not logged by `d21b4dc`, so the observed runtime period cannot
be reconstructed; the reproduced bug matches its unexpected delay.

The rounding correction needs a new headset check. Remaining gaps are not
declared resolved, and the measured eye GPU time alone does not justify GPU
quality reductions or speculative changes to simulation timing.
