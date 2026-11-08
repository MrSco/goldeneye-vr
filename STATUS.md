# GoldenEye VR: status

Rewrite this file in place and keep it under about 120 lines. Older logs are
in docs/archive/ (the per-build log to v0.5.15-test.26 is
[status-2026-11-07](docs/archive/status-2026-11-07.md)); features keep their
own notes. No game data is shipped; building publishes nothing.

**Release:** v0.5.14 (versionCode 95, protocol 25), 2026-11-01.
**Pre-release:** v0.5.15-test.31 (vc 126, protocol 26), 2026-11-08, branch
`prerelease/v0.5.15`, tag on d059d943 (test.30 + no falling into Cradle's pits).
Builds from test.12 on join only each other.

## v0.5.15 batch (unreleased)

- **Friends and invites** ([design](docs/friends-and-invites.md)): friend
  codes, search, Friends panel, invites, Link Discord (Social SDK from
  build/discord/, not in the repo), Discord invites, goldeneyevr.com/join.
  Worker on goldeneyevr.com main. No game protocol change.
- **Launcher unified** ([plan](docs/launcher-unify.md)): one settings table
  (port/src/gevr_settings.c) for the launcher, the hold-Menu panel and the
  online pause; tabs Play, Multiplayer, Controls, Comfort, Screen, Rules,
  Mods; no page scrolls (fit tests). The watch's VR page is gone.
- **Force pull** ([spec](docs/force-pull.md)), off by default (ForcePull):
  lock, flick, catch; arm-ray aim; disarm; gold glow; online through the host
  (NET_MSG_FORCE_PULL, protocol 26); nothing through glass, walls or doors
  (gevrPullSightClear, also the hand grab). Disarmed guards fetch a gun or punch.
  Co-op guards' grenades and rockets fly on clients (NET_MSG_COOP_SHOT).
- **Hands:** two-handed rifle butt, empty-hand clench; GE-X sniper bat (fist
  on the stock, off hand anywhere on the barrel below, its own Gun fit
  VrGexBatFit/GexBat*, owner's fit as default); in VR the trigger does no
  melee (fists, butt, knife; throwing knives and bots keep it); hip
  Holster and Grip hand toggles gone; the cycle tap skips empty guns (#234).
- **Third person**, flat, solo (#230 step 1). **Audio (#229):** 48 kHz
  device, resampled in the game; Reset sound.
- **Bean:** bulletin board, glass (#231), ground holes, doorways (Dab's
  closeDoorGaps), stacked decals, bullet holes on the release's wall,
  Streets' vehicles and posters, Jungle UVs.
- **Fixes:** SpaceWarp default only with Bean at 120 Hz; script teleports
  keep loco smoothing; crashes 8e53cb27, a8d242cd/42ed2cf1, sig-e97854f4,
  ebcf7d40 (hack campaigns load the hack's setup names). Arenas give no
  start or pickup to floor only glass leads to (Archives (Full)'s
  courtyard; also Caverns, Dam, Aztec): gevr_arena.c arenaGlassFloors. A
  drop too tall to climb back into floor with no way up is a wall in arenas
  (arenaPits; only Cradle's antenna platform and dish floor, #232).
- Test hooks: gevr_cheat.txt cycle/cycleback, ammo<T>_<N>; gevr_fakegrip.txt's
  9th field holds a grip. Suite 104/104 at test.31; symbols on the reports repo.

**Code review, 2026-11-07** (fixes published in test.27):
friends settings retried every second offline; search opt-out lost on a new
identity; offline Discord unlink undone; Discord stuck connecting after one
failed refresh; Reset sound could leave silence; no resampler played 2.2x
fast; hack setup names installed unchecked; a pull lost in flight hung in
the air; GE-X bat pose read a matrix before its check; ammo<T> wrote past
ammoheldarr; Mods and Haptics tab changes were saved only by START. Suite
104/104 and FriendsClientTest 11/11 after them; APK builds. Open from it:
- Force pull online: the host settles only setup pickups. Pool guns and
  grenades are each headset's copy, and a grenade is owned at the catch
  before the host agrees: two players can keep one gun, or one grenade
  blows twice; a refusal after the catch (RTT > ~0.2 s) is too late.
- The Worker (goldeneyevr.com social.ts) answers any failed Discord call
  with 401, read as an expired link: a Discord outage unlinks players.
- gevr_player.json (secret, Discord refresh token) sits in external app
  storage; getFilesDir() needs a migration.

**Owed before release:** two-headset force pull and co-op guard shots (and
through a dedicated server); online pause tabs as host and client; force
pull feel (GEVR_PULL_FLICK_*); Bean doorways (Statue hedge, Control lift);
Jungle mines by tap; goldeneyevr://join from the Quest browser; #229 by its
reporter. Then docs/releases/v0.5.15.md, version bump, tag.

## Releases

Each has docs/releases/vX.Y.Z.md. v0.5.14 all maps in Horde/Deathmatch,
dedicated server; v0.5.13 co-op world from the host
([#216](docs/issue-216-coop-world.md)); v0.5.12 Bean frame rate; v0.5.11
XBLA Community Edition; v0.5.10 Facility fog flash; v0.5.9 voice gain;
v0.5.8 quit ANR; v0.5.7 Horde maps, Mirror Mode; v0.5.6 battery clocks;
v0.5.4/5 Horde, AI ally, SpaceWarp; v0.5.3 ragdolls; v0.5.0-2 XBLA 120 Hz;
v0.4.20 hack campaigns, Project Bean ([PR #172](docs/pr172-handoff.md)).

## What it is

Native standalone Quest port of n64decomp/007 with Perfect Dark VR's port
layer and GEVR PC presentation; the player supplies an owned NTSC-U ROM.
Releases reach players through the headset updater and SideQuest; the site
(goldeneyevr.com) has its own repository. Solo stereo VR and a flat or
curved screen; experimental multiplayer to eight (bots, co-op and Horde to
four, internet lobbies, voice, host migration, dedicated server). README.md
and MULTIPLAYER.md describe it all.

## Outstanding issues / limits

- Two-headset checks owed from earlier releases: Horde co-op (revives,
  late join, host loss, new maps, swarm/boss rounds), the ally after host
  takeover, tracked arms, a Facility co-op join, #216, #164, #161-#163,
  #95 Statue/Cradle, bots on a client, "First to N"; #88 at 5+ headsets.
- Runway autoguns firing after a timed mine: get a log (`autogun:`).
- v0.5.14: fog-edge prop fade unseen; #228 and #226 with reporters; server
  on real ARM and the Windows setup. Surface I sky and Frigate engine-room
  judder with Bean.
- Co-op cutscenes, revive, menus and debrief need two-headset play.
- #30 water lines; #85 Egypt door (gevr_zdebug.txt, gevr_decal.txt).
- Hand reload, body slots, game rules WIP. GE-X: KF7/arms only; with GE-X
  manual reloading a cycle tap still skips the sniper beside a KF7.
- Dam speedboat: a full crossing still to play. Bean: no water shaders,
  120 Hz misses in fast turns, simplified floors.
- Crash reports: issues in private MrSco/goldeneye-vr-reports; each
  build's symbols zip goes to its "symbols" release.

## How to work on it

- Build: android/gradlew.bat assembleRelease with JAVA_HOME=C:/Program
  Files/Java/jdk-20; signing from gitignored android/keystore.properties
  and the .jks (preserve the key). NDK 25.1.8937393, CMake 3.22.1.
- Suite: every port/tests/test_*.py, Windows python first, WSL on failure,
  with the GE/GF64/TND ROMs and build/xbla-files; Java: gradlew
  testReleaseUnitTest. Debug loop: tools/gevr_boot_test.ps1.
- Release: bump versionCode/versionName, tag the build commit, sign
  GoldenEye-VR-vX.Y.Z.apk. Never install to a headset unasked.
- A batch is one branch (now `prerelease/v0.5.15`); a new branch gets its
  own worktree. Commit before building. The user reviews in the headset and
  says merge. Prefer Dab's Mod, GEVR PC and Perfect Dark VR precedents.
- ROMs and model exports stay local. Mixed line endings (* -text): keep
  each file's endings (msys `sed -i` strips CRs); compare
  `git diff --stat` with `--ignore-cr-at-eol`.
- Code: src/ game; port/src platform/input/audio/net; port/vr OpenXR and
  launcher; port/fast3d renderer; android/ Java; tools/ scripts. References
  beside the repo: gevr-up, pdvr, gepc-ref, GoldenEye-007-HD, dabs-mod.
