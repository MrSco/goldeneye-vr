# GoldenEye VR on phones, tablets and Apple Vision Pro

Research and plan only (2026-11-09); no game code changes. It extends
[pc-platforms.md](pc-platforms.md) and uses its structure: one CMake
`GEVR_PLATFORM` switch, `gevr_platform.h` as the "no headset" interface, and
`port/flat/` as the flat implementation the server and PC already share. Flat
targets start the way the dedicated server and flat PC do, without the in-VR
launcher.

## Where it stands (2026-11-09)

**Android phone/tablet (M1), first pass in** on branch `claude/pc-platforms`:

- **Build:** `cd android && ./gradlew assembleMobileDebug` makes the separate
  APK (`com.gevr.port.mobile`, flavor `mobile`, CMake `GEVR_MOBILE`): the PC
  flat game's window, launcher and services on the Quest's GLES 3 path,
  without OpenXR.
- **Rendering:** a single view, without multiview; the window's height is
  capped at 1440. It runs landscape, either way up, full screen.
- **Touch:** controls in `port/flat/window/flat_touch.cpp` (see "Touch
  layout").
- **Tested** on a Pixel 8 emulator (Android 16): launcher, ROM, intro, menus
  by touch, and Dam (look, walk, fire, reload, crouch).
- **Screen shape:** 4:3 or 16:9 on every flat mode (launcher Screen page,
  `ScreenShape`), ported from gepc-ref's native widescreen (D334/D335).
  - At 16:9 the world widens sideways (Hor+), the HUD keeps its shape at the
    edges and the front end is pillarboxed.
  - Flat builds default to 16:9; the headset's virtual screen stays 4:3 until
    its cost is measured.
- **In-game panel:** hold the on-screen START (a tap is still START), or on a
  PC F10 or a pad's Back, for the headset's panel (settings, Friends, back to
  the launcher); a solo mission pauses under it.
- **Touch extras:** a buzz on a press (Touch vibration, on) and Gyro aim
  (off) on the Controls page; the panel's "Edit touch layout" moves and
  resizes the buttons (saved as `TouchLayout`). A flat game shows no
  tracked-controller rows.
- **Picker and home screen:** the window's surface is made again on return
  (it stayed black before).
- **Not yet:**
  - A real phone (gyro's feel, a pinch on a button), an online match
    against a Quest, and Bean import (the emulator's file picker would not
    take a tap on the zip).
  - A START tap on the briefing sometimes needs a second one. (The first
    tap on a launcher tab now selects it.)

**iOS and iPadOS (M3), first pass in** on the same branch:

- **Build** (on the Mac): `tools/build_angle_apple.sh` once, then
  `tools/build_ios.sh simulator --app` (or `device --app`).
  - ANGLE lives in one place for every project, `~/angle` (`ANGLE_DIR`),
    never in the repo: rebelancap's revision, Metal only, linked with Xcode's
    own linker (that revision's lld cannot read the iOS 27 SDK), and the
    Metal toolchain must be downloaded once
    (`xcodebuild -downloadComponent MetalToolchain`).
  - CMake's `ios` platform (`GEVR_IOS`) builds the PC flat game as one static
    library, `build/ios-<kind>/libgevr_ios_all.a`. xcodegen makes the app from
    `port/ios/project.yml`: `com.gevr.port.ios`, landscape, iPhone and iPad.
- **Rendering:** OpenGL ES 3 through ANGLE on Metal (`port/ios/ios_angle.mm`).
  `port/ios/ios_gl.h` sends the game's `SDL_GL_*` calls to EGL; SDL is built
  without EAGL. It is the single-view path (ANGLE's Metal backend has no
  multiview), as on the Android emulator.
- **App shell** (`port/ios/ios_app.m`): the scene manifest the iOS 27 SDK
  requires, SDL's window moved onto the scene, SDL's Bluetooth pad backend
  off. Drawing waits while the app is in the background.
- **Files:** Documents is the data folder: the ROM, `goldeneye-vr.ini`, saves
  and logs. The Files app shows it as "GoldenEye VR". The launcher's pickers
  use the system's document picker.
- **Touch:** the Android phone's controls. A touch's mouse never fires or
  takes the pointer; an iPad's real mouse and keyboard do.
- **Services:** HTTPS through NSURLSession (`port/ios/ios_http.mm`) for the
  lobby, friends, mods and reports.
- **Played on an iPhone 16 Pro Max** (user, 2026-11-09): Dam and its
  tunnel with Bean, 60 fps at the panel's 2868x1320, touch, gyro (Off /
  While aiming / Always, with a speed), the Taptic Engine on a press, Show
  stats. A signed build: `~/src/gevr-ios-device.command` on the Mac, opened
  from its desktop session (ssh cannot reach the signing keychain), with the
  personal team only (`DEVELOPMENT_TEAM`); it installs over `devicectl`.
- **Lost touches:** iOS sometimes never tells the app a finger lifted (the
  stick, AIM held for good; ten lifts in one fight). Every touch is held and
  its phase asked each poll, as perfect-dark-ios's D-034 watchdog does; a new
  finger also takes over the stick or a held button, and the oldest gives
  way when all ten slots are taken (Android too).
- **Online against a Quest** (user, 2026-11-09: the Quest on test.32
  hosting, the iPhone joining by code): play, look and aim, voice both
  ways, the match's volume rows. Two bugs found and fixed for every flat
  joiner: mouse/touch look was read for player 1 only, and SDL reported the
  iPhone's mic at 16 kHz while it delivered 48 kHz (the voice now goes
  through a converter from the rate measured in its first 1.5 s).
- **Not yet:** Steam Audio's iOS library, LAN discovery (iOS wants Apple's
  multicast entitlement), an iPad, the launcher's update rows (meaningless
  on iOS), and coming back from "Restart to launcher" (an iOS app cannot
  start itself: it closes, and the player opens it again).

