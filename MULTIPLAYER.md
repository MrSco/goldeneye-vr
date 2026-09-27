# GoldenEye VR - Online Multiplayer

This document describes the design, architecture, networking protocol, and testing instructions for online multiplayer in **GoldenEye VR** on native Meta Quest.

---

## 1. Overview & Architecture

The multiplayer subsystem ports the networking model from the Perfect Dark PC port (`pd`), adapted for GoldenEye 007 and Meta Quest OpenXR 6DoF virtual reality:

- **Transport:** Vendored **ENet** (reliable and unreliable UDP transport).
- **Topology:** Host-authoritative client/server model over LAN / direct IP. Up to 4 players per match.
- **VR Rendering:** Decoupled local stereo rendering. GoldenEye's split-screen logic is bypassed in online VR so each headset only renders its own 6DoF stereo view, while other players exist as 3rd-person character entities in the level.
- **Port:** UDP port `27015` (default). LAN discovery on UDP port `27016`.

---

## 2. Networking Protocol

All packets begin with a 32-bit magic (`0x47455652` / "GEVR") and a 16-bit protocol version (`GEVR_NET_VERSION`).

### Packet Types:
- `NET_MSG_DISCOVERY_PING` / `NET_MSG_DISCOVERY_PONG`: LAN discovery beacons for server browser.
- `NET_MSG_CONNECT_REQ`: Client requests connection with player name.
- `NET_MSG_CONNECT_ACK`: Host assigns player slot and sends match configuration.
- `NET_MSG_DISCONNECT`: Graceful disconnect notification.
- `NET_MSG_LOBBY_STATE`: Periodic synchronization of connected slots, readiness, chosen character IDs, and stage ID.
- `NET_MSG_LOBBY_STAGE`: Host informs lobby of stage selection change.
- `NET_MSG_LOBBY_READY`: Toggle player ready state.
- `NET_MSG_LOBBY_START`: Host initiates transition to gameplay (supplying stage ID, scenario, and sync seed).
- `NET_MSG_PLAYER_MOVE`: High-frequency player tick synchronization (positions, forward/side movespeeds, yaw/pitch angles, duck state, trigger input, weapon ID, and 6DoF hand pose).
- `NET_MSG_HIT_REPORT`: Client or Host hit detection report (target slot, weapon ID, hit location, impact vector, raw damage).
- `NET_MSG_DAMAGE_EVENT`: Host-authoritative broadcast executing damage across all connected machines.
- `NET_MSG_PING` / `NET_MSG_PONG`: Round-trip latency heartbeat.

---

## 3. Key VR Multiplayer Adaptations

### A. Independent Stereo View & Local Slot Isolation
- GoldenEye traditionally alternates `g_CurrentPlayer` across ticks and frames for split-screen. In VR online mode:
  - `gevrStereoFrame()` explicitly binds to `g_playerPointers[netGetLocalSlot()]`. Remote players dying, pausing, or viewing their watch menus will never drop the local headset out of stereo.
  - `lvlRender()` forces `set_cur_player(netGetLocalSlot())` and only executes viewport submission for the local slot.
  - Snap and smooth turning only apply to the local slot (`s_gevrBaseYaw`), preventing remote head turns from rotating other players.

### B. Character Movement & Room Transitions
- Remote player positions are interpolated using `netplayermove` state vectors.
- GoldenEye uses room-based portal occlusion (`stan`). After updating remote positions in `netPlayerSyncBeforeTick()`, `bondviewUpdatePlayerRoom(pl)` recalculates room membership so players do not disappear or clip when traversing doorways and corridors.

### C. Weapons & Held Model Synchronization
- Local players transmit their held item via `getCurrentPlayerWeaponId(GUNRIGHT)`.
- Receiving clients dynamically update the remote character's held weapon model via `chrSetWeaponFlag4()` and `chrGiveWeapon()`, attaching proper 3D models to remote player character hands.

### D. Authoritative Damage, Scores & Respawns
- Hit registration is detected locally for responsive gunplay and sent via `NET_MSG_HIT_REPORT`.
- The Host resolves damage via `netProcessHitReport()`, applies it locally, and broadcasts `NET_MSG_DAMAGE_EVENT` to all peers.
- All peers execute `record_damage_kills()` on the target slot. This updates engine health, awards kill scores, triggers death animations, and plays combat audio accurately on all headsets.
- Explosions from grenades, mines, and rockets report hits via `netSendHitReport()` attributed to the explosion creator.
- When a dead player respawns at a random spawn pad, the sudden positional delta clears `bonddead` and revives the remote character cleanly.

### E. Non-Sequential Slot Mapping
- `inputControllerConnected()` and `inputControllerMask()` support arbitrary slot assignments (`(idx == netGetLocalSlot() || netIsRemotePlayerActive(idx))`) so non-contiguous client slots are properly recognized by the engine without phantom controllers.

---

## 4. How to Test on LAN

### Prerequisites:
- Two Meta Quest headsets running the debug build on the same Wi-Fi / local network.
- UDP traffic permitted on ports `27015` and `27016`.

### Host:
1. Boot GoldenEye VR.
2. In the VR Launcher, navigate to the **Multiplayer** menu.
3. Select **Host Match**.
4. Choose a stage (e.g. Temple, Complex, Facility) and configure settings.
5. Wait for peer(s) to appear in the lobby slots.
6. Press **Launch Match** when all players are ready.

### Client:
1. Boot GoldenEye VR.
2. In the VR Launcher, navigate to the **Multiplayer** menu.
3. Select **LAN Discovery** to scan for the host, or enter the host's LAN IP address directly.
4. Select **Connect**.
5. Once in the lobby, toggle **Ready** and choose your character.
6. The host will launch the match.
