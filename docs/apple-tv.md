# GoldenEye VR on Apple TV

The Apple TV build is the iPhone and iPad game ([mobile-platforms.md](mobile-platforms.md),
"iOS and iPadOS") built for tvOS: the PC flat game, OpenGL ES 3 through ANGLE
on Metal, SDL's UIKit app around it. A TV has no touch screen, no Files app,
and keeps only caches on the device, so those three things are what differ.
CMake's `tvos` platform sets `GEVR_IOS` and `GEVR_TVOS`; everything under
`GEVR_IOS` applies unless `GEVR_TVOS` says otherwise.

## Where it stands (2026-11-18)

- **Builds** for the tvOS 27.2 simulator and, unsigned, for the device
  (Xcode 27.2), with ANGLE's own tvOS slices. The app and both frameworks are
  tvOS 17.0 Mach-Os.
- **Tested in the Apple TV 4K (3rd generation) simulator:**
  - The launcher comes up at 3840x2160 through ANGLE on Metal, with the Apple
    TV wording.
  - The upload dialog works end to end: "Choose ROM file..." showed the TV's
    address, the page answered, and a 12 MB ROM POSTed to it arrived whole.
    The dialog closed, and the launcher adopted the ROM.
  - Dam played with `--boot-stage 33` and a scripted walk, turn and shot: the
    level, the gun and the HUD drawn at 16:9.
- **Not yet tested:**
  - the Siri Remote and a game pad (see "Testing in the simulator");
  - a real Apple TV.
- **TestFlight:** 0.5.19 (1), uploaded 2026-11-18 under `com.mrsco.goldeneyevr`
  with the user's OK, from the combined testing branch. It was made with
  `tools/build_tvos.sh device --archive 1 --upload` and the Vision Pro wrapper's
  signing environment, with the tvOS profile "GoldenEye VR tvOS App Store"
  (the iPhone and Vision Pro profile does not cover Apple TV). It validated and
  uploaded without errors; nobody has run it on a device yet.
  0.5.20 (2) followed with the release, from its tag.

## Build (on the Mac)

1. Once: the tvOS simulator, `xcodebuild -downloadPlatform tvOS` (3.8 GB).
2. Once: ANGLE's Apple TV slices, `tools/build_angle_apple.sh tvos-device tvos-simulator`.
   - They go beside the iOS ones in `~/angle` (`ANGLE_DIR`), as
     `xcframeworks-tvos`.
   - The pinned revision's gn builds tvOS itself (`target_platform = "tvos"`,
     with the `use_blink` it asks for); no retargeting as for visionOS.
3. `tools/build_tvos.sh simulator --app`, or `device --app`. A signed device
   build is the iPhone one's (`~/src/gevr-ios-device.command`) with this script:
   `DEVELOPMENT_TEAM=<team> GEVR_SIGN="-allowProvisioningUpdates CODE_SIGN_STYLE=Automatic" tools/build_tvos.sh device --app`.

Details of the build:

- **Library:** CMake builds `build/tvos-<kind>/libgevr_ios_all.a`, the iOS
  library's sources plus `port/tvos/*.mm`.
- **App:** xcodegen makes it from `port/tvos/project.yml`.
- **Bundle id:** `com.mrsco.goldeneyevr`, the same App Store Connect app as the
  iPhone, iPad and Vision Pro builds.
- **Deployment target:** tvOS 17.0.

## How it differs from iOS

**The ROM and other files** (`port/tvos/tvos_upload.mm`). Every launcher picker
(the ROM, GE-X, hack campaigns, the XBLA copy, the Community Edition updater)
calls `gevrIosChooseFile`, as on iOS. On Apple TV it does this instead of
opening a document picker:

- A dialog shows the TV's address: `http://<ip>:8007`, or any free port if
  8007 is taken.
- A phone or computer on the same network opens it and sends the file from a
  small page. The page sends the raw file in one POST, with a progress bar on
  both ends.
- The file lands in the app's temporary folder under its own name, and the
  launcher copies or imports it as it does iOS's picked copy.
- The server runs only while the dialog is open.
- Controller presses reach UIKit only while the dialog is up, so the remote or
  a pad can press Cancel.

**Storage** (`port/src/main.c`):

- The data folder is `Library/Caches/GoldenEyeVR`. A tvOS app can't write
  Documents on the device (the simulator allows it), so both use the cache
  folder.
- tvOS may empty caches when the device runs short of space. The ROM then has
  to be sent again, and settings and saves go back to their defaults.
- Keeping them would need iCloud (key-value storage holds 1 MB), which is not
  done.

