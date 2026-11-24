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
  - When `netLocalSlotCount() > 1`, `START` / `Enter` on any local player opens GoldenEye's per-viewport menu inside that player's viewport (`Resume`, `Ready`, `Character`, `Team`, `Loadout 1–4`, `Next Map`/`Weapons`, `Drop Out` for guests; plus host quick actions and `Full Menu / Settings...` for `P1`).
  - `F10` / `Back` (or `Full Menu / Settings...`) opens the full-screen ImGui overlay (Scoreboard, Kick, Rules, Friends, Settings, Leave Match).
  - When `netLocalSlotCount() == 1`, `START` keeps opening the full ImGui overlay directly as today.
- **Voice (`net_voice.c`):**
  - Single shared couch mic transmits under `s_local_slots[0]`; host never relays a peer's voice back to its own `ENetPeer*`.
  - When `netLocalSlotCount() > 1`, skip single-head binaural HRTF panning (play centered) and compute proximity gain from the **nearest living local player** to each remote speaker.

### Phase 5 — Dedicated server & automated tests
Files: `port/server/server_host.c`, `port/tests/net_core_native.c`, `port/tests/multiplayer_native.c`, PC `--input-script` integration tests.

- Verify `gevr-server` with multi-slot peers + bot eviction; add `--local-players N` to `--connect` for automated multi-seat testing.
- Native unit tests: multi-slot `HELLO`/`WELCOME`, all-or-nothing capacity rejection, peer broadcast deduplication, `netPeerOwnsSlot` anti-spoofing, lobby & mid-match `NET_MSG_LOCAL_GUEST` add/remove, single-guest vs. peer kick, multi-slot host migration & rejoin, `NET_FUN_NO_TELEPORT` config validation & gating.
- Scripted PC integration tests (`--input-script` + `pad2..pad4`): 2P local split-screen + remote peer/server across Deathmatch, Co-op, and Horde, covering lobby join, mid-match drop-in (`1P -> 2P -> 3P`) and drop-out (`3P -> 1P`), per-viewport pause menu, HUD/radar placement, shooting/damage, and revives.
