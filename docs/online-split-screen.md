# Online split screen (local guests in online matches)

Plan for hybrid local split-screen + online multiplayer on flat builds (PC, Android phone/tablet, iOS, Apple TV), bundled with the optional **No teleport** host rule under **protocol 28**.

## Scope & goals

- **1–4 local players on one flat machine** can host or join online matches alongside remote VR headsets, flat players, other split-screen machines, and dedicated servers.
- **All online modes supported:**
  - **Deathmatch** (all scenarios, including bots & teams): up to **8** total players.
  - **Co-op campaign:** up to **4** total players.
  - **Horde:** stays at up to **4** total players (built on Co-op's 4-player PvE layer and guard-slot/frame-time budgets), with full local split-screen support (e.g., 2 local + 2 online, or 4 local).
- **Mid-match drop-in / drop-out:** A local player (`P2–P4`) can join or leave an active online match without reloading the stage.
- **Guest profiles:** Auto-named `<PrimaryName> (2)..(4)` with optional editable names, character, and loadout remembered per guest seat in `goldeneye-vr.ini`.
- **Two-tier in-game pause:** `START` / `Enter` opens GoldenEye's translucent per-viewport watch menu (`src/game/mpmenu.c`) inside only that player's viewport (never blinding couchmates); `F10` / `Back` (or P1's "Full Menu / Settings...") opens the full-screen ImGui match & settings overlay (`port/vr/vr_pause_menu.cpp`).
- **Host rule — No teleport (protocol 28):** Bundled in the same protocol bump so tournaments can forbid VR teleporting without splitting the player base again. Allowed by default.

## Core architectural model

Today the engine conflates three identities into `netGetLocalSlot()` and `getPlayerCount()`. We split them cleanly:

1. **Primary slot (`netGetLocalSlot()`):** `s_local_slots[0]`, the machine's account/host/mic/launcher anchor (`-1` on dedicated servers).
2. **Local human slots (`s_local_slots[0..s_local_count-1]`):** `1..4` match slots (`0..7`) controlled by humans at this screen (`1` in stereo VR, `0` on dedicated servers).
   - `netLocalSlotCount()` -> `0..4`
   - `netSlotIsLocalHuman(int slot)` -> `bool`
   - `netLocalSeatOfSlot(int slot)` -> `0..3` (or `-1`)
   - `netSlotOfLocalSeat(int seat)` -> `0..7` (or `-1`)
   - `netSlotOwned(int slot)` -> `netSlotIsLocalHuman(slot) || (netIsHost() && netSlotIsBot(slot))`
3. **Local viewport count & index:** Decouples screen layout from total match slots (`getPlayerCount()`, `2..8`) so arbitrary online slot assignments (e.g. a 2-player couch in slots `2` and `5`) render as clean 2-player split-screen:
   - `gevrLocalViewCount()` -> `netIsActive() ? netLocalSlotCount() : getPlayerCount()`
   - `gevrLocalViewIndex(int slot)` -> `netIsActive() ? netLocalSeatOfSlot(slot) : slot`

## Phased implementation plan

### Phase 1 — Protocol 28, multi-slot peers & No Teleport rule
Files: `port/src/net/net_protocol.h`, `net_rules.h`, `net_core.h`, `net_core.c`, `net_game.h`, `port/src/gevr_settings.c`, `port/src/input.c`, `src/game/gevr_teleport.c`, `src/game/mpmenu.c`.

- **Protocol bump:** `GEVR_NET_VERSION` `27` -> `28`.
- **No teleport rule (`NET_FUN_NO_TELEPORT = 128`):**
  - Uses the last free bit in `NetMatchConfig.fun_flags` (`NET_FUN_MASK = 247`, `NET_COOP_FUN_MASK = 255`); off (teleport allowed) by default.
  - Exposed on Host Rules in launcher and pause menu; when active online, `src/game/gevr_teleport.c` disables teleport and `port/src/input.c` falls back to normal stick walking so VR players aren't stuck.
- **Multi-slot peer mapping on the host (`net_core.c`):**
  - Multiple slots in `s_client_peers[GEVR_MAX_PLAYERS]` may point to the same `ENetPeer*`.
  - Store the owned slot bitmask in `peer->data` (`(void *)(uintptr_t)slot_mask`); replace `(intptr_t)peer->data - 1 == slot` checks with `netPeerOwnsSlot(peer, slot)`.
  - Deduplicate all peer-level sends (`netBroadcastBuf`, `netClockTick`, `netSendLobbyHandoffTo`, `netSendMatchStartTo`, `netSendWorldSnapshot`, `netCoopBroadcast`, voice relay) so each `ENetPeer*` receives one copy per message and one clock-sync stream shared across its slots.
- **Atomic multi-slot handshake (`NET_MSG_HELLO` / `NET_MSG_WELCOME`):**
  - `HELLO` sends `local_count` (`1..4`) and per-seat `{ name, requested_chr_id, previous_slot, input_class }`.
  - Host checks availability all-or-nothing (evicting up to `local_count` bots in Deathmatch if needed; refusing with `NET_DISCONNECT_NO_ROOM` if fewer than `local_count` slots are free).
  - `WELCOME` returns `assigned_count` + `assigned_slots[assigned_count]`.
- **Dynamic guest add/remove (`NET_MSG_LOCAL_GUEST`):**
  - Works in both the lobby/warmup and mid-match (drop-in / drop-out).
  - `ADD`: allocates a free slot (or replaces a Deathmatch bot), marks it `loaded = 1` immediately when the peer is already in-stage, and returns the assigned slot; on failure returns a brief refusal reason (`Match full`, `Team full`).
  - `REMOVE`: drops just that guest slot (cleans up scores, downed state, votes; refills a bot if `Fill` in warmup) without disconnecting the peer.
- **Machine ownership in `NetLobbySlot`:**
  - Add `uint8_t owner_slot` (the primary slot of the owning machine).
  - Peer disconnect drops all slots for that peer.
  - Host migration (`netHostLost`): drops all slots with `owner_slot == old_host_slot`, elects the lowest surviving primary human (`slot == owner_slot && !is_bot`), and rejoins multi-slot clients with all their `previous_slot[]` values intact.
  - Kick: kicking a guest drops only that guest; kicking a primary player disconnects the peer and all its guests.
- **Per-slot lobby mutators:** Generalize `netLobbySetReady`, `netLobbySetCharacter`, `netLobbySetTeam`, `netLobbySetLoadout`, and `netSetLocalVote` to accept an explicit `slot` (with primary-slot wrappers preserved). `NET_MSG_STAGE_READY` marks all slots owned by that peer as `loaded = 1`.

### Phase 2 — Dynamic split-screen rendering, HUD & radar
Files: `src/game/lv.c`, `src/game/player.c`, `src/game/bondview2.c`, `src/fr.c`, `src/game/viewport.c`, `port/include/hudaspect.h`, `src/game/radar.c`, `src/game/gunfire.c`.

- Online matches already pre-allocate `selected_num_players = s_max_players` (`2..8` Deathmatch, `4` Co-op/Horde) and the split-screen Z-buffer at stage load, so mid-match drop-in/drop-out resizes viewports on the next frame with zero reload.
- `lvlRender` (`lv.c`): draw a full viewport for every `netSlotIsLocalHuman(playernum)`; run headless `gevrViewPass` for occupied remote players and bots.
- `gevrDeathPlaysHere(i)` (`lv.c`): `!netIsActive() || netSlotIsLocalHuman(i)`.
- `shuffle_player_ids()` (`player.c`): place all local human slots first (seat `0` at index `0`) so once-per-frame world work always runs on the first drawn pass.
- Viewport geometry (`bondview2.c`, `fr.c` `viSetupScreensForNumPlayers`, `viewport.c` `zbufClearCurrentPlayer`): compute 1P / 2P / 3P / 4P screen rects and split dividers from `gevrLocalViewCount()` and `gevrLocalViewIndex(get_cur_playernum())`.
- `PORT_HUD_ASPECT` (`hudaspect.h`): gate on `gevrLocalViewCount() < 2`.
- `radar.c`: position radar per viewport using `gevrLocalViewCount()` / `gevrLocalViewIndex()` while continuing to plot all `getPlayerCount()` players (`0..7`).
- `gevrDrawNameTags` (`gunfire.c`): exclude `get_cur_playernum()` instead of `netGetLocalSlot()` so couchmates see each other's name tags.
- `net_player_sync.c`: deliver match notices/countdowns/fades to all local human viewports; make spectator target/input state per local seat.

### Phase 3 — Per-seat input, sync, Co-op, Horde & core audit
Files: `port/src/input.c`, `port/src/net/net_player_sync.c`, `port/src/net/net_coop.c`, `port/src/net/net_horde.c`, `src/game/gevr_horde.c`, `src/game/bondview2.c`, `src/game/gun.c`, `src/game/gunfire.c`, `src/game/propobj.c`.

- **Input routing (`input.c`):**
  - `inputReadController(slot, npad)` resolves `seat = netLocalSeatOfSlot(slot)` and reads `s_seatSlot[seat]` (KB+M for `inputOwnPlayer()`, or assigned gamepad `pads[devSlot]`).
  - Make `gevrCrouchToggle` per-slot (`[MAX_PLAYER_COUNT]`); map `inputOwnPlayer()` online to the KB+M seat's online slot; map gunfire/damage rumble per player via `netLocalSeatOfSlot`.
  - Add mid-match unseated-device drop-in listener (unseated pad presses `START` or `A + START` -> requests `NET_MSG_LOCAL_GUEST(ADD)` and binds that device to the newly assigned slot).
- **State sync (`net_player_sync.c`):**
  - `netPlayerSyncBeforeTick`: skip remote-copy overwrite for all `netSlotOwned(playernum)`.
  - `netPlayerSyncAfterTick`: send every `netSlotIsLocalHuman(playernum)` via `netSendOwnedMove(playernum, &move)`. On flat players (`!g_gevrStereo`), derive `UCMD_FIRE` / `UCMD_FIRE_LEFT` from `pl->hands[hand].weapon_firing_status`.
  - Track `netSyncLocalActivity` per slot.
- **Co-op & Horde (`net_coop.c`, `net_horde.c`, `gevr_horde.c`):**
  - Host guard damage applies locally for any host local human and sends `NET_MSG_COOP_DAMAGE` only for remote slots; clients accept `NET_MSG_COOP_DAMAGE` for any `netSlotIsLocalHuman(target)`.
  - Stamp `NET_MSG_COOP_HIT`, `NET_MSG_COOP_EVENT`, `NET_MSG_COOP_TANK`, and `NET_MSG_HORDE_STATE` client actions with `get_cur_playernum()`.
  - Downed/revive: per-slot `s_host_sees_me_down[]` / `s_down_sent_us[]`; `gevrCoopLocalDowned()` checks `get_cur_playernum()` so couchmates can revive each other and remote teammates independently.
  - `NET_MSG_COOP_GRANT` gives shared mission gadgets to all local human slots; `gevrCoopCinemaTick()` applies cutscene camera/fade to all local viewports; `mission_timer` ticks once per frame on `s_local_slots[0]`.
  - `gevr_horde.c`: index `s_purchase_hold`, grant watermarks, and point HUD status per slot (`[4]`) so each couch player can use the Mystery Box, Wall Buys, ammo/power drops, and see their own credits independently.
  - Drop-in spawn behavior: Co-op spawns beside `P1` immediately (`NET_MSG_COOP_JOIN`); Horde spawns beside `P1` during prep/intermission (or spectates until intermission during active waves, matching remote drop-in); Deathmatch spawns onto a start pad in warmup, when replacing a bot, or into an open slot in non-YOLT/non-team rounds (using the slot's pre-loaded body until next round).
- **Game-core `netGetLocalSlot()` sweep (`bondview2.c`, `gun.c`, `gunfire.c`, `propobj.c`):**
  - VR-only stereo hooks stay on primary slot; per-player gameplay/HUD/weapon/damage/pickup hooks switch to `netSlotIsLocalHuman(cur)`; once-per-frame machine hooks (`isSimOwner`, `mission_timer`) stay on `s_local_slots[0]`.

### Phase 4 — Launcher UI, two-tier pause, guest profiles & voice
Files: `port/vr/vr_launcher.cpp`, `port/vr/vr_pause_menu.cpp`, `port/vr/vr_settings.cpp`, `src/game/mpmenu.c`, `port/src/net/net_voice.c`.

- **Launcher (`vr_launcher.cpp`, flat builds only):**
  - "Local Players (Split Screen)" strip in the Online screen (usable before Host/Join and inside the Lobby): press `A`/`Start` on extra pads (or click Add) to join Seat `2..4`, `B` to leave; per-guest editable name, character, team, and loadout; persisted in `goldeneye-vr.ini`.
  - Join list checks `max_players - player_count >= local_count` and notes `"Needs N open slots"` when a lobby can't fit the couch. No lobby-service Worker changes required.
- **Two-tier in-game pause (`mpmenu.c`, `vr_pause_menu.cpp`):**
  - When `netLocalSlotCount() > 1`, `START` / `Enter` on any local player opens GoldenEye's per-viewport menu inside that player's viewport: in a deathmatch's warmup the Multiplayer Options folder (below), in a round the LOBBY page, the quick page between rounds (`Ready`, `Next Map`/`Weapons` while voting, the host's `Start Match` and `Return to Lobby`, `Multiplayer Options`, `Drop Out` for guests, `Full Menu / Settings...` for `P1`). The match's setup, the character, team, health and loadout are the folder's. Horde has no LOBBY page: each player's START is their own folder.
  - `F10` / `Back` (or `Full Menu / Settings...`) opens the full-screen ImGui overlay (Scoreboard, Kick, Rules, Friends, Settings, Leave Match).
  - When `netLocalSlotCount() == 1`, `START` keeps opening the full ImGui overlay directly as today.
- **Voice (`net_voice.c`):**
  - Single shared couch mic transmits under `s_local_slots[0]`; host never relays a peer's voice back to its own `ENetPeer*`.
  - When `netLocalSlotCount() > 1`, skip single-head binaural HRTF panning (play centered) and compute proximity gain from the **nearest living local player** to each remote speaker.

### Select Character (drop-in and the pause menu)
Files: `src/game/front.c`, `src/game/mpmenu.c`, `src/game/bondview2.c`, `src/boss.c`, `port/src/input.c`, `port/src/net/net_core.c`, `port/vr/gevr_pause_widgets.h`, `port/vr/vr_pause_menu.cpp`.

- **The game's own screen:** the N64 split-screen Select Character panel (front.c: "Select Character", the scrolling strip of portraits, the name under it) runs for one match slot inside that player's view, driven by their pad. front.c's per-player tick and per-panel draw are shared by the front end and the match; in a match the panel's 440 x 330 folder coordinates are mapped onto the view (a sheet of the folder's paper behind it) and anyone may pick any character, as the launcher allows. Online character ids are front.c's `mp_chr_setup` rows one for one (net_match.c), so GoldenEye X, Goldfinger and the other hack campaigns show their own names and portraits (gevr_romcampaign.c installMpChars).
- **Drop-in:** a pad that joins mid-match (`inputOnlineSeatsSync`) opens the panel in its new view (`gevrCharSelOpen(slot, 1)`). The body the match spawned stays where it joined, frozen as in any watch page, and its own machine (which applies that player's damage) spares it until the player chooses (`netSetSlotChoosing`). A, Z or START picks; B before picking keeps the saved character.
- **Co-op while choosing:** a joiner still at the panel is frozen and unhurt, so co-op's guards leave them be and they revive nobody (`net_coop.c` `coopPlayerTargetable`, `netCoopReviveTick`); otherwise a pad left at the panel would be an invulnerable decoy beside the player it joined and an automatic revive. Their moves carry it (`UCMD_CHOOSING`), so every machine knows (`netSlotChoosingSeen`).
- **Lobby and warmup:** A on the per-viewport LOBBY page's CHARACTER row opens the panel (the right stick still steps the name); B or a choice returns to the page. The solo online player opens it from the pause window's Player tab ("Select Character"); a choice returns to the window. The launcher's dropdown is unchanged.
- **Applying it:** the choice goes out through `netLobbySetCharacterSlot` (a guest's is also saved as its seat's `GuestNChr`). Every machine rebuilds that player's body from the lobby character once the choice has held half a second and the player is alive (`gevrNetBodyFollowsCharacter`, the third-person rebuild), so it shows at once rather than at the next load; with under half a megabyte of stage heap for a body not yet loaded, it waits for the next load.
- **VR:** the same screen, on the head-locked HUD panel (see *The game's screens on the headset* below). The pause window's Player tab offers "Select Character" there too.

### Select Stage (the map and the vote)
Files: `src/game/front.c`, `src/game/mpmenu.c`, `port/src/input.c`, `port/vr/gevr_pause_widgets.h`, `port/vr/vr_pause_menu.cpp`.

- **The game's own screen:** the N64 multiplayer stage select's look (front.c: a dark band behind each row of photos, the photos tinted grey, the highlighted one lit, the names on them) runs for one match slot inside that player's view, as Select Character does. A on the LOBBY page's MAP row (the host's own seat) picks the map with it; A on NEXT MAP (anyone, while the next round is voted) casts that seat's vote. The right stick still steps both rows. The solo online player opens it from the pause window's Match tab ("Map photos", "Vote with photos"); a choice returns to the window.
- **Every online stage has a tile** (net_match.c, more than the screen's eleven). The photo is the screen's own where its table has one (Dam, Runway, Frigate, Statue, Cradle, Aztec and Depot are in it but were cut from its menu), otherwise that level's own picture from the same set (the ROM keeps one for every mission level, `level/siloicon` and the rest, the same 68 x 44 size), and a "(Full)" stage shows its map's photo marked FULL. A stage whose picture the ROM lacks gets a card of the same size with its name. A Bean game shows XBLA's pictures of the same set (gevr_bean_images.c). A stage the party cannot play is dimmed and passed over, as the rows pass over it.
- **The vote grid** starts with the screen's Random photo as **No vote**. Each photo shows how many players vote for it, the player's own vote (or, in the host's grid, the current map) is framed in green, and the line under the grid names the highlighted stage and its votes. Nothing on the wire changes: the choice goes out as the rows' steps do (`CFG_STAGE` by list position, `netSetVoteSlot`).
- **Layout:** four photos across at the screen's size in a full view; a short split-screen view shrinks them to fit two rows and takes more columns where it is wide; the rest scroll, with arrows at the side. Names go on the photos only where they fit.
- **Pad:** the stick moves the highlight and repeats while held; the d-pad and the C buttons step once a press (the d-pad is also the stick in menus, input.c `inputFlatPadSticks`). A, Z or START chooses, B backs out unchanged. The grid stays until the button is let go, so the press does not reach the page or window behind it.
- **VR:** the same grid, on the head-locked HUD panel (below); the Match tab offers "Map photos" and "Vote with photos" there too.

### The game's screens on the headset (Select Character, Select Stage)
Files: `src/game/mpmenu.c`, `port/src/input.c`, `port/vr/vr_pause_menu.cpp`, `port/vr/vr_launcher.cpp`.

- **Where they draw:** in stereo the watch menu is captured onto the head-locked HUD quad (`lv.c`: `VR_HUD_CAPTURE_BEGIN_H` with `VR_HUD_FULL_SIZE_BEGIN`; a 44 degree panel 1.4 m out), so the screens go there instead of the two eye buffers. The capture is the eye buffer's size and the game's screen covers it at one scale (`gfx_pc.cpp` `gfx_adjust_viewport_or_scissor`, which the text, the portraits' viewport and the scissor all go through inside a head capture), so a 4:3 screen loses its sides on a squarer eye, and the HUD's draws come out a ninth larger (the vertex shader's `w = 0.9`). `gevrMenuPanelRect` gives the panel the part that shows, less a margin (`gevrMenuPanelFit`, `port/tests/test_menu_panel.py` for Quest 2/3, Index, Pico, Pimax and 16:9 eyes); on a flat screen it is the player's view, as before.
- **Shared helper:** a screen is a watch page whose `mpmenumode` is listed in `s_gevrMenuPanels`. `gevrMenuPanelIsOpen(slot)` (the pause window stays shut, `input.c` `gevrNativePauseOpen`; the headset's sticks read both ways) and `gevrMenuPanelCancel(slot)` (leaves it as B would, back to the LOBBY page unless it is Select Character's own close) cover every listed screen; a new screen lists its mode there, draws into `gevrMenuPanelRect`, and its open function has no stereo guard.
- **Controllers:** either thumbstick moves (the stick repeats while held, as on a pad), A or X picks (the trigger too, as Z), B or Y backs out to the pause window. The press that opened the screen is not a choice. Menu leaves the screen unchanged and closes the menu. Coming back, the pause window ignores an A or B still held from the screen (`gevrFeedPauseInput`), so the B that left Select Character does not also close the window.

### Handicap and Teams (protocol 29)
Files: `port/src/net/net_protocol.h`, `net_rules.h`, `net_core.c`, `net_game.h`, `src/game/front.c`, `src/game/mpmenu.c`, `src/bondconstants.h`, `port/src/input.c`, `port/vr/gevr_pause_widgets.h`, `port/vr/vr_pause_menu.cpp`, `port/vr/vr_launcher.cpp`, `port/vr/vr_settings.cpp`, `port/server/server_host.c`.

- **Protocol bump:** `GEVR_NET_VERSION` `28` -> `29`. `NetLobbySlot.handicap` (one byte a slot in LOBBY_STATE), `NetMatchConfig.handicaps` (the host's rule, a byte at the end of every match config), the round's latched `handicap[]` (one byte a slot in the round settings: START_MATCH, ROUND_RESET, LOBBY_STATE, MATCH_SNAPSHOT) and `NET_MSG_LOBBY_HANDICAP` (66, client -> host: a seat's own pick). An eight-player snapshot is 739 bytes.
- **Per-player health:** a slot's handicap is `0` (`NET_HANDICAP_MATCH`, the host's HEALTH) or one more than a front.c `MP_handicap_table` index (1 Hero .. 11 Rookie), so a cleared slot plays at the match's. `NET_HANDICAP_HOST` (`0x80`) marks the host's pick for that player, which always counts. A player's own pick counts only while the host's **Handicaps** rule is on (off by default, so a stranger cannot make themselves tougher in a match that did not ask for it); turned off, own picks wait and come back when it is on again. The host sets anyone's, bots' too (the host's own seat, the launcher roster's player menu); a player sets their own under the rule and not over the host's. Stepping onto the match's own health makes it the match's again (it then follows the host's HEALTH).
- **When it applies:** like HEALTH, at the next load: `netLatchRoundSettings` resolves each slot into `s_round.handicap` and `netApplyMatchConfig` sets `player_handicap[p]` from it (lv.c reads it into the player's damage scale at the load). A pending change reloads the warmup (`netWarmupSettingsChanged`). A player who takes a seat after the load (a drop-in guest, a joiner in a seat a player with their own health left, a human in a bot's place) plays at theirs at once (`netSlotHandicapJoined`, `netSyncRoundHandicaps`): only the owner's machine applies its player's damage, so that copy is the one that counts. License to Kill keeps its one-shot damage.
- **PvE:** co-op and Horde ignore handicaps (Normal for everyone): the mission's difficulty and the waves set the damage, and the party has no one to balance against. The rows and buttons hide there.
- **Host migration:** the slots' values travel in LOBBY_STATE, so a new host has them; marks the old host made stay the host's.
- **The Handicap screen** (front.c `frontHandicapDraw`, mpmenu.c `gevrHandicap*`): GoldenEye's Select Handicap screen for one match slot in that player's view: "Select Handicap", the health under the cursor in the screen's own words ("Health -1" over "(Veteran)") between arrows where it may change, then every player with their health (MATCH for the match's, `*` for the host's), the cursor's row on a dark band. Left and right step; the host's own seat moves up and down the list and sets anyone's; A sends every change, B leaves them. A short view drops the title and the big text before the roster (the arrows go on the cursor's row). A on the LOBBY page's YOUR HEALTH row opens it; the solo online player uses the pause window's Player tab ("Handicap").
- **The Teams screen** (front.c `frontTeamSelectDraw`, mpmenu.c `gevrTeamSel*`): "Select Teams", Red Team and Blue Team in their colours side by side, each over its players (the one choosing first, then players, then bots) and its open places, then those on neither: who is on which side. Left picks Red, right Blue, down neither, up back to the current one; the pick blinks in its column, as the screen's changing team does. A sends it as the YOUR TEAM row does (`netLobbySetTeamSlot`), so net_core.c's team rules stand: a full side refuses (the footer says so), a change unreadies, bots give way, and the round plays its latched teams until the next load (a team changing then is starred). A on YOUR TEAM opens it; the solo player uses "Select Team" on the Player tab.
- **Names in quarter views** keep their last "(...)": guests are "Name (2)", "Name (3)", bots "Character (Bot)". A screen's names are cut alike (front.c `frontPanelFitNames`): every name the same room, its own part cut to the same length, so the guests read "AGENT 44.(2)", "AGENT 44.(3)" side by side.
- **A guest in a bot's place** (a drop-in when every place is taken) takes the bot from the side with the most room for humans, and its side only while that side has room for another human; else the other side with room, else neither (`net_core.c` `netBotToReplace`, `netJoinerSide`). Mid-round they play the bot's side and change at the next round. Before, four split-screen players on a 2v2 could put three on one side ("TOO MANY ON A TEAM").
- **Launcher:** none now: the match is set up in the game (the folder's Handicaps and Health rows; `MpHandicaps` in goldeneye-vr.ini keeps the last choice; the server lists it with `--list`).
- **Flat screens only:** VR keeps the rows (YOUR HEALTH, HANDICAPS, YOUR TEAM) and the pause window's dropdowns.

### The Multiplayer Options folder (in-game match setup)

- **Where the match is set up:** an online deathmatch's or Horde run's own GoldenEye Multiplayer Options folder (`MENU_SETUP`, front.c `frontMpOptionsDraw`), one per seat in its own view, a listed game screen like those above (so on the headset it is on the HUD panel). The host's rows (Players, Scenario, Level, Game Length, Weapons, Bots...; Horde's Level, Starting Wave, Friendly Fire, AI Ally) are dimmed at the other seats; each seat sets its own Characters, Health, Teams and Loadout. START is the START tab: the host's START MATCH (Horde: START WAVE), anyone else's READY (Horde: WAITING FOR HOST). See `docs/in-game-match-setup.md`.
- **Split screen:** a pad's START in the warmup opens that seat's folder (the LOBBY page has a MULTIPLAYER OPTIONS row in a round); a new seat joining in a warmup (Horde: the setup break) gets the folder on its Select Character. In Horde every seat's START is its own folder at any time (`input.c` `inputHordeStart`; the first seat's Full Menu is the pause window); this replaced the first player's pause window over every view. A quarter view uses the compact layout (label and value on one line, small font, short labels such as Char., Mode:, Guns:, Wave:).
- **Opens by itself** on arriving and after RETURN TO LOBBY (the round notice's lobby flag), and on NEXT MAP when the host asks for votes; in Horde in each run's setup break. Round notices say `START: ...` (`MENU: ...` on the headset).

### Phase 5 — Dedicated server & automated tests
Files: `port/server/server_host.c`, `port/tests/net_core_native.c`, `port/tests/multiplayer_native.c`, PC `--input-script` integration tests.

- Verify `gevr-server` with multi-slot peers + bot eviction; add `--local-players N` to `--connect` for automated multi-seat testing.
- Native unit tests: multi-slot `HELLO`/`WELCOME`, all-or-nothing capacity rejection, peer broadcast deduplication, `netPeerOwnsSlot` anti-spoofing, lobby & mid-match `NET_MSG_LOCAL_GUEST` add/remove, single-guest vs. peer kick, multi-slot host migration & rejoin, `NET_FUN_NO_TELEPORT` config validation & gating.
- Scripted PC integration tests (`--input-script` + `pad2..pad4`): 2P local split-screen + remote peer/server across Deathmatch, Co-op, and Horde, covering lobby join, mid-match drop-in (`1P -> 2P -> 3P`) and drop-out (`3P -> 1P`), per-viewport pause menu, HUD/radar placement, shooting/damage, and revives.
