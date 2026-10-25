# Report 6acb6d38: watchdog termination during a Train slowdown

Report: Quest 3, v0.5.14, build `f7109317`, built 2026-10-23 20:42.
The player reports multiplayer on Train slowing down and crashing after using
the rocket launcher. The attachment is base64-encoded gzip despite its `.gz`
extension. The decoded report stays outside the repository.

## What the report establishes

This is a real process termination, with SIGSEGV / **SI_TKILL**, not an
invalid-address memory fault. The watchdog explicitly sends SIGSEGV after a
stalled retrace count. Although the separate tombstone attachment is unavailable,
the app logcat contains a native crash summary and backtrace.

The report's exit-history entry is `2026-10-25T00:13:48Z`. The logcat signal
is at 01:13:33 local time (+0100), or 00:13:33Z; these are separate timestamps
in the supplied report, about 15 seconds apart. Both the signal and the
watchdog's abort message describe the same pid 6955 / SDLThread 6995 session.
Native build ID: `edafa50ffd7a72cc7dba6e75e4eac7c585e8049f`.

- Train (stage 25) loads at 01:08:08. Bean rooms, weapons and ragdolls are active.
- By 01:12:55, audio submissions have gaps of 1.91 seconds, then 1.09 and
  1.82 seconds. At 01:13:22 a gap reaches 3.74 seconds. The slowdown therefore
  precedes the watchdog decision.
- At 01:13:29 the watchdog reports five seconds without a retrace, pump stage
  3, background 0, focused 1.
- The diagnostic stack is logged at 01:13:32:

  ```text
  __freeParam + 0x10
  alEnvmixerPull + 0x68
  alAuxBusPull
  alFxPull
  alMainBusPull
  alSavePull
  alAudioFrame
  amHandleFrameMessage
  gevrAudioFrame
  gevrSchedBlockedRecv
  osRecvMesg
  bossMainloop
  ```

- At 01:13:33 the watchdog reports eight seconds without a retrace and sends
  SIGSEGV. The resulting native backtrace is **different**:

  ```text
  gevrBeanBeginFrame + 56
  dynGetMasterDisplayList + 8
  bossMainloop + 1344
  ```

The thread moved between these samples. An endless loop in `__freeParam` is
not established, and the final Bean frame is not proof of a Bean memory fault.
There are also model-slot exhaustion warnings and a large swap figure in the
crash summary; neither identifies the cause of the slowdown.

## Reproduced defect and change

The watchdog on current main reads `os_scheduler.frameCount` once when entering
its stalled branch. It then reads focus and logs before sending SIGSEGV, without
checking that the frame count is still unchanged. Retraces can resume during
those operations and the watchdog still kills the recovered game. A background
or focus transition during the abort log can similarly invalidate the decision.

Recheck the stalled frame count and pause/stack conditions before the abort log
and again immediately before sending the fatal signal. Use relaxed atomic reads
and the matching atomic retrace increment for this cross-thread progress counter;
it does not publish any other frame data. The existing startup threshold, stall
thresholds, diagnostic signal, frame-wait protection and explicit kill marker
remain in effect. A later persistent stall still gets its own stack and abort.

This closes a reproduced stale-decision window. It **does not establish the
cause of the Train slowdown or prove that this particular termination happened
inside that window**. There is still a small interval between the final check
and signal delivery; this change does not make those two operations atomic.

## Existing-fix check

Fetched main is `bdeb44dc`. Its watchdog still contains the stale decision.
The watchdog is also unchanged on the other non-PR branch,
`claude/nice-keller-p3aw2a`.
Open PRs #220–224 do not change the watchdog, audio mixer or Bean frame arena.
PR #222 changes music buffers for a different campaign memory fault; PR #223
clears cached cut borders between stages. The merged SFX-queue and music-stop
repairs address other paths and are already present on main.

The reported commit `f7109317` is not available from the current remote (GitHub
commit lookup returns 422). No matching archived ELF or APK was supplied. Do
not use a rebuilt main binary to symbolize its unnamed instructions.

## Validation and remaining review

`python3 port/tests/test_watchdog_recovery.py --verify-baseline` executes the
production watchdog loop with controlled callbacks under ASan/UBSan. Unchanged
main aborts after retrace recovery during logging or the focus check, and after
background/focus changes during logging. The fixed loop passes all eleven cases,
including persistent hangs, later hangs after recovery, paused/unfocused apps,
frame waits, pending stack dumps and the explicit marker.

The existing crash-hang and frame-timing suites also pass. Android
`assembleDebug testDebugUnitTest` succeeds with 31 JVM tests, zero failures or
errors; the debug APK passes signature verification. No Quest or ROM is
available here. Keep this as a draft
until the reviewing agent checks the reported build and a headset repeats Train
multiplayer with rockets, bots, Bean and retained bodies. Confirm whether the
slowdown persists and collect a profile or the matching native binary to resolve
its cause. This PR should not be described as a verified rocket/audio/Bean fix.
