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
| `vos_input.mm` | Answers a Touch pair. For now it reads a game pad through GameController. With no tracked controllers, the hands are held in front of the head. |

Conventions:

- **Image rows:** the runtime reads images bottom-up, as the Quest's runtime does. In
  `vr_openxr.cpp`, `GEVR_XR_ES` marks the Quest's GL paths, which visionOS shares.
  `GEVR_XR_PC` marks desktop-only code: SteamVR quirks, the mirror window, the eye
  dump, and top-down sub-images.
- **Colours:** swapchains are `GL_RGBA8`, sampled through an sRGB view into the
  `bgra8Unorm_srgb` drawable. The game's colours arrive unchanged.
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

## Next

1. **Device build and TestFlight.** Signing goes the way `tools/build_visionos.sh`
   does it, then the tester runs the build.
2. **PSVR2 Sense controllers (V2).** GameController's spatial gamepad for buttons,
   ARKit's accessory tracking for poses, and haptics. Klepton's
   `KleptonControllers.swift` is the reference.
3. **ROM picker.** A window opened beside the space, as Klepton's "kimport" does. For
   now the ROM goes in through the Files app.
4. **Swapchain size and foveation.** A layer per eye at the drawable's size; then
   rasterization rate maps if ANGLE allows them.
5. **Depth for the compositor's reprojection.** Today depth is cleared to "far", which
   gives rotation-only reprojection.
