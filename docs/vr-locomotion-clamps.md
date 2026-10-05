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

The existing interpolation delay, logical timeline, camera composition and
simulation pump remain unchanged. A native test demonstrates that a persistent
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
It is a local test build; it has not been published or visually validated.

Compare open-ground locomotion with pushing into the same barrier, keeping the
head relatively still, at 90 and 120 Hz with the same other settings. Then try
physical head movement parallel to, away from, and into the barrier. Record the
new fields during stutters. Rising PHYSICAL resets with early clamps identifies
collision-history churn; late clamps with large AHEAD/PHASE indicate timing;
large CPU PEAK/render/XR gaps also warrant rendering/driver investigation.
