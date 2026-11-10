# GoldenEye VR on PC: flat desktop, PCVR and Quest from one codebase

Branch `claude/pc-platforms`, started 2026-11-09 from the v0.5.15 batch
(`prerelease/v0.5.15` at `e0125b8e`, which holds `v0.5.15-test.32`). It
follows that batch: when v0.5.15 is released, this branch merges the
release, not main's older state.

## Decisions (user, 2026-11-09)

- **Order:** Windows flat desktop first, then PCVR through OpenXR.
- **Crossplay:** mouse-and-keyboard players can join VR players, and players
  who want a VR-only match can filter flat players out (see P3).
- **PCVR testing:** a Quest over Quest Link or Virtual Desktop. SteamVR
  is a runtime choice for the same build, not separate code.
- **No rebase onto another port** (see "Why not rebase onto Project Midas").
  Desktop ports are references we port from, like gepc-ref.
- **Steam Frame compatible** (user, 2026-11-09): every step keeps the Frame
  in reach (see "Steam Frame").
- **Input by mode** (user, 2026-11-09): VR takes tracked controllers only
  (no hand tracking). Flat mode anywhere takes keyboard and mouse, a
  gamepad, or touch when nothing else is connected (a Windows tablet as much
  as a phone), with the touch layer shared with the mobile builds.
- **Mobile and full parity** (user, 2026-11-09): Android phones, iOS and
  Vision Pro are being researched separately (docs/mobile-platforms.md);
  online play and features at parity across every platform and mode.

## Where it stands (2026-11-09)

P0 and P1's first steps are in: `python tools/build_pc.py` builds
`GoldenEyeVR.exe` (one static exe, about 2 minutes clean); with the ROM
beside it the Quest's launcher opens in the window (ROM, settings,
Start) and the game plays with sound, keyboard, mouse and gamepads.
The Java-side services are ported (lobby, mods, crash reports, friends,
updater; see **Services**), and the XBLA / Bean import with the Community
Edition patch. Not yet: PCVR beyond step 1.