## Recommendation in one paragraph

Do **Android phones/tablets** right after PC P1. They are the PC flat build
on the Quest's own GLES path. Next, **move the Java-only services to shared C**
(PC P3 needs this anyway). Then do **iOS/iPadOS** as a sideloaded IPA built on
GitHub's macOS runners, rendering through ANGLE on Metal as
rebelancap/Project-Midas-ios does. After that comes **visionOS in a window**,
then (originally) Midas's **stereo panel** and **6DoF immersive**; both
are dropped for now by the user's decisions below, and Vision Pro stays at
most a flat window. One protocol bump at the release carries an input-class byte in
HELLO and the lobby listing, and a host's "VR players only" rule.

## Where it fits

`GEVR_PLATFORM` values. A binary that can run VR and flat chooses at run
time, like TC2's `--desktop` (pc-platforms.md).

| Value | Binary | Modes | Graphics | XR runtime |
|---|---|---|---|---|
| `quest` | APK, flavor `quest` | VR, virtual screen | GLES 3.0 + `GL_OVR_multiview2` | OpenXR (Meta) |
| `server` | Linux exe | headless | none | none |
| `pc` | Windows exe | flat, PCVR | GL 3.3 core | OpenXR (any) |
| `android` (new) | APK, flavor `mobile` | flat | GLES 3.0, the Quest path without multiview | none |
| `ios` (new) | IPA (iPhone, iPad) | flat | see "iOS graphics" | none |
| `visionos` (new) | visionOS app | window, stereo panel, then immersive | ANGLE on Metal | Compositor Services + ARKit, not OpenXR |

Shared pieces every new target reuses, in the order PC creates them:

- `gevr_platform.h` + `port/flat/`: frame to a window, no head pose
  (pc-platforms P0/P1).
- The flat launcher: ImGui drawn to the window, `imgui_impl_sdl2` already
  vendored (P1).
- A new `port/flat/touch*`: touch overlay + gyro, flat-only. It feeds the
  same virtual controller that `port/src/input.c` (SDL2 GameController, 27
  call sites) already reads.
- The C services layer (below), which replaces `gevrJavaCommand` /
  `gevrJavaString` in `vr_launcher.cpp` 505-560.

## Per target

### Android phone and tablet

| Piece | Plan | Port from | Size |
|---|---|---|---|
| Build | Gradle product flavors `quest` / `mobile` over one CMake tree. `mobile` drops the OpenXR loader, the `com.oculus.*`/`IMMERSIVE_HMD` manifest entries, passthrough and the OpenXR permissions. Same `arm64-v8a` (`android/app/build.gradle` 37). 16 KB page alignment for Play. | Quest build | S |
| Graphics | GLES `300 es` path of `port/fast3d/gfx_opengl.cpp` as is, with no multiview. Optional extensions already fall back (depth clamp 4531-4556, clip distance 4607, buffer storage 651). Phone GPUs vary (Adreno, Mali, Xclipse, PowerVR): test on Mali too. | Quest | S |
| Window | `SDLActivity`/`MainActivity` without `nativeSetVrJavaContext`. Rotation locked landscape, cutout-safe insets for the HUD and overlay. | SDL2 | S |
| Input | Gamepads: SDL2 already (Bluetooth Xbox/PS/8BitDo). Touch: overlay in C (see "Touch layout"). Gyro aim: later, SDL2 sensor API (no reference port has it). **Delete** the dead stubs `TouchControls.java`/`GameView.java`: from the initial commit, nothing calls them, and their native methods do not exist. | PD Android `port/src/touch.c` | M |
| Build note | Link with `-Wl,-z,max-page-size=16384`; Play requires 16 KB pages for apps targeting Android 15+ (enforced from 2025-11-01). Harmless for sideloading. | | S |
| ROM/XBLA import | Already Storage Access Framework: `MainActivity.openPicker` (190-240) copies to `data/picked*.z64`; Bean/CE import gets an fd (`ModManager.nativeBeanImport(int fd, ...)`). No change. | Quest | S |
| Audio | SDL2 audio. Steam Audio `android-armv8/libphonon.so` is already linked (`CMakeLists.txt` 464-468). | Quest | S |
| Network | Same ENet/libjuice/Opus. Android drops a backgrounded app's sockets; host migration and rejoin already cover a host or guest that vanishes (MULTIPLAYER.md). `RECORD_AUDIO` is already requested at runtime. | Quest | S |
| Distribution | Sideloaded APK from GitHub releases, like Quest. Play Store: see risks. A Play build may not self-update (`REQUEST_INSTALL_PACKAGES`, `UpdateChecker`) and must drop it. | | S |
| CI | Same Gradle job, second flavor, on `ubuntu-latest`. | | S |

