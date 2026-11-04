# Friends, presence and invites

Status: design, 2026-11-04. Branch `claude/friends-invites`. Nothing built yet.

## Goal

Players see which friends are online and what they are doing, and invite them
into a launcher lobby. It works with no account of any kind. A Discord account
can be linked for more (Discord friends, Rich Presence, invites through
Discord), but nothing depends on it.

Decisions so far (user, 2026-11-04):

- Our own friends system on the lobby service is the base; Discord is an
  optional link on top, supported out of the box.
- An invite always pulls the friend into the inviter's **launcher lobby**. A
  running mission is never turned into a co-op session.
- Presence shows details (mission or map, mode, players) by default, with a
  setting to hide them.
- Meta's Platform SDK (Group Presence, invite panel, friends) is not used: it
  needs a store entitlement for every player and a reviewed Data Use Checkup,
  and would register the port with Meta. See "Not used" below.

## What exists to build on

- **Lobby service** (goldeneyevr.com repo, `services/lobbies`): a Cloudflare
  Worker at `lobbies.goldeneyevr.com` with one SQLite Durable Object
  (`LobbyRegistry`, instance `global-v1`). Lobbies have an 8-character code
  (`[A-Z2-9]{8}`), an owner token (stored hashed), visibility, a 45 s host
  heartbeat, WebRTC offer/answer signalling, TURN credentials, per-IP rate
  limits (`limits` table) and daily stats with alert emails.
- **Lobby client** (`android/.../LobbyClient.java`): a worker thread fed
  text commands (`lobbyCommand`) and drained of text events (`lobbyEvent`)
  by the native launcher.
- **Launcher** (`port/vr/vr_launcher.cpp`): ImGui pages; the Multiplayer page
  (`gevrMultiplayerPage`) hosts, lists and joins by code.
- **Back to the launcher** (`port/src/input.c`): the menu-hold prompt calls
  `gevrRestartToLauncher()`, which restarts the app into the launcher; an
  online session says goodbye first (`gevrLobbySessionStopped`).
- **Player name**: `vrEnsurePlayerName`, kept in the settings ini.

## Identity

- On first launch the game makes a **player ID** (16 random bytes, hex) and a
  **secret** (32 random bytes). Both go in the save folder, next to the
  settings. The Worker keeps the ID and a hash of the secret, as it does for
  lobby owner tokens.
- Each player gets a **friend code**, 6 characters from the lobby alphabet,
  shown as `GE-7K3QX2` style with the prefix. Different length from lobby
  codes, so the two are never confused.
- The display name is the existing player name; the friend code is shown
  beside it wherever a name could be confused or faked.
- **Losing the ID** (reinstall, new headset): the Profile page offers a
  restore code (ID + secret, base32) to write down, and Discord linking (phase
  3) restores it by signing in.
- Dedicated servers get no player ID and never appear as players.

## Friends

- Add by friend code. The other player sees a request and accepts or declines.
- Remove a friend, block a player (blocked players cannot send requests or
  invites and do not see your presence). Limits: 200 friends, 50 pending
  requests, 20 requests sent a day.
- Phase 3 adds Discord friends who have linked GoldenEye VR: they appear
  without a code (marked "Discord"), until either side unlinks.

## Presence

One request every 30 s while the app runs (launcher or game), sent by
`LobbyClient` on its worker thread:

`POST /v1/me/presence` with `{state, detail, lobby}` returns friends' presence,
pending friend requests and pending invites in one answer. One call carries
both directions, so polling costs nothing extra.

States: `launcher`, `solo` (campaign or solo match), `lobby` (in or hosting a
launcher lobby), `match` (online match), plus `offline` when no heartbeat for
90 s. `detail` (default on): mission or map, mode, difficulty, players/max,
e.g. "Facility, Agent" or "Deathmatch, Statue (Full), 3/8". The settings page
has **Show what I'm playing** (on) and **Appear offline** (off).

Rough cost: one player online for an hour is 120 requests. 300 player-hours
a day is 36,000 requests, under the existing 100,000 Durable Object alert. If
it grows, presence moves to its own Durable Object class, sharded by player ID.

## Invites

- **Send:** from the friends list, an online friend has **Invite**. The
  inviter must be in a lobby; from the launcher with none, Invite creates a
  private lobby first (current host settings) and invites into it. From inside
  a mission, the watch's friends page can invite only when the player is in an
  online match whose lobby is open.
- **Receive in the launcher:** a banner "Natty invites you to Facility co-op
  (2/4)" with **Join** and **Ignore**. Join uses the normal join-by-code path.
