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
assets/...          models, textures, data the scripts load:
  models/<Name>     a cartridge model file in the cartridge's place (Assets)
  textures/...      PNGs named as GLideN64 ("Rice") packs name them
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

- A campaign mod saves to `eeprom-mod-<id>.bin`. Its script can mark a
  mission complete there (`mod.mission.complete`); the base game's save
  is never written by a mod.
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
  (`NET_MSG_MOD`, at most 1 KB per message, rate-limited; the calls are
  under API v1's "Where each hook sits", Network). The mod decides what it
  syncs. The dedicated server runs scripts too.
- **Where an event fires:** on the machine where the game already settles
  that outcome (`net_coop.c`).
  - **Guards:** the host runs every guard's AI and applies all damage to
    guards; a client only reports its hits. Guard events, and
    `before_damage` for a guard, fire on the host only (except
    `guard_spawning` for the setup's guards at load, below). A client's
    script that needs them gets them from the host over `mod.net`.
  - **Players:** a player's shots, throws and damage are settled on that
    player's headset, so `weapon_fire_attempt`, and `before_damage` and
    `damage_applied` for that player, fire there.
  - **Guards:** calls that change a guard (spawn one, give one a weapon)
    work on the host only and return false on a client. The game's co-op
    sync carries the result to the others, as it does for the host's
    spawns today.
  - **Pickups:** the setup's pickups are shared. Each headset places its
    own copy at load, and when a player takes one the others' copy goes too
    (`NET_MSG_OBJECT_STATE`, by the record's setup index; `net_core.c`
    `netSendObjectEvent`). A swap keeps the record's index and kind, so
    this holds for swapped pickups as long as every headset swaps the same
    way (`mod.random`, below). What the game makes at run time, such as a
    dead guard's gun, is per headset (`docs/issue-94-coop.md`). So is a
    pickup a mod spawns (not built yet): it's placed only on the headset
    that calls it, so a mod that wants every player to have it calls it on
    every headset (mid-mission, by sending the choice over `mod.net`, say
    from the host's `guard_killed`), and settles over `mod.net` which
    player may take it, if it limits that.
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
  mission. Built as `NET_MSG_MOD`'s `NET_MOD_OFF`: each headset that hears
  it stops the mod for the mission, puts back what it changed and shows a
  note ("it stopped on another player's headset"), without telling anyone
  again. In solo a stop is told to no one.

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
  - `mod.note(text)`: a short line on this player's screen, the game's own
    bottom message as a pickup's is ("Magazine dropped"): 1 to 80
    printable characters, one line. Shown at the frame's end, the last
    of the frame; a stopped mod's own note goes first, and one waiting as
    a mission loads is dropped.
  - `mod.setting(name, default)`: the player's choice for it
    (`settings.txt` in its data folder, `name=value` lines), as the
    default's type, else the default.
  - `mod.capabilities()`: `vr`, `hands`, `headless`, `platform` (`quest`,
    `android`, `pc`, `ios`, `tvos`, `visionos`, `server`).
  - `mod.mission.difficulty()`: `"agent"`, `"secretagent"`, `"00agent"` or
    `"007"`, the one chosen (`selected_difficulty`); nil without one, as
    in multiplayer. `mod.mission.folder()`: the save folder the run plays
    from, 1 to 4 as the folder screen shows them (`selected_folder_num`),
    or nil, so a mod can keep its progress per folder in its data.
    `mod.mission.day()`: today's number, whole days since 1 January 1970
    (UTC), for daily challenges; each headset reads its own clock, so a
    co-op mod sends the host's. `mod.mission.complete(mission, difficulty[,
    seconds])` marks a mission (1 Dam to 20 Egypt, the folder screen's
    order) complete at that difficulty and those below it, in the run's
    save folder, through the game's own `fileUnlockStageInFolderAtDifficulty()`
    (as the unlock cheats write it), with its time if given (else none).
    Only in a mod campaign, whose save is its own file
    (`eeprom-mod-<id>.bin`), never the base game's: true when written,
    false outside one or with no folder chosen. It's this headset's save;
    in co-op each headset that should have it calls it.
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
  values); pickups it swapped stay as they were placed until the mission
  ends, and what it gave or took stays the player's. In co-op it is turned
  off on every headset (see Multiplayer).
  Crash reports name the active mods.

## API v1

Events and calls, all on the game's tick, never per draw call. This list
is the proposal to refine with mod authors (Tactical first).

| Area | Events | Calls |
|---|---|---|
| Lifecycle | `campaign_enter`, `mission_prepare`, `mission_start`, `mission_end`, `tick(dt)` (capped) | `mod.log`, `mod.setting`, `mod.capabilities` |
| Weapons | `weapon_fire_attempt`, `weapon_fire`, `reload_attempt`, `reload_start/end`, `weapon_drawn`, `weapon_dropped` | read and set loaded ammo, magazine size, fire rate, damage (within limits) |
| Hands (VR) | `grip`, `release`, `hand_near(anchor)` | hand pose, body anchors (body slots), haptics |
| Items | `pickup_spawning` (can swap a weapon, ammo or armour pickup at its existing pad), `item_pickup`, `pickup_removed`, `item_spawned` | spawn a pickup, take or give an item |
| Guards | `guard_spawning`, `guard_spawned`, `guard_alerted`, `guard_shot`, `guard_killed`, `corpse_retiring` | read state, give a guard's weapon or outfit (from the ROM's bodies), spawn a guard and send it along a patrol path through the game's own spawn and AI commands |
| Damage and player | `before_damage`, `damage_applied` (a player's too: no separate `player_damaged`), `player_downed`, `player_revived`, `player_died` | health and armour |
| Assets | — | the pack's model files in the cartridge's place and its textures over the HD pack (no call: they're there); `mod.weapon.model` (a gun's model per hand), `mod.attach` (a model on the gun, a hand or a body slot); `mod.audio.play` (ROM sound effects only), `mod.effect.play` |
| UI | `ui_click`, `ui_change` | a page on the in-game screen from a simple layout (text, toggles, choices, sliders, buttons): `mod.ui.page`, `remove`, `open`, `close` |
| Network | `peer_ready`, `mod.net.on` (includes the sender's player ID) | `mod.net.send`, `mod.net.role`, `mod.net.slot` |
| Storage | — | read and write in `mods/<id>/save/` |

Not in v1: drawing, shaders, level geometry, new AI behaviour, the
multiplayer modes, anything per vertex or per draw call.

### What the API covers

Each event and call is a thin adapter over a system the game already has
(for example the body slots, hand reload, pickups, guard spawning, the
in-game screen). We maintain the adapters, not the mods.

- **In the API:** events for what the game does, a way to cancel or change
  it at that moment (a shot, a pickup, damage, a guard's loadout), and calls
  that drive existing systems.
- **In the mod, as Lua:** anything the game doesn't already do. Per-item
  state (magazines, chambers, attachments, durability), inventories and
  transfers, loadout and Workshop screens, custom rules, and what the mod
  syncs between players and how it settles conflicts (`mod.net`, the host
  decides). The mod's menus are built from the in-game screen's page
  layout (`mod.ui`).
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
  - `out_of_ammo(hand, item)` when a hand's gun and its reserve are both
    empty and the game is about to switch that hand to its next weapon
    (`gevrAutoAdvanceHand()` in VR, `autoadvance_on_deplete_all_ammo()`
    flat, as the trigger is let go). A handler returning false keeps the
    empty gun in hand; it then waits for a reload, or for the player to put
    it away. Kept, it isn't asked again until the gun fires, gets rounds
    (`reload_end`) or is drawn. Leaving the tank isn't asked: its shells go
    with it.
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
    to 10 times the weapon's own; `inaccuracy` (the KF7's 10, the sniper
    rifle's 0; 0 to 100, or ten times its own), `recoilBack` and
    `recoilUp` (how far the hands pull back and the gun kicks up; 0 to
    10, or ten times its own), `sound` (the shot's ROM sound effect,
    `mod.audio`'s ids) and `loudnessMin` and `loudnessMax` (how far
    guards hear the shot, a suppressor's lower; 0 to 100, or ten times
    its own most, min at most max); only items with stats of their own.
    `automatic` (true or false): the gun fires while the trigger's held
    at its `fireRate` (an automatic's branch of the firing switch,
    `gunfire.c`), or a shot a press (a pistol's); the game decides it by
    the item, and `gevrModFireAs()` sends a mod's choice down the other
    branch. A gun made automatic needs a `fireRate`, its own or given in
    the same call; `fireRate` is refused on a gun that isn't automatic.
    `stats` reads it back. Guards holding that weapon fire it the same
    way.
    A thrown weapon or gadget (a grenade, knife, mine, plastique, the bug,
    the camera, the GoldenEye key) has its trigger throw's `throwSpeed`
    (1 to 100 units a tick; the game's 16.7, a knife's 25, the key's 6.7),
    `throwLift` (-50 to 50 upward; the game's 5) and `throwGuide` (true
    or false). The trigger throw is the game's own: along the hand's aim
    (the crosshair's, flat), at that speed with that lift, plus the
    player's own motion (`gun.c` `gevrThrowTrigger`, which the thrown
    grenades, knives and objects all launch through); the next one
    readies, or the detonator, as the game does. `throwGuide` draws a
    short line from where the throw leaves the hand along the way it
    would leave now, from the same calculation less the item's random
    spread, as the game draws a tracer (`gunfire.c` `gevrThrowGuideDraw`),
    for the local player; the base game's Throw guide setting draws it
    for every thrown item. It shows only while an item is ready in the
    hand (idle, or the trigger pressed), not while a throw leaves or the
    next comes up, and not while the grip winds up a motion throw, which
    is separate. `throwGuide` can also be a look, `{ length, width,
    colour, fade }`: length 10 to 500 and width 0.5 to 30 in world units,
    colour `{ r, g, b }` 0 to 255 each, and fade (to nothing at its far
    end), what's left out the game's tracer's (100 long, 6 wide, white,
    not fading); the engine still places and draws it, as a tracer with
    that radius and colour. `stats` reads them back for a thrown item,
    the look as `throwGuideStyle`; an item that isn't thrown refuses
    them.
    Stats are per weapon type, guards' too, and go back to the game's own
    at a mission's end and when a mod stops.
  - Scopes (VR): `mod.weapon.scope(hand, t)` puts the game's per-hand
    scope (issue #40, `gevrScopeBeginHand()`: the world drawn again
    through a camera on the shot's line, shown as a lens on the gun) on
    the gun the hand holds. `t = { at = { x, y, z }, radius = r, zoom = m }`:
    the eyepiece's centre and radius in the drawn model's own coordinates,
    as a model editor shows them (the game's own table, `s_gevrScopes`,
    is in the same units: the sniper rifle's is `13.5, 126.5, -128, 23.5`),
    and `m` the magnification, 1 to 25. In model units, the lens follows
    the player's gun size, the gun's place in the hand and a GoldenEye X
    gun's mirroring in the left hand, as the game's own scopes do; it has
    no Gun fit trims of its own. Calling it again (a new `zoom`, say)
    changes it at once. `false` takes the scope off, even the game's own
    on its scoped guns (the KF7's and AR33's magnifiers, for iron sights);
    `nil` gives the game's own back. It holds for the gun in the hand now:
    each draw puts the game's own back, so `weapon_drawn` is the place to
    set it, and it goes when a mod stops. True when set, false with
    nothing in the hand; it shows only in VR, and each lens costs a draw
    of the world, as the game's own do (two with a scope in each hand).
    The reticle is the game's red sight, with the "Aim: crosshair"
    option, as on the game's own scopes. `mod.weapon.look(hand)` says
    which models the hand's gun is drawn with (`"original"`, `"gex"`,
    `"bean"`, or `"mod"` for a mod's own, below), so a mod can keep an
    eyepiece for each.
  - A gun's model per hand: `mod.weapon.model(hand, item, file)` draws
    the mod's `assets/models/<file>` when that hand holds `item`, in place
    of the item's cartridge model, so several of a mod's guns can share one
    of the game's items and each hand can show a different one. The file
    is built over the item's cartridge model as `assets/models`'
    replacements are (its header: switch and texture counts, skeleton),
    and must fit the room the game makes for a hand's gun. It wins over
    GoldenEye X's and Bean's models for that hand, and takes the original
    models' fits. A hand holding the item now loads it at once; `nil`
    gives the item's own back. True when set, false when the mod has no
    such file (a file that won't load leaves the cartridge's, and the log
    says why). Every hand's own come back at a mission's start and when a
    mod stops. The muzzle is the model's own: the flash and the shots
    start at its flash node (the position node on switch 3, which
    `gunfire.c` reads from the drawn model, as GoldenEye X's models give
    theirs), so a longer gun puts its muzzle at its own barrel's end; the
    item's Gun fit muzzle trim still adds. What stays the item's: its
    stats, sounds, Gun fit places (the two-handed hold's among them) and
    hand reload's magazine (a mod's model has none of the cartridge's
    measured magazines, so its gestures run the game's own reload). `gun.c` keeps the choices as a table, and each tick a hand
    showing another model than its gun's choice loads it again, so a
    choice never touches a player mid-load (`gevrModGunReconcile()`).
  - A self-describing `GEVRWPN1` or `GEVRWPN2` mesh can also be used as
    `file`; leave the exported bytes unchanged. It draws at the stock
    gun's root and keeps the stock hand and sleeve: the lists the game
    shows and hides as the hand (switches 8-13 and 35), less the gun
    pieces three of GoldenEye's own models file there (the PP7's, the
    silenced PP7's and the TT-33's, `modelGunHandList()` in `model.c`).
    The fist's stock model is the hand itself, none of it under those
    switches, so a mesh for the fist replaces the hand. The stock gun still
    drives animation and reload, and the shot origin unless the options
    below give a muzzle. To fit the pack body at an
    authored offset from the carrier root, use `mod.weapon.model` with a
    fourth argument `{ frame = ... }`, using the same affine rows as
    `mod.attach`. Use the converted game model basis below. To place a
    part from a pack body mount, compose the body's fit and that mount
    when supplying its frame. Changing only the fit for a loaded file
    takes effect on the next draw.

    The options may also include `muzzle = { x, y, z }`, a point on the
    pack body in game model units in the converted basis, before the fit:
    the file's `(x, y, z)` metres become `1000 * (-x, z, y)`, so +X is
    the gun's left, +Y up and +Z forward. Each component is from -4000 to
    4000. The point goes through the body's fit and then the hand's gun
    matrix, as the body is drawn, and replaces the carrier's shot origin:
    shots and launched projectiles leave there (in VR it's noted as the
    hand's muzzle, `gevrStereoNoteMuzzle`). The carrier's Gun fit muzzle trim doesn't
    add to it, and Gun fit doesn't offer the barrel tip while it's in use,
    since that trim is the player's saved one for the carrier item. Each
    call sets the whole look, so a later call without `muzzle` clears it;
    give it again with a new fit. A legacy file refuses these options, and
    a thrown weapon refuses `muzzle`, since it leaves the hand by the
    throw. Replacing the gun body still hides the stock muzzle flash; the
    muzzle option moves the shot origin, not the flash. This format alone
    does not add action geometry, a grip, optics, or remote weapon looks.

    **Pack mesh byte layout.** Integers and IEEE 754 floats are little
    endian; there is no implicit padding. The same file can be a held
    model or an attachment, such as a prop or a custom guard helmet.

    | Order | Bytes | Contents |
    |---|---|---|
    | Magic | 8 | ASCII `GEVRWPN1` or `GEVRWPN2`, no terminator |
    | Picture count | 4 | Unsigned count, 1 to 32; the renderer tracks picture passes in a 32-bit mask |
    | Pictures | repeated | Unsigned 16-bit width and height, then `width * height * 4` bytes of row-major RGBA8 |
    | Triangle count | 4 | Unsigned, nonzero; exactly three consecutive corners follow for each triangle |
    | Corner | 28 each | Three float32 position values, two float32 UVs, RGBA8 colour, one zero-based picture index, three reserved bytes (write zero). All three corners of a triangle must use the same picture |
    | Wrist point (v2 only) | 36 | Nine float32 values: `along[3]`, `normal[3]`, `centre[3]` in the file's own frame. The first two are direction vectors; centre uses the same units as positions. Reserved for a later watch binding; it does not draw a watch |

    Positions are in metres in the file's X-right, Y-forward, Z-up frame.
    At load, `(x, y, z)` becomes `1000 * (-x, z, y)` in game model units,
    rounded into the signed 16-bit Fast3D vertex fields. UVs are normalized
    in picture space and wrap. Each picture dimension is 1 to 512: the
    renderer uses signed 16-bit 10.5 vertex UVs, with room for a repeated
    image across a seam, and 10.2 tile bounds. If a picture pixel or a
    corner using it has alpha below 255, all its triangles use the
    depth-tested blended pass; other triangles also write depth. If a
    picture's alpha stores data other than opacity, add
    `assets/models/<file>.opaque` beside that mesh (for example,
    `body.bin.opaque`). List its zero-based picture indexes separated by
    ASCII whitespace, each once, in fewer than 128 bytes. Those pictures
    keep their RGB and ignore texture alpha; corner alpha still controls
    blending. Unlisted pictures keep the original behavior, so one mesh
    can use opaque body pictures and blended glass. The same applies
    whether the file is held or attached. Invalid lists refuse the mesh.
    Files with invalid counts, non-finite values, missing bytes or extra
    bytes are refused and logged; an allocation failure is logged too.
    Pack geometry currently does not inherit world fog on guards.
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
  - Built (`port/src/gevr_modapi.c` over `src/game/gevr_modgame.c`; the
    hooks in `prop.c` and `propobj.c`): before the stage's first record is
    placed, `gevrModPickupScan()` notes the records a swap leaves alone.
    Each eligible record is asked as it's about to be placed: a weapon in
    `weaponAssignToHome()` after multiplayer's weapon set, before its
    models load; armour once its amount is read; a box or a crate before
    `domakedefaultobj()` and the crate's rounds' models. Asked while the
    mission loads (the loading time limit, outside the frame budget).
  - `item_pickup(kind, id, rounds, pickup)` in `propPickupByPlayer()` once the
    game has taken the pickup, before a freed one is gone, for this
    machine's human player: `"weapon", item`; `"ammo", type, rounds` (a
    crate sends one for each type it held, its 9mm_2 as 9mm, solo's
    multiplier applied); `"armour", amount`; `"key"`; `"object"` for
    anything else (a mission document, say). It reports; it can't refuse.
    `pickup` is the pickup's id: the setup record's index for the stage's
    own (`pickup_spawning`'s `index`, below `0x10000`), or for one the
    game made while playing (a dropped gun, a thrown weapon, a box), an
    id from `0x10000` up given as it's first named, never given again in
    the mission. The game recycles its weapon and box slots, so the id is
    the mod API's, not the slot's. Each headset numbers its own. A gun
    the player already has still reports: its rounds go to the reserve
    first.
  - `pickup_removed(pickup)` when a pickup the game made while playing,
    one a mod has had an id for, is freed without this headset's player
    taking it: the game freed the oldest dropped gun for room (its 30
    weapon slots full), another player took it in multiplayer, a thrown
    weapon was used up. Told at the next frame in play, before `tick`,
    never inside the call that freed it (a `mod.pickups.drop` can free
    the oldest gun); at most 64 a frame (more are logged and dropped).
    The stage's own pickups keep their ids and aren't told.
  - `weapon_dropped(pickup, item, from)` when a weapon a chr held is
    dropped to the floor (`propobjSetDropped()`): a dead or disarmed
    guard's gun, a player's in multiplayer. `from` is the guard's id,
    `"player"`, or nil. Each headset drops its own copy of what happens
    there, so ids are this headset's (never reused in the mission). Not
    for a mod's own drop.
  - `mod.pickups.drop{ item = gun }`, `{ ammo = type }` or `{ armour =
    amount }`, with `from = "player"` or a guard's id (a corpse too): the
    game's own drop, a gun or thrown weapon as a player's inventory is
    dropped (`drop_inventory()`), an ammo box of that type as a shot crate
    drops one, or body armour (more than 0, up to 1, as the stage's own),
    falling from that chr. Returns the new pickup's id, or nil with no
    free slot or no such guard. The box carries the type's own rounds,
    and a gun the game's usual rounds with it. The game makes no armour
    while playing, so a dropped one is a record of the mod API's own (8
    at once, freed as they're taken), the stage's body armour model, taken
    by the game's own armour pickup (only when it's more than the player
    has) and worn down by shots as the stage's is. A model the stage
    never placed is loaded first, as the game loads a hat it gives.
    Without `shared`, placed on this headset only: in co-op, each headset
    that should have it drops it (from the host's `guard_killed`, say,
    sent over `mod.net`), and each player can take their own.
  - `shared = true` (the host, or solo; a player's is refused): one
    pickup for every headset. Its id comes from `0x40000000` up, the same
    on every headset. The host's drop goes to the others (`NET_MSG_MOD`'s
    `NET_MOD_DROP`), and each drops its own copy from the same chr (a
    guard by its id, or the host's player) with that id, once it's in
    play and only in the mission it was sent in. **The host decides who
    takes it.** A player's pickup of it (walking over it, a hand grab) is
    held back, before the game changes anything, and claimed from the
    host (`NET_MOD_CLAIM`, again each half second while it's held back).
    The host awards the first claim it has while its own copy's still
    there, its own player's pickup the same way, and tells everyone
    (`NET_MOD_GRANT`): the winner's game takes it through its own pickup,
    so exactly one inventory changes and one `item_pickup` is told; the
    other copies are freed, told there as `pickup_removed`. A late or
    repeated claim gets nothing, and the host answers a claim for one it
    no longer has with `NET_MOD_GONE`, so the claimer's copy goes. A
    player's pickup lands a round trip after it walks over it; the host's
    at once. A shared copy the game frees untaken (its slot recycled for
    room, say) is sent as `NET_MOD_GONE` (through the host), so every copy
    goes, and the slot's next pickup has a new id. A headset that can't
    make it (no such guard there, no free slot) logs it and goes without;
    a guard's id is 0 to 32767 for a shared drop. Stock pickups and
    drops that aren't shared are taken as before.
  - Calls: `mod.inventory.has(item)` (any item), `give(item)` and
    `take(item)` (guns and thrown weapons only, the swap list below).
    `give` returns true when the item is new and brings no rounds; remote
    mines bring the detonator, as picking them up does. `take` returns
    true when the item was there, takes its dual pair too, and a hand
    holding it goes empty (the gun hand to its fist, the other unarmed).
    `mod.ammo.get(type)` and `set(type, rounds)` read and set the spare
    rounds (not the loaded ones, `mod.weapon.loaded`), 0 to
    `mod.ammo.max(type)`. `mod.ammo` also names the types: `9mm`,
    `9mm_2` (the Klobb's), `rifle`, `shotgun`, `grenade`, `rockets`,
    `remotemine`, `proxmine`, `timedmine`, `knife`, `grenaderound`,
    `magnum`, `goldengun` (1 to 13).
  - Not built yet: `item_spawned`, a pickup a mod can refuse, dropped
    armour, and a dropped pickup's own round count.
- **Hands (VR).** Built (`port/src/gevr_modapi.c` over
  `src/game/gevr_modgame.c`; the hooks in `bondview2.c` and
  `gevr_bodyslots.c`), for this machine's human player in stereo play.
  `hand` is the game's, as in the weapons area: `"right"` is the gun hand
  and `"left"` the off hand, whichever controller holds each (left-handed
  mode swaps them).
  - `grip(hand, anchor, taken)` and `release(hand, anchor)`: the grip as
    the game reads it (`port/src/input.c`'s poll, the press the grip
    gestures take), told on the next game tick, once
    `gevrGripGestureTick()` has decided the press. `anchor` is the body
    slot the hand is in, or nil. `taken` is true when one of the game's
    grips used it: a slot drew or put away, the reload belt, a force
    pull's catch, a door or a switch. A mod can't take a grip from the game.
  - `hand_near(hand, anchor)`: the hand came into a body slot, with the
    slot's own buzz. Only with the Body slots option on, and only at a slot
    with something there for that hand.
  - Anchors: `hip_gun`, `hip_off`, `back_gun`, `back_off`, `chest` and
    `belt`, the body slots' places, each named for the hand whose side
    it's on.
  - Calls: `mod.hands.position(hand)` gives x, y, z and
    `mod.hands.anchor(name)` x, y, z and the anchor's reach, in cm, in the
    body slots' torso frame: x toward the gun hand's side, y up, z ahead,
    from the eye's place upright over the torso. They work with either
    handedness and with the slots off, so a mod can place its own pouches
    from them; nil without VR hands in play or for a hand that isn't
    tracked. `mod.hands.haptic(hand, amplitude, seconds)`: a pulse,
    amplitude 0 to 1, up to 0.5 s. The player's Haptics settings have a
    Mods row that scales its strength and caps its length (0 turns mods'
    pulses off); false without VR haptics.
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
  - Built, part 1: the events and reading a guard (`port/src/gevr_modapi.c`
    over `src/game/gevr_modgame.c`; the hooks in `chraction.c`, `chrai.c`,
    `chr.c` and `prop.c`). A guard's id is its `chrnum`: the setup's for
    its own guards, the game's own number for one it spawns.
  - `guard_spawning(g)` in `expand_09_characters()`, for the setup's
    guards as the stage loads, on every headset: after the game picks the
    body and head, before the model is made. `g` has `id`, `index` (the
    setup record's), `body`, `head` (nil for a body with its own) and
    `weapon` (its setup gun, nil for none). A handler returns nil to keep
    the guard, or a table that changes `body` (a body row of the
    cartridge's table, not a head's), `head` (a head row; none for a body
    with its own head, and a body that needs one and gets none has the
    game pick it as it would) or `weapon` (a gun, `ITEM_WPPK` to
    `ITEM_ROCKETLAUNCH` without the watch laser; it replaces the guard's
    setup guns in `weaponAssignToHome()`, before the enemy-rockets cheat,
    so only a guard with a setup gun can get one). Handlers run in turn as
    for `pickup_spawning`; an answer the game can't take stops the mod for
    the mission and the guard keeps its setup. A guard can't be left out
    yet.
  - `guard_alerted(id, how, player)`, once per guard: `"saw"` before
    `chrCheckTargetInSight()` sets the target, `"shot"` as `chrlvTickArgh()`'s
    reaction ends, `"heard"` in `chrlvAlertGuardToPlayerPosition()`,
    `"alarm"` when the guard's own list runs `AI_AlarmOn`, and `"other"` for
    anything else that turns it on a player (`chrlvSetTargetToPlayer()`,
    say a script's attack). A guard that saw or heard a player before
    (the game's own marks, as a co-op host change carries them) isn't told
    again. `player` is the player slot the guard was acting on (in co-op
    the nearest, or for ten seconds whoever shot it).
  - `guard_killed(id, how, item, player)` as the death begins: `"shot"` in
    `triggered_on_shot_hit()` with the gun's item, `"explosion"` in
    `chrlvExplosionDamage()`. `player` is the slot whose shot hit the guard
    last (`handles_shot_actors()` records every hit, nil for a guard's
    shot; the host applies a client's hit with the shooter current), nil
    for an explosion (its owner isn't known there). Not yet: deaths by
    other ways (horde's nuke, the cheats).
  - `corpse_retiring(id, x, y, z)` in `chrlvTickDead()`, world units, before
    the body is marked for removal. A body the Bodies stay option keeps is
    told when the game retires its id (`gevrBodyRetireForAi()`, after the
    usual fade's time, when reinforcements may take its place), though it
    may lie there a while longer.
  - Alerts, deaths and corpses are told on the machine that runs the
    guards (solo, the co-op host), each once per guard; a slot's new guard
    starts again (`chrAllocate()`).
  - Calls: `mod.guards.list()` (the stage's guards' ids, living or dying),
    `mod.guards.get(id)`: `id`, `body`, `head`, `alive`, `alerted`,
    `weapon`, `weaponLeft`, `health` (what's left of `maxHealth`) and `x`,
    `y`, `z` (world units), or nil. On a co-op client it's the host's
    copy as last sent.
  - Built, part 2: the calls that change a guard. They work where the
    guards run (solo, the co-op host) in a stage; elsewhere they return
    false. The game's co-op sync carries what they do to the others, as
    for the host's own scripts (the spawn's announcement, the guns in the
    guard's state). An order the game can't take (a bad pad, body, head,
    list or gun) is the mod's error.
    - `mod.guards.spawn{pad, body, head, ai, weapon, weaponLeft}` spawns
      through `chrSpawnAtPad()`, as `AI_TRYSpawningChrAtPad` does, then
      arms it with `chrGiveWeapon()`. `pad` is a setup pad, or 10000 and
      up a bound pad, checked against the stage's. `body` and `head` as
      for `guard_spawning`; no head lets the game pick one. `ai` is
      `"standard"` (the default: idles, then clones or runs to attack),
      `"simple"` (stands or patrols until it notices), `"deaf"` (the same,
      by sight only), `"chase"`, or a level's own guard list's id; never a
      subroutine, the alarm raiser (it runs to a preset pad a spawned
      guard has none of) or a level script's list. The game tries the pad,
      then eight spots 60 units around it, each clear and out of the
      player's view (`chrAdjustPosForSpawn()`, as its own scripts usually
      ask); none, or no free slot: false.
    - `mod.guards.giveWeapon(id, item, hand)`: a gun into a living guard's
      hand (`"right"`, the default, or `"left"`); the gun it held goes
      (freed, as co-op's copies lose theirs; a dual pair's other gun is
      unlinked from it).
    - `mod.guards.patrol(id, path)`: `if_actor_able_set_on_path()` along
      the stage's path, as `AI_StartPatrol`; `mod.guards.paths()` lists
      the stage's path ids.
    - `mod.guards.goTo(id, pad[, speed])`: the guard walks (the default),
      runs or sprints to the pad by the game's own route (`chrGoToPad()`,
      as `AI_WalkToPad`, `AI_RunToPad` and `AI_SprintToPad`); true when it
      set off. The game holds it back while ten or more guards have
      recently seen the player, as it does for its own scripts.
      `mod.guards.face(id, pad)`: the guard stands and turns to the pad
      (`check_set_actor_standing_still()` with `TARGET_PAD`, as
      `AI_TRYFacingTarget`). Both where the guards run (solo, the co-op
      host), for a living guard and a pad the setup has; the game's co-op
      sync carries what they do.
    - `mod.guards.giveHat(id, hat)`: one of the cartridge's hats on a
      living guard that has none, as the game's own scripts give one
      (`AI_TRYGiveMeHat`, `hatCreateForChr()`): `fur`, `furbrown`,
      `furblack`, `sidecap`, `sidecapbrown`, `helmet`, `helmetgrey`,
      `moon`, `beret`, `beretblue`, `beretred`, `peaked`. True when it's
      on. A pack's own model in that hat's file (`assets/models`, the
      hats' `Phat*Z`) changes how it looks; anything else on a guard's
      body (a vest) isn't built.
    - `mod.pads.count()`: the stage's pads (ids from 0) and its bound pads
      (ids from 10000), as `mod.guards.spawn` and these take them.
      `mod.pads.get(id)`: `{ x, y, z, room, look = { x, y, z } }` in the
      game's world units (as `mod.guards.get`), `room` the pad's tile's
      (left out when it has none), `look` the way the pad faces; nil for
      no such pad. On every headset: the pads are the setup's.
    - `guard_spawned(id)` for a guard the game spawns in play
      (`AI_TRYSpawningChrAtPad`, `AI_TRYSpawningChrNextToChr`, a
      reinforcement's clone once armed and its id settled), where the
      guards run. A script's guard gets its gun from its own list
      afterwards. A mod's own spawn returns the id instead: telling it
      during the mod's call would run its handlers inside that call.
    - `guard_shot` is left to the damage area's `before_damage` and
      `damage_applied`.
- **Damage and revives.** Emit `before_damage` and `damage_applied` around
  native damage in `handles_shot_actors()` and `chrlvExplosionDamage()`
  for guards and `record_damage_kills()` for players. The before event
  allows the script to change or cancel the pending damage. The after
  event reports the result. `player_downed` and `player_revived` report
  the co-op transitions in `coopSetLocalDowned()` and
  `coopAnnounceDowned()`.
  - Built (`port/src/gevr_modapi.c` over `src/game/gevr_modgame.c`; the
    hooks in `chraction.c`, `bondview2.c` and `net_coop.c`). A guard's
    damage is told where the guards run (solo, the co-op host); a
    player's, on that player's own headset.
  - `before_damage(d)` and `damage_applied(d)`, each with a table:
    `target` (`"guard"` or `"player"`), `id` (a guard's chrnum, a
    player's slot), `amount` (the game's units: a guard's as its health,
    `mod.guards.get`'s; a player's, 1 a full bar, before the difficulty's
    handicap), `how` (`"shot"` for a guard; for a player `"explosion"`,
    `"gas"` or `"hit"`, anything else: a guard's shot, an autogun, another
    player), `part` (a guard's shot: `"head"`, `"body"`, `"limb"`, `"gun"`,
    `"hat"`), `item` (a shot's gun) and `by` (the player slot behind it,
    when the game knows: a player's shot, a player's own blast or another
    player's). `damage_applied` adds `health` (what's left, in the same
    units; a player's 0 to 1) and a player's `armour` (0 to 1).
  - `before_damage` is asked in turn, as `pickup_spawning` is: nil keeps
    the damage, false leaves none (no one after is asked), a number or a
    table's `amount` (0 to 1000) changes it. Where: a guard's shot in
    `handles_shot_actors()` once its damage is reckoned (the gun's, the
    part's multiplier, horde's) and before the guard takes it; with none,
    it still flinches, as it was still shot. A shot that does no damage (a
    hat, a gun) isn't asked. A blast in `chrlvExplosionDamage()` before it
    touches the guard (declined, it never hit; any blast that lands kills,
    as GoldenEye has it, so only cancelling changes the outcome). A
    player's in `record_damage_kills()` after co-op's own checks (a
    spectator, a teammate's copy, a player who's down) and before the
    game's own rules (invincibility, the watch, the last hit's flash):
    those can still take it to nothing, and then `damage_applied` isn't
    told.
  - `damage_applied` after the damage is taken: a guard's shot, a blast
    once the guard is dying, a player's when health or armour moved.
    An answer the game can't take is the mod's error, as for the other
    asked events.
  - `player_downed(slot)` and `player_revived(slot)` on every headset, for
    every player: this headset's own as it changes
    (`coopSetLocalDowned()`), the others' as they're announced
    (`coopAnnounceDowned()`). `player_died(slot)` as this headset's player
    dies (`bondviewKillCurrentPlayer()`, once per death; in co-op a player
    goes down instead).
  - Calls: `mod.player.health()` gives health and armour (0 to 1),
    `setHealth(x)` (more than 0, up to 1) and `setArmour(x)` (0 to 1) set
    them; false while the player is dead or down. `setSpeed(scale)` (0.25
    to 2; 1 the game's own) scales this machine's player's walking and
    strafing, where the game applies its own speed boost (`bondview2.c`),
    until the mission ends or the mod stops; a player's own steps in
    room-scale VR aren't scaled.
- **Sound and effects.** `mod.audio.play` uses `sndPlaySfx()` to play
  an existing ROM sound effect by ID. Positional playback uses
  `chrobjSndCreatePostEventDefault()`.
  `mod.effect.play` selects built-in effects such as smoke and bullet
  impacts through `explosionCreateSmoke()` and
  `explosionCreateBulletImpact()`.
  - Built (`port/src/gevr_modapi.c` over `src/game/gevr_modgame.c`), on
    this headset only: a mod that wants every player to hear or see one
    plays it on every headset, sending the choice over `mod.net`.
  - `mod.audio.count()`: the cartridge's sound effects bank's size; ids
    are 1 to it, as `sndPlaySfx()` takes them (the hack's own bank on a
    hack cartridge). `mod.audio.play(id, volume)` plays one as the game
    plays its own; `mod.audio.playAt(id, x, y, z, volume)` from a place in
    world units, as `chrobjSndCreatePostEvent()` places one (the
    distance's volume, 5000 to 6000 units as `...Default` has it, scaled
    by `volume`; the port's spatial place). `volume` is 0 to 1; without
    it the sound keeps its own. False outside a stage or when the game
    has no voice free.
  - `mod.effect.play(name, at)`: smoke from `explosionCreateSmoke()`'s
    own kinds, `"puff"`, `"smoke_small"`, `"smoke"`, `"smoke_light"`,
    `"smoke_dark"`, `"smoke_large"` and `"smoke_huge"`, where something is:
    `"player"` (this headset's), a guard's id, or `{ pad = n }`. Smoke needs
    a tile to stand on and its rooms, which a bare point doesn't give.
    False when there's no such place; the game drops it when its 20
    smokes are all in use.
  - Not built: bullet impacts. `explosionCreateBulletImpact()` marks a
    surface a shot hit, with its normal and room; a mod has no shot's hit
    to give it.
- **Network.** One message, `NET_MSG_MOD` (protocol 28), sent and
  checked in `net_core.c` (`netSendModMessage()`, `netReceiveMod()`);
  the calls and the held messages are in `gevr_modapi.c`.
  - `mod.net.send(channel, data [, to])`: `data` is a string of any
    bytes, at most 1024; `channel` is 1 to 32 printable characters with
    no spaces; `to` is a player's slot (0 to 7), or left out for everyone
    else. True when sent. False in solo, to this headset's own slot, or
    past the mod's rate: 30 messages and 16 KB a second. The host's to an
    empty seat is false too; a player's goes to the host, which drops it.
    A channel, message or slot past its limits is a script error.
  - `mod.net.on(channel, fn(data, from))`: each message of this mod's on
    that channel, to its handlers in the order they were added. `from`
    is the sender's slot, nil for a dedicated server (it has no slot). A
    mod hears only messages sent by the same mod.
  - Messages wait for the next frame in play and reach the handlers
    before its `tick`, so one that comes while the mission loads waits
    for it to start. A mission preparing drops the last one's. At most 64
    wait; more are dropped, with a log line.
  - Routing: a player's message goes to the host. The host relays one
    for everyone to the others, never back to its sender, and forwards
    one for a single player to that player alone. The sender's slot is
    the seat of the connection it came on: the host drops a message whose
    slot isn't its connection's, and a player takes messages only from
    the host's connection, and only those for everyone or for it. The
    host relays at most 64 a second of each player's. A malformed one (a
    name with spaces or no end, a length that doesn't match) is dropped
    whole.
  - `mod.net.role()`: `"solo"`, `"host"`, `"client"` or `"server"` (a
    dedicated server), from `netIsHost()` and `netIsDedicated()`.
    `mod.net.slot()`: this headset's player's slot, 0 in solo, nil on a
    dedicated server.
  - `peer_ready(slot)` on the host (or the dedicated server) as each
    other player first reaches the stage: the validated stage-ready
    transition in `netReceiveStageReady()`. It waits like a message, so a
    player who loads before the host's mission starts is told as it
    starts.
  - A mod stopped on one headset is stopped on all of them
    (`NET_MOD_OFF`; see Multiplayer).
- **The UI.** A mod's page is a sub-tab under the in-game screen's
  Mods/Fun tab, beside the section's own rows (now under Options): the
  hold-Menu screen, the solo panel and the online match screen's Settings
  tab alike (owner, 2026-11-23: the watch's VR page is gone since the
  launcher unification, so the in-game screen is the place). Built in
  `gevr_modapi.c`, drawn by `port/vr/gevr_modui_ui.h`.
  - `mod.ui.page(id, { title, text, rows, buttons })` makes or replaces a
    page. `title` (1 to 12 characters) names the tab; `text` (up to 160)
    sits above the rows. Up to 8 rows, each with an `id`, a `label` (up to
    24 characters), an optional `tip` (up to 160) and `blocked` (false, or
    the reason it can't be used now, up to 32: greyed with it):
    - `{ kind = "toggle", value = true }`;
    - `{ kind = "choice", choices = { ... }, value = n }`: 2 to 12 names
      of up to 24 characters, `value` the chosen one's number from 1;
    - `{ kind = "slider", min = a, max = b, value = v }`: whole numbers,
      -100000 to 100000, at most 1000 steps.
    Up to 4 `buttons`, each an `id`, a `label` (up to 14) and `blocked`.
    The rows are drawn as the settings' rows are, so they look and fit
    the same (one column, or two when they fit); no Reset.
  - At most 4 pages, every mod's together. `mod.ui.page` returns true
    when the page is made or replaced, and false when all four are taken
    by others (a mod can't know how many the others use); a malformed
    page is still an error. A page stays until the mod removes it
    (`mod.ui.remove(id)`), across missions; a stopped mod's pages hide
    until it runs again.
  - `ui_change(page, row, value)` and `ui_click(page, button)` reach only
    the page's own mod, at its next frame (before `tick`), even while the
    solo panel holds the mission paused. A change is kept on the page at
    once; a value is a toggle's boolean, a choice's number from 1 or a
    slider's number, and a slider dragged sends its last value once. A
    blocked row or button sends nothing. A mission's loading drops the
    clicks not yet sent.
  - `mod.ui.open(id)` opens the in-game screen on the page, as the hold
    opens it: a solo mission pauses under it, and online the match screen
    comes up on Settings. False where there's no screen (the dedicated
    server). Asked during a solo mission's intro (from `mission_start`,
    say), the screen comes up when the intro ends, paused before play:
    the pause is a Start press, which the intro would take as a skip
    (`gevrIntroPlaying()`). `mod.ui.close()` closes the screen, as Resume
    does.
  - The limits keep the fullest page (four pages, eight rows of the
    longest labels, choices and reasons, four buttons) on both screens
    without scrolling at the Quest's font size
    (`port/tests/ingame_fit_native.cpp`).
- **Assets, part one: the pack's files** (owner, 2026-11-23: these first,
  attachments after). A GoldenEye model file has no header of its own: its
  switch and texture counts and its skeleton are the game's, for the file
  it is (SubDrag's editors import into an existing object's slot). So a
  mod's model takes a cartridge model's place, and a mod's textures go over
  the player's HD pack. Both are the run's mod campaign's, the same build on
  every machine of a party (the content check), and need no call.
  `port/src/gevr_modassets.c`.
  - `assets/models/<Name>`: a model file (a gun, prop or character:
    `GpistolZ`, `PcartonZ`, `CdjbondZ`) in place of the cartridge's,
    wherever the game loads it (`ob.c` `load_resource`), on the dedicated
    server too. As the cartridge stores it (Rare's 1172 compression) or
    decompressed, at most 4 MB. It keeps the cartridge's header, so it must
    fit it: one that won't decompress into the room the game makes for the
    cartridge's file, or that `gevr_model.c` can't rebuild with the
    header's counts, leaves the cartridge's, with a log line.
  - GoldenEye X's guns, when they're on, are drawn instead of a mod's
    (theirs are whole models of their own). The patches made for the
    cartridge's meshes leave a mod's model alone: the VR hands' closed
    shells, Oddjob's hat, Bean's meshes and the magazines taken out by
    hand (all measured on the cartridge's). So with Bean on, a model the
    mod replaces keeps the mod's look, and a gun it replaces has no
    cartridge magazine to take out.
  - `assets/textures/`: PNGs named `IDENT#CRC#F#S[#PAL]_all.png` as an HD
    pack's are, in any folders, read as a layer of their own
    (`gevr_texpack.cpp`): asked first, over the player's pack and Bean's
    fill, and kept when the player switches HD textures off. Bean's own
    pictures are bound before any pack is asked, so they win where Bean
    has one. No HD pack needs to be chosen. A mod that can't start shows
    none. They match textures by content, so a mod over Goldfinger 64 or
    Tomorrow Never Dies names that cartridge's own.
- **Assets, part two: a gun's model per hand and attachments** (Tactical's
  review, #291). The gun's model per hand is `mod.weapon.model`, in the
  weapons area above. Attachments: `src/game/gevr_modattach.c`.
  - `mod.attach.add{ model, like, on, hand, at, turn, scale }` draws the
    pack's `assets/models/<model>` on the local player's gun, a hand or the
    body. `like` names the cartridge model file it's built over (a G gun
    or P prop file, its header: `Gak47Z`, `PcartonZ`), as `assets/models`'
    replacements keep the cartridge's header. Loaded once a mission
    through `load_resource`, into a buffer of its own as
    `gevr_heldgun.c`'s guns are; a file that won't load over that header is
    refused (nil), never drawn as the cartridge's. At most 64 model files
    a mission, and 128 attached at once, every mod's together; past those,
    nil. The limits come from a measured loadout (#291: two rifles of 14
    parts each, one in hand and one on the back, five magazines, a sheath
    and a scabbard; 35 attached and about 20 files for one player), with
    room for a second player's rig and guards' gear. A frame without room
    left in the game's per-frame buffers skips an attachment rather than
    overrun them (logged once a mission). Loading reads the file, so
    attach at `mission_start` or `weapon_drawn`, not every tick.
  - `on`: `"gun"` with `hand` (`"right"`, `"left"`): on the gun that hand
    holds, on its model's root as drawn, so it moves with the gun's
    recoil, reload and switch, and a mirrored left gun's part mirrors with
    it; flat too. `node` (0 to 255, the gun model's matrix; 0 the root)
    puts it on a part the game animates instead (a slide, a bolt), and it
    isn't drawn on a gun model without that matrix. `"hand"` with `hand`: on that controller's grip (VR). A
    body anchor (`hip_gun`, `hip_off`, `back_gun`, `back_off`, `chest`,
    `belt`, as `mod.hands.anchor`): on the slot's centre on the torso, kept
    while a mod runs even with body slots off (VR). `"guard"` with
    `guard` (a guard's id) and `part`, one of the body parts the game's
    hits name (`bondaicommands.h` `HITTARGET`): `chest`, `head`,
    `pelvis`, `left_shoulder`, `right_shoulder`, `left_arm`,
    `right_arm`, `left_hand`, `right_hand`, `left_thigh`,
    `right_thigh`, `left_leg`, `right_leg`, `left_foot`, `right_foot`:
    on that part's matrix as the guard is drawn (the node of the part's
    bounding box, whose `ModelNumber` is the part), so a vest moves with
    the chest and a pouch with the thigh. Drawn as the objects stuck to a
    guard are, fading with the body, in every view the guard is drawn in,
    flat or VR; kept on the body, alive or dead (after its id retires
    too), until the body is removed, when it's gone and its id is no
    mod's. Nil for no such guard, or a body without that part. The frame
    is the part's own, in the character model's units.
  - `at` (model units, ±4000), `turn` (degrees about the anchor's X, Y
    and Z, in that order) and `scale` (0.01 to 100) lay it in the anchor's
    frame. Every frame is a gun model's units at the player's gun size,
    with X the holder's left, Y up and Z ahead, as the game's guns are.
    Instead of those three fields, `add` and `place` accept `frame` with
    four rows of three numbers: the attachment's X, Y and Z axes in the
    anchor's coordinates, followed by its origin. The axes must be
    independent, with components between -100 and 100. Origin components
    are between -4000 and 4000 in model units. An attachment on a gun
    node follows that node as it animates, even with an authored frame.
  - For a self-describing pack mesh use `model = "part.bin"` with `like`
    omitted. A cartridge-format attachment still needs `like`. See
    **Pack mesh byte layout** under Weapons for the format and limits
    used by `mod.weapon.model`. Malformed pack meshes are refused.
  - The engine places each at its anchor every rendered frame, and a hand's
    follows its controller between game frames as the guns do; a script
    only attaches, hides and removes. (A position set from `tick` would
    trail the hand: scripts tick at 60 Hz, a headset draws at 72 to 120.)
  - `mod.attach.show(id, shown)` hides or shows one, kept attached;
    `mod.attach.place(id, { at, turn, scale })` moves it in its anchor's
    frame from the next frame (a slide locked back after the last shot);
    `mod.attach.remove(id)`. Each answers true only to the mod that
    attached it. None stay past a mission, nor past a mod stopping.
  - Built over a prop (P) file, a model's parts are laid out by its own
    nodes, as a prop's are. A gun (G) file's joints are posed only by the
    gun's animation, so a model built over one is drawn rigid at its root,
    as the body slots draw a gun: build parts over a prop file. Nothing on
    an attachment moves of itself.
  - The gun, the hands and the body: the local player's view only;
    another player sees none, and a guard's gun none. A guard's: on this
    headset, so in co-op each headset's mod attaches the same (a guard's
    id is the same on every headset).

