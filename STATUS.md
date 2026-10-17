# GoldenEye VR: status

Rewrite this file in place and keep it under about 120 lines. Older session
logs are in docs/archive/; feature investigations keep their own notes.

**Current release:** v0.5.3 (versionCode 83, protocol 21), 2026-10-16.
v0.4.16 through v0.5.2 can join; v0.4.15 and older can't. Building does not
publish a GitHub release. No game data is shipped.

**Unreleased #189:** Horde in Archives, solo/1–4 humans, normal armed guards,
endless paced waves, automatic resupply/weapon progression, revives, results
and retry. Protocol 22 requires all players to update. Native/synthetic checks
and debug APK build pass; no headset acceptance yet. [Horde](docs/horde-mode.md).

On main, unreleased (PR #196): Mods > Oddjob's hat. Throwing knives look like
Nightfire's hat in the Original, Bean and GE-X hands, in flight and lying
about; the hat is a launcher download from MrSco/goldeneye-vr-mods (each mod's
own releases), not in the APK. Headset-accepted; GE-X's weapon panel unchecked.
[Oddjob's hat](docs/oddjob-hat.md).

v0.5.3 (PR #187, PR #188): optional ragdolls and dismemberment, off by
default, on Play > Game rules (Damage needed / Chance sliders, Bodies stay in
every mode). Cosmetic, local to each headset; tuning, cut rules, caps and level
collision ported from Perfect Dark CE (Bean caps use CE's rule on Bean's skin);
shot bodies bleed. GE-X rocket launcher: far-side decals culled, 1 cm decal
band for hand-held models, its own rocket in the tube. Headset-accepted on Dam;
Quest frame time with many bodies and two-headset play still unverified.
[Ragdolls](docs/ragdolls.md), [v0.5.3](docs/releases/v0.5.3.md).

v0.5.0 (PR #180, PR #183): XBLA at 120 Hz on Quest 3 (early game tick before
the XR wait, eye pass drawn by the redraw replay; 4-8 missed frames a minute on
Dam, all in full-speed turns), Bean parity from Dab's Mod (node mode, body
fade, scorched props, wounds, reflections, GoldenEye's water motion, hidden
backdrop rooms, release boot logos, HD fill), #181 misses fly past Bond,
Wreck's blood patch (Mods, off by default), guard tower stairs camera, Goldfinger
title cast crash da10370a. v0.5.1: wounds on Bean heads (head box hits snap to
the head, 40-unit radius), blood patch on its own Mods row above Done.
v0.5.2 (PR #184, PR #185): Bean wound crashes 53f092b3/71b515e8 (shared head
nodes), the Dam speedboat on the water, driven by the move stick in VR and 2D,
stopped by the docks, grip on and off. [v0.5.0](docs/releases/v0.5.0.md),
[v0.5.1](docs/releases/v0.5.1.md), [v0.5.2](docs/releases/v0.5.2.md),
[performance](docs/bean-performance-improvements.md),
[Bean backlog](docs/bean-parity-backlog.md).

v0.4.22's co-op start fixes (#164) still need two/four-headset starts, reloads,
host swaps and Jungle Natalya. [#164 evidence](docs/issue-161-164-campaign-fixes.md).

v0.4.20 merged PR #172: the hack campaigns, Project Bean, the Dam speedboat
and alarms. Headset checks covered Bean grips, gun fits and low doorways.
[PR #172](docs/pr172-handoff.md), [Bean](docs/project-bean.md),
[campaigns](docs/rom-hacks.md), [v0.4.20](docs/releases/v0.4.20.md).

Movement speed: 50%-200%, 100% default, separate solo and host settings;
host changes apply to the whole session, bots included. Room-scale walking
is unchanged. Native checks pass; two-headset checks pending. Details:
[movement speed](docs/player-movement-speed.md). Launcher Match and Comfort
use two columns, and the eight-player lobby fits without scrolling. Chest
and Belt defaults use saved Quest fits at 170 cm; custom fits stay intact.
See [body slots](docs/body-slots.md).

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

- #164: v0.4.22 fixes the logged vacant-body blocker; broader co-op mission
  starts and reloads remain to be confirmed. Keep the issue open for now.
- #161–#163: fixes released in v0.4.16; issues closed at v0.4.18 on the
  user's say. Only Natalya's solo movement/combat is headset-confirmed; the
  co-op checks in docs/issue-161-164-campaign-fixes.md are still owed.
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
- Solo Dam speedboat: grip at its edge (or B without grip use) to board at
  the north jetty, the move stick drives, grip at the island or jetty to step
  off. Hull is 1.75× retail. See [Dam speedboat](docs/dam-speedboat.md).
  Headset: jetty boarding, driving and the dock confirmed; a full crossing to
  the island landing still to play.
- Bean: the release's water shaders (bump, distortion, reflection) are not
  drawn; Dam's reservoir and Caverns' pools move as GoldenEye's water. Remaining
  120 Hz misses are in full-speed turns. Tower glass can pop in and out (not
  yet reproduced from the PC). Silo/Control floor
  materials are simplified;
  the grenade stays in GoldenEye's taser hand (the release has no hand
  holding one, user). Campaigns, speedboat and alarms need broader play.
- Use git worktree list for active checkouts.

## How to work on it

- Build: android/gradlew.bat assembleRelease; signing uses gitignored
  android/keystore.properties and the existing .jks. Preserve the key.
  On this Windows machine set JAVA_HOME=C:/Program Files/Java/jdk-20 each time.
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