- **Receive in a mission:** a bottom HUD message (never on the wrist) for a
  few seconds, then the invite waits in the friends list. Accepting opens the
  existing back-to-the-launcher prompt with the invite named ("This mission
  will not be saved"). A yes writes the lobby code to a pending-join file, and
  the restarted launcher reads it, deletes it and joins.
- An invite carries the lobby code and lives 10 minutes or until the lobby
  closes. Private lobbies work: the code is the key, as with join by code.
- Limits: 10 invites a minute per player, one pending invite per friend.

## Worker API (all under `/v1/me`, `Authorization: Bearer <id>.<secret>`)

| Method | Path | Does |
|---|---|---|
| POST | `/v1/players` | register: returns ID, secret, friend code |
| POST | `/v1/me/presence` | heartbeat; returns friends, requests, invites |
| POST | `/v1/me/friends` | `{code}`: send a request |
| PUT | `/v1/me/friends/<id>` | `{accept: true\|false}` |
| DELETE | `/v1/me/friends/<id>` | remove |
| POST | `/v1/me/blocks` | `{id}`: block |
| POST | `/v1/me/invites` | `{to, lobby}`: invite (lobby must exist and be open) |
| DELETE | `/v1/me/invites/<id>` | ignore or cancel |
| PUT | `/v1/me/settings` | `{showDetail, appearOffline}` |
| POST | `/v1/me/restore` | prove a restore code; returns a new secret |

Tables (new, same Durable Object at first): `players(id, secret_hash,
friend_code, name, created, last_seen, state, detail, lobby, show_detail,
appear_offline, discord_id)`, `friends(a, b, status, created)` with `a < b`,
`blocks(owner, blocked)`, `invites(id, from_id, to_id, lobby, expires)`.

Rate limits reuse the `limits` table. Names are checked as lobby names are.

## Client

- `LobbyClient` grows `presence|...`, `friend-add|code`, `friend-answer|id|0/1`,
  `invite|id`, ... commands and `FRIENDS|...`, `INVITE|...` events, in the
  existing text protocol.
- Launcher: a **Friends** page (list with state and detail, Add by code, your
  own code, requests, blocked) and an invite banner on every page.
- Game: the presence state and detail come from the running stage and mode;
  the watch gets a Friends page (room screen, not the wrist) with Invite and
  Accept.

## Discord (phases 3 and 4)

Discord Social SDK, Android build, behind a launcher **Link Discord** button.

- **Link:** the SDK's OAuth flow in the headset browser. The Worker stores the
  Discord user ID against the player ID (verified from the token server-side),
  so signing in on another headset restores the same player.
- **Discord friends:** the SDK's relationship list, matched against linked
  players, joins our friends list.
- **Rich Presence:** "Facility co-op lobby, 2/4" in Discord. On mobile
  platforms Discord only publishes presence for linked accounts.
- **Invites through Discord:** to friends not in the game, with a join link
  `https://goldeneyevr.com/join/<code>`. The page opens the game into that
  lobby if it is installed (Android app link, needs `assetlinks.json` on
  goldeneyevr.com; Quest browser support to check), else shows the code and
  the download.
- Needs: a Discord application (client ID from the user), the join URL set in
  its General settings, and Android as a supported platform.
- Not used: Discord's provisional accounts. They would keep the friends list
  on Discord's side; ours stays the source of truth.

## Not used: Meta Platform SDK

Group Presence, the invite panel and the friends API exist, but every player
needs an entitlement (store, release channel or key), the friends API needs a
manually reviewed Data Use Checkup, and the app would be registered with Meta.
Sideloaded builds only get these for the developer's own organisation.

## Phases

1. **Friends and presence:** registration, friend codes, requests, block,
   presence and its settings, the launcher Friends page. Worker + client +
   launcher; tests in the Worker's `test/` and a launcher layout test.
2. **Invites:** send and receive in the launcher, the in-mission notice and
   the pending-join restart, the watch Friends page.
3. **Discord link:** SDK, sign-in, Discord friends, Rich Presence.
4. **Discord invites and join links:** the join page and app link.

Phases 1 and 2 ship without Discord. Protocol: none of this touches the game's
network protocol; the Worker API is versioned under `/v1`.

## Open questions

- Restore code UX: shown once at first launch, or only on the Profile page?
- Should a host's lobby be listed as "friends only" (visible to friends,
  hidden from the public list)? Cheap to add with presence.
- Quest browser and Android app links for `goldeneyevr.com/join/<code>`: to
  test on a headset before phase 4.
