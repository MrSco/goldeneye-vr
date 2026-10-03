# GoldenEye VR: status

Rewrite this file in place and keep it under about 120 lines. Older session
logs are in docs/archive/; feature investigations keep their own notes.

**Updated / current build:** 2026-10-03, v0.4.2; versionCode 53, protocol 17.
The release tag identifies the signed build commit. No game data is shipped.

**v0.4.1:** co-op teammates watch the host's scripted and ending cutscenes
through the host's camera and fades (#94, NET_MSG_COOP_CINEMA; protocol 17
unchanged, v0.4.0 ignores the new message and still plays with v0.4.1). A
force-stopped run is no longer reported as a crash (#96); the single-player
Line mode cheat no longer crashes on a level's first frame (#97).

**v0.4.0:** online co-op campaign for up to four players (#94: the solo
campaign through the game's own menus, the host driving; each player's own
folder and saves; revive, teammate radar, host-run guards). Eight-player
deathmatch (#88): the host picks 2 to 8 players on any stage; stages short
of start pads gain them at ammo spots. Patched hand shells for the PP7
family, golden gun, Cougar, DD44, knives, watch arm and taser/grenade hand,
and left-hand gadgets held mirrored (#9). Guards hold rifles two-handed
(#93). Cradle and smoke layering (#89), solo ammo crates give only their own
ammo (#74), VR crosshair depth, Quest runtime render size and Auto refresh
(#90), four GE Plus fixes (#95), HD + AI pack 2026.10.03.3 and boot preload
of HD menu art. Protocol 17: 16 was only the unreleased test builds, whose
packet layout changed under that number.

**Recent releases:** v0.3.10 kept the full watch arm while gripping (#60),
eased the watch grip, fixed water precision (#30) and stereo glass. v0.3.9
added scopes in either hand, the snap-turn vignette, panel hands and room
decal fixes (#72). v0.3.8 fixed crash reporting. v0.3.7 added clock/life-ID
combat sync (protocol 15), hand cycling and the TURN fallback. Earlier
evidence is in docs/playtest-* and Git history.

**v0.4.2 pause/editor work (PR #102):** a shared opaque launcher-style
Quest pause window, direct ray controls, Match/Rules/Player/Audio tabs,
eight-player scores and independent votes, four-player co-op party/objectives,
original health/armour arcs with live radar in the header, Start toggling,
host restrictions and confirmed leave/end mission. The mobile
Menu Studio starter matches that window; JSON exports guide native rearrangement.
Signed local release builds and desktop/phone browser + production ImGui/input
checks pass. Quest interaction and multiplayer headset testing remain
outstanding. Protocol is unchanged; this build is not published as a release.

## What it is and what works

Native standalone Quest port of n64decomp/007, with Perfect Dark VR's port
layer and GEVR PC presentation. Player supplies an owned NTSC-U ROM.
Releases reach users through the headset updater and SideQuest. The site
is goldeneyevr.com, in its own repository.

- Solo missions in true stereo: headset camera, controller guns/muzzle
  fire, two-handed holds, real scopes in either hand, gadgets/watch arm,
  patched hand shells, melee, HUD, snap/smooth turning and comfort at 90 Hz.
  Menus/cutscenes can use the flat or curved virtual screen.
- Weapon controls: A/X cycles, grip + A goes back, hold A opens the weapon
  panel, hold X the left panel. Solo dual guns use the game's inventory pair.
- Launcher: ROM picker, play mode, turning/comfort, gun fit, unlock-all
  (RAM only), textures, updater, music/SFX/voice and microphone settings.
- Multiplayer is experimental: LAN/direct IP and public/private internet
  lobbies, ICE with TURN fallback. Deathmatch for 2 to 8 players (the host's
  count, any stage) with synced weapons, damage/deaths/respawns/pickups,
  clocks/life IDs, late-join spectators, ballots, host migration,
  names/tags, teams (up to 4v4) and proximity/team voice. Co-op campaign for
  up to four (Mode: Co-op mission). Opus has its own ENet channel; Steam
  Audio supplies binaural direction. Mute via launcher, watch/pause menu or
  Menu + right B. Details: MULTIPLAYER.md, docs/issue-94-coop.md.

## Outstanding issues / limits

- #94 co-op: accepted on one headset as host; two-headset play (menus
  followed, revive, debrief on a teammate, and v0.4.1's cutscenes through
  the host's camera) is untested. Bond's cinema animations and script music
  stay on the host. Notes: docs/issue-94-coop.md.
- #88 eight players: checked on one headset (8 slots on Facility/Egypt,
  90 Hz). Measure frame time and host upload with 5+ headsets before relay
  batching or culling the other players' view passes (MULTIPLAYER.md).
- #23: multiplayer stays open/experimental. Remote hands do not animate;
  held guns are the original low-detail third-person models.
- #95: GE Plus survey; four fixes shipped, the rest of its list is open.
- #9: the issue stays open for any hands not yet patched.
- #30: intermittent colored water lines cleared on launcher restart and
  did not recur in final capture; still unresolved.
- #85: Egypt Golden Gun room exit door can draw black. The cryptdoor log
  targets room/portal visibility; no confirmed fix yet.
- Laser/AR33 and other penetrating weapons keep the original game's object
  penetration rules.
- Debug controls remain: files/gevr_zdebug.txt and cryptdoor logging; the
  optional surface probe is disabled unless its marker is present.
- Use `git worktree list` for active checkouts. Unmerged: feature/xbla-hd,
  fix/30-water-sky.

## How to work on it

- Build: android/gradlew.bat assembleRelease; signing uses gitignored
  android/keystore.properties and the existing release .jks. Preserve the
  key: changing it forces uninstall. Local Java is JDK 20; NDK 25.1.8937393
  and CMake 3.22.1. A short junction can avoid Windows long-path failures.
  Debug loop: tools/gevr_boot_test.ps1. A clean configure fetches SDL2/Opus.
- Release: bump versionCode and versionName; tag equals the build commit;
  asset name must be GoldenEye-VR-vX.Y.Z.apk for the updater. Release-signed.
- One branch per issue/feature. Commit before building so the launcher hash
  identifies the code; the user reviews in the headset and says merge.
  Never install to a headset unasked. Original game behavior remains unless
  the user authorizes a change; prefer GEVR PC / Perfect Dark VR precedents.
- The ROM and model exports are local only and never committed. Hand patches
  store additions/fingerprints, not cartridge meshes. Keep generated C and
  patch/recipe files consistent.
- Files use mixed line endings (`* -text`); preserve them for focused edits.
  Compare diff --stat with diff --stat --ignore-cr-at-eol before committing.
- Code map: src/ decomp; port/src platform/input/audio/net; port/vr OpenXR,
  launcher/settings; port/fast3d renderer; android/ app/Java bridges;
  services/lobbies Worker; tools/ development scripts.
- References beside the primary repo: gevr-up, pdvr, gepc-ref, GoldenEye-007-HD
  and goldeneyevr.com. Common port faults: N64 byte order, truncated pointer
  slots and scale/frame mismatches. See docs/RARE-LOGO-AUDIO-HANDOFF.md and
  docs/gepc-port-guard-sweep.md.