### iOS and iPadOS

| Piece | Plan | Port from | Size |
|---|---|---|---|
| Build | Midas-ios layout: CMake builds the game into a static library, xcodegen (`app/project.yml`) makes the Xcode app around it, one script per target (`scripts/build-ios.sh {simulator,device,xrsimulator,visionos}`). SDL2 2.32.8 static, the version the server already fetches (`CMakeLists.txt` 195-199). Our game core needs none of Midas's 4 GiB guest-window work: it is already 64-bit/endian clean (server on ARM64). | Midas-ios | M |
| Graphics | See "iOS graphics" | Midas-ios | S-M |
| Input | Touch overlay (shared with Android). MFi/Xbox/PS/Switch pads through SDL2's GameController MFi backend, as Midas does (`app/ios/GEController.h`, plus its own `gamecontrollerdefault.txt`). iPad keyboard/mouse: SDL2's relative mouse (pointer lock on iPadOS 14+); Midas has no code for it, so test it. | Midas-ios, SDL2 | M |
| ROM/XBLA import | Midas `GEOnboarding.m`: `UIDocumentPickerViewController initForOpeningContentTypes:asCopy:YES`. Info.plist `UIFileSharingEnabled` + `LSSupportsOpeningDocumentsInPlace`, so Files shows our data folder (saves, ini, logs), and `CFBundleDocumentTypes` "Viewer + Alternate" for AirDrop and share-to. The ROM is copied to `Documents/data/picked.z64` and the launcher re-checks it as files arrive. For Bean, the picked copy's fd goes into the existing `gevr_bean_import.c` entry, the same C path as Android's. | Midas-ios | S |
| Audio | SDL2 CoreAudio. `AVAudioSession` category handling as in Midas `app/ios/GEAudio.m` (`playAndRecord` while voice is on). `NSMicrophoneUsageDescription`. Steam Audio ships iOS arm64 as a static `libphonon.a`. visionOS is not listed, so voice there uses the directional-stereo fallback that already exists (`net_spatial.c` 75-84). | SDL2, Midas | S |
| Network | libjuice, ENet and Opus are POSIX/C and build for iOS. **LAN discovery** sends UDP broadcast on 27008. iOS 14+ needs `NSLocalNetworkUsageDescription` and, for broadcast/multicast, the `com.apple.developer.networking.multicast` entitlement, which Apple grants on request to paid accounts only. So iOS uses the internet lobby and direct IP (and receives no beacons). A backgrounded app is suspended within seconds and its sockets go dead. Treat it as a leave (host migration exists). | | S |
| Services | Shared C services layer with an `NSURLSession` HTTP backend (one `.m`). | Dab's Mod `ghostnet.c` split | M (shared) |
| Updater | iOS can't install an update itself. The updater only says "vX available". Users update through the SideStore/AltStore source. | | S |
| Distribution | Unsigned IPA on each GitHub release, plus an AltStore/SideStore source JSON on goldeneyevr.com. See "Distribution". | Midas-ios | S |
| CI | `macos-15` runner (free for this public repo): `xcodebuild` builds an unsigned archive, packs it as an IPA, and sends the symbols (dSYM) to the reports repo like the `.so` symbols today. Midas has no iOS CI (it builds IPAs on a Mac), so this part is ours. Dab's Mod's `macos-14` jobs are the nearest pattern. ANGLE is a roughly 18 GB checkout, so build its xcframeworks once (Midas `scripts/make-angle-xcframeworks.sh`), publish them as a release asset in a deps repo, and have CI download them. | Dab's Mod CI, Midas scripts | M |

### iOS graphics

- **Apple's own OpenGL ES 3.0** (deprecated since iOS 12, still shipping) runs
  our `300 es` shaders unchanged through SDL2's EAGL view. It is the least
  work. The risk is that Apple removes it, and visionOS has none: SDL3
  `include/build_config/SDL_build_config_ios.h` 179-181 defines
  `SDL_VIDEO_OPENGL_ES` only `#if !TARGET_OS_MACCATALYST && !TARGET_OS_VISION`.
- **ANGLE on Metal** keeps the same GLES API on Metal, and Midas-ios proves
  it on iPhone, iPad and Vision Pro. It is one path for iOS and visionOS.
  How Midas sets it up (`app/gfx/gfx_angle_egl.mm`):
  - `SDL_Metal_CreateView` gives a `CAMetalLayer`, with `SDL_WINDOW_METAL`
    and no `SDL_WINDOW_OPENGL`;
  - `eglGetPlatformDisplayEXT(... EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE)`;
  - an ES 3.0 context, so our `300 es` shaders fit.

  This replaces `gfx_sdl2.cpp`'s GL context creation on Apple only.
- **Vulkan through MoltenVK** is the third option: Dab's Mod already has a
  5,361-line `port/fast3d/gfx_vulkan.cpp` for the same Fast3D, which loads
  `libMoltenVK.dylib` on Apple (4566). That is a second renderer to
  maintain, so it is only worth it if ANGLE fails.

**Recommendation:** ANGLE on Metal on both iOS and visionOS, so Apple has
one GL path; Apple's GLES is a fallback for a first spike only.

### visionOS in a window