**Controls** (`port/ios/ios_app.m`, `port/src/input.c`,
`port/flat/window/flat_launcher.cpp`):

- **Siri Remote:** a pad (SDL's `SDL_HINT_TV_REMOTE_AS_JOYSTICK`): the touch
  surface or ring, the click as A, Back as B, Play/Pause as X. It drives the
  launcher. It is read through GameController, which takes a real remote
  whatever UIKit's responders do. As keys (the hint at 0), the remote's presses
  never reached SDL's view in the simulator, though the system's Settings took
  the same presses.
- **Swipes step** (`flat_launcher.cpp` `tvSwipeTrack`): SDL reports the touch
  surface as the finger's travel from where it landed. In the launcher and the
  in-game panel each third of the surface swiped is one d-pad press, as in
  tvOS's own menus (a held half-way swipe repeated, a short one did nothing).
- **Friends panel:** right on a page's right-most item crosses into it, left on
  its left-most item comes back (`gevr_friends_ui.h` `gevrFriendsNavCross`, on
  every platform: the panel is its own ImGui window, out of a pad's reach).
- **Vibration:** Controls has a Pad vibration row on every flat game. Apple's
  full pads are asked to rumble even when SDL reports none; the log has each
  pad's rumble on assignment and its first refusal.
- **Back stays in the app:** controller presses are the game's alone
  (`SDL_HINT_APPLE_TV_CONTROLLER_UI_EVENTS` 0), so Back doesn't leave the app.
  The TV button still does.
- **Player 1:** the remote connects first, so `inputTvPadFirst` gives the first
  slot to a full pad (two sticks) whenever one is connected. The remote is
  player 1's only when no pad is there.
- **The launcher reads every pad**, not only the first.
- **No touch overlay, gyro or Taptic Engine.** The overlay never shows, so a TV
  player counts as a pad player online. The touch and gyro rows are hidden from
  the settings.

**Odds and ends:**

- `syscall(SYS_gettid)` is refused on tvOS. It is only for Linux's `/proc` in
  the frame watchdog, so it's left out.
- Online, Apple TV says it is an iOS device (`NET_PLATFORM_IOS`), so the
  network protocol is unchanged.

## Testing in the simulator

- **Run it:** install with `xcrun simctl install`. Pass the game's test
  arguments to `xcrun simctl launch` after the bundle id (`--boot-stage 33`,
  `--input-script <path>`).
- **The data folder:** `xcrun simctl get_app_container <udid>
  com.mrsco.goldeneyevr data`, then `Library/Caches/GoldenEyeVR`. The log,
  scripts and ROM go there.
- **The upload dialog:** a script's `pick !` opens the real picker rather than
  answering it (port/pc/pc_testinput.c), with `point`/`click` on "Choose ROM
  file...". Then POST a file from the Mac:
  `curl --data-binary @rom.z64 "http://127.0.0.1:8007/upload?name=rom.z64"`.
- **Pictures:** take them with `xcrun simctl io <udid> screenshot`. A script's
  `shot` comes out black on ANGLE's Metal backend.
- **The remote can't be tested here:**
  - `agent-device tv-remote` (XCUIRemote) presses go through UIKit and never
    reach GameController.
  - The simulator also lists a "Gamepad (MFi)" extended pad of its own, which
    takes player 1.
  - The remote and real pads need an Apple TV, or the Simulator's own remote
    window.

## Icons

`python tools/make_icons.py tvos` writes `port/tvos/Assets.xcassets`:

- **Home screen:** a layered icon that tilts under the focus. The app icon
  (`docs/art/app_icon.jpg`) floats in front of a blurred copy that fills the
  TV's wider shape.
- **Top Shelf:** the README's Dam banner (`docs/banner.png`).
- **Sizes:** every size App Store validation asks for, at 1x and 2x.
- **Resolution:** the banner is 420 pixels tall, so the 2x Top Shelf images
  (1440) are upscaled. A taller source would sharpen them.

## Not yet

- **Voice chat:** an Apple TV has no microphone of its own, and opening SDL's
  capture without one ended the app when a game started (reports 86a85fd7,
  51bf4ffb). It listens only unless AVAudioSession reports an input
  (`pc_services.cpp`). Talking from a connected headset is untried.
- **LAN discovery:** as on iOS, it needs Apple's multicast entitlement. The
  internet lobby and direct connect don't.
- **Updates:** they come through TestFlight or the App Store; the launcher has
  no update line, as on iOS.
- **Going back to the launcher** closes the app, as on iOS.
