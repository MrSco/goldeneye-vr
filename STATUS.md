# GoldenEye VR: status

Rewrite this file in place and keep it under about 120 lines. Older logs are
in docs/archive/ (the v0.5.15 batch as it stood before release is
[status-2026-11-10](docs/archive/status-2026-11-10.md)); features keep their
own notes. No game data is shipped; building publishes nothing.

**Release:** v0.5.21 (versionCode 134, protocol 27), 2026-11-19, tag on
f501fbff ([notes](docs/releases/v0.5.21.md)): the rest of Dab's Mod's Bean
fixes (docs/bean-parity-backlog.md; the HD level drawn to its fog, HD hit
tests, monitors, hats, seams, the Community Edition's level, colour and sea
fixes, the Aztec closet, mines on the HD surface), held guns' labels and
overlapping faces in painter's order (VR_DEPTH_PULL_NEXT, #267), each
player's device in the roster and the lobby service's player totals (#262),
ghost races' drops (#263) and VR count (#265), the Apple TV fixes (#260).
Protocol 27 for the stage data fixes (gevr_romswap.c gevrRomFixData): plays
online only with v0.5.21. 9 downloads from one commit; TestFlight iOS 141,
visionOS 17, tvOS 3. Gates: suite 113/113 (93 Windows + 20 WSL), Java 48x3,
server gate passed. Closed at release with a reopen note: #241, #245, #246,
#250-#253, reports #31 and #32, PR #260. #249 stays open.
**Next:** headset checks of the Aztec closet, the RPG-7 held by the grip, a
grenade or mine left in hand after a throw, and a ghost race's drops; the
roster's Device column on a real headset; Bean throat gaps (Dab's
beanAddNeckTube, still on the backlog); the round-at-a-time Bean reloads;
Apple TV's remote and pads on a device.

## Batch after v0.5.21 (branch t3/visionos-ghosting-issue, not pushed)

- Vision Pro ghosting while walking or turning (Discord, report 29ca74ce):
  ANGLE has no buffer storage, so the eye pass was never kept: no redraw
  between game frames, and at 45 fps each picture held two slots. Staged
  vertex ring (5c2ff86e), the runtime's own SpaceWarp for held slots
  (5f941b37), GLES 3.0 UBO calls filled (cf84def0). Simulator only.
- Vision Pro keyboard for report notes and text boxes (a07a30e6, sim-tested);
  perf/pump lines every 10 s (ea1f9771).
