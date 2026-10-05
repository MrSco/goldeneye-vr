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

## Repeatable Dam curb trigger, 2026-10-05

The user can reproduce stutter by continuously pushing into the curbs at Dam's
start, rather than stepping up/down. The 16:46:09 screenshot is `39e6916`, not
the subsequent PR #127 combined build. Its delay is now correctly 16.7 ms at
120 Hz. The matching 16:46:08.801 log window has zero clamps/resets and eight
samples. Its worst XR gap is 17.917 ms: the fresh frame contains 17.222 ms CPU
work, including 4.709 ms eye-image wait and 15.134 ms inclusive fresh rendering.
The preceding redraw contains 3.853 ms work and 2.511 ms image wait. GPU eye
peaks are 2.1/1.9 ms, with no prediction skips. The 30.0 ms render gap is a
separate measurement; average XR 120 FPS does not establish even submission
spacing. That screenshot's stutter cannot be attributed to a history reset.

The fuller retained log (`android/app/build/locomotion-device/curb-stutter-1646.log`)
contains 36 cadence windows, 16:45:42.664-16:46:17.847. Eight contain resets,
totalling 136 PHYSICAL resets; clamp counts total 288. The exact screenshot
window misses that intermittent reset activity. These counts are not dropped
frames, and the retained data does not identify which windows correspond to
each deliberate curb push. The physical-reset gate still uses combined requested
movement, so blocked joystick movement can contribute to its projected loss
when head movement exceeds the jitter threshold. Changing that gate without
separating actual physical collision compensation could smooth a head through
a wall; additional motion evidence is needed.

The next combined test build retains PR #127 and adds measurements only:

- `xr-gap-motion` / `xr-gap-prev-motion` / `xr-work-motion` pair requested versus
  actual movement, physical head step, body/root step and presented-camera step
  with the frames already retained by the CPU collector. Vectors are game cm,
  with explicit validity flags and reset reason numbers matching the existing
  locomotion enum. `body_step` spans simulation snapshots; `camera_step` spans
  XR presentation evaluations, so those intervals differ.
- `xr-motion-max` retains the largest valid camera displacement in the window,
  including whether it was a fresh render or redraw. Physical resets preserve
  diagnostic continuity so their jumps remain measurable; context/tracking/
  clock discontinuities and aborted submissions invalidate the delta baseline.
- CPU sections add inclusive `draw_batch` (vertex upload, draw setup and issued
  drawing), `draw_issue` (GL draw submission/state), `shader_bind`,
  `shader_compile` and `texture_upload`. `draw_issue` nests in `draw_batch` on
  fresh draws, and shader binding can nest in compilation. Do not sum them.

All measurements run with Show stats enabled and add clock reads/logging;
compare perception with stats off as well. They change neither collision rules
nor presentation interpolation. Native tests exercise paired motion retention,
window rollover, reset/abort validity, repeated-query handling and the new CPU
fields; locomotion and rendering regressions remain green. Repeat 120 Hz open
movement, then a stationary-head joystick push into the same curb, then an
equivalent push against a tall wall. Keep each segment long enough to capture
multiple windows and note its time. This distinguishes ordinary CPU/driver
pacing from curb-induced movement changes and physical-history resets.

## Angled curb test and physical collision correction

The user tested `5d3de95` and still reports stutter when running into the Dam
curb at an angle. The 16:58:49 screenshot has 44 late clamps, no resets and
18.4 ms worst XR gap; 16:59:22 has no clamps/resets and 17.7 ms worst XR gap.
The retained log starts at 16:59:18, so the earlier screenshot cannot be paired
retrospectively. Thirteen retained windows through 16:59:30 contain 125 PHYSICAL
resets and one SESSION reset; 247 of their 251 clamps are early, four late.
These windows do not isolate each deliberate joystick push from head motion.

The new movement pairing provides concrete evidence of the physical collision
bounce. At 16:59:26, a redraw moves about +0.258/+0.109 cm in X/Z; the following
fresh frame moves -0.262/-0.111 cm while the body's X/Z displacement is zero.
Its physical head step is +0.521/+0.217 cm. Another window shows 36 PHYSICAL
resets/s. These are real blocked head movements, beyond the former 1 mm jitter
gate. Increasing the threshold would hide a physical collision response.

