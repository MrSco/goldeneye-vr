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

- The zip holds these at its top, or inside one folder (as a release zip
  often does). What a computer adds by itself (`__MACOSX/`, `.DS_Store`,
  `Thumbs.db`, `desktop.ini`) is left out. Anything outside `scripts/`
  (`.lua` only) and `assets/` is refused, except `.md` and `.txt` files at
  the top (a readme, a licence).
- `files.sha256` is `sha256sum`'s own output: a `<sha256>  <path>` line
  (or `<sha256> *<path>`) for every file but itself, each once, with `/`
  between folders. From the mod's folder:
  `find . -type f ! -name files.sha256 -printf '%P\0' | xargs -0 sha256sum > files.sha256`
- Names are printable ASCII without `\ : * ? " < > |`; no part is `.` or
  `..`, ends in a dot or a space, or is a Windows device name (`CON`,
  `NUL`, `COM1`...); no two names differ only in case. Every platform can
  then hold the same files.

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

- `id`: 1 to 32 lowercase letters, digits and `-`, not starting or ending
  with `-`; unique. It names the save file, the ghost tag and the folder.
- `title` and `by`: up to 48 plain ASCII characters. `version`: up to 24,
  no spaces. `site` (optional): an `https://` address.
- `api`: the API version the mod was written for (see Versioning).
- `kind`:
  - `campaign`: a separate entry in the campaign list, its own saves.
  - `overlay`: changes the current campaign (a look or a rule), selected on
    the Mods page like Oddjob's hat today.
- `base`: which cartridge it runs on: `ge`, `gf`, `tnd`, `mirror` or `mario`.
  The base ROM must be installed.
- `coop` (optional, `false` when left out): whether the mod supports
  online co-op (see Multiplayer).
- `affectsPlay` (overlays): `true` when the overlay changes rules, not only
  looks. It is then checked in the lobby (see Multiplayer).
- `modes` (overlays that affect play): the multiplayer modes it works in,
  from `coop`, `deathmatch` and `horde`.
- Keys this build doesn't know are skipped, so a later format can add some.
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
- **One installer for every platform:** `port/src/gevr_modpack.c` (C,
  miniz) on the headset, PC, Apple, Android phones and the dedicated
  server. Each platform's file picker only hands it a zip, as an open file
  (`gevrLauncherModPicked`), and it installs on a thread of its own:
  - Quest and Android phones: the system picker; Java hands over the file's
    descriptor (`MainActivity.nativeModPicked`), so nothing is copied and
    a 2 GB mod needs no second 2 GB.
  - PC (Windows, Linux): the open dialog; the player's own file is read
    where it is.
  - iPhone and iPad, Apple TV (its upload page) and Vision Pro: the
    system's copy, deleted once installed. Klepton: its staged copy, the same.
- The Mods page's MODS row: **Install from file...**, the install's
  progress with Cancel, its outcome on the line, and **Installed (n)...**,
  which lists each mod (version, author, campaign or overlay, why it can't
  run) with Remove.
- Install checks, all before anything is in place: the manifest (above),
  `api` in the range this build runs, every file against `files.sha256` as
  it unpacks, the names (above), and the limits: 10,000 files, 256 MB a
  file, 2 GB in all, `mod.json` 64 KB, `files.sha256` 2 MB. The zip unpacks
  beside the installed mods (`.<id>.unpacking`) and only then replaces
  `<id>`. A failure or a cancel leaves the installed mods as they were; a
  crash's leftovers are tidied at the next start.
- Update: same as packs today (a newer release in the index or a newer zip).
- Today's own packs (HD, Bean, GE-X, Oddjob's hat) don't change.

## Launcher

- Installed `campaign` mods appear in the campaign list after the hack
  campaigns, titled from the manifest, with "(mod)" and the author in the
  tooltip. One whose cartridge's ROM is missing is listed greyed, "needs
  Goldfinger 64". Choosing it sets both the cartridge and the mod.