### Pack optics

A pack can put its own lens and reticle on a gun attachment. For example,
a science-fiction campaign can switch between its own low-power glass and
a wide dot sight using the same attachment. The game draws the lens in
the same hand pass as the housing, including on a mirrored left-hand gun.
The ordinary `mod.weapon.scope` API still controls the game's own scope.

- `mod.attach.optic(id, { profile = "glass.opt", folded = "glass-folded.bin" })`
  binds a profile in `assets/models/` to an owned pack mesh on a gun.
  `folded` is optional and replaces the housing in modes marked folded.
  The first mode is selected on bind. It returns true on success. Bad or
  missing assets return false and keep any existing binding. Pass false
  instead of a table to unbind. Hiding the attachment hides the lens.
  Removing the attachment or changing its carrier retires the binding.
- `mod.attach.opticMode(id[, mode[, zoom]])` returns the selected mode
  (one-based), zoom, mode count, kind (`collimator`, `scope`, `night` or
  `thermal`) and whether the mode is folded. It returns nil if there is
  no owned binding. Pass a mode to select it and optionally a zoom. Zoom
  is clamped to the mode's range and defaults to its minimum.
- In VR, looking through a configurable lens enables a short right-stick
  click to change mode. Hold the stick button and move it up or down to
  change zoom. A lens showing a magnified scene uses the shared scope view
  capture. If two stock scopes are already active, their views take priority.
  Thermal is a first pass that colours the scene by brightness. A follow-up
  will add character and environment heat. Night view still needs headset
  validation.

