# GoldenEye VR: status

Rewrite this file in place and keep it under about 120 lines. Older logs are
in docs/archive/ (the v0.5.15 batch as it stood before release is
[status-2026-11-10](docs/archive/status-2026-11-10.md)); features keep their
own notes. No game data is shipped; building publishes nothing.

**Release:** v0.5.26 (versionCode 139, protocol 27), 2026-11-23, tag on
e8ca03bd ([notes](docs/releases/v0.5.26.md)): hand reload on by default (on once
for older inis, `HandReloadDefaulted`) with grip use held on for every model set;
force pull one setting (no Pull disarm / Pull visuals / Auto pickup); reload mode
in multiplayer (roster column, host "Hand reload only", Join list "Hide games with
button reload", `NET_MSG_RELOAD_MODE` 64, additive; lobby service #19); ghost
runs note the reload; the Graphics tab with per-headset Auto defaults measured on
a Quest 3; the lobby's player menu with Add friend (and the ghost boards'); doors
and Dam's truck read `CHRFLAG_HIDDEN` (64-bit Android/Linux/Apple read the prop
pointer there) and treat bodies alike with Bodies stay and ragdolls on or off;
the third-person camera comes in at solid objects; proximity voice 8-25 m; flat
Crouch key and pause Enter fix; back to launcher waits for the old process; the
Join list's server filter (#281, service #18); iCloud sync on TestFlight (App
Store profiles remade with iCloud, archives with `GEVR_ICLOUD=1`). Protocol 27
unchanged: plays online with v0.5.25 to v0.5.22. 9 downloads from one commit;
TestFlight iOS 148 and visionOS 23 with iCloud, tvOS 9 without
(tvOS wants aps-environment beside iCloud: push capability, profile, entitlement). Gates: port/tests 120/120 on Windows and 120/120 on
Linux (#302's toolchain fixes), the 12 asset tests on both, Java 147 (three
flavours), dedicated server gate passed (26 kills, 3.6% peak CPU); played by the
user on a Quest 3 (the truck, third person, gestures, launcher). PR #295.
**Next:** volumetric smoke and explosions (#249, design thread), the mod system
(`mod-system`, #292), host rule against teleporting (protocol 28), bolts that
move on the meshes, CloudKit schema in Production if not yet deployed.
**Before it:** v0.5.25 (versionCode 138), tag on c4218068
([notes](docs/releases/v0.5.25.md)), PR #278; v0.5.24 (versionCode 137), tag on
e73c2a4f ([notes](docs/releases/v0.5.24.md)), PR #272.
**In v0.5.23 (from t3/discuss-issue-269):** #269's Apple TV
list: the Info.plist's GCSupportsMultipleMicroGamepads + DirectionalGamepad
(tvOS 26 merged a 2nd generation remote into a ring-less "Coalesced Remote";
the HD's 1st generation remote checked after it), Pad vibration 30%, Trigger
feedback / Look speed / Look deadzone / Invert look rows, Head bob, Frame
rate Auto/Full/Half (Auto tries the full rate again, nothing saved) and at
half rate two game steps a frame (HD A/B: steps 1/2/3+ 5/21/5 before, 0/30/0
after). Then a flat online report: a guest now reads slot 0's keyboard
binds (it couldn't move), and Upright look is the real default with a
once-per-folder fix (the mouse was inverted in matches). Bullet holes
(#9): corners stored in eighths of a unit (whole units tilted a hole ~8 deg
into Dam's walls) and flat shaders get the decal band's uDecalBias nudge
(RenderDoc: it was multiview-only, so on PC/phones/Apple TV a decal drew
only where it matched the wall's depth exactly); PC and the HD with Bean
checked. #7 on the HD: every settings save took ~130 ms (vrHapticsDumpCTable's
~60 vr_log lines, a file open each, ~115 ms; the file 12-14 ms), longer than
the ~100 ms of queued sound, so the menus' saves (folding Friends) broke the
sound up: the dump is gone from saves and the file is written on a thread of
its own (vrSettingsFlush at load, background, restart, exit); HD folds now
clean. The perf line has AUDIO/VOICES: Frigate's engine room mixes at ~1-1.7
ms a frame on the HD (peaks 5-6 ms, ~4x upstairs) and holds ~55-60 at full
rate; the deck and big rooms are draw-bound (41-55). Reset all settings
(#10, launcher Play tab). #8 (Dam's grates, Bean): Bean's own grate faces
fought its wall; the wall's long diagonal triangles pass the decal grid's
4096-cell limit (DAB's too), so levelBigBases() now takes solid faces as well
(only Dam's 16 grate faces change; HD checked by the user). #6 texture size
cap measured on the HD (512: +3-7% at Dam Bean, 256 breaks the grate):
dropped (user). #11 skipped (user). Flicker checks need frame bursts, not
stills. Loading (user asked: everything, every platform): one loading screen
(port/vr/gevr_loading.cpp, logo + gold bar, fades) at START until the title
and at every level, Bean on or off, headsets on their screen quad (engine
shim gevrVrLoadingFrameBegin/End; Quest 3 checked); flat screens hold it over
a level's slow first frames (HD: 0.96-1.8 s hidden). Behind it: Bean's level,
sky, pictures to the GPU and the watch arm built at stage load
(boss.c gevrStageFrontLoad, before net*StageLoaded); shaders from
$S/gevr_shaders_*.txt compiled at boot; Bean's level passes and room matching
cached in $S/gevr_cache (build-stamped, input-hashed, 8 files): Dam Bean
front-load PC 1.83 -> 1.23 s, Quest 3.4 -> 1.8 s, HD 6.3 -> 3.7 s. The first
menu open no longer loads the watch arm (HD 137 ms). Not done: Bean guns and
hands kept across levels (memory on HD/phones/Quest).
**In v0.5.24 (from t3/multiplayer-teleport-locomotion):** teleport locomotion
([docs/teleport.md](docs/teleport.md)), Comfort > Movement: Teleport, headset
only, off by default. The move stick aims an arc from its hand, letting go
jumps behind a 0.17 s blink, a flick hops 1 m. A jump goes only where the
walk's own collision test passes in 20 cm steps (no doors, glass, objects,
edges) and is paid from a 6 m stamina filling at 90% of the forward run, so
it never out-travels running; no protocol change (others see a dash). Played
on a Quest 3 by the user ("works really well"); the doc's test sheet (stairs,
two headsets) has not been gone through item by item.
Also in v0.5.24: hand reload's rack
([docs/hand-reload-rack.md](docs/hand-reload-rack.md)), part of Hand reload
WIP. A pistol, a submachine gun or a rifle fired dry needs its slide or bolt
racked by the off hand after the new magazine (rounds wait in GevrRack until
then; a magazine changed with rounds left needs none; the belt's touch and
dual wielding skip it). Also the AR33, RC-P90, sniper rifle and automatic
shotgun (its pump, once dry); the pump shotgun is pumped for every shot. The
two-handed hold lets go while a gun waits, so the off hand slides on it.
With a magazine out the free hand has no hip or belt holster; a Bean
shotgun's shells are taken from its own saddle, not the belt. Bean's sniper rifle has a magazine made for it
(gevr_beanmag.c made). The gun gives 1.2 cm with the pull; only the pistols'
slides move. A ring shows the place; Gun fit's reload page sets it (the off
hand's grip; ini Rack*, GexRack*, BeanRack*). test_rack.py, test_hand_reload.py
(rackReload, pumpReload). Played on a Quest 3 with Bean by the user through
2026-11-21 ("going pretty good"): the KF7's and AR33's places are theirs.
GE-X's guns and left-handed mode were not played.
**Next:** device checks: split screen with two real pads on an Apple TV and
a phone, 3-4 players on a slow device; trigger feedback on Xbox and
DualSense pads; a pad in the Quest and PC VR headsets; Vision Pro's ghosting
fix on the device; the 1st generation remote on the HD; the half rate on
Android and PC. Then: phase 2 of split screen (local guests in online
matches, a protocol bump); true drop-in mid-match; Bean throat gaps (Dab's
beanAddNeckTube); the round-at-a-time Bean reloads.

Dev installs without TestFlight: the iPhone and both Apple TVs are paired to
the Mac (gevr-dev-ios/tvos(-hd).command build ~/src/gevr-vos-kb); unattended
benchmarks with `--boot-stage 33 --perf-log`. Scripted PC tests drive up to
four virtual pads (`pad2`..`pad4`, port/pc/pc_testinput.c); the game runs in
the exe's folder, so give `--input-script` an absolute path.

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

Each has docs/releases/vX.Y.Z.md; v0.5.22 back to v0.4.20 (hack campaigns,
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
