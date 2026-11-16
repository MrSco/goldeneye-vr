# Vision Pro, native

Branch `claude/visionos-native`. This is a native visionOS build of the game, the
fully native path from `docs/mobile-platforms.md`. It sits beside the Klepton build
(the Quest APK run inside Klepton, `tools/build_visionos.sh`). Klepton stays the
shipping Vision Pro build until this one has run on a device.

## Shape

The native build is the PC VR game (`port/vr`, OpenXR) built for visionOS.

- **Graphics:** OpenGL ES 3.0 through ANGLE on Metal, the same as iOS.
- **OpenXR runtime:** Apple has no OpenXR, so `port/visionos` contains a runtime of
  the game's own. It implements the roughly 45 `xr*` calls that `port/vr` makes.
  There is no loader: the game links to these functions directly. Because of this,
  `vr_openxr.cpp` and `vr_input.cpp` stay the code the Quest runs. The alternative
  was the plan's V1 (an XR interface with two backends), which would have split
  them.

| file | what it does |
|---|---|
| `VosApp.swift` | SwiftUI app. One full-immersion `ImmersiveSpace` with a `CompositorLayer`, which is the app's first scene. |
| `vos_app.mm` | Takes the layer renderer, then runs the game's `main` (`main.c` `gevrVosGameMain`) on its own thread with a 16 MB stack. Also holds the shell stubs (file picker, voice-chat mode). |
| `vos_egl.mm` | ANGLE context with a pbuffer and no window. Turns Metal textures into GL textures (`EGL_METAL_TEXTURE_ANGLE`), and fences GL work before Metal work with a shared event. Both techniques come from Midas's `GEEyeTargets.mm`. |
| `vos_window.cpp` | fast3d's window manager, which has no window here. SDL is built with no video (`CMakeLists.txt`). |
| `vos_xr.mm` | The runtime. Covers instance, session and events; spaces (ARKit world tracking is the head; STAGE and LOCAL_FLOOR are ARKit's origin); swapchains; the frame loop; and actions. Swapchain images are Metal-backed GL textures. Eye arrays are blitted per layer into Metal textures. `xrWaitFrame` is the compositor's frame up to its drawable. `xrEndFrame` draws the projection layer (with the field-of-view remap) and the quad layers into the drawable in Metal, then presents. |
| `vos_input.mm` | Answers a Touch pair. It prefers PlayStation VR2 Sense controllers (`VosControllers.swift`) and falls back to a game pad, with the hands held in front of the head. |
| `VosControllers.swift` | Sense controllers, ported from Klepton's `KleptonControllers.swift`. Poses come from ARKit accessory tracking (`.grip`, `.rendered`), predicted to display time, with Klepton's tuned turn to a Touch grip: 5.037 + -37 degrees about X, and an offset of (-0.005, 0.02, -0.02) m with x mirrored on the left. Buttons are read by element name, and rumble goes through CoreHaptics. The session runs again whenever the paired set changes. |
| `VosApp.swift` picker | A window opens for the launcher's file pickers through a scene activation request, using `.fileImporter`. While it is up, the game's thread keeps presenting black frames (`vosKeepAlive`). |

Conventions:

- **Image rows:** the runtime reads images bottom-up, as the Quest's runtime does. In
  `vr_openxr.cpp`, `GEVR_XR_ES` marks the Quest's GL paths, which visionOS shares.
  `GEVR_XR_PC` marks desktop-only code: SteamVR quirks, the mirror window, the eye
  dump, and top-down sub-images.
- **Colours:** swapchains are `GL_RGBA8`, sampled through an sRGB view into the
  `bgra8Unorm_srgb` drawable. The game's colours arrive unchanged.
- **Depth:** the drawable's depth is written: 2 m for the eye images, and each
  quad's own depth. visionOS discards a frame whose depth is all 0 (reverse-Z
  infinity) as empty. Klepton measured this on the device, and builds 6-8 were black
  because of it. The simulator does not do this.
- **Scene role:** the launch scene role is `CPSceneSessionRoleImmersiveSpaceApplication`.
  Using UIKit's `UISceneSessionRoleImmersiveSpaceApplication` made SwiftUI abort at
  launch.

## Build and run (the Mac)

```
tools/build_angle_apple.sh             # once: adds ANGLE's visionOS slices (iOS's, retargeted)
tools/build_visionos_native.sh simulator --app
```

Simulator test:

1. Build with `GEVR_BUNDLE_ID=com.gevr.port.visionos` so Klepton's simulator install
   is left alone.
2. Run `launchctl setenv` with `GEVR_FRIENDS_URL`, `GEVR_REPORTS_URL` and
   `GEVR_UPDATES_URL` set to `http://127.0.0.1:9`.
3. Install the app and copy the ROM into its Documents folder
   (`xcrun simctl get_app_container ... data`).
4. Launch with `--boot-stage 33`.
5. Read the log from `Documents/gevr.log`.

## State (2026-11-16)

- Builds for the simulator. The app links and launches straight into the space.
- Dam ran with the stereo VR mode on: eyes 3840x2160 (the simulator's single view)
  and 3 layers.
- The picture in the simulator was upright, with correct colours, the hands in view,
  at about 27 Hz.
- The simulator has one view and a fixed head (ARKit gives the origin), so stereo and
  6DoF need the device.

- TestFlight 0.5.18 (6), uploaded 2026-11-16 under `com.mrsco.goldeneyevr` with the
  user's OK. It replaces the Klepton build on the tester's headset. It was made with
  `tools/build_visionos_native.sh device --archive 6 --upload` and the wrapper's
  signing environment. The archive, with its dSYMs, stays in
  `build/visionos-device/testflight-0.5.18-6`.

## Next

1. **Tester's report on build 6.** Check stereo, head tracking, frame rate, and the
   picture's orientation and colour on the device.
2. **Sense controllers and the picker on the device.** Both were added on
   2026-11-16. In the simulator the picker window opens beside the space
   (`gevr_input.txt` with `20` in Documents is the launcher's hook), and accessory
   tracking does not exist there.
3. **Logs.** `gevr.log` and the crash records sit in the app's Documents folder,
   which the Files app shows. The crash report's foreground marker follows the
   session state.
4. **Swapchain size and foveation.** A layer per eye at the drawable's size; then
   rasterization rate maps if ANGLE allows them.
5. **Depth for the compositor's reprojection.** Today depth is cleared to "far", which
   gives rotation-only reprojection.
