# Quest display validation

This change supports runtime recommendations at initialization. It does not implement
live dynamic resolution or claim verified QGO compatibility.

Baseline observed on the connected Quest 3 on 2026-10-02, before this change:

| Value | Observed |
| --- | --- |
| OpenXR recommendation | 1680×1760 |
| Eye buffers at scale 1 | 1832×1919 |
| Maximum swapchain | 8192×8192 |
| Supported display rates | 72, 80, 90, 120 Hz |
| Saved preference | DisplayHz=90 |
| Refresh requests | 90 Hz at session start and at START |

## Required comparison

Record the build, device/OS, saved render scale, DisplayHz, QGO profile, raw OpenXR
recommendations, final eye-buffer dimensions and actual refresh rate for each run.
Keep the original configuration and restore it after testing.

1. Launch without an external profile, at scale 1 and Auto. Confirm final dimensions
   match the even runtime recommendation and no explicit refresh request occurs.
2. Launch from QGO with a different resolution and refresh profile, at scale 1 and Auto.
   Confirm the raw recommendations change, eye buffers follow them and the actual
   display rate follows the profile. An ADB property experiment is useful diagnostic
   evidence but does not substitute for this actual QGO launch.
3. Repeat at saved scale 2, in stereo and virtual-screen gameplay. Check HUD, watch,
   scopes and surface dimensions. Exercise an in-game scale change and relaunch.
4. Start with an explicit saved rate; select Auto in the launcher. Confirm the app
   withdraws its existing preference with a zero-rate request. Verify supported
   explicit rates and a session restart.
5. Test bounds and allocation failures through automated fault injection. Avoid
   deliberately exhausting headset memory. Required resource failure must clean up
   and terminate through the existing fatal-error path, never report VR ready.

If QGO does not change the recommendations, leave QGO compatibility unverified and
investigate its actual mechanism. Do not substitute guessed Android property handling.

Useful existing log filters:

```powershell
adb logcat -d --pid=<game-pid> | Select-String 'HMD .*resolution|HMD eye|HMD render|display:|VR system ready|FATAL'
```

## Local checks

```powershell
python port/tests/test_vr_display.py
python port/tests/test_launcher_ui.py
python port/tests/test_surfaces.py
cd android
./gradlew.bat assembleDebug testDebugUnitTest --console=plain
```

## Implementation checks on 2026-10-02

- Native production-code checks pass for sizing, desktop rounding, refresh transitions,
  saved preferences, session recreation, and injected controller/swapchain/depth/FBO failures.
- Launcher checks pass for tab selection, ROM collapse and error recovery, disabled debug
  clicks, overlapping stick clicks, session reset, rate layout and fixed footer bounds.
- Existing surface tests and all 18 Android unit tests pass. Android debug assembly passes.
- A debug APK signed with the project's existing certificate was installed in place.
  After the controllers were woken, the 14:52 launch reported both eyes recommending
  1680×1760, limits of 8192×8192, final eye buffers of 1680×1760 at scale 1, matching
  Android surface dimensions, successful VR initialization, and the saved 90 Hz request.
  This confirms the recommendation and surface path on Quest 3. An actual-rate reading,
  Auto/higher-scale gameplay runs, and an actual QGO launch comparison are still pending.

The user reviewed the launcher on Quest and approved the final Play, Controls, Comfort,
and Screen layout, including compact expanded ROM details and no Send debug tooltip.

Launcher device checks still needed: exercise every tab with stick navigation;
recover from a bad ROM selection and verify START stays reachable in that state.
Confirm separate stick clicks leave Send debug disabled, overlapping clicks enable it,
and quitting the game requires the combo again. The unlock is never persisted.