#### Optic profile byte layout

All integers and floats are 32-bit little-endian. A file starts with the
eight bytes `GEVROPT4` followed by an unsigned mode count (1?12). Each mode
contains the following fields in order.

1. A flags word with the kind in bits 0?7 (1 through 4 in the order
   above), folded in bit 8, palette in bits 9?10, and first focal plane
   in bit 11. Keep the remaining bits clear.
2. An outline point count (3?64), then reticle width and height (1?1024).
3. The lens centre and right, up and forward vectors (three floats each),
   two positive half sizes, two texture coordinates for the reticle
   centre, the collimator's texture units per radian, and minimum and
   maximum zoom (both at least 1).
4. The outline's pairs of floats, given in order around the centre as
   fractions of the half sizes, followed by the reticle as RGBA8 pixels.

The lens coordinates use the pack mesh's authored frame (metres, X
right, Y forward, Z up). The outline must describe a fan from the
centre. A valid file ends immediately after the last reticle.

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
- **No setup record points at it.** Objectives and level scripts find
  their objects through tags (`PROPDEF_TAG`, `objFindByTagId()`), so a
  tagged pickup can be a mission item even when it's a gun. A rename
  (`PROPDEF_RENAME`) gives a mission item its own name, a link
  (`PROPDEF_LINK`) pairs two guns as a dual pickup, and a safe's record
  (`PROPDEF_SAFE_ITEM`) names what's inside it. A pickup any of these
  point at keeps its setup, and so does one inside another object
  (`PROPFLAG_INSIDEANOTHEROBJ`).

