# GoldenEye VR: status

Rewrite this file in place and keep it under about 120 lines. Older logs are
in docs/archive/ (the v0.5.15 batch as it stood before release is
[status-2026-11-10](docs/archive/status-2026-11-10.md)); features keep their
own notes. No game data is shipped; building publishes nothing.

**Release:** v0.5.18 (versionCode 131, protocol 26), 2026-11-15, tag on
d8bd0011 ([notes](docs/releases/v0.5.18.md)): lobby push signaling (the
Worker's WebSocket, polling fallback), voice FEC + adaptive jitter buffer +
Quest echo cancellation, GE-X hand reload (label over the gun, PP7/DD44
slide lock, belt rounds for single-round guns, pistols' magazine fits,
Cougar/Golden Gun grip caps), co-op Frigate defuser and hostages (#241),
Bean unlit faces, Bean 16:9 front end, flat hands (#238), Windows without a
console, iOS crash-prompt and controller fixes. Plays online with v0.5.15
to v0.5.17. Downloads as v0.5.17's (8 files, same commit).
**Next batch** (branch release/v0.5.19, not pushed): the t3/66d9016a fixes
(co-op Control door locked behind the host #246, lock waits for the party;
co-op mission held by a teammate that never loaded #245, dropped 60 s after
the host loads; Dam boat lands on the island floor #247), the native Vision
Pro build (claude/visionos-native, TestFlight build 14), the Mario
Characters campaign and co-op in every hack campaign (t3/a9e4fd65, PR #248,
docs/mario-characters.md), and PC VR quitting when the runtime ends the
session (report fa49c715: no false crash prompt). Suite 109/109 with the
Mario ROM; Quest APK and both PC exes build. Not yet headset-run: Mario,
Dam boat to the island, co-op #245/#246 (two headsets), PC VR quit from
the SteamVR dashboard; visionOS still to build on the Mac.

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
  players only / hide flat (Worker), roster "Plays", LAN beacon tail.

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

Each has docs/releases/vX.Y.Z.md. v0.5.18 push signaling, voice, GE-X
reload aids, co-op Frigate; v0.5.17 PC fixes, crash context, perf
pass; v0.5.16 speedrunning; v0.5.15 new platforms, crossplay, Friends
and invites, unified launcher, force pull; v0.5.14 all maps in
Horde/Deathmatch, dedicated server; v0.5.13 co-op world from the host
([#216](docs/issue-216-coop-world.md)); v0.5.12 Bean frame rate; v0.5.11
XBLA Community Edition; v0.5.10 Facility fog flash; v0.5.7 Horde maps,
Mirror Mode; v0.5.4/5 Horde, AI ally, SpaceWarp; v0.5.3 ragdolls; v0.5.0-2
XBLA 120 Hz; v0.4.20 hack campaigns, Project Bean
([PR #172](docs/pr172-handoff.md)).

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
  `GoldenEyeVR-X.Y.Z-ios.ipa`). Notes list every file's SHA-256 (the server
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