- **Native target, as Midas does it:**
  - Its own `Info-visionos.plist` and a SwiftUI `WindowGroup`.
  - visionOS 26 minimum, because the ANGLE xrOS slices are built for 26.0.
  - SDL2 2.32 has no visionOS (no `TARGET_OS_VISION` in its UIKit backend),
    so Midas carries `patches/sdl2/visionos-compat.patch` and
    `scripts/build-sdl2-visionos.sh`. We port that patch rather than move
    to SDL3.
  - SDL3 has visionOS (`SDL_PLATFORM_VISIONOS`,
    `src/video/uikit/SDL_uikitvideo.m` 194-209), but its build config
    turns GL ES off there
    (`SDL_build_config_ios.h` 179-181: `#if !TARGET_OS_MACCATALYST &&
    !TARGET_OS_VISION`). On visionOS, ANGLE is the only GL.
- **Distribution:** Midas ships a separate `-visionOS.ipa` through a
  SideStore source (`rebelancap/all-ports` `apps-visionos.json`). SideStore
  itself goes onto Vision Pro with iloader.
- **Input:** gaze-and-pinch arrives as touches, and an on-screen stick is
  poor there. Gamepads (Vision Pro pairs MFi/PS/Xbox pads) are the real
  input; the touch overlay stays as a fallback.
- **Size:** M on top of iOS.

### visionOS stereo panel (the step between)

Midas's "3D mode" (`app/vision3d/*`, `GEVisionApp.swift`) is the nearest
existing work. It opens `ImmersiveSpace(id:"GE-3D") { CompositorLayer(...)
}` with `.immersionStyle(.mixed)` and shows the game's N64 camera as a
world-locked stereoscopic panel: both eyes are drawn every frame, but it
is not 6DoF. It is what our Quest virtual screen (`port/vr/vr_screen.h`)
does, in stereo. GL reaches Compositor Services like this
(`GEEyeTargets.mm`):

- ANGLE draws into app-owned `MTLTexture`s through
  `eglCreateImageKHR(EGL_METAL_TEXTURE_ANGLE)` and
  `glEGLImageTargetTexture2DOES`. The file notes that
  `eglCreatePbufferFromClientBuffer` does **not** accept
  `EGL_METAL_TEXTURE_ANGLE`.
- A 3-deep ring is synced with `MTLSharedEvent`.
- Compositor Services samples the ring in Metal. RealityKit is not used.

Port this as M5a. It gives Vision Pro owners a 3D GoldenEye with gamepads
before any XR-runtime work. Size: M.

### visionOS immersive (VR)

- **No OpenXR on Apple.** `vr_openxr.cpp` (5,680 lines) can't run. PC P0
  splits it into shared OpenXR code and a graphics binding. visionOS needs
  the split one level higher: an XR-runtime interface (views and
  projections, head/hand/controller poses, frame begin/end, haptics,
  recenter), with OpenXR and Compositor Services implementations.
- **Rendering:**
  - Compositor Services (Metal only: "Render your own content using
    Metal") draws from Metal textures.
  - We use M5a's bridge (ANGLE into app-owned `MTLTexture`s via
    `EGL_METAL_TEXTURE_ANGLE` images, which Compositor samples) with the
    real per-eye projections.
  - ANGLE's Metal backend has **no** `GL_OVR_multiview`: none of the
    `mNativeExtensions` set in `src/libANGLE/renderer/metal/DisplayMtl.mm`
    is multiview, while the Vulkan backend sets `multiviewOVR`. So each eye
    is its own pass. That is PC P2's "per-eye fallback", and visionOS makes
    it mandatory. It costs about twice the draw calls of the Quest
    path, the cost `gevr-perf-lessons` names.
  - Foveation (rasterization rate maps) is Metal-only and stays off.
- **Tracking:** ARKit `WorldTrackingProvider` for the head and
  `HandTrackingProvider` for the hands. visionOS 26 adds PlayStation VR2
  Sense controllers, which give tracked controllers close to Touch. Hands
  only means designing pinch-to-fire, grip and reload gestures from
  nothing, against the user's "port, don't invent" rule. Controllers port
  the Quest's Touch bindings almost one to one.
- **Comfort/UI:** the virtual-screen launcher works as a SwiftUI window
  (flat launcher) next to the immersive space. No head-locked UI
  (user rule).
- **Size:** L. Do it last, after a one-week spike: one Compositor Services
  frame from ANGLE, both eyes, with head pose.

## Touch layout

Port it; don't design it. **Done (first pass):** `port/flat/window/flat_touch.cpp`.

- **Buttons:** Midas's buttons and numbers, less GADGET (GoldenEye has none).
  - FIRE = Z, AIM = R, USE = B, WEAPON = A, START.
  - RELOAD is B with a reload-only flag (`gevrReloadOnlyHeld`, lv.c), so it
    never opens a door.
  - CROUCH flips the Quest's crouch toggle.
- **Stick:** the floating stick sets the C buttons past a quarter, as a pad's
  left stick does. In menus it moves the front end's cursor, FIRE selects
  (A) and USE goes back.
- **Look:** goes in through the flat game's mouse look
  (`inputMouseGetLookDeg`, bondview2.c), which now covers any flat build
  (`GEVR_FLAT`).
- **Drawing:** ImGui draw lists in their own context, over the frame after
  `vr_screen_present`'s blit.
