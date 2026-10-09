# GoldenEye VR: status

Rewrite this file in place and keep it under about 120 lines. Older session
logs are in docs/archive/; feature investigations keep their own notes.

**Updated / current build:** 2026-10-08, v0.4.14 released (versionCode 69,
protocol 20; v0.4.13 and older can't join). Building does not publish a
GitHub release. No game data is shipped.

## Recent releases

**v0.4.14:** bots handle doors, soft player collision, the hit immunity rule
(Short default), object escape, HUD through hits (#153). Notes:
[v0.4.14](docs/releases/v0.4.14.md).
**After v0.4.14:** co-op briefing hang and the Caverns music-stop wait
(reports 7b162d90, 919e73ee, c3ad6b13). Notes:
[unreleased](docs/releases/unreleased.md).
**v0.4.13:** crash fixes (#151): stale portal table at the title, full sound
queue. Notes: [v0.4.13](docs/releases/v0.4.13.md).
**v0.4.12:** the GE-X detonator watch's default fit. Notes: [v0.4.12](docs/releases/v0.4.12.md).
**v0.4.11:** deathmatch bots and 3D game audio (#149), GE-X for every weapon and
item (#137-#148), co-op tank sync (#143), No radar and silent-headset drop
(#144), Statue/Depot pop-in (#133, #140), #136, #139, #145. Notes:
[v0.4.11](docs/releases/v0.4.11.md).

## Release baseline

**v0.4.10:** smoother wall and curb movement, fewer render stalls, shared co-op
mission gadgets (#127, #131). Notes: [v0.4.10](docs/releases/v0.4.10.md).

**v0.4.9:** smooth VR locomotion between game ticks; performance status;
GE-X muzzle flashes follow fitted barrel tips (#126); calibrated belt reloads
(#124, #125). Notes: [v0.4.9](docs/releases/v0.4.9.md).

**v0.4.6-v0.4.8:** settings persistence (#113), co-op spawn unclogging (#121),
GE-X KF7 and hand reload (#114-#118), #95 round 2 (#110), watch settings (#111).
Notes in docs/releases/.

**Earlier:** v0.4.5 added screen passthrough and fixed Frigate fixtures,
co-op readiness and sniper no-lean zoom. v0.4.4 added in-hand gadgets and
persistent gadget fit. v0.4.3 added wrist status and independent hand reloads.
v0.4.2 added weapon wheels and multiplayer pause UI. v0.4.1 added host-camera
co-op cutscenes. v0.4.0 added four-player co-op and eight-player deathmatch.
See docs/releases/, docs/playtest-* and Git history for earlier evidence.

## What it is and what works

Native standalone Quest port of n64decomp/007, with Perfect Dark VR's port
layer and GEVR PC presentation. Player supplies an owned NTSC-U ROM.
Releases reach users through the headset updater and SideQuest. The site
is goldeneyevr.com, in its own repository.

- Solo stereo: headset camera, controller guns/muzzle fire, two-handed
  holds, scopes in either hand, gadgets/watch arm, patched hands, melee,
  HUD, snap/smooth turning and comfort. Flat/curved screen for menus/cinema.
- A/X cycles; grip + A goes back; hold A/X opens right/left weapon wheels.
  Solo dual guns use the game's inventory pair.
- Launcher: ROM pickers, play mode, display/comfort, gun fit, cheats,
  textures, updater, audio and microphone settings.
- Experimental multiplayer: LAN/direct IP and public/private internet
  lobbies, ICE/TURN, up to eight deathmatch players (bots fill slots) or four
  co-op players, synced combat/pickups, clocks/life IDs, late join, voting,
  host migration, names, teams and proximity/team voice (Opus/Steam Audio).
  Mute via launcher, watch or Menu + right B. See MULTIPLAYER.md.

## Outstanding issues / limits

- Co-op cutscenes on a teammate, revive, menus and debrief need continued
  two-headset testing; the invisible ending report remains open. Bond's
  cinema animation and script music stay on the host.
- #88: eight slots tested on one headset; measure 5+ headsets' frame time
  and upload before relay batching/culling. Remote hands do not animate.
- Bots: untested on two headsets (client view, mid-round replace, adoption);
  bots may stall at closed doors. MULTIPLAYER.md has the test script.
- Two-headset checks still owed from #95 (closed): Statue/Cradle online,
  remote ledges and guns.
- #30: intermittent colored water lines; no confirmed fix. #85: Egypt
  Golden Gun room exit door can draw black; cryptdoor visibility logging.
- Hand reload, hip holster, grip to hand and game rules remain WIP;
  grip to hand/game rules need real-play and two-headset checks.
- GE-X: KF7/arms only; Dam headset play covered magazines/holds/fits/watch.
  Mid-level watch refresh and handedness changes still need headset checks.
- Debug markers: gevr_zdebug.txt, gevr_decal.txt, cryptdoor logging;
  surface probe disabled unless its marker is present.
- Use git worktree list for checkouts. Unmerged: feature/xbla-hd,
  fix/30-water-sky; claude/gex-watch-sleeves is parked, not for merge.

## How to work on it

- Build: android/gradlew.bat assembleRelease; signing uses gitignored
  android/keystore.properties and the existing .jks. Preserve the key.
  NDK 25.1.8937393, CMake 3.22.1; short junctions avoid long-path failures.
  Debug loop: tools/gevr_boot_test.ps1. Clean configure fetches SDL2/Opus.
- Release: bump versionCode/versionName, tag the build commit, release-sign
  GoldenEye-VR-vX.Y.Z.apk for the updater. Never install to a headset unasked.
- One branch per issue/feature. Commit before building for a useful build
  hash. User reviews in headset and says merge. Original game behavior stays
  unless authorized; prefer GEVR PC / Perfect Dark VR precedents.
- ROMs/model exports stay local. Hand patches contain additions/fingerprints,
  not cartridge meshes. Keep generated C and recipe/patch files consistent.
- Mixed line endings (* -text): preserve focused edits and compare diff stats
  with --ignore-cr-at-eol. Code: src/ game; port/src platform/input/audio/net;
  port/vr OpenXR/launcher; port/fast3d renderer; android/ Java; tools/ scripts.
- References beside primary repo: gevr-up, pdvr, gepc-ref, GoldenEye-007-HD,
  goldeneyevr.com. Port faults: byte order, pointer truncation, scale/frame
  mismatches. See docs/RARE-LOGO-AUDIO-HANDOFF.md and docs/gepc-port-guard-sweep.md.
