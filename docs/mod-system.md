# Mod system (spec, draft)

Status: proposal, nothing implemented. Written 2026-11-23 after the Tactical
PRs (#285–#289) were declined. This spec lives on the `mod-system` branch
and is tuned with mod authors' feedback: propose changes as pull requests
against that branch (or an issue). Tactical is the first mod built to it.

## Goals and rules

- GoldenEye VR carries **no mod's code**. A mod is a pack the player
  installs; the launcher lists it and can start it.
- Mods work on **every platform** we ship: Quest, PC (flat and PC VR),
  Linux, Android, iOS, Apple TV, Vision Pro, and the dedicated server.
  So mod logic is a script, never a native library (Apple platforms can't
  load one).
- **The mod's author owns the mod.** We own the spec and the API. If a
  game release breaks a mod that only uses the API as documented, that's
  our bug. If it relies on anything else, it's theirs. A release never waits
  for a mod.
- We take a mod only when it's finished. A mod in development is the
  author's local install, not a launcher entry.
- The game stays fast: rendering stays native, scripts react to events,
  and mods share a per-frame time budget.
- **Performance floor: Quest 2 at 90 fps or better.** The slowest headset we
  support sets the bar. A mod that can't hold 90 fps on a Quest 2 in its
  heaviest mission isn't ready, and fixing it is its author's job. It can
  still be listed, with a performance warning. Faster headsets get the same
  mod at their own rates (Quest 3 at 120 Hz).
- **A mod's own systems are Lua in the mod.** Its inventory, magazines,
  menus, loadout screens, rules and network state are the mod's scripts and
  data. The API only reports what the game does and lets a script steer
  systems the game already has. See "What the API covers" below.

## Pack format

A pack is a zip that unpacks into `texture-packs/mods/<id>/` (the Quest's
`files/`, `$S` on PC and Apple), beside today's packs:

```
mod.json            manifest (below)
scripts/main.lua    entry point; other .lua files under scripts/
assets/...          models, textures, data the scripts load
files.sha256        checksum list of every other file
```

`mod.json`:

```json
{
  "format": 1,
  "id": "tactical",
  "title": "Tactical",
  "by": "Kelly-D-Ripper",
  "version": "1.0.0",
  "api": 1,
  "kind": "campaign",
  "base": "ge",
  "coop": true,
  "site": "https://github.com/..."
}
```

- `id`: lowercase letters, digits and `-`, unique. It names the save file,
  the ghost tag and the folder.
- `api`: the API version the mod was written for (see Versioning).
- `kind`:
  - `campaign`: a separate entry in the campaign list, its own saves.
  - `overlay`: changes the current campaign (a look or a rule), selected on
    the Mods page like Oddjob's hat today.
- `base`: which cartridge it runs on: `ge`, `gf`, `tnd`, `mirror` or `mario`.
  The base ROM must be installed.
- `coop`: whether the mod supports online co-op (see Multiplayer).
- `affectsPlay` (overlays): `true` when the overlay changes rules, not only
  looks. It is then checked in the lobby (see Multiplayer).
- `modes` (overlays that affect play): the multiplayer modes it works in,
  from `coop`, `deathmatch` and `horde`.
- The content ID is the SHA-256 of `files.sha256`, computed at install. It
  identifies the exact build of the mod.

## Installing

- **v1: install from file** on the Mods page (zip picker; adb push or a file
  manager on the Quest). We don't distribute mod content.
- **Later: a mod browser.** A `mods.json` index in the public
  goldeneye-vr-mods repo, holding only names, authors, descriptions,
  screenshots links and release URLs (no assets). The launcher's Mods page
  browses it under "Community mods", downloads from the author's release
  and installs it the same way as a file, with a line saying these are
  third-party. A mod gets listed when it's finished and its author asks.
  The author shows a perf log from a Quest 2 in the mod's heaviest mission;
  a mod that doesn't hold 90 fps there is listed with a performance warning
  on its row. We can delist it. Design the v1 installer so the browser only adds a
  download step in front of it.
- Install checks: manifest syntax, `api` supported, checksums match,
  no paths outside the folder, size limits. Failure leaves nothing installed.
- Update: same as packs today (a newer release in the index or a newer zip).
- Today's own packs (HD, Bean, GE-X, Oddjob's hat) don't change.

## Launcher

- Installed `campaign` mods appear in the campaign list after the hack
  campaigns, titled from the manifest, with "(mod)" and the author in the
  tooltip.
- A mod whose base ROM is missing, or whose `api` this build doesn't
  support, shows as unavailable with the reason. Starting it never falls
  back silently to another campaign without a note (as the hacks do today).