- **Pads:** a pad button hides the overlay, and the next touch brings it
  back. A PC shows it only after a finger lands.

- **Code** from Mr4xd3mn/perfect-dark-android `port/src/touch.c`. It is
  C, and its base is the fgsfds PD port our `port/src` host comes from.
  - It has a floating stick and relative look fed into the port's
    mouselook.
  - It has a button pad, written into `OSContPad`.
  - It shows a menu d-pad while in menus.
  - One C file serves Android and iOS, so the overlay isn't written twice
    (Midas's is UIKit-only).
- **Layout and feel** from Midas-ios `app/ios/GETouchOverlay.m` /
  `GETouchLayout.m` (itself from their GoldenEye-Recomp-ios), which is
  tuned for this game:
  - floating move stick on the left 45%, 62 pt travel, 0.15 radial dead
    zone, response 0.4 linear + 0.6 cubic;
  - drag-look on the right, in view degrees per point;
  - drag-to-look also on the AIM and FIRE buttons, and a mirrored
    left-thumb FIRE while aiming;
  - haptic on press, hidden while a controller is connected, and a layout
    editor.
- **Second GoldenEye reference:** Mozarella-png/Goldeneye-007-Android-Port
  (from gepc-ref) `port/src/touchpad.c`.
- **Gyro:** none of these have it. Add it later through SDL2's sensor API
  as an option.
- **Drawing:** in the flat frame (ImGui, so the launcher's look and
  scaling apply). It writes the same controller state `input.c` gives the
  game, so menus, the launcher and multiplayer don't see touch at all.
- **Licences:** check each source before copying. Midas and its fork are
  MIT; the PD Android fork's port licence is unconfirmed. Credit them in
  CREDITS.md.
- It is hidden while a gamepad is connected.
- GoldenEye's own auto-aim (the N64 default) stays on for touch, and the
  host's rules can turn it off for everyone.

## Distribution

| Target | Channel | Notes |
|---|---|---|
| Android | GitHub release APK (+ SideQuest-like stores, Obtainium) | Same as Quest today. |
| Android | Google Play | Play's current text on ROM-loading apps was not retrieved. N64 emulators have been pulled under its IP policy before, and every decomp port surveyed (PD, Ship of Harkinian, 2Ship, sm64) ships as a GitHub APK, not on Play. A Play build may not self-update. |
| iOS/iPadOS | SideStore / AltStore with a free Apple ID | Unsigned IPA plus a source JSON. Free IDs re-sign every 7 days and hold 3 apps. SideStore refreshes on the device. |
| iOS/iPadOS | Paid developer account (99 USD/year) | 1-year signing, TestFlight (still reviewed), multicast entitlement request. |
| iOS (EU) | AltStore PAL / alternative marketplaces | Needs notarization by Apple and a paid account. Apple has refused to notarize emulators more than once (9to5mac 2025-04, Six Colors 2025-02). |
| iOS | App Store | Guideline 4.7 covers "retro game console and PC emulator apps" (2024 changes). This is a decomp port, not an emulator, with Rare's decompiled code and the GoldenEye name, so 5.2 (IP) applies. Expect rejection. No HarbourMasters or rebelancap port is on the App Store. |
| visionOS | SideStore (installed on Vision Pro with iloader), as Midas does | Separate visionOS IPA from the same pipeline. |

## Building without a Mac

- Xcode (macOS only) is needed for the iOS/visionOS SDKs, the asset
  compiler and signing.
- GitHub's macOS runners are free for public repos (this repo is public:
  `gh api repos/MrSco/goldeneye-vr` reports `"visibility": "public"`), so CI
  builds the IPA, and the user needs no Mac for releases.
- Day-to-day debugging (Xcode debugger, Instruments, Metal frame capture)
  still wants a Mac. Without one, an iPhone build gets debugged by logs
  only: the existing LogReporter path, moved to C.
- xtool builds and signs SwiftPM apps on Linux/Windows. It still needs
  Apple's Xcode download for the SDK and is experimental, so it is not a
  route for an app with ANGLE and an Xcode-generated project.

## Parity matrix

Legend: **have** = works today; **free** = comes with the shared C
services; **new** = needs work here; **n/a**.