The latest patch replaces that reset path. It derives a horizontal contact
normal from requested minus actual movement and removes only the head step's
component toward contact, bounded by observed movement loss. It immediately
rebases retained root positions by that physical correction, preserving their
timestamps and sample count. Cached render cameras retain their original root
reference so absolute redraw transforms apply the correction once. Joystick
collision loss is not used as an immediate root correction.

Sub-tick head movement toward the most recent contact is constrained on redraws;
head rotation, movement parallel to contact, and retreat remain current. The
constraint is applied only to motion since that contact's tracked pose. Older
head travel must still cancel the root rebase when querying previous geometry
at a fresh-render boundary. Contact clears when movement is unobstructed or
history is explicitly reset. This uses an inferred contact normal, not a new
collision query: complex corners/curb response still needs headset verification.
Gameplay collision, body position, aiming, controllers, HUDs, simulation timing
and eye-pass count are unchanged.

Native production-code tests cover repeated blocked head steps, forward/backward
and stationary wall slides with tangential physical travel, immediate retreat,
contact clearing, current tracked hands, fresh/cached boundary equality, and
72/80/90/120 Hz scheduling. They include large world coordinates. Samples remain
eight deep with one initial seed and no physical resets. Query targets within
1 microsecond of a history endpoint hold that endpoint without incrementing the
clamp counter; they never extrapolate. Retained late-clamp windows have sub-ms
positive leads, but the absent 16:58:49 log cannot establish how large its 44
late clamps were.

CPU evidence still shows hitches: the 16:59:22 gap frame contains 15.5 ms work,
3.7 ms image wait, about 1.5 ms draw-batch work, 0.09 ms shader binding, and no
shader compilation/texture uploads. Other windows have a 7.3 ms texture upload.
The remaining fresh CPU duration is not yet attributed. Added `gpu_poll` and
`hud_readback` sections measure diagnostic timer-query polling and the existing
synchronous ammo-crop readback, respectively, without changing their behavior.
The physical-collision patch is ready for another angled-curb comparison; it
does not establish that all rendering hitches or the stutter report are resolved.

## Remaining hitch: synchronous ammo bounds readback

The user still reproduces the angled-curb hitch on `3f33c22`. The 17:14:45
screenshot matches the 17:14:44.799 window: 34 late clamps, no resets, 17.701 ms
worst XR gap, 17.470 ms work, 5.764 ms image wait, and 5.713 ms `hud_readback`.
The latter runs `gevr_measure_R_capture`, which downsamples the ammo texture and
calls `glReadPixels` directly into CPU memory to find nontransparent bounds.
That existing call forces pixel delivery before the render thread can continue.
GPU polling takes only 0.008 ms in this frame; shader compilation/texture uploads
are zero, and draw batches take 1.511 ms. This is a measured rendering stall,
though it need not explain every curb-induced movement irregularity.

The retained log `android/app/build/locomotion-device/curb-contact-1714.log` has
16 reporting windows from 17:14:43.787 through 17:14:58.864: no resets/seeds,
eight samples throughout, and 310 late clamps. Largest positive target lead is
only 0.006 ms. The 1 microsecond clamp threshold makes small clock drift visible
as high clamp counts; these are not frame-sized history deficits. Counts alone
do not justify adding another display frame of locomotion delay. Readback costs
roughly 4.8-6.5 ms on the retained worst frames. Some worst-frame pairs show
smooth, similar successive camera deltas while their submission gap exceeds
the 8.33 ms display interval.

