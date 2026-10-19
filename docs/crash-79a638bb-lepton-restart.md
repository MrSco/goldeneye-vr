# Report 79a638bb: Lepton container restart reported as a crash

Report: v0.5.9, commit `ee2d86b4`, Valve Lepton. The summary reads "Previous
foreground run from 2026-10-19T11:12:17Z ended unexpectedly", and no
tombstone is attached. This was the app's own foreground marker, not an
Android exit record. The exit history has `(no records)`.

## Evidence

- The APK was installed at 11:12:13Z, and the previous run started 4 seconds
  later. Its game log stops at 11:12:20 after the usual startup lines (GL
  context, VR session, scope targets). The current run's log has the same
  lines, and its logcat shows the launcher open with `rom none`. The launcher
  logs only to logcat, so a run that stays in the launcher writes nothing more
  to `gevr.log`. The 12 minutes without game-log lines fit a player who stayed
  in the launcher. They do not show a hang or a crash.
- The logcat starts with the Android container booting at 11:24:32
  (`SurfaceFlinger: Boot is finished`, `ActivityManagerTiming: FinishBooting`).
  Then Lepton's launcher (pid 1027) force-stops and starts `com.gevr.port`
  2 seconds later. The run's logcat from before that boot is not in the
  report.
- At that boot, `SystemServerTiming: finishUserBoot took too long.
  elapsedTimeMs=9417634` and `KeyguardViewMediator ... startTime=9413488`
  show that `elapsedRealtime` and `uptimeMillis` were about 2.6 hours. The
  container keeps the host's clocks. `LogReporter` computes the boot time as
  `currentTimeMillis - elapsedRealtime`, which gives about 08:47:35Z, well
  before the 11:12:17Z marker. The stored boot time matched and uptime only
  went up, so none of the reboot checks added for report 73a899cd fired.
  Report 73a899cd was a restart where the clock did reset.

The likely cause is that the Lepton container restarted (the player closed
the app or the system tore it down), not a game crash. The report cannot rule
out a hang, but no evidence points to one.

## Change

The foreground marker now also stores `Settings.Global.BOOT_COUNT` (public
since API 24, the app's `minSdk`). System server raises it on every Android
boot, and the container keeps its settings database between restarts. A
changed count is a reboot (`CrashExitPolicy.bootCountChanged`). A count the
system does not keep, or a marker written by an older build, is stored or
read as -1 and leaves the existing checks in charge.

## Verification

- `CrashExitPolicyTest` gains `leptonContainerRestartIsARebootByBootCount`.
  It uses this report's times to show that the clock checks alone call the
  run unexpected, and it covers the boot-count rule, including -1. All 12
  tests pass with JUnit 4.13.2.
- `LogReporter.java` could not be compiled without an Android SDK. `javac`
  reports only missing Android symbols, with no syntax errors.
- Not yet confirmed on a Lepton device that its Android raises `BOOT_COUNT`
  on a container restart. Acceptance: on Lepton, start the app, restart the
  container while the app is in the foreground, relaunch, and check that no
  crash offer appears.