- A mod campaign stays on its own cartridge: choosing another campaign,
  adopting a hack's ROM, a missing ROM's fallback or joining a listed game
  of another campaign turns the mod off. START waits while an install runs.
- A mod whose base ROM is missing, or whose `api` this build doesn't
  support, shows as unavailable with the reason. Starting it never falls
  back silently to another campaign without a note (as the hacks do today).
- A mod campaign is a cartridge plus a mod: `VrCampaign` keeps naming the
  cartridge (the manifest's `base`) and `CampaignMod=<id>` in
  goldeneye-vr.ini names the mod. The tables keyed by cartridge (ROM
  layouts, names, the lobby's campaign part) don't change. Only what sets
  a mod campaign apart reads the id: the save file, the ghost tag, the
  launcher's list, the friends and lobby names, the content check. No
  per-mod entries in our code.
- At start (`romdataInit`) the chosen mod is checked again: its manifest,
  its `api`, every listed file present. One that can't run is off for that
  run, and the launcher says why (`gevr_campaign_failed.txt`, as for the
  hacks). If its cartridge doesn't load, the mod is off too and GoldenEye
  starts, with a note naming both.

## Saves and records

- A campaign mod saves to `eeprom-mod-<id>.bin`.
- Mod settings and data the script stores: `$S/mod-data/<id>/` through the
  API (`mod.save`, `mod.setting`), at most 1 MB. It sits outside the mod's
  install folder, so an update, which replaces that folder, keeps it.
- Speedrun ghosts use the tag `mod-<id>`. Online ghost boards are off for
  mods in v1 (the content ID would need to be part of the board key).
- iCloud sync leaves mods out in v1.

## Multiplayer

- Co-op in a mod campaign needs the same mod on every machine. The lobby's
  content check (`net_protocol.h` `NET_CONTENT_*`) gets one part: the mod's
  content ID. The host refuses a joiner without the same mod, and says
  which mod and version is needed.
  - The part (`NET_CONTENT_MOD`) is the content ID's first 8 bytes, or 1
    for "no mod". Every peer sends it (protocol 28), since a part that is 0
    on either side counts as unknown and is let through.
  - A host with a mod refuses a joiner without it or with another build;
    a host without one refuses a joiner with one. The mod is named before
    the campaign, as it names its cartridge too.
  - Before the refusal the host sends `NET_MSG_MOD_REQUIRED` (its mod's id,
    title and version), so the joiner reads "this party plays the mod
    Tactical 1.0.0: install it on the Mods page", "...and yours is 0.9", "a
    different build", or "this party plays without a mod".
  - Before the game starts, the launcher's chosen mod is the one checked
    (`gevrModPlay`), as the chosen cartridge is.
- The internet lobby lists a mod campaign's game under its cartridge (the
  lobby service is unchanged); friends see the mod's title in presence.
- An `overlay` mod that changes only looks is not checked: like Bean or HD
  today, it doesn't matter what another player sees. An overlay that changes
  rules declares `"affectsPlay": true` and is checked like a campaign mod.
- Mods with `"coop": false` aren't offered in online co-op: the Online
  page says the campaign is for one player, and neither Friends panel
  joins or invites. A mod campaign plays co-op only, like a hack's.
- A dedicated server's mod campaign comes from `CampaignMod=` in its
  goldeneye-vr.ini, checked at its start like any other.
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
  - **Guards:** calls that change a guard (spawn one, give one a weapon)
    work on the host only and return false on a client. The game's co-op
    sync carries the result to the others, as it does for the host's
    spawns today.
  - **Pickups:** ordinary pickups aren't synced in co-op; each headset has
    its own copy (`docs/issue-94-coop.md`). Spawning a pickup only places
    it on the headset that calls it, so a mod that wants every player to
    have it calls it on every headset: at load with `mod.random` (below),
    mid-mission by sending the choice over `mod.net` (say, from the host's
    `guard_killed`). Which player may take it, if the mod limits that, is
    the mod's to settle over `mod.net`.
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
  Lua 5.4.7, vendored unchanged in `port/external/lua` and built as its own
  library (`gevr_lua`); the script host is `port/src/gevr_modscript.c`.
- **Sandbox:** no `io`, `os`, `package`, `debug` or `load` of binary chunks.
  Files only through the API, inside the mod's own folder.
  - Open: the base, coroutine, math, string, table and utf8 libraries.
    Gone: `dofile`, `loadfile`, `collectgarbage`, `string.dump`,
    `math.randomseed`; `load` takes text only; `print` is `mod.log`.
  - `require "a.b"` loads `scripts/a/b.lua` from the mod, once. The entry
    point is `scripts/main.lua`; a mod without one is data alone and runs
    nothing.
- **The calls of phase 3** (the lifecycle row of API v1, and storage):
  - `mod.on(event, function)`: `campaign_enter()`, `mission_prepare(stage)`,
    `mission_start(stage)`, `mission_end(stage, "complete" | "left")`,
    `tick(dt)` (seconds, at most 0.25). An unknown event is an error.
  - `mod.log(...)`: one log line, `mod <id>: ...`, at most 30 a second.
  - `mod.setting(name, default)`: the player's choice for it
    (`settings.txt` in its data folder, `name=value` lines), as the
    default's type, else the default.
  - `mod.capabilities()`: `vr`, `hands`, `headless`, `platform` (`quest`,
    `android`, `pc`, `ios`, `tvos`, `visionos`, `server`).
  - `mod.random([m [, n]])`, also `math.random`: seeded each mission, in
    co-op from the seed every headset shares.
  - `mod.save.read/write/remove(name)`: files in its data folder; names are
    letters, digits, `_`, `-` and `.`, not first a dot; writes are whole or
    not at all.
  - `mod.id`, `mod.title`, `mod.version`, `mod.api`.
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
  - A count hook stops a single runaway call (an endless loop) at once:
    over 100 ms in play, over 2 s while loading.
  - Over budget for 3 seconds: the most expensive mod is paused for the
    mission with a note on screen (in co-op on every headset, see
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
  mission and logs the error with the mod's name and a traceback, never
  crashing the game; a note on screen says so. One in `scripts/main.lua` or
  `campaign_enter` turns it off for the run (it plays without its scripts).
  Whatever the mod changed goes back (weapon stats return to the ROM's
  values). In co-op it is turned off on every headset (see Multiplayer).
  Crash reports name the active mods.

## API v1

Events and calls, all on the game's tick, never per draw call. This list
is the proposal to refine with mod authors (Tactical first).

| Area | Events | Calls |
|---|---|---|
| Lifecycle | `campaign_enter`, `mission_prepare`, `mission_start`, `mission_end`, `tick(dt)` (capped) | `mod.log`, `mod.setting`, `mod.capabilities` |
| Weapons | `weapon_fire_attempt`, `weapon_fire`, `reload_attempt`, `reload_start/end`, `weapon_drawn`, `weapon_dropped` | read and set loaded ammo, magazine size, fire rate, damage (within limits) |
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
  - Built (`port/src/gevr_modapi.c` over `src/game/gevr_modgame.c`; the
    hooks in `gunfire.c` and `bondview2.c`): a shot is asked once per
    shot (an automatic, at each of its rate's ticks), after the firing
    switch and before the round, the sound, the beam and the damage. A
    declined shot clicks dry as an empty gun does (`DRY_FIRE`, nothing
    more until the trigger is let go). A grenade, knife or mine is asked
    as the hand lets go, a motion throw before it leaves the hand, a
    cooked grenade's drop too; declined, nothing leaves the hand and a
    cooking grenade stops cooking. Melee isn't asked.
  - `reload_attempt(hand, item)` before a reload takes rounds from the
    reserve: the native reload as it starts to lower the gun (the button,
    an empty gun's automatic reload, and the hand reload's gestures that
    use it: the chest cross, the belt's touch for a gun without a
    magazine), and, with hand reload, a fresh magazine taken at the belt
    by either hand, a dual-wielded gun's touch at the belt, and a round
    taken for a gun loaded a round at a time. Hand reload's magazine path
    is the same for the original, GoldenEye X and Bean models. A handler
    returning false declines it: the gun stays as it is and nothing
    leaves the reserve. An empty gun's automatic reload, declined, isn't
    asked again until the hand's item or its reserve changes; a reload
    the player asks for is always asked. A magazine pulled out and put
    back isn't asked: its rounds aren't the reserve's.
  - `weapon_fire(hand, item, loaded)` after each spend; `reload_start` as
    the native reload lowers the gun or a hand reload's magazine leaves
    it (`gevrGexMagOut`); `reload_end(hand, item, loaded)` after the
    native reload's ammo move or a magazine seated (`gevrGexMagIn`).
    Each `reload_end` follows a `reload_start`: a magazine topped up at
    the belt while still in the gun sends its start as it goes in.
    `weapon_drawn` at a switch's end. A hand reload's single rounds
    (`gevrGexRoundTick`) send no `reload_start` or `reload_end` yet.
  - The reserve moves without an event in three places a script that
    keeps magazines itself should know: the native reload and a fresh
    magazine top the gun up from it (loaded becomes the lesser of the
    magazine size and loaded plus the reserve) before `reload_end`; a
    drawn gun is topped up the same way before `weapon_drawn` (unless
    hand reload kept its magazine); and a magazine dropped from the hand
    or the gun gives its rounds back to the reserve. Setting the reserve
    and the loaded rounds in `reload_end` and `weapon_drawn`, and
    declining in `reload_attempt`, covers all three.
  - Only for this machine's human player (`!netIsActive() || the current
    player == netGetLocalSlot()`), never a copy or the host's bot. `hand`
    is `"right"` or `"left"` (the game's gun hands); `item` an id from
    `mod.items`.
  - Calls: `mod.weapon.held(hand)`, `loaded(hand)`, `setLoaded(hand,
    rounds)` (0 to the held weapon's magazine), `stats(item)` (`magazine`,
    `fireRate` for an automatic, `damage`) and `setStats(item, t)`:
    magazine 1 to 255, fireRate 1 to 60 ticks (automatics only), damage 0
    to 10 times the weapon's own; only items with stats of their own.
    Stats are per weapon type, guards' too, and go back to the game's own
    at a mission's end and when a mod stops.
  - Known gaps: on a pump gun a lower `setLoaded` reads as a shot (the
    rack, `gevr_rack.h`); in co-op another headset's copy of the player
    still shows a declined shot's fire (the fire bit comes from the
    trigger and the rounds), for the network step (5).
- **Pickups.** During `proplvreset2()`, emit `pickup_spawning` only for
  loose weapon, ammo and armour pickups at existing pads. A script can
  replace an eligible pickup before its placement by `weaponAssignToHome()`
  or `domakedefaultobj()`. Keys and other mission items use their native
  setup. Weapons flagged `PROPFLAG_ASSIGNEDTOCHR` use `guard_spawning`.
  Which pickups qualify, and what a swap can do: "Pickup swaps" below.
- **Guards and corpses.** Emit `guard_spawning` during
  `expand_09_characters()` and `chrSpawnAtPad()`. Allow a ROM body and
  head choice before the model is created and a native weapon choice
  through `weaponAssignToHome()` or `chrGiveWeapon()`. Guard spawning and
  patrol calls use `AI_TRYSpawningChrAtPad` (`chrSpawnAtPad()`) and
  `AI_StartPatrol` (`if_actor_able_set_on_path()`). Emit `guard_alerted`
  once per transition into alert. Report `heard` for noise through
  `chrCheckGuardsHeardSound()` and `chrlvAlertGuardToPlayerPosition()`,
  `saw` when the sight check finds the player (`chrCheckTargetInSight()`),
  `shot` when a guard turns on the player after being hit
  (`chrlvTickArgh()`), or `alarm` for an alert through `alarmActivate()`.
  Not `chrlvSetTargetToPlayer()` itself: it also runs on every tick of an
  attack and after a sidestep or jump-out. Emit `corpse_retiring` in
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

### Pickup swaps

`pickup_spawning` rewrites a pickup's setup record before the game places
it, the way multiplayer weapon sets already do in `weaponAssignToHome()`:
a new `weaponnum`, `obj` and `extrascale`, or no pickup at all. A gun
swapped in is then preloaded through `weaponLoadProjectileModels()` like
any other gun in the level.

A pickup can be swapped only when both of these hold:

- **It's an eligible kind.** Of the kinds a player can collect
  (`objIsCollectable()`):
  - Weapons (`PROPDEF_COLLECTABLE`, not `PROPFLAG_ASSIGNEDTOCHR`) whose
    item is a gun or a thrown weapon: `ITEM_KNIFE` to `ITEM_ROCKETLAUNCH`
    except `ITEM_WATCHLASER`, and `ITEM_GRENADE`, `ITEM_TIMEDMINE`,
    `ITEM_PROXIMITYMINE` and `ITEM_REMOTEMINE`. The item is read through
    `gevrItemAs()`, so a hack campaign's gun on a special item's slot
    (Goldfinger's Luger on the taser's) counts as the gun it is.
  - Ammo: single-type boxes (`PROPDEF_MAGAZINE`) and crates
    (`PROPDEF_AMMO`).
  - Body armour (`PROPDEF_ARMOUR`).
  - Never keys (`PROPDEF_KEY`), hats, gadgets or mission items.
- **No setup tag points at it.** Objectives and level scripts find their
  objects through tags (`PROPDEF_TAG`, `objFindByTagId()`), so a tagged
  pickup can be a mission item even when it's a gun. It keeps its setup.

A swap keeps the pickup's kind, since each kind is a different setup
record:

- a weapon becomes another eligible weapon, or nothing;
- ammo changes its type and amount, or becomes nothing;
- armour changes its amount, or becomes nothing.

Every headset runs the swaps at load, so in co-op the choices come from
`mod.random` (see Multiplayer).

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

1. **Agree on this spec** (done 2026-11-23): Tactical's list of needs,
   its first real test, is in from #291.
2. **Packs and launcher:** manifest, install from file, the generic mod
   campaign entry, saves, the content check. No scripting yet: a
   data-only mod can be installed and started. In three parts:
   - **2a, core (C):** the manifest, install from a zip with every check,
     the installed list and remove, the content ID, the run's mod
     campaign, its save file and ghost tag, boards and iCloud leaving mods
     out (`port/src/gevr_modpack.c`, `port/tests/test_modpack.py`).
   - **2b, launcher:** the Mods page's install from file and installed
     mods, campaign mods in the campaign list, each platform's file picker.
   - **2c, co-op:** the content check's mod part and the refusal message.
3. **Scripting core:** Lua, sandbox, budget, errors, the lifecycle and
   storage calls, the perf log line.
4. **API areas** one at a time, each with native tests (as the repo's
   `port/tests/` do): weapons and items, hands and body slots, guards,
   player, assets, UI. Weapons first (`port/tests/test_modapi.py`), then
   pickups and items.
5. **Network:** `NET_MSG_MOD`, co-op with mods, the dedicated server.
6. **Sample mod and API reference.**
7. **Platform check:** Quest, PC, Apple, server, with the sample mod.
   Quest 2 at 90 Hz and Quest 3 at 120 Hz on the heaviest levels settle the
   script budget; the Quest 2 must hold 90 fps.
8. **Mod browser** (after v1): the community index and the Mods page's
   "Community mods" list.