- The campaign tables that list every campaign (names, ghost tags,
  friends, net names) get one generic "mod campaign" entry. The mod's
  `id` supplies its tag. No per-mod entries in our code.

## Saves and records

- A campaign mod saves to `eeprom-mod-<id>.bin`.
- Mod settings and data the script stores: `mods/<id>/save/` through the
  API, at most 1 MB.
- Speedrun ghosts and boards use tag `mod-<id>`. Online ghost boards are off
  for mods in v1 (the content ID would need to be part of the board key).
- iCloud sync leaves mods out in v1.

## Multiplayer

- Co-op in a mod campaign needs the same mod on every machine. The lobby's
  content check (`net_protocol.h` `NET_CONTENT_*`) gets one part: the mod's
  content ID. The host refuses a joiner without the same mod, and says
  which mod and version is needed.
- An `overlay` mod that changes only looks is not checked: like Bean or HD
  today, it doesn't matter what another player sees. An overlay that changes
  rules declares `"affectsPlay": true` and is checked like a campaign mod.
- Mods with `"coop": false` aren't offered in online co-op.
- Deathmatch and Horde: campaign mods stay out (those modes use GoldenEye's
  maps and tables). An overlay that changes rules works in any mode **only
  when every player has the same mod** (same content ID, checked in the
  lobby like co-op). It declares the modes it supports in the manifest
  (`"modes": ["coop", "deathmatch", "horde"]`); a lobby whose host has
  it on refuses joiners without it. Bots follow the host's rules.
- Scripts run on every machine. The host is authoritative, as for guards
  today: the API has `mod.net.send` / `mod.net.on` over one generic message
  (`NET_MSG_MOD`, at most 1 KB per message, rate-limited). The mod decides
  what it syncs. The dedicated server runs scripts too.