**Release download:** `python tools/package_pc.py [VERSION]` (VERSION
defaults to build.gradle's versionName) builds and packs
`build/pc-dist/GoldenEyeVR-VERSION-windows-x64.zip` (28 MB): both exes
without their debug info (GoldenEyeVR.exe 11 MB, GoldenEyeVR-VR.exe 12 MB),
Steam Audio's phonon.dll (the Quest's 4.8.1, fetched and checked by
build_pc.py), Khronos's openxr_loader.dll (1.1.54), Time Crisis II VR's
launch scripts (`Play VR.cmd`: the VR exe on the active OpenXR runtime, Link
or Virtual Desktop; `Play SteamVR.cmd` + `Start-SteamVR.ps1`: SteamVR's
`steamxr_win64.json` from the Steam path in the registry, set as
`XR_RUNTIME_JSON` for that launch only; the flat exe runs directly),
port/pc/package/README.txt, the licences (with the loader's Apache 2.0) and
a VERSION file. `GoldenEyeVR-VERSION-windows-x64.debug` and
`GoldenEyeVR-VR-VERSION-windows-x64.debug` stay beside it for crash
addresses, as with the server. Checked from a fresh unzip: a ROM dropped in
under any name is found and renamed, START plays. The launcher hides the
rows only a headset has (settings table `.headset`), and the flat exe plays
on the screen (VrPlayMode 0).

**Release step** (with the APK's, STATUS.md "How to work on it"), from the
tagged build commit:
1. `python tools/package_pc.py` (versionName is the release's, so no
   argument; give one only to override).
2. Attach `build/pc-dist/GoldenEyeVR-X.Y.Z-windows-x64.zip` to the GitHub
   release: `gh release upload vX.Y.Z build/pc-dist/GoldenEyeVR-X.Y.Z-windows-x64.zip`
   (or list it in `gh release create`). The updater takes exactly that name
   and checks it against GitHub's own per-asset SHA-256 (`digest`), so the
   zip is attached as built: never re-zipped or renamed. Pre-releases too
   (offered to players with "Offer test builds" on).
3. The two .debug files go with the symbols, to the reports repo's
   `symbols` release beside the Quest's `native-debug-symbols-vX.Y.Z.zip`:
   `gh release upload symbols build/pc-dist/GoldenEyeVR-X.Y.Z-windows-x64.debug build/pc-dist/GoldenEyeVR-VR-X.Y.Z-windows-x64.debug -R MrSco/goldeneye-vr-reports`.
   They are not published with the game. A crash's `gevr_crash.txt` offset
   reads with `llvm-addr2line -e <that .debug> 0x140000000+offset`.
4. Linux: `sh tools/package_linux.sh` (on Linux or in WSL, from a plain
   clone so the build id is the commit's), then upload
   `build/linux-dist/GoldenEyeVR-X.Y.Z-linux-x64.zip` to the release and the
   two `*-linux-x64.debug` files to `symbols` as above.

### SteamVR (2026-11-10, a Quest 3 through Virtual Desktop, SteamVR 2.18.4)

What SteamVR does differently from Virtual Desktop's own runtime and Meta's,
found with the user in the headset, and what the game does about it
(`vr_openxr.cpp`, all only under SteamVR unless said):
- **A quad layer shows the picture of whatever was in its place in the
  layer list before.** The menus come and go from frame to frame (sent when
  redrawn), so a scope lens showed the weapon wheel's picture until the
  wheel first opened, and the launcher came up upside down or cut off until
  a grip or a recenter. The eye dump proved the game's images right
  (`swS0.bmp`: the scope's swapchain held the scope's view). Fix: every quad
  keeps its place every frame; one missing this frame goes in as its last
  copy, a speck behind the head.
- **A screen image 1440 rows tall loses its top fifth.** The PC VR screen's
  swapchain is made once, 1920x1080, every picture stretched over all of it
  (every PC runtime): no sub-rectangle (SteamVR read one upside down and
  halved) and no remaking at a new size.
- **Quad layers draw over the eye images, whatever their order.** The
  launcher draws the pointer's spot itself; the laser's beam stays behind
  the screen (it could be drawn as a quad of its own).
- **The first frames give the head at the floor.** The screen's first
  placement waits for a head above 30 cm (floor spaces, all runtimes).
- **Through a streaming headset the screen's edge smears in one eye on a
  quick turn:** SteamVR bakes the quads into the streamed frame, which the
  headset re-aims; Virtual Desktop's own runtime draws them on the headset.
  Not the game's; a headset on SteamVR directly does not do it.
- Tried and dropped: swapchain priming (each image cleared and released at creation)
  made it worse - blur and both eyes cut - and was taken out.

**Launcher:** `port/vr/vr_launcher.cpp` itself, the Quest's, drawn in the
window (`port/pc/pc_launcher.cpp`: its texture letterboxed into the window,
mouse mapped onto its canvas, arrows/Return/Escape/Tab and a pad for ImGui
navigation). The seven calls it makes into Java have PC bodies beside the
Android ones: the ROM pickers are Windows' Open dialog (copied in as
data/picked*.z64, which the launcher adopts as on the Quest), restart runs
the exe again, the version comes from android/app/build.gradle. The
one-line messages to Java (lobby, mods, reports, friends, voice permission)
go to `port/pc/pc_services.cpp`, where the native services land in P3. The
internet lobby is there (`port/pc/pc_lobby.cpp`, LobbyClient.java ported:
the same commands and events, WinHTTP after Dab's Mod's ghostnet.c, Dab's
bounded JSON reader with SDP-safe strings; offline test
port/tests/pc_lobby_native.cpp). Checked against lobbies.goldeneyevr.com:
the public list, a private lobby hosted and removed on quit, and a second PC
copy joining it by code (ICE, ENet, slot, clock sync, voice open). The
other services follow it (see **Services** below); LAN and direct-address
play need no Java. `--boot-stage` and `--no-launcher` skip
the launcher. Open: the Play page still offers "Stereo VR" (meaningless
until PCVR); the Haptics page is empty (no Touch controllers).

**Services** (P3): the Java services ported line for line into `port/pc/`,
answering the same one-line messages (`pc_services.cpp`). They share
`pc_http.h`: WinHTTP (any scheme and port, redirects, streamed downloads with
progress and cancel), Dab's bounded JSON reader, Base64, SHA-256 (bcrypt),
files on UTF-8 paths, `gevr_prefs.txt` beside the exe in place of Android's
SharedPreferences, and ModArchive's bounded zip extraction on miniz. Offline
tests: `port/tests/pc_lobby_native.cpp` and `pc_services_native.cpp` (109
checks: the Java tests' version order, release choice, archive and crash
policy cases, the redaction patterns, the gzip member, the crash record, the update zip's checks).
- **Mods** (`pc_mods.cpp`, ModManager + ModArchive): the same pack list,
  status lines and commands; packs unpack into `texture-packs/<id>` beside the
  exe, where ext_tex.c, gfx_pc.cpp (`$S/texture-packs`) and Oddjob's hat
  (`texture-packs/oddjob-hat/oddjob_hat.bin`) look. Checked for real: the
  release check (the HD + AI pack's newer 2026.10.19 is offered), Oddjob's hat
  installed and removed, GoldenEye 007 HD (129 MB, 1762 files) installed, and
  a download cancelled half way leaves nothing behind.
- **XBLA / Bean** (`pc_mods.cpp` importOwn / applyCe, ModManager's): the
  Bean row's "Choose copy..." and "CE..." open Windows' Open dialog
  (`pc_launcher.cpp` pcOpenPicker, for MainActivity.openXblaPicker /
  openCePicker) and the chosen file goes to the Quest's native import
  (`gevr_bean_import.c` gevrBeanImportStream, `gevr_bean_ce.c`
  gevrBeanCeApply), opened by path where Android hands over a descriptor:
  the same hidden folder, marker and move into place, the same progress
  ("Importing N%") and Cancel, the same messages. Bean's files land in
  `texture-packs/ge007-bean` beside the exe: the loaders' root
  (`GEVR_BEAN_PACK_DIR`, gevr_bean_ce.h) is `../texture-packs/ge007-bean`
  from the Quest's files/data and `texture-packs/ge007-bean` from the PC's
  exe folder. Checked with the player's own copies on this PC (a scripted
  `pick`, see below): GoldenEye_007_XBLA_Retail.zip imported (880 files, 382
  MB), an import cancelled at 45% leaves nothing, a missing file and a
  non-XBLA file give the Quest's messages, CommunityEditionUpdaterV6.zip
  applied (148 files), and Dam boots with Bean's rooms, guns, heads and the
  CE's sky read from there. The real Open dialog was not opened in a test
  (it is modal on the desktop).
- **Crash reports** (`pc_reports.cpp`, LogReporter + CrashExitPolicy): the
  same status ("offer" after a crash, sending, sent, error:) and commands.
  What Android hands LogReporter the PC makes itself: crash.c's Windows
  handler was never built (nothing defines `PLATFORM_WIN32`) and now is on PC;
  with sysFatalError it writes `gevr_crash.txt` (time, exception, address in
  the exe, the stack walk: `llvm-addr2line -e GoldenEyeVR.exe 0x140000000+offset`
  or the release's .debug) in place of the exit record and tombstone. The
  foreground marker lives in `gevr_prefs.txt`, cleared on a clean exit and
  before a restart, so a killed or hung run is offered too; the install time is
  the exe's write time; boot detection uses `GetTickCount64` (no boot count on
  Windows). The game log is the Quest's `gevr.log` / `gevr.prev.log` beside the
  exe (port/src/system.c), and the tail of `vr_debug.txt` stands in for
  logcat. The device line is "PC Windows <version> x64". The upload address is
  `GEVR_REPORTS_URL` when set (a real report becomes an issue in the reports
  repo: tests use a local mock). A scripted run (`--input-script`, with a new
  `crash` command) skips sysFatalError's message box. A pad's two stick clicks
  unlock "Send debug log", the Quest's gesture; on the keyboard, Ctrl+Shift+D
  held (pcLauncherStickClick; like the Quest's gesture it is not shown in the
  launcher, and like it the keys must be up first). Checked against a mock: a
  crash, the offer on the next start, the report sent (kind crash, trace,
  redacted note) and not offered again; a run killed from outside offered as
  "ended unexpectedly"; a clean quit not offered; a manual debug log.
- **Friends** (`pc_friends.cpp`, FriendsClient): the same commands and events
  vr_friends.cpp sends and drains (presence, settings, search, add, answer,
  remove, decline-all, block, unblock, invite, drop, restore, reset, and the
  Discord ones, which the PC never sends: vr_discord.cpp has no SDK there, so
  linking stays unavailable). The player is `gevr_player.json` beside the exe,
  written to a new file and renamed over the old. The service is
  `GEVR_FRIENDS_URL` when set (tests: a local mock; the real Worker never
  saw a PC test). Checked against a mock of the Worker's contract: register,
  presence with friends, requests, blocked and an invite (the sidebar draws
  them), search, add (both answers), the other commands' requests, an error
  reply, settings, the 5 s gap after a change, restore (good and bad), a 401
  signing the player out, and the saved player reloaded.
- **Updater** (`pc_updater.cpp`, UpdateChecker + UpdateVersion): the same
  status line and commands, reached through the launcher's
  gevrUpdaterStatus / gevrUpdaterCommand (their PC branches now ask
  pc_services.cpp). The same GitHub releases (`GEVR_UPDATES_URL` for a mock),
  looking for `GoldenEyeVR-VERSION-windows-x64.zip` (package_pc.py's). Update:
  download into `updates/`, size and GitHub's asset SHA-256 digest (the APK's
  check), unpack into `updates/staging` (ModArchive's bounds), then in place of
  Android's package, version and signer checks: the zip must hold
  GoldenEyeVR.exe and a VERSION naming the offered release, not older than
  this build. Nothing is signed: trust is GitHub's digest and HTTPS. Install:
  each file the zip holds moves the game's own aside as NAME.old (a running
  exe and a loaded DLL can be renamed), the new one moves in, any failure
  puts every file back; the launcher's next status poll restarts the game
  (pcRestartSelf, which now also clears the crash marker and closes the log
  first). The next start deletes the .old files and the downloads and says
  "Updated to vX." No "install unknown apps" permission state on PC.
  Checked: the real releases (no Windows zip yet: up to date), and against a
  mock release in a throwaway copy of the game folder: offered, downloaded,
  swapped, restarted into the new version with the notice, leftovers gone; a
  wrong digest fails with Retry and leaves the game as it was. The zip must also hold the
  exe that is running (GoldenEyeVR-VR.exe restarts into its own new copy);
  every file in it is swapped, both exes, both DLLs and the launch scripts
  included. Checked again with package_pc.py's two-exe zip (17 files) as a
  mock release in a throwaway game folder: all swapped, restarted, leftovers
  gone; port/tests/pc_services_native.cpp 109 checks. Open: no Windows zip
  is published yet (see the release step above).

**Controls:** GoldenEye's 1.2 Solitaire, as the Quest's screen mode plays.
- Keyboard and mouse: W/S walk, A/D strafe (the C buttons), arrows look,
  left mouse or Space fires, right mouse or Z aims, Q or the wheel changes
  weapon (A), E or R uses and reloads (B), F or X is L, Enter is Start.
  Click in the window to capture the mouse for looking, Escape releases it.
- Gamepad, as the Quest's screen mode holds the Touch controllers: left
  stick walks and strafes (the C buttons past a quarter), right stick looks
  and turns (the game's stick), right trigger fires, left trigger or RB
  aims, A/X are A, B/Y are B, LB is L, Start, the d-pad is the C buttons;
  in menus the stick pushed further navigates.
- The Perfect Dark port's pad support was off for VR: its controller
  setup in inputInit and the pad branch of inputKeyPressed run again on PC.
  Custom binds from the ini are not loaded yet (inputLoadBinds is off).

**Mouse look** is Project Midas's (`src/game/bondview2.c`, PC only),
modelled on the 1964 GoldenEye mouse injector: the mouse turns the camera
angle directly each tick, 0.1 degree a count at sensitivity 2.5, scaled by
the zoom; in aim mode it moves the crosshair; the gun sways with it; in
the tank it turns the turret. Not ported yet: Midas's aim lock, wide-window
aim reach, pad look stick, crosshair snap and aim-mode edge turn.

**Testing without the desktop:** `--input-script FILE` (port/pc/pc_testinput.c)
feeds timed mouse moves, clicks, held keys, a virtual gamepad (SDL's,
seen by the game as a real pad), a launcher pointer (`point X Y`, canvas
units) and typed text into the game, `pick PATH` answers the next file
picker (a scripted run never opens the Open dialog; with no pick waiting the
picker is cancelled), and `shot NAME` writes the window to
`shots/NAME.bmp`. The game never grabs the real mouse
or raises its window, so a test runs while someone uses the computer.

What it took:

- `port/flat/flat_headset.c`: the server's "no headset" answers, now shared
  by the server and the PC (60 Hz pacing on a high-resolution waitable timer
  on Windows).
- `port/pc/`: the window (Dab's Mod's flat SDL2 window, no Vulkan), host
  stand-ins for the launcher, the renderer's headset hooks, and
  `pc_compat.h` (force-included: `mkdir`, `lstat`, `bzero`, `FLT_MAX`,
  `arc4random_uniform`).
- **The frame is drawn the Quest's way.** fast3d's own desktop path (no
  multiview) has rotted: its uniforms no longer match its shaders and it
  draws nothing. The PC draws through the Quest's virtual-screen target
  (`vr_screen.cpp`, `GL_OVR_multiview`) and copies layer 0 into the window,
  so the image is the one the headset shows. GPUs without multiview (older
  Intel) need that desktop path fixed: open.
- **The decomp's libc stays out of the PC build.** `src/sprintf.c` returns
  `written - 1` and broke fast3d's shader text when it replaced the C
  runtime's; on the Quest the C library's versions are evidently the ones
  the game binds to (its shaders build). To check: whether `src/str.c`'s
  NULL-safe `strcpy` (gepc-ref D150) is in effect on the Quest at all.
- glad's desktop loader stops at GL 4.1, so `glTexStorage3D` is fetched
  from the driver on PC.

## The model: Time Crisis II VR

`other_projects/time-crisis-2-vr` builds one host (`quest/quest_host.c`)
for Android and Windows. The Windows side adds `pc/pc_main.c`,
`pc/pc_gl_context.c` and a 33-line `pc/CMakeLists.txt`. Its single exe runs
OpenXR with desktop OpenGL (PCVR) or, with `--desktop`, a window with mouse
and keyboard. `Play SteamVR.cmd` only sets `XR_RUNTIME_JSON`. The toolchain is
LLVM-MinGW clang plus SDL2, the OpenXR loader and zlib, fetched into `.tools/`.
We need clang too: GCC can't build our tree (token pasting; see
`gevr-no-desktop-build` notes from 2026-09-27).

## Where we start

| Piece | Today | PC work |
|---|---|---|
| Game core | Builds and runs on x86-64 and ARM64 Linux (dedicated server, clang) | Small: Windows CRT details (`mkdir`, `bzero`, POSIX headers) |
| Renderer (`port/fast3d`) | GLES `300 es` and desktop `330 core` shader paths, glad | Small, except stereo without `GL_OVR_multiview2` |
| OpenXR (`port/vr/vr_openxr.cpp`, 5,680 lines) | Android EGL + `XR_KHR_opengl_es_enable` | Medium: port PDVR's WGL + `XR_KHR_opengl_enable` binding (`other_projects/pdvr/port/vr/vr_openxr.cpp` 747) |
| Platform interface | `port/server/server_headset.c` stubs every `vr_*`/`gfx_*` call the game makes | Make it a header and give it a flat implementation |
| Launcher | ImGui (`vr_launcher.cpp`), `imgui_impl_sdl2` already vendored | Small: draw to the window in flat mode |
| Java-only parts | LobbyClient (HTTPS polling), Bean/CE import by fd, updater, LogReporter, restart | Medium: C lobby client over WinHTTP, path-based import |
| Steam Audio | `android-armv8/libphonon.so` only | Small: SDK's Windows/Linux libraries |
| Controllers | Touch profile | Add Index, Vive, WMR, simple profiles |

## Phases

**P0: one platform switch.** CMake `GEVR_PLATFORM=quest|pc|server` in place
of the `ANDROID`/`GEVR_HEADLESS` checks. `gevr_platform.h` lists what
`server_headset.c` stubs today. `vr_openxr.cpp` splits into shared OpenXR code
and a per-platform graphics binding; Quest-only extensions (refresh rate,
passthrough, spacewarp, foveation) depend on the runtime offering them.
Gate: Quest build behaves the same on the headset, server passes
`tools/gevr_server_test.sh`.

**P1: Windows flat desktop.** LLVM-MinGW x86-64, `tools/build_pc.py`, deps in
`.tools/`. SDL2 window, GL 3.3 core, drawing the frame Quest puts on the
virtual screen. Mouse look, keyboard binds, gamepad and widescreen: port from
Project Midas (`port/src`, MIT, same game) and check Dab's Mod
(`dabs-mod/port/src/input.c`). Data and ini beside the exe, `roms/` folder
like TC2. Gate: a solo mission and a Bean import on a PC.

**P2: PCVR.** PDVR's WGL binding, interaction profiles, a desktop mirror
window, `Play VR.cmd` / `Play SteamVR.cmd` / `Play Desktop.cmd`. Stereo needs
`GL_OVR_multiview2` (NVIDIA, recent AMD); a per-eye fallback only if a tester
needs it. Gate: Quest over Link and over Virtual Desktop.

**P3: online and release plumbing.** C lobby client, updater, crash/log
upload, Windows zip + symbols per release, CI building Quest, PC and server
on every PR (the PD desktop code rotted because nothing built it). Crossplay
on the same protocol. Flat players carry an input flag in HELLO and the
lobby listing; a "VR players only" host option refuses flat joins, and the
browser can hide such lobbies. The flag rides the one protocol bump at the
release.

**Later:** Linux desktop and SteamVR on Linux (the server already builds
there).

## Linux (2026-11-09)

`tools/build_linux.sh` builds both binaries on Linux or WSL from the same
sources: `GoldenEyeVR` (flat) and `GoldenEyeVR-VR` (OpenXR over GLX, SDL's
X11 driver; the distro's OpenXR loader, copied beside it). No root: the
headers and link libraries (libcurl, GL/GLX, X11, PulseAudio, OpenXR,
pkgconf) are fetched with `apt-get download` into `~/gevr-linux-deps`, clang
comes from `~/clang18` as for the server, and Steam Audio's `libphonon.so`
from the same pinned zip as Windows. What differs from Windows:

- HTTPS through libcurl (`pc_http.cpp`), SHA-256 built in (the updater's
  check; known-answer tests in `pc_services_native.cpp`, 122 checks on both);
- file pickers through zenity or kdialog; restart with `posix_spawn`;
- crashes through the Linux signal handler (`crash.c`), reported as on
  Windows;
- PC VR waits for the runtime (Monado, SteamVR) rather than starting it.

Checked under WSLg: the flat game renders the intro and menus and plays
sound through PulseAudio. PC VR on Linux is built, not yet run on a
headset.

**Release package (2026-11-09):** `tools/package_linux.sh [VERSION]`, on
Linux or in WSL, builds and packs `GoldenEyeVR-VERSION-linux-x64.zip`
beside the Windows zip, in the same layout:
- the two binaries, stripped, with `libphonon.so` and
  `libopenxr_loader.so.1` beside them (rpath `$ORIGIN`);
- `play-vr.sh` and `play-steamvr.sh` (SteamVR for one launch, through
  `XR_RUNTIME_JSON`), README.txt (`port/pc/package/README-linux.txt`), the
  icon and logo, the licences and VERSION;
- each entry's Unix mode set in the zip, so a plain unzip runs;
- the debug info beside the zip (`*-linux-x64.debug`), for the reports
  repo's symbols release.

The updater picks its platform's zip (`-windows-x64.zip` or
`-linux-x64.zip`; an iPhone or iPad is offered none), sets the modes again
after unpacking, and restarts into the new binary by its path:
`/proc/self/exe` is the old file by then (renamed and deleted), and the
first try started the old version again. Tested end to end in WSL against a
mock release (GEVR_UPDATES_URL): offered, downloaded, checked, 21 files
replaced, restarted into the new file. A release attaches both zips; the
WSL build's id reads "nogit" (git there cannot follow the worktree's
Windows path), which a release build from a plain clone does not.

## Steam Frame

The Frame is ARM64 SteamOS (Snapdragon 8 Gen 3) with Valve's Lepton
running Android 10+ APKs, Touch-style controllers and an OpenXR runtime.

1. **The Quest APK in Lepton** (decision of 2026-10-01): no new build. The
   APK needs only `XR_KHR_android_create_instance` and
   `XR_KHR_opengl_es_enable`; the FB extensions are optional. When a Frame is
   at hand: install it, fix what Lepton rejects, check UDP discovery.
2. **A native ARM64 Linux build**, if Lepton falls short: this plan's PC
   layer on Linux. The game core already builds and runs there (the
   dedicated server's aarch64 tarball), `port/flat` and `port/pc` are
   SDL2 + GL, and PCVR's OpenXR path takes the GL binding (Xlib/EGL on
   Linux instead of WGL). So each PC step keeps to SDL2, OpenXR and plain
   GL/GLES, and nothing Windows-only goes into shared code: Windows calls
   stay behind `_WIN32` in `port/pc` or the few files gated above.

## Why not rebase onto Project Midas

[murkantor/Project-Midas](https://github.com/murkantor/Project-Midas)
(412 stars, active, MIT port code) is a Windows/Linux GoldenEye port from the
same n64decomp/007 source we use. Its own portability note
(`docs/port/abi-and-portability.md`) says it is **32-bit i386 by design**:
`-m32` everywhere, 8-byte `Gfx`, host pointers stored in `u32` fields, a
fixed PE image base that `seg_addr` relies on, and assets compiled into the
program at build time from the cart. The Quest runs only arm64, so Midas
can't be our Quest base, and moving onto it would throw away our 64-bit
pointer and byte-order work and redo it in its tree. (The iOS/visionOS fork
`rebelancap/Project-Midas-ios` did an arm64 build of it; worth reading if we
want a second 64-bit opinion.)

The decomp itself: n64decomp/007's last commit is still `c435646` (Aug 17,
100% decompiled), so there is nothing newer to rebase onto. Other desktop
ports (jkdansereau/goldeneye-pc-port = gepc-ref, already our 64-bit
reference; akratch/mgb64, discontinued) are references, not bases.

What Midas is good for: desktop input, mouse look, settings and controller
layouts for P1. Credit it in CREDITS.md when code is adapted.
