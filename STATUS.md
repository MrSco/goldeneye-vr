# GoldenEye VR: status

Rewrite this file in place and keep it under about 120 lines. Older session
logs are in docs/archive/; feature investigations keep their own notes.

**Updated / current build:** 2026-10-04, v0.4.6; versionCode 59, protocol 18.
The release tag identifies the signed build commit. No game data is shipped.

**v0.4.6:** #95 round 2 (#110): launcher cheats (2x/10x Health, 2x Armor,
Max ammo, Extra weapons); Comfort WHEN HIT (no knockback, default on; keep
firing when hit; red flash switch); Statue and Cradle online; Perfect Dark's
cylinder floor finder (stan.c stanFindGroundAtCyl) for remote bodies and
Bond's tile recovery; guards and other players hold the detailed
first-person guns (gevr_heldgun.c, ini DetailedGuns); stage guns' HD
textures at load; MP death music fix. Upstream GEVR PC features (#111): a
watch VR settings page, smooth-turn speed, grip gestures (hip holster WIP,
grip use, grip to hand WIP and off, mine re-grab), hand reload (WIP, off),
Perfect Dark VR per-gun recoil (off; two hands: almost no kick, tighter
spread), game rules (mines stick to guards, bodies stay; off). Room decals
draw one pass pulled by a reach of 3 x D_800364CC (v0.4.5's 80 drew the Dam
bridge's shadows through it; bisected live with gevr_decal.txt mode 4) and
are no longer cut in front (#72/#84); the watch pause sits on the room's
virtual screen. Melee on the Dam and Surface measures the hand in the
guards' units (#112). Non-USA ROMs refused in every byte order (#108).
Protocol 18: v0.4.5 rejects stages 22/41. Notes: docs/releases/v0.4.6.md,
docs/issue-95-round2.md, docs/upstream-vr453-features.md.

**v0.4.5:** optional Meta Quest passthrough behind the 2D virtual screen,
saved in goldeneye-vr.ini and toggled in the SCREEN tab; Frigate hull "06"
marking restored (white face over black drop shadow) and recessed fixtures
fixed; co-op joiner ready button fixed (#107); sniper aim with no-lean zooms
without movement. Notes: docs/releases/v0.4.5.md.

**v0.4.4:** mission gadgets and items held in the hand in stereo, with a
per-item pose table (bondview2.c s_gevrItemPoses); Gun fit fits gadgets too,
stays on across saves, and Menu + A toggles it in single player; debug logs
carry gevr_itempose.txt; the watch magnet and other watch gadgets need the
hand at the watch, like the detonator. Notes: docs/releases/v0.4.4.md.

**v0.4.3:** wrist status on the watch face, independent per-hand reloads,
handed ammo counters, and multiplayer ready/warmup/kick flow (#106).

**v0.4.2:** hold A/X opens a round weapon category wheel (#10: stick picks a
wedge, triggers step within it); a laser-pointer pause window for deathmatch
and co-op with health/armour arcs and live radar in its header (PR #102);
bullet hit flash, smoke and spark pools sized to the bullet hole pool (#91).
Co-op joiners fall back to player 0 in party title menus, preventing the
viSetupCurrentPlayerView NULL-player crash (reports 253b6b4f, e8d14102).
Protocol 17 unchanged: v0.4.0 and v0.4.1 still play with v0.4.2.

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
  category wheel, hold X the left wheel. Solo dual guns use the game's inventory pair.
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
- #23: multiplayer stays open/experimental. Remote hands do not animate.
- #95: GE Plus survey; 14 of 21 items done (#110 untested on two
  headsets: Statue/Cradle matches, remote bodies at ledges, remote guns).
- #9: the issue stays open for any hands not yet patched.
- #30: intermittent colored water lines cleared on launcher restart and
  did not recur in final capture; still unresolved.
- #85: Egypt Golden Gun room exit door can draw black. The cryptdoor log
  targets room/portal visibility; no confirmed fix yet.
- Laser/AR33 and other penetrating weapons keep the original game's object
  penetration rules.
- Debug controls remain: files/gevr_zdebug.txt, gevr_decal.txt (mode 4 a:
  room decal reach) and cryptdoor logging; the optional surface probe is
  disabled unless its marker is present.
- #111 WIP: hand reload, hip holster and grip to hand need refining; grip to
  hand and the game rules are untested in real play / on two headsets.
- GoldenEye X (claude/gex-weapons, WIP toggles): the KF7 from the player's
  own GE-X ROM with its animations, reloaded by hand in the headset (clips
  keep their rounds), GE-X's arms with GoldenEye's watch, Gun fit's reload,
  off hand and grip modes; the watch's VR settings now hold the launcher's
  options in sections. Other guns next. Notes: docs/gex-weapons.md.
- Use `git worktree list` for active checkouts. Unmerged: feature/xbla-hd,
  fix/30-water-sky; claude/gex-watch-sleeves is parked, not for merge.

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