| Feature | Quest VR / screen | PC flat | PCVR | Server | Android | iOS/iPadOS | visionOS window | visionOS immersive | What it needs |
|---|---|---|---|---|---|---|---|---|---|
| Internet lobbies (list/host/join) | have | P3 | P3 | have (bridge) | free | free | free | free | C lobby client (from LobbyClient.java 394 lines) over a per-platform HTTP primitive |
| ICE/TURN game traffic | have | P1 | P1 | have | have | build libjuice | same | same | libjuice for iOS/visionOS; nothing else |
| LAN discovery (UDP 27008 beacon) | have | P1 | P1 | have | have | **receive no / send no** without the multicast entitlement | same | same | Direct IP always works; paid account + entitlement to enable |
| Direct IP join | have | P1 | P1 | have | have | have | have | have | none |
| Voice chat (Opus, SDL capture) | have | P1 | P1 | n/a | have | `playAndRecord` session + mic string | same | same | Steam Audio binaural where available, plain pan elsewhere (`net_spatial.c`) |
| Friends + invites (Worker) | have (FriendsClient.java 449) | P3 | P3 | n/a | free | free | free | free | Move FriendsClient to C |
| Discord link/invites (Social SDK) | have | P3 | P3 | n/a | have (Android lib) | Social SDK iOS lib | check visionOS | check | Per-platform SDK binary from build/discord/ |
| Join links goldeneyevr.com/join | have (intent filter) | P3 (URL scheme) | P3 | n/a | have | universal link / URL scheme | same | same | Apple: associated domains need a paid account; a custom URL scheme works sideloaded |
| Bean (XBLA) import | have (fd) | P1 (path) | P1 | n/a | have | fd from security-scoped URL | same | same | none in C |
| HD / AI texture packs (download + install) | have (ModManager) | P3 | P3 | n/a | have | free | free | free | Move ModManager's download/unzip to C (miniz is vendored) |
| GE-X / Goldfinger / TND / Mirror ROM packs | have | P1 | P1 | content hash | have | picker | picker | picker | Same picker per platform |
| Crash/log reporting | have (LogReporter.java 274, crash.c) | P3 | P3 | have | have | free + dSYM upload | same | same | C reporter; signal handler in `crash.c` works on Apple (no Breakpad needed) |
| Updater | have (self-installs APK) | P3 (zip) | P3 | have (script) | have (sideload) / none (Play) | notice only | notice only | notice only | C release check; install step per platform |
| Dedicated-server joins | have | P3 | P3 | host | free | free | free | free | Lobby listing's `dedicated` flag already exists |
| Bots, Horde, co-op | have | P1 | P2 | have | have | have | have | have | none (game core) |
| Force pull, hand gestures | VR only | n/a | have | n/a | n/a | n/a | n/a | controllers: yes; hands: new | XR backend |

### Java-only parts that move to shared C

Today the launcher talks to Java over one-line commands (`lobbyCommand`,
`lobbyEvent`, `modsCommand`, `modsStatus`, `reportCommand`, `reportStatus`,
`updaterCommand`, `updaterStatus`, `friendsCommand`; `vr_launcher.cpp`
505-560 and call sites). The dedicated server already replaces Java with
pipes to `tools/gevr_lobby_bridge.py` using the same lines
(`port/server/server_lobby.c` header). Keep the line protocol and move the
logic:

1. `port/src/services/gevr_http.h`: one blocking `http(method, url, body,
   token)` on a worker thread, with platform backends: WinHTTP (PC, from
   Dab's Mod `ghostnet.c`), libcurl (Linux server; Dab's Mod
   `CMakeLists.txt` 300-330), `NSURLSession` (Apple), JNI to
   `HttpURLConnection` (Android: no TLS library to ship). JSON: a vendored
   small parser (cJSON, MIT).
2. Move to C in this order. Each stays a thread and a queue of lines, so
   the launcher code doesn't change:
   - `LobbyClient` (signaling, heartbeats, TURN credentials);
   - `FriendsClient`;
   - `LogReporter` (POST to `/v1/reports`);
   - `UpdateChecker` (GitHub releases API, version compare from
     `UpdateVersion.java`);
   - `ModManager` (pack downloads, miniz unzip, GE-X patching already in
     C).
3. Android keeps Java only for what is Android: the SAF picker, the APK
   installer, permissions, `restartToLauncher`, `triggerResize`.
4. The server can drop `gevr_lobby_bridge.py` for the libcurl backend
   later. Optional, since it works.

Size: M. It is the same work as PC P3's "C lobby client, updater,
crash/log upload". Do it once, in P3, with the backends above in mind.

### Lobby and protocol changes (the one bump at the release)

- **HELLO:**
  - Add `u8 input_class`: 0 VR (Quest/PCVR/visionOS immersive), 1
    mouse+keyboard, 2 gamepad, 3 touch.
  - Add `u8 platform`: quest, pc, android, ios, visionos, server.
  - These are separate from `NET_CLIENT_CAP_*`, which are capabilities, not
    identity.
  - Put them in the HELLO tail beside the content block
    (`net_protocol.h` 121-133), which is already additive.
  - A flat player's input class can change mid-match (pad connected);
    re-send it in a lobby message for the scoreboard icon only.
- **Host rule:**
  - "VR players only" (`VrMpVrOnly`) refuses a HELLO whose
    `input_class != 0` with `NET_DISCONNECT_VR_ONLY`, so the joiner sees why.
  - Lives in the match config, so a dedicated server's operator can set it.
