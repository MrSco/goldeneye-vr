# GoldenEye VR: status

Rewrite this file in place and keep it under about 120 lines. Older session
logs are in docs/archive/; feature investigations keep their own notes.

**Updated / current build:** 2026-10-07, v0.4.11, versionCode 64, protocol 18;
main is ahead of it (below) on protocol 19. Building does not publish a
GitHub release. No game data is shipped.

## Unreleased on main (after v0.4.11)

- Deathmatch bots (claude/mp-bots, replaces rejected PR #146): host-run player
  slots with AI pad input and Perfect Dark's simulant brain, six difficulties,
  stan-tile pathfinding, pickup seeking, Flag Tag/Golden Gun awareness; lobby
  and pause rows Off/Fill/Fixed. Mid-round joins replace the lowest bot; a new
  host adopts the bots. Solo host + 7 bots headset-accepted 2026-10-07.
- 3D game audio in every mode: placed sounds go through the voice chat's
  Steam Audio HRTF, and are measured round walls through the portals (PD's
  path distance). Split screen keeps the game's rule.
- Pause laser on its own eye layer over the panel; death sting plays again
  (blood drip paced at 60 Hz); name tags hidden by fog; HUD top message and
  clock no longer hidden by other players' damage flashes.

## v0.4.11

- GE-X magazine weapons and per-weapon VR fits: PP7, silenced PP7, DD44, Klobb,
  ZMG, D5K, silenced D5K, Phantom, and AR33 alongside KF7 (#137). Physical reload
  grips, support rotation, held magazine preview, magazine well, and installed
  magazine mesh fitting baked into defaults.
- Bullet tracers stop on the player cylinder in solo mode (#136).
- Statue and Depot outdoor room pop-in fix (#133): draw neighboring rooms when
  bounds are on screen even if the portal box missed them.
- Updated launcher header and branding assets (#134).

## Release baseline

**v0.4.10:** smoother wall and curb movement, fewer render stalls, shared co-op
mission gadgets (#127, #131). Notes: [v0.4.10](docs/releases/v0.4.10.md).

**v0.4.9:** smooth VR locomotion between game ticks; performance status;
GE-X muzzle flashes follow fitted barrel tips (#126); calibrated belt reloads
(#124, #125). Notes: [v0.4.9](docs/releases/v0.4.9.md).

**v0.4.8:** immediate launcher settings persistence, read-only INI recovery and
atomic writes (#113); co-op spawn unclogging in narrow mission starts (#121).
Notes: [v0.4.8](docs/releases/v0.4.8.md).

**v0.4.7:** GE-X KF7 from patched Perfect Dark ROM, physical hand reload,
GE-X arms with live watch, co-op and Facility fixes (#114-#118). Notes:
[v0.4.7](docs/releases/v0.4.7.md).

**v0.4.6:** #95 round 2 (#110), watch VR settings and grip gestures (#111),
#112, #108; protocol 18 rejects older stage lists. Notes:
[v0.4.6](docs/releases/v0.4.6.md), docs/issue-95-round2.md.

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
- #95: GE Plus survey, 14 of 21 done. Statue/Cradle, remote ledges/guns
  still need two-headset checks. #9 stays open for remaining hand patches.
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