The new production module `port/fast3d/gevr_hud_bounds.cpp` uses three 64 KB pixel
pack buffers. It queues the existing downsample/read into a buffer, fences it,
and polls with flags 0 and timeout 0 on subsequent captures. CPU mapping and
alpha scanning happen only after an already-signaled/satisfied fence. A full
ring skips a request; unavailable APIs never fall back to synchronous pixels.
This follows the pixel-buffer offset and fence polling contracts in the
[Khronos read-pixels reference](https://github.com/KhronosGroup/OpenGL-Refpages/blob/main/es3.0/glReadPixels.xml)
and [client-wait reference](https://github.com/KhronosGroup/OpenGL-Refpages/blob/main/es3.0/glClientWaitSync.xml).
Driver submission can still have CPU cost, so the new `hud_readback` duration
must be measured on Quest; an asynchronous buffer is not a guarantee of zero
driver time.

Aim changes request a short burst and preserve previously completed bounds for
each aim mode. Results from a previous aim/layout epoch are discarded, and
resolution changes invalidate cached bounds. Before a new mode's first valid
measurement, a conservative full capture prevents clipping digits. Tight bounds
arrive later, so initial/aim-transition placement needs visual checking. The
original 30-capture periodic update remains. GL framebuffer, texture, pixel-pack,
packing and scissor state are restored. XR shutdown deletes buffers, fences and
the downsample framebuffer/texture without waiting.

Native production-code tests verify no CPU-directed readback, no pending map,
zero-timeout/no-flush polls, full rings, stale aim/resize results, map/unmap/fence
and framebuffer/allocation failures, transparent captures and resource cleanup.
Locomotion, GPU/replay and surface regressions pass. No gameplay, collision,
head/hand tracking, interpolation delay or eye-pass changes are included here.
The next headset test should repeat the same angled curb and also inspect ammo
placement when entering/leaving aim; release acceptance remains pending.

## Curb binding after asynchronous readback

The user reports uncertain overall improvement on `2114837`, but definite
binding while strafing against the Dam curb. The 17:32:21/25 screenshots show
13.0/13.8 ms worst XR gaps, 12.2/10.9 ms CPU work peaks, no resets and 60/54
late clamps. Those are separate windows and cannot establish a matched
before/after performance gain.

Read-only capture `android/app/build/locomotion-device/curb-async-1732.log`
retains 12 stereo windows from 17:32:25.170 onward (the exact screenshot windows
have already rolled out of logcat). Worst-frame ammo readback is usually
0.007-0.012 ms; its maximum among the retained work frames is 0.247 ms. The
previous 5-6 ms crop stall is absent in this capture. Remaining worst gaps reach
20.676 ms, work reaches 16.523 ms, and one redraw contains a 14.644 ms runtime
image wait. Low timed eye GPU duration does not identify the cause of that wait.
History stays populated; positive target leads are only 0.007-0.009 ms.

The 17:32:28.187 work sample requested (-9.3210, +0.2593) cm horizontally and
accepted (0, 0), despite a physical head step of only (-0.0029, -0.0041) cm.
Its root/camera steps are almost zero and it has no history reset. Other samples
slide along a consistent oblique direction. A stopped collision tick is real,
but isolated worst-frame samples do not establish whether steady tangential
input alternates between stop and slide. Nor do they distinguish a legitimate
corner stop from a collision defect or a presentation pacing hitch.

Add a bounded per-window trace of up to 64 submitted local movement ticks,
including small/released inputs. Each trace records time since the first tick,
requested/accepted X/Z centimetres, the first failed simple-move edge's unit
tangent, attempted/accepted fallback bit masks (1 simple, 2 fraction, 4 edge,
8 end-hop), and total collision calls/calls allowing scoot. A second collision
call exposes height rechecks. The overlay counts `MOVE CLIP` and `STOP` among
ticks requesting at least 1 cm: CLIP is any nonzero movement with at least
0.01 cm loss, not necessarily an accepted edge slide. STOP accepts at most
0.01 cm. Logs preserve all the vectors to evaluate those thresholds separately.
Overflow is explicit, aborted frames are discarded, and stats-off/remote players
do not record paths. Trace rows are grouped eight ticks per log line once per
reporting window, with no disk I/O or per-tick logging.

Production-code tests compare the diagnostic and uninstrumented collision
fallback block across 26,244 result/scoot scenarios, verifying identical calls
and short-circuit order. Collector regressions cover stopped ticks between
slides, multiple calls, successful moves with uninitialized edge outputs,
overflow, reporting rollover, failed submissions and bounded formatting.
This build changes diagnostics only; it does not adjust gameplay collision,
movement speed, interpolation delay, or the frame pump. Repeat a steady angled
push along the same straight curb section and then ordinary open-ground strafe
to identify how the binding corresponds to the game's collision decisions.