- **Lobby Worker** (`services/lobbies/src/index.ts`; done and deployed
  2026-11-10 with the user's OK, version 5416c6e6, backwards compatible):
  - a `vr_only` column (the `ALTER TABLE` pattern of `dedicated`);
    `create` and the heartbeat `PUT` take `vrOnly` (a heartbeat without it
    keeps it); list, resolve and activity return it.
  - `GET /v1/lobbies?inputClass=N` leaves VR-only games out of a flat
    player's list (1 to 3), and `POST /joins` with a flat `inputClass` is
    refused early (403 "This game is for VR players only"). No class or 255
    (every older client) is let through, as the host's own check does.
  - The game sends them: `create|...|vrOnly`, `refresh|...|vrOnly`,
    `list|version|inputClass`, `join|code|version|inputClass`
    (`vr_launcher.cpp`, `LobbyClient.java`, `pc_lobby.cpp`).
  - Flat players (2026-11-10, Worker version ea914842, also backwards
    compatible): the heartbeat takes `flatPlayers` (0 to `players`; without
    it the last count stays, lowered to `players`), the reads return it,
    and `GET /v1/lobbies?hideFlat=1` leaves out games with any. The host
    counts itself when it plays flat and each guest by its HELLO's class
    (`net_core.c netGetFlatPlayerCount`; a guest from before the byte, or
    one kept through a host change, is not counted). The game sends
    `refresh|...|vrOnly|flatPlayers` and `list|version|inputClass|1`; the
    list shows "VR only" or "N flat" beside a game, and a VR player gets a
    "Hide games with flat players" box (`MpHideFlat` in the ini). The
    dedicated server sends both too (`server_lobby.c`, the bridge), and
    registers with the ini's `MpVrOnly`.
  - Not done: a flat player who connects a pad mid-match keeps the class
    it joined with (the HELLO is not sent again).
- **LAN beacon:** one byte for `vr_only` in the discovery packet
  (`net_discovery.h`). It versions with the protocol.
- **Aim data:**
  - Flat clients already have to fill `PLAYER_STATE`'s barrel
    (`UCMD_AIMVALID`) from the camera ray so VR peers draw them aiming
    right. This is PC P1/P3 work, which mobile inherits.
- **Content hash:** unchanged. Every platform loads the same ROM part, so
  crossplay needs nothing more.

## Recommended order (after the PC phases)

| Step | What | Gate | Size |
|---|---|---|---|
| PC P0-P1 | platform switch, `port/flat/`, Windows flat | (pc-platforms.md) | |
| **M1** Android flat | `android` platform, `mobile` flavor, touch overlay + gyro, delete dead Java stubs | solo mission + online match against a Quest on a phone; Bean import on a phone | M |
| PC P2-P3 | PCVR; C services layer, crossplay flag, CI for all | (pc-platforms.md) | |
| **M2** services everywhere | services layer used by Quest and Android too; Java services deleted | Quest regression (lobby, friends, mods, report, update) | M |
| **M3** iOS/iPadOS | Xcode-generator build, ANGLE/Metal, picker, CI IPA, AltStore source | sideloaded iPhone + iPad joins a Quest lobby; voice both ways | M-L |
| **M4** visionOS window | visionOS target with Midas's SDL2 visionOS patch, gamepad first | Vision Pro plays a solo mission and joins online | M |
| **M5a** visionOS stereo panel | port Midas `vision3d` (ANGLE into `MTLTexture`s into `CompositorLayer`, mixed immersion) | stereo panel at 90 fps with a gamepad | M |
| **M5b** visionOS immersive | spike first; XR-runtime interface; ARKit head + PSVR2 Sense controllers; per-eye passes | spike: 6DoF stereo with head pose; then a deathmatch vs Quest | L |

M1 can start in parallel with PC P2 once P1 has created `port/flat/`.
Android needs no new services work, since its Java services already work.

## Biggest risks

1. **IP takedowns on stores.** A store listing for "GoldenEye VR" (Play, App
   Store, even AltStore PAL) is a public target for Nintendo, Rare,
   MGM or Microsoft. Sideload-only keeps the current posture (GitHub releases,
   SideQuest).
2. **visionOS immersive is a second XR stack**: no OpenXR, no native GL, no
   ANGLE multiview, and (unless PSVR2 controllers are required) no
   controllers. It is the biggest single cost. Midas stops at a stereo
   panel, so 6DoF GoldenEye on Vision Pro has no reference to port from.
3. **ANGLE as a dependency:**
   - an 18 GB source checkout;
   - xrOS slices that need visionOS 26;
   - Midas doesn't name its ANGLE revision.

   Pin a revision ourselves and build the xcframeworks once.
4. **iOS without a Mac:** CI can build, but on-device debugging of GPU or
   crash issues is slow without one.
5. **Free sideloading friction:** the 7-day re-sign, the 3-app limit and 10
   App IDs a week. LAN discovery is off without a paid account's
   entitlement.
6. **Phone GPU spread:** Mali/PowerVR drivers differ from Adreno, the only
   GPU the Quest has tested. The PD and Ship of Harkinian Android ports run
   Fast3D-style renderers on them, but our extension use (clip distance,
   buffer storage) needs per-GPU checks.
7. **Fairness:** touch + auto-aim versus VR hands. The VR-only filter is the
   answer the user chose; aim assist is a host rule.

## Decisions (user, 2026-11-09)

1. **Sideload only**, as on Quest: no Play Store, App Store or AltStore PAL.
   Store policies don't constrain anything.
2. **Apple:** the user has a paid developer account and a Mac mini. So
   1-year signing, the multicast entitlement can be requested (LAN discovery
   on iOS), and iOS builds and debugging happen on the Mac; CI is optional.
   **ANGLE lives in one global place on the Mac mini**, built once and
   shared by every project (as rebelancap keeps one checkout per machine);
   the build finds it through an `ANGLE_DIR` setting, never a copy in the
   repo.
3. **Vision Pro never holds anything back.** No VR on it for now (M5a/M5b
   dropped). At most the flat window (M4), with features switched off there
   where they don't fit; if even that costs too much, the platform is dropped.
4. **Tracked controllers are required in VR, and only there.** VR plays
   with tracked controllers only, never hand tracking. Flat mode on any
   device (phone, iPad, Android tablet, a Windows tablet) takes a gamepad,
   or touch controls when no controller is connected (the touch layout
   section stands). The only online filter is "VR players only": VR players
   apart from flat ones, whatever the flat player holds. No "no touch
   players" option.
5. **Android: two APKs** from one tree (`quest`, `mobile` flavors).

### Corrections after a second look

- **iOS online play is not new ground.** rebelancap/perfect-dark-ios (Dab's
  Mod on iPhone, iPad and Vision Pro, same author as Project-Midas-ios) runs
  Dab's netplay on iOS: direct connect, Dab's lobby server, and LAN once the
  multicast entitlement is granted. Its port layer is the Perfect Dark PC
  port ours comes from, so it is the closer reference for the whole iOS
  shell, ahead of Project-Midas-ios.
- **ANGLE is for iPhone and iPad too, not only Vision Pro.** Both rebelancap
  ports render GLES through ANGLE on Metal on every Apple target
  (`app/gfx/gfx_angle_egl.mm`, `scripts/make-angle-xcframeworks.sh`). Its
  18 GB checkout is a one-time build on the Mac; the app carries two small
  frameworks. Apple's own OpenGL ES is deprecated since iOS 12.
- **Neither fork has iOS CI**: Project-Midas-ios's `main.yml` is upstream's
  PC build (manual only), perfect-dark-ios has only `refresh-sources.yml`.
  With a Mac at hand this matters less.
- **No multiview on ANGLE's Metal backend**, so iOS needs fast3d's
  non-multiview path fixed (pc-platforms.md: it draws nothing today). The
  same fix covers PC GPUs without `GL_OVR_multiview`.

## Research notes (2026-11-09)

**rebelancap/Project-Midas-ios:**

- **Repo:** MIT, 3 stars, created 2026-11-04, 4 squashed release commits
  (1.0.0-1.0.3).
- **Development** was private: the `docs/port-ios/*` it cites are absent,
  and the arm64 work came from a non-public "GoldenEye-Decomp-ios".
- **Sibling ports** by the same author: perfect-dark-ios, Shipwright-ios,
  Starship-ios, sm64coopdx-ios and about 20 more. Its README credits the PD
  one with the visionOS stereo recipe.
- **What we take:**
  - ANGLE/Metal setup (`app/gfx/gfx_angle_egl.mm`);
  - the visionOS bridge (`app/vision3d/GEEyeTargets.mm`,
    `GEVisionApp.swift`);
  - the SDL2 visionOS patch;
  - the touch layout;
  - the document picker and plist keys;
  - the xcodegen project;
  - the SideStore source format.
- **What we don't need:** its 64-bit scheme. A 4 GiB-aligned guest window
  makes `(u32)host_ptr` the guest address, because Apple arm64 maps
  nothing below 4 GiB. It uses `GUEST_PTR`/`GUEST_FIX`, a three-way
  `portSegAddr64`, a 16-byte Gfx, and converts the ROM at boot instead of
  compiled-in assets. Our tree is natively 64-bit already.
- **Networking:** none yet. ENet is absent, and the plist only reserves
  `NSLocalNetworkUsageDescription` and Bonjour `_projectmidas._udp` for a
  "Phase 7" netplay. Our online play would be new ground on iOS, built
  from parts that are all plain C.

**Mobile decomp ports with touch:**

| Repo | Touch | GL | ROM | Ships as |
|---|---|---|---|---|
| Mr4xd3mn/perfect-dark-android (fgsfds base) | C, `port/src/touch.c` into `OSContPad` | SDL2, GLES 2.0+ | system picker copies on first run | GitHub APK |
| Mozarella-png/Goldeneye-007-Android-Port (from gepc-ref) | `port/src/touchpad.c`, `app/src/main/cpp/touch_layout.c` | not checked | not checked | CI builds |
| Waterdish/Shipwright-Android (663 stars), linkzenic 2Ship/Shipwright-Ports | Java XML overlay, JNI `setButton`/`setAxis` | libultraship GLES 3.0 | `ACTION_OPEN_DOCUMENT` | GitHub APK |
| VDavid003/sm64-port-android-base | drawn in-game as GBI (`controller_touchscreen.c`) | SDL2, GLES 2.0 | baked in at build | build it yourself |
| sandstranger/com.mobilerpgpack.phone (GPL-3) | Xbox-layout overlay | GLES or Vulkan via ANGLE | picker | APK |

No N64Recomp/Zelda64Recomp, Starship or SpaghettiKart Android builds were
found. rebelancap's libultraship ports run on iOS/visionOS through
libultraship's Metal backend.

**Facts checked:**

- ANGLE Metal has no multiview (`DisplayMtl.mm`); the Vulkan backend does
  (`vk_caps_utils.cpp`).
- Compositor Services is Metal-only.
- Steam Audio ships iOS arm64 static, macOS and Android; visionOS is not
  listed.
- iOS broadcast/multicast needs the restricted
  `com.apple.developer.networking.multicast` entitlement.
- Suspended apps lose their sockets (TN2277).
- Play's 16 KB page rule applies from 2025-11-01.

Not verified: Play's current ROM-app policy text, Discord Social SDK
availability on visionOS, and iPad pointer lock in SDL2 for this game.