- **Where an event fires:** on the machine where the game already settles
  that outcome (`net_coop.c`).
  - **Guards:** the host runs every guard's AI and applies all damage to
    guards; a client only reports its hits. Guard events, and
    `before_damage` for a guard, fire on the host only (except
    `guard_spawning` for the setup's guards at load, below). A client's
    script that needs them gets them from the host over `mod.net`.
  - **Players:** a player's shots, throws and damage are settled on that
    player's headset, so `weapon_fire_attempt`, `player_damaged` and
    `before_damage` for that player fire there.
  - **World changes:** calls that change the world (spawn a guard or a
    pickup, give a guard a weapon) work on the host only and return false
    on a client. The game's co-op sync carries the result to the others,
    as it does for spawns today.
- **The same choices on every headset.** Each headset loads the mission
  itself from a shared seed, and the setup's guards and pickups line up
  slot for slot. Events during load (`pickup_spawning`, and
  `guard_spawning` for the setup's guards) run on every headset and must
  choose the same on all of them. The sandbox replaces `math.random` with
  `mod.random`, which in co-op is seeded from the seed every headset shares
  for the mission (`gevrCoopIntroSeed()`). A choice made from anything
  else, like a player's own save, can differ.
- **Weapon stats belong to the weapon type** (magazine size, fire rate,
  damage), not to one gun, so a change applies to everyone holding that
  weapon. Every headset sets the same values, and the game puts the ROM's
  values back at mission end.
- **A mod turned off on one headset** (a script error, or over budget) is
  turned off on every headset: that headset tells the host and the host
  tells the others. Otherwise the players drift apart for the rest of the
  mission.

## Scripting

- **Language:** Lua 5.4 (MIT licence, plain C, builds on all our targets
  including the server). The interpreter only: no JIT (iOS forbids it).
- **Sandbox:** no `io`, `os`, `package`, `debug` or `load` of binary chunks.
  Files only through the API, inside the mod's own folder.
- **Budget:** 0.25 ms per frame for **all mods together**, script time and
  garbage collection included. Why: the game's own logic is about 1.5 ms
  of the early tick (4.1 ms mean), and heavy levels already run DRAW/TICK
  at 7.3–8.3 ms (docs/bean-performance-*.md, docs/jungle-bean-perf.md).
  0.25 ms is about 2% of a Quest 2 frame at 90 Hz (11.1 ms), 3% of a
  120 Hz frame (8.33 ms) and a sixth of the game's logic: room for several
  hundred script calls on Quest. An event-driven mod should use a few
  hundredths of a millisecond.
  - Checked as an average over one second, so one heavy event (a mission
    start, a burst of pickups) doesn't trip it. Lua's garbage collector runs
    in small steps inside the budget.
  - A count hook stops a single runaway call (an endless loop) at once.
  - Over budget for 3 seconds: the most expensive mod is paused for the
    mission with a note on the watch (in co-op on every headset, see
    Multiplayer).
  - The perf log (`--perf-log`) shows each mod's time; `mission_start`
    and loading are not counted (loading screen).
  - The budget is held on the **Quest 2 at 90 Hz** (11.1 ms frames, the
    slowest CPU we support), not only on faster headsets. On a Quest 3 the
    same scripts take less time.
  - The number is a starting value, dialed in as the first mod (Tactical)
    is built to the spec and we see how it runs. Phase 7's runs on the heaviest levels
    (Jungle, Bean, body piles) with the sample mod, on a Quest 2 at 90 Hz
    and a Quest 3 at 120 Hz, confirm or change it.
- **Memory:** an allocator cap per mod (start at 32 MB).
- **Errors:** a script error disables that mod for the rest of the
  mission and logs the error with the mod's name, never crashing the game.
  Whatever the mod changed goes back (weapon stats return to the ROM's
  values). In co-op it is turned off on every headset (see Multiplayer).
  Crash reports name the active mods.

## API v1

Events and calls, all on the game's tick, never per draw call. This list
is the proposal to refine with mod authors (Tactical first).

| Area | Events | Calls |
|---|---|---|
| Lifecycle | `campaign_enter`, `mission_prepare`, `mission_start`, `mission_end`, `tick(dt)` (capped) | `mod.log`, `mod.setting`, `mod.capabilities` |
| Weapons | `weapon_fire_attempt`, `weapon_fire`, `reload_start/end`, `weapon_drawn`, `weapon_dropped` | read and set loaded ammo, magazine size, fire rate, damage (within limits) |
| Hands (VR) | `grip`, `release`, `hand_near(anchor)` | hand pose, body anchors (body slots), haptics |
| Items | `pickup_spawning` (can swap a weapon, ammo or armour pickup at its existing pad), `item_pickup`, `item_spawned` | spawn a pickup, take or give an item |
| Guards | `guard_spawning`, `guard_spawned`, `guard_alerted`, `guard_shot`, `guard_killed`, `corpse_retiring` | read state, give a guard's weapon or outfit (from the ROM's bodies), spawn a guard and send it along a patrol path through the game's own spawn and AI commands |
| Damage and player | `before_damage`, `damage_applied`, `player_damaged`, `player_downed`, `player_revived`, `player_died` | health and armour |
| Assets | — | load a model or texture from the pack, attach it to a hand, gun or body slot, `mod.audio.play` (ROM sound effects only), `mod.effect.play` |
| UI | — | a watch page from a simple layout (rows, buttons, sliders) |
| Network | `peer_ready`, `mod.net.on` (includes the sender's player ID) | `mod.net.send`, `mod.net.role` |
| Storage | — | read and write in `mods/<id>/save/` |

Not in v1: drawing, shaders, level geometry, new AI behaviour, the
multiplayer modes, anything per vertex or per draw call.

### What the API covers

Each event and call is a thin adapter over a system the game already has
(for example the body slots, hand reload, pickups, guard spawning, the
watch). We maintain the adapters, not the mods.

- **In the API:** events for what the game does, a way to cancel or change
  it at that moment (a shot, a pickup, damage, a guard's loadout), and calls
  that drive existing systems.
- **In the mod, as Lua:** anything the game doesn't already do. Per-item
  state (magazines, chambers, attachments, durability), inventories and
  transfers, loadout and Workshop screens, custom rules, and what the mod
  syncs between players and how it settles conflicts (`mod.net`, the host
  decides). The mod's menus are built from the watch page layout.
- **New game systems** (a new kind of menu, per-instance weapon state, new
  rendering features, new network channels) are not mod requests. They join
  the API only as game features: useful beyond one mod, contributed or
  agreed as part of the game, and working on every platform we ship
  (Quest, PC, Linux, Android, Apple, the dedicated server) with tests. Until
  then a mod does without them or does them in Lua.
- A request for the API names the existing game system the adapter would
  sit on. If there isn't one, it's a new game system (above).

### Where each hook sits

- **Campaign and mission.** Emit `campaign_enter` once the selected
  campaign's base ROM is loaded by `romdataInit()`. Emit `mission_prepare`
  in `lvlStageLoad()` before `proplvreset2()` sets up mission props and
  guards. `mod.capabilities` reports VR hands from the active stereo
  mode (`g_gevrStereo`) and VR readiness (`gevrVrReady()`). Headless
  capability comes from the build state (`GEVR_HEADLESS`).
- **Shots and throws.** Emit `weapon_fire_attempt` before ammo is spent
  in `gunTickHandState()` for trigger shots and `gevrMotionThrowTick()`
  for VR throws. A script can cancel either action. Loaded ammo uses the
  hand's existing `weapon_ammo_in_magazine` count, read by
  `get_ammo_in_hands_magazine()`.
- **Pickups.** During `proplvreset2()`, emit `pickup_spawning` only for
  loose weapon, ammo and armour pickups at existing pads. A script can
  replace an eligible pickup before its placement by `weaponAssignToHome()`
  or `domakedefaultobj()`. Keys and other mission items use their native
  setup. Weapons flagged `PROPFLAG_ASSIGNEDTOCHR` use `guard_spawning`.
- **Guards and corpses.** Emit `guard_spawning` during
  `expand_09_characters()` and `chrSpawnAtPad()`. Allow a ROM body and
  head choice before the model is created and a native weapon choice
  through `weaponAssignToHome()` or `chrGiveWeapon()`. Guard spawning and
  patrol calls use `AI_TRYSpawningChrAtPad` (`chrSpawnAtPad()`) and
  `AI_StartPatrol` (`if_actor_able_set_on_path()`). Emit `guard_alerted`
  once per transition into alert. Report `heard` for noise through
  `chrCheckGuardsHeardSound()` and `chrlvAlertGuardToPlayerPosition()`,
  `saw` for player sighting through `chrlvSetTargetToPlayer()`, or `alarm`
  for an alert through `alarmActivate()`. Emit `corpse_retiring` in
  `chrlvTickDead()` before the body is marked for removal, while its
  position is available.
- **Damage and revives.** Emit `before_damage` and `damage_applied` around
  native damage in `handles_shot_actors()` and `chrlvExplosionDamage()`
  for guards and `record_damage_kills()` for players. The before event
  allows the script to change or cancel the pending damage. The after
  event reports the result. `player_downed` and `player_revived` report
  the co-op transitions in `coopSetLocalDowned()` and
  `coopAnnounceDowned()`.
- **Sound and effects.** `mod.audio.play` uses `sndPlaySfx()` to play
  an existing ROM sound effect by ID. Positional playback uses
  `chrobjSndCreatePostEventDefault()`.
  `mod.effect.play` selects built-in effects such as smoke and bullet
  impacts through `explosionCreateSmoke()` and
  `explosionCreateBulletImpact()`.
- **Co-op.** Emit `peer_ready` on the validated stage-ready transition
  in `netReceiveStageReady()`. `mod.net.role` uses `netIsHost()` and
  `netIsDedicated()`. The sender ID in `mod.net.on` comes from the
  engine's peer-to-player slot mapping in `netHandlePacket()`.

## Versioning

- `api` is one integer. A build supports a range (for example 1–2).
- Within a version, we only add events and calls, never change or remove
  them.
- A breaking change makes a new version. The old version stays
  supported for at least two releases after it, with a note in the release
  notes and in the launcher when an installed mod uses it.
- The API reference (`docs/mod-api.md`) lists each call with the API
  version that added it.

## For mod authors

- A sample mod (in goldeneye-vr-mods) that uses every area of the API.
- A desktop editor or tool for a mod stays in the mod's own repo.
- Test on a Quest 2 at 90 Hz with `--perf-log` before release. The log's
  per-mod line shows script time against the budget.
- Asking for a new event or call: an issue with the use case. We add it
  generically if it fits the API; we don't add mod-specific hooks.

## Plan

1. **Agree on this spec** with mod authors. Tactical's list of needs
   (events and calls, #291) is its first real test.
2. **Packs and launcher:** manifest, install from file, the generic mod
   campaign entry, saves, the content check. No scripting yet: a
   data-only mod can be installed and started.
3. **Scripting core:** Lua, sandbox, budget, errors, the lifecycle and
   storage calls, the perf log line.
4. **API areas** one at a time, each with native tests (as the repo's
   `port/tests/` do): weapons and items, hands and body slots, guards,
   player, assets, UI.
5. **Network:** `NET_MSG_MOD`, co-op with mods, the dedicated server.
6. **Sample mod and API reference.**
7. **Platform check:** Quest, PC, Apple, server, with the sample mod.
   Quest 2 at 90 Hz and Quest 3 at 120 Hz on the heaviest levels settle the
   script budget; the Quest 2 must hold 90 fps.
8. **Mod browser** (after v1): the community index and the Mods page's
   "Community mods" list.
