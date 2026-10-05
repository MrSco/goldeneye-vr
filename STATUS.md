# GoldenEye VR: status

Rewrite this file in place and keep it under about 120 lines. Older session
logs are in docs/archive/; feature investigations keep their own notes.

**Updated:** 2026-10-04. Latest release: v0.4.6, versionCode 59, protocol 18.
The release tag identifies its signed build commit. Current source includes
the changes below; no newer APK release is implied. No game data is shipped.

## Current source after v0.4.6

- #115 / #114: co-op clients prepare dead guards' hand weapons for normal
  drop/detach/activation. Pickups stay individual; live weapon changes stay
  intact. Native regressions pass; two-headset acceptance is pending.
- #116: co-op start preserves a local save folder or falls back to folder 1
  when its picker was bypassed. Missing saves no longer crash Statistics
  (a16b05b5). The invisible Bunker ending cutscene remains unresolved.
- #117: optional GE-X 6a KF7 and animations from the player's own patched
  Perfect Dark USA v1.1 ROM; removable magazines in stereo (rounds retained
  or returned to reserve when dropped), belt reloads and dual-wield support.
  Optional GE-X arms wear GoldenEye's live/pause watch. Both toggles off.
  Separate gun/grip/scope fits; X cycles gun/scope/reload/off-hand modes.
  Reload grab/belt points, palm/watch and supporting-hand fits are saved.
- #117: watch VR settings in Comfort, Controls, Gestures, Weapons, Display,
  Game rules, Mods and fun; includes handedness, screen/passthrough and
  immediate refresh requests. Texture packs stay in the launcher.
- #118: Facility command-buffer overruns (4bde84c2, 76717e5d) get larger
  frame pools, auxiliary allocation guards, completed-list validation and
  usage telemetry. About 1.13 MiB more of Facility's existing heap;
  individual command writes remain unchecked. Heavy Quest 2 combat pending.
- Combined validation: 63 multiplayer tests, seven frame-pool regression
  groups, co-op debrief/baseline regressions, watch, ammo/input/wrist,
  pause UI and display/settings checks; Android ARM64 debug APK builds.
  The combined build has not been installed or played on a headset.

Player guide: [GE-X setup](docs/gex-setup.md). Implementation:
[GE-X weapons](docs/gex-weapons.md). Full changes and remaining checks:
[unreleased notes](docs/releases/unreleased.md), [co-op notes](docs/issue-94-coop.md),
[debrief crash](docs/crash-a16b05b5-coop-debrief.md), [Facility crash](docs/crash-4bde84c2.md).

## Release baseline

**v0.4.6:** #95 round 2 (#110): extra launcher cheats, Comfort WHEN HIT,
Statue/Cradle online, cylinder floor finder for remote bodies/tile recovery,
detailed held guns, stage HD texture preload and MP death music fix. #111:
watch VR settings, turn speed, grip gestures, WIP hand reload, per-gun recoil,
and optional single-player mine/body rules. Room decal reach fixed, pause
watch on the virtual screen, Dam/Surface melee scale fixed (#112), non-USA
ROMs refused (#108). Protocol 18 rejects older stage lists. Notes:
[v0.4.6](docs/releases/v0.4.6.md), docs/issue-95-round2.md,
docs/upstream-vr453-features.md.

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
  lobbies, ICE/TURN, up to eight deathmatch players or four co-op players,
  synced combat/pickups, clocks/life IDs, late join, voting, host migration,
  names, teams and proximity/team voice (Opus/Steam Audio).
  Mute via launcher, watch or Menu + right B. See MULTIPLAYER.md.

## Outstanding issues / limits

- Co-op cutscenes on a teammate, revive, menus and debrief need continued
  two-headset testing; the invisible ending report remains open. Bond's
  cinema animation and script music stay on the host.
- #88: eight slots tested on one headset; measure 5+ headsets' frame time
  and upload before relay batching/culling. Remote hands do not animate.
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