A swap keeps the pickup's kind, since each kind is a different setup
record:

- a weapon becomes another eligible weapon (it takes that gun's floor
  model, `getPropForHeldItem()`), or nothing;
- an ammo box changes its type (1 to 13, `mod.ammo`), or becomes
  nothing; the rounds come with the type, as for any box
  (`get_ammo_in_magazine()`), and the box takes the type's look (its
  magazine or shells, or the round or thrown weapon itself);
- an ammo crate changes how many rounds of each type it holds (0 to the
  type's most), or becomes nothing;
- armour changes its amount (more than 0, up to 1, a full vest), or
  becomes nothing.

A handler gets a table: `kind` (`"weapon"`, `"ammo"`, `"crate"` or
`"armour"`), `index` (the record's, the same on every headset), and
`item`, `ammo`, `crate` (type = rounds) or `amount`. It returns nil to
keep the pickup, false for none, or a table of the same kind; a field
it leaves out keeps the pickup's own, so a handler can change the table
it was given and return it. Handlers run in turn, each given what the
last decided; after a false none are asked. An answer the game can't
take (another kind, an item off the list, a value past its limit) is a
script error: the mod stops for the mission and the pickup keeps its
setup.

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
   player, assets, UI. Weapons, then pickups and items, then hands
   (`port/tests/test_modapi.py`; the grips' timing on the production
   poll in `port/tests/test_body_slot_interactions.py`), then guards'
   events and reads (`test_modapi.py`, which also runs
   `gevr_modgame.c`'s told-once bookkeeping), then the calls that change a
   guard, then damage and the player, then sound and smoke; after the
   network (5), the UI's pages on the in-game screen (the fit on both
   screens in `port/tests/test_ingame_fit.py`). Then assets: the pack's
   files first (`port/tests/test_modassets.py`, the routing in
   `port/tests/test_romcheck.py`), then a gun's model per hand and
   attachments (`port/tests/test_modapi.py`).
5. **Network:** `NET_MSG_MOD`, co-op with mods, the dedicated server.
   `mod.net`, `peer_ready` and a stop told to every headset
   (`port/tests/test_modapi.py`; the routing and checks on the production
   `net_core.c` in `port/tests/test_multiplayer.py`). Co-op's content
   check came in 2c.
6. **Sample mod and API reference.**
7. **Platform check:** Quest, PC, Apple, server, with the sample mod.
   Quest 2 at 90 Hz and Quest 3 at 120 Hz on the heaviest levels settle the
   script budget; the Quest 2 must hold 90 fps.
8. **Mod browser** (after v1): the community index and the Mods page's
   "Community mods" list.
