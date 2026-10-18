# Crash 37093815: an ANR after a quit

v0.5.7, commit `c4174fc`, Quest 3, player "jack bauer". The report is an
ANR, `Input dispatching timed out (... MainActivity t11375 does not have a
focused window)`. The attached "tombstone" is the system's ANR trace in
base64, not a native tombstone.

## What the trace shows

The trace is of pid 13779 at 18:57:44.52 local (17:57:44Z), 109 ms after
that process started. Its main thread is still in `handleResumeActivity`
(`ViewRootImpl` → `VrosSpatialAudio.init`, a binder call), and
`SurfacePollingThread` is sleeping between polls. Nothing in the process is
stuck. A five-second input timeout cannot start 109 ms into a process's
life, so the timeout belonged to the activity record before this process
existed.

The exit history gives the order:

| UTC | Exit |
|---|---|
| 17:56:53 | relaunch to the launcher (`:relaunch` exits, the game pid gets SIGKILL) |
| 17:57:44 | `other(13) destroyTimeout`: that game process |
| 17:57:44 | pid 13779 starts; the ANR fires at once |
| 17:57:47 | `anr(6)` "user request after error": the player closed the dialog |

The "previous game run" log is pid 13779: OpenXR focused at 18:57:45.08,
then `onWindowFocusChanged: false` at 46.2, which is the ANR dialog
taking focus, and `onPause` at 47.8. The game was fine until then.

## Cause

Quitting from the universal menu finishes `MainActivity`. Its `onDestroy`
called `SDLActivity.onDestroy`, which sends `SDL_QUIT` and joins the game
thread with no timeout. On Android the game thread ignores `SDL_QUIT`
(`gfx_sdl_handle_events` handles it only off Android) and never returns.
So every quit held the UI thread until the system's ten-second destroy
timeout killed the process. Every quit in this report's history ends in
`destroyTimeout`, including the ones on 2026-10-17 under the previous
build. HANDOFF item 17 recorded this in September; the audio fix that closed
it stopped the sound, not the hang.

Launching the game again from the Library in those ten seconds starts a new
activity record in the stuck process. It never gets a window. Controller
input sent to it times out after five seconds, and the ANR is blamed on
whichever process holds the record when it fires. Here that was the fresh
process the system had started after the kill. The 17:53:08Z ANR follows
the 17:52:25Z `destroyTimeout` in the same way. That run's log is not in
this report, so the link is likely but unconfirmed.

## Change

`MainActivity.onDestroy` does its own teardown as before: it clears the
foreground marker, removes the lobby (at most 1.5 s), pauses audio and runs
`nativeDestroy` (VR settings save, audio close). Then it ends the process
with `Process.killProcess` and does not call `SDLActivity.onDestroy`. The
game log is opened and closed per line, so nothing buffered is lost.
`CrashExitPolicy` already treats SIGKILL as not a crash, and the restart to
the launcher (`RelaunchActivity`) has ended the game the same way since
#16.

Protocol and release version are unchanged.

## Validation

This container has no Android SDK, so it has not been compiled or run here.
On the headset:

- Quit from the universal menu: the app should close at once. The next
  launch's exit history should show `signaled(2) status=9`, not
  `other(13) destroyTimeout`.
- Quit, then reopen from the Library at once: no "isn't responding" dialog.
- Hold the menu button to return to the launcher: unchanged.