- Apple TV: SIGPIPE ignored (mid-match home-screen crash, #268/report 33).
- Pads vibrate on flat builds (#268): the haptics table was a zero stub
  (ee647dbf), then Dab's Mod's Rumble Pak pulse for pads (0.2 s shot, 0.25 s
  hit, times Vibration; the row buzzes). User-confirmed 2026-11-19 on iPhone
  (Switch Pro Controller) and Apple TV 4K (8BitDo SN30 Pro+).
- Dev installs without TestFlight: iPhone, Apple TV 4K and Apple TV HD paired
  to the Mac (gevr-dev-ios/tvos(-hd).command; GEVR_DEST in build_tvos.sh).
  Benchmarks unattended: `--boot-stage 33 --perf-log` (perf and mesh lines
  each second; the perf line every 10 s on Apple anyway, with its fps),
  xctrace --all-processes from the desktop session.
- Siri Remote 2nd gen clicks through the menus (189d81f4, #268); retest.
- Apple TV HD (A8) stopped at startup ("no ES 3 config"): ANGLE's ES 3 gate
  down to Apple2 (tools/build_angle_apple.sh). Runs.
- Bean on flat GLES games (Apple TV, iPhone, iPad, Android phone): static
  meshes and GPU skinning were headset-only (gevr_mesh_supported asked for
  multiview), so every Bean vertex went through gfx_sp_vertex. Apple TV HD,
  Dam with Bean: 15 -> 40 fps; simulator 32 -> 59; same picture (A/B with
  gevr_nomesh.txt). For e-ee's A10X (#268, "Bean <30 fps"); retest.
- Trigger feedback (a player's suggestion): Xbox trigger motors, DualSense
  adaptive triggers (SDL effects on PC, GCDualSenseAdaptiveTrigger on Apple);
  Controls row, on. Untested on a pad.
- Pads in the headset (Quest, PC VR) as a Touch pair, as Vision Pro does:
  the virtual screen's play like the flat game. PC VR built; Quest not built
  here (no keystore in this worktree); untested.
- Apple TV home-screen icon: Frozen Inferno (docs/art/tv_icon.jpg, make_icons
  tvos), wide like HotD's; the file page (ROM, XBLA, CE updater) shows a QR code of
  the address, as HotD's ROM page (simulator: decodes, 40 MB sent, progress).
- Reports #35: the dev install's relaunch, not a crash.
- Open: #249. Device checks for all of the above.

## Platforms (docs/pc-platforms.md, docs/mobile-platforms.md)

- **Quest** (main target): `quest` flavor, com.gevr.port.
- **Windows:** GoldenEyeVR.exe (flat) and GoldenEyeVR-VR.exe (OpenXR: Link,
  Virtual Desktop/VDXR, SteamVR). User-tested over Virtual Desktop on VDXR
  and SteamVR. SteamVR: in-game quads drawn as a projection layer
  (vr_hud_layer_render), beam and spot as quads; the desktop mirror shows
  the screen's picture.
- **Experimental, untested on real hardware:** Linux (flat checked in WSLg),
  Android phone (`mobile` flavor, emulator only), Steam Frame (`frame`
  flavor, Khronos loader), iOS (simulator and one development iPhone).
- **Crossplay:** NET_INPUT classes in HELLO and NET_MSG 59/60 (additive), VR
  players only / hide flat (Worker), LAN beacon tail.
- **Device** (v0.5.21): each player's device (Quest, Quest flat, PC VR, PC,
  Android, iPhone/iPad, Apple TV, Vision Pro, Steam Frame) in the launcher
  roster and the in-game pause table (NET_MSG_PLATFORMS 62, additive); the
  lobby service gets `devices|key@build,...` and presence carries device,
  build and input for its anonymous daily totals ("Who's playing" on
  lobbies.goldeneyevr.com; goldeneyevr.com #13, #14 deployed 2026-11-19).

## Help wanted (listed in the v0.5.15 and v0.5.16 notes)

- Two headsets (v0.5.18): voice in a real round (echo with the speakers,
  FEC and the jitter buffer: `voice: slot N ran dry` lines), the co-op
  Frigate defuser by a client.
- Speedrun: LiveSplit against a real LiveSplit from a Quest; the debrief
  stamp's VR values (turning, display rate, SpaceWarp); a long walked run's
  ghost animation.

- Two headsets: force pull online (and on a dedicated server), co-op guards'
  grenades and rockets; the online match screen's tabs as host and client.
- Force pull's feel (GEVR_PULL_FLICK_*); Bean doorways (Statue hedge,
  Control lift); Jungle remote mines by a cycle tap; goldeneyevr.com/join and
  goldeneyevr:// from the Quest browser.
- Real hardware: an Android phone (Bean import too), a Steam Frame, Linux
  with a headset, an iPad, Quest Link, SteamVR with other headsets.

## Known issues (v0.5.15)

- Force pull online: the host settles only setup pickups; pool guns and
  grenades are each headset's copy and a grenade is owned at the catch, so
  two players can keep one gun, or one grenade blows twice.
- The Worker (goldeneyevr.com social.ts) answers any failed Discord call
  with 401, read as an expired link: a Discord outage unlinks players.
- gevr_player.json (secret, Discord refresh token) sits in external app
  storage on Android; getFilesDir() needs a migration.
- SteamVR through Virtual Desktop: the left Menu tap (START) also opens
  SteamVR's dashboard (later: another START button under SteamVR); no curved
  screen (SteamVR has no cylinder layers; later: the game draws the screen).
- iOS: no Steam Audio library, no LAN discovery (multicast entitlement),
  "Restart to launcher" closes the app.
- GE-X manual reloading: a cycle tap still skips the sniper beside a KF7.
- Older, still open: Horde co-op two-headset checks (revives, late join,
  host loss, swarm/boss rounds), the ally after host takeover, #216, #164,
  #161-#163, #95, bots on a client, #88 at 5+ headsets; Runway autoguns after
  a timed mine; Surface I sky and Frigate engine-room judder with Bean; #30
  water lines; #85 Egypt door; Statue's Natalya at the helicopter.

## Releases

Each has docs/releases/vX.Y.Z.md; v0.5.21 back to v0.4.20 (hack campaigns,
Project Bean, [PR #172](docs/pr172-handoff.md)) are summarised there.

## What it is

Native port of n64decomp/007 with Perfect Dark VR's port layer and GEVR PC
presentation, now one codebase for the Quest, PC (flat and VR), Android, the
Steam Frame and iOS; the player supplies an owned NTSC-U ROM. Releases reach
players through the in-game updaters, SideQuest and goldeneyevr.com (own
repository; "Other platforms" links the latest release's files). Solo stereo
VR and a flat or curved screen; experimental multiplayer to eight (bots,
co-op and Horde to four, internet lobbies, voice, host migration, dedicated
server, Friends). README.md and MULTIPLAYER.md describe it all.

## How to work on it

- Build (Quest): android/gradlew.bat assembleQuestRelease with
  JAVA_HOME=C:/Program Files/Java/jdk-20; signing from gitignored
  android/keystore.properties and the .jks (preserve the key). NDK
  25.1.8937393, CMake 3.22.1. A fresh .cxx can't download opus (xiph.org,
  HTTP/2): add `-DFETCHCONTENT_SOURCE_DIR_OPUS=<opus-src>` to build.gradle's
  arguments for the build with `git update-index --assume-unchanged` on it
  (the build id stays clean), then restore. PC: `python tools/build_pc.py`.
- Suite: every port/tests/test_*.py, Windows python first, WSL on failure,
  with the GE/GF64/TND ROMs and build/xbla-files; Java: gradlew
  testReleaseUnitTest. Server gate: tools/gevr_server_test.sh ROM BIN in WSL.
  Never build or run the suite while the user plays (it starves the game).
- Release (all from the tagged bump commit, each binary's build id clean):
  Quest `GoldenEye-VR-vX.Y.Z.apk`; `assembleMobileRelease`/`assembleFrameRelease`
  as `GoldenEyeVR-X.Y.Z-android.apk`/`-frame.apk` (never GoldenEye-VR-*:
  older Quest updaters take that); `python tools/package_pc.py`;
  Linux and servers in WSL from a plain clone (`tools/package_linux.sh X.Y.Z
  --build-dir <new>`, `tools/gevr_package_server.sh X.Y.Z` with
  GEVR_SERVER_BUILD/_ARM); iOS on the Mac (`bash tools/build_ios.sh device
  --app`, unsigned; Payload/GoldenEyeVR.app zipped as
  `GoldenEyeVR-X.Y.Z-ios.ipa`; Apple TV the same with tools/build_tvos.sh as
  `-tvos.ipa`). Notes list every file's SHA-256 (the server
  setup reads them). Symbols (APK zips, .debug files) to the reports repo's
  `symbols` release. Never install to a headset unasked.
- A batch is one branch; a new branch gets its own worktree. Commit before
  building. The user reviews in the headset and says merge. Prefer Dab's
  Mod, GEVR PC and Perfect Dark VR precedents.
- ROMs and model exports stay local. Mixed line endings (* -text): keep
  each file's endings (msys `sed -i` strips CRs); compare
  `git diff --stat` with `--ignore-cr-at-eol`.
- Code: src/ game; port/src platform/input/audio/net; port/vr OpenXR and
  launcher; port/pc PC services; port/flat flat window and touch; port/ios;
  port/fast3d renderer; android/ Java; tools/ scripts. References beside the
  repo: pdvr, gepc-ref, GoldenEye-007-HD, dabs-mod, perfect-dark-ios.
