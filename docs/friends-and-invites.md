# Friends, presence and invites

Status (2026-11-04): all four phases built, in pre-releases v0.5.15-test.5 to
test.9 (branch `prerelease/v0.5.15`). Link Discord works on the user's Quest
(test.8, after updating the Quest's Discord app). Worker:
`services/lobbies/src/social.ts` on goldeneyevr.com `main`, deployed.

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
- A friend in a **public or friends-only** lobby has a **Join** button; a
  private lobby needs an invite (its code never reaches friends' presence).
- Hosts get a third visibility, **Friends**: unlisted, offered to the host's
  friends on their Friends page.
- The restore code is under the sidebar's "Privacy and restore code", with a
  one-time nudge once the player has a friend to lose, not at first launch.
- Friends is a **sidebar** to the right of the launcher, open from the start
  and foldable to a strip (user, 2026-11-04), not a page behind a button.
- In game, **holding Menu opens an in-game panel** in solo play and online
  alike: what the old back-to-the-launcher prompt offered, the VR settings
  that were on the watch (moved off it), and the same Friends sidebar (user,
  2026-11-04).
- **No limit a stranger can trigger may lock a player out.** Limits are keyed
  on the actor (address or player), never on the target or the whole service
  (user, 2026-11-04: restore-code guessing had locked a code for a day).

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

- The first time the app runs with a name, `FriendsClient` registers: the
  Worker makes a **player ID** (16 random bytes, hex), a **secret** (32 random
  bytes) and a **restore code**, and keeps hashes of the secret and the
  restore code, as it does for lobby owner tokens. The client saves them in
  `data/gevr_player.json` (not the ini, so a profile restore cannot mix them
  up), written to a new file and renamed over the old one.
- Each player gets a **friend code**, 6 characters from the lobby alphabet,
  shown as `GE-7K3QX2` style with the prefix. Different length from lobby
  codes, so the two are never confused.
- The display name is the existing player name; the friend code is shown
  beside it wherever a name could be confused or faked.
- **Losing the ID** (reinstall, new headset): the **restore code**, 16
  characters shown as `ABCD-EFGH-JKLM-NPQR`, with the friend code brings it
  back (`POST /v1/players/restore`). That issues a new secret, which signs the
  old headset out (it shows "Signed out" and offers restore or a new code).
  The restore code itself does not change. Guesses: 10 an hour and 30 a day
  per address, never per friend code; at 80 bits, a million addresses would
  need about 5 x 10^13 years per code. Discord linking (phase 3) will restore
  by signing in.
- Dedicated servers get no player ID and never appear as players.

## Names and search

Decided with the user, 2026-11-04:

- **Display names, not unique** (as on Steam): the existing player name, 1-15
  printable characters. Edited at the top of the Friends panel (beside the
  friend code and an Online / Appear offline status), or on the Multiplayer
  page; not while in an online game. The lobby service keeps at most **ten
  renames a day** per player, so a name cannot churn to pass for someone else;
  a rename it keeps back says so once.
- **Search by name** ("Find a friend"): 3+ characters, any case, part of the
  name; up to 20 results, exact names first, then the most recently seen.
  Each result shows the name and friend code (names repeat) and Add, Accept,
  Request sent or Friends. Never whether someone is online. Players blocked
  either way are left out. 30 searches a minute per player.
- **Show me in search**, under Privacy, **on by default**: off, only a friend
  code finds you. `POST /v1/me/search {q}`; `searchable` in the settings and
  the players table (`FriendsSearchable` in the ini).

## Friends

- Add by name search or friend code. The other player sees a request and
  accepts or declines.
- Remove a friend, block a player (blocked players cannot send requests or
  invites and do not see your presence). Limits: 200 friends, 50 pending
  requests, 20 requests sent a day.
- Phase 3 adds Discord friends who have linked GoldenEye VR: they appear
  without a code (marked "Discord"), until either side unlinks.

## Presence

One request every 30 s while the app runs (launcher or game), sent by
`FriendsClient` on its own worker thread:

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
- **Receive in the launcher:** at the top of the Friends sidebar, "Natty
  invites you to Natty's game (2/4)" with **Join** and **Ignore** (above the
  page instead while the sidebar is folded). Join uses the normal
  join-by-code path.
- **Receive in a mission:** the invite shows low in the view for 8 seconds
  ("NATTY INVITES YOU TO NATTY'S GAME (2/4) / HOLD MENU TO ANSWER"), never on
  the wrist. Holding Menu opens the in-game panel, whose Friends side panel
  has the invite with Join. Join asks to leave the mission (it is not saved),
  writes the lobby code and the time to `data/gevr_pending_join.txt` and
  restarts; the launcher reads and deletes it, and joins if it is under 5
  minutes old. A friend's own Join works the same way. The online pause has
  the same side panel.
- An invite carries the lobby code and lives 10 minutes or until the lobby
  closes. Private lobbies work: the code is the key, as with join by code.
- Limits: 10 invites a minute per player, one pending invite per friend.
- Requests: 20 sent a day per player; 50 waiting per player, a new one
  dropping the oldest rather than being refused (a spammer cannot crowd out
  real friends); **Decline all** clears them (`DELETE /v1/me/requests`).
  Registration: 5 an hour and 20 a day per address, no service-wide cap.

## Worker API

`/v1/me` requests carry `Authorization: Bearer <id>.<secret>`. The
goldeneyevr.com repo's `services/lobbies/README.md` has the full table.

| Method | Path | Does |
|---|---|---|
| POST | `/v1/players` | `{name}`: register; returns ID, secret, friend code, restore code |
| POST | `/v1/players/restore` | `{friendCode, restoreCode}`: a new secret |
| POST | `/v1/me/presence` | `{state, detail, lobby, version, name}`; returns friends, requests, sent, invites, blocked |
| POST | `/v1/me/friends` | `{code}`: send a request (accepts theirs if they asked first) |
| PUT | `/v1/me/friends/<id>` | `{accept: true\|false}` |
| DELETE | `/v1/me/friends/<id>` | remove, cancel or decline |
| POST, DELETE | `/v1/me/blocks`, `/v1/me/blocks/<id>` | block (ends the friendship), unblock |
| POST | `/v1/me/invites` | `{to, lobby}`: invite (lobby must exist, be open and not full) |
| DELETE | `/v1/me/invites/<id>` | ignore or cancel |
| PUT | `/v1/me/settings` | `{showDetail, appearOffline, name}` |

Tables, in the same Durable Object: `players`, `friends(a, b, status,
from_id, created)` with `a < b`, `blocks(owner, blocked)`, `invites(id,
from_id, to_id, lobby, created, expires)`. Rate limits reuse the `limits`
table, keyed by player ID; the per-address write limit (40 a minute) still
applies. Friends receive a lobby code only for a public or friends-only lobby
that is open and not full. Lobby visibility `friends` is counted as private in
the stats and the activity page.

## Client

- `FriendsClient.java` (beside `LobbyClient`, its own thread and queue;
  `friendsCommand` / `friendsEvent` on `MainActivity`). Commands:
  `presence|state|lobby|version|name|detail`, `settings|showDetail|offline`,
  `add|code`, `answer|id|1/0`, `remove|id`, `block|id`, `unblock|id`,
  `invite|id|lobby`, `drop|inviteId`, `restore|friendCode|restoreCode`,
  `reset`, `refresh`. Events: `ME|status|friendCode|restoreCode|detail|offline`,
  a `FRIENDS_BEGIN` ... `FRIENDS_END` snapshot (`FRIEND`, `REQUEST`, `SENT`,
  `INVITE`, `BLOCKED` lines), `INVITED|id`, `NOTICE|text`, `ERROR|text`.
  Presence posts every 30 s, sooner (5 s apart at most) when the state or
  lobby changes, and stops a minute after the native side's last presence
  line, so a sleeping headset goes offline.
- `port/vr/vr_friends.cpp`: composes the presence line once a second from the
  launcher or the game (`gevrLobbyGameTick`), sends it on change and every
  20 s, keeps the model, queues invites until a lobby has a code, and keeps
  the pending join. Command logging stops at the verb: no restore code in a
  debug report.
- `port/vr/gevr_friends_ui.h`: the Friends sidebar, one widget for the
  launcher and the game (invites, requests with Decline all, friends online
  then a folded Offline list, Join / Invite / Remove / Block, Add a friend,
  Privacy and restore code, Blocked).
- One layout for every ImGui surface (user, 2026-11-04: standardize): the
  launcher, the in-game panel and the online pause are each a 1280 x 960
  window with the Friends **side panel** to its right
  (`gevrFriendsSidePanel`, its own window), open (500 px) or folded to a 76 px
  strip, one `FriendsSidebar` setting for all three. The canvas
  (`gevrSurfaceCanvasW`) and the screen quad grow by its width
  (`vr_screen_set_width_scale`), so no surface gets narrower. In the
  launcher, Join and hosting for an invite go through the Multiplayer page.
- Game: holding Menu 1.5 s opens the in-game panel (`gevr_panel_ui.h`, drawn
  by `vr_pause_menu.cpp` on the online pause's surface; `input.c` counts it
  as the native pause, so the game gets no input and fire waits for a trigger
  release after it closes). Its header is the online pause's
  (`gevrPauseHeader`: health and armour arcs, solo too via `gevrPanelGauges`;
  radar online). Below it the VR settings, a tab per section across the full
  width, from the settings table (`port/src/gevr_settings.c`, drawn as ImGui
  controls by `gevr_settings_ui.h`); the watch no longer has its VR settings page.
  Footer: Resume, HD textures on/off, Back to launcher (focused, so hold Menu
  then A still goes back). A solo mission pauses: the panel sends START to
  open the watch under it (not in a cutscene) and again to close it. Menu+A
  (gun fit) and Menu+B (microphone) still fire before the 1.5 s. B, Resume or
  a Menu tap closes the panel.
- Settings in `goldeneye-vr.ini`: `MpVisibility` 0/1/2 (public, private,
  friends), `FriendsShowDetail`, `FriendsAppearOffline`, `FriendsRestoreNoted`,
  `FriendsSidebar`.

## Discord (phases 3 and 4)

Discord Social SDK 1.10, Android build, behind **Link Discord** in the Friends
side panel. Application ID 1567606949862711468 (the GoldenEye VR app on the
user's Discord developer account); OAuth2 redirect
`discord-1567606949862711468:/authorize/callback`; **Public Client off**.

- **Building:** the SDK is not in the repository. Its Android library
  (`discord_partner_sdk.aar`, from the developer portal's Social SDK
  Downloads, inside the C++ zip at `lib/release/`) goes in `build/discord/`
  (gitignored), or `gevr.discordSdk=<path>` in `android/local.properties`.
  `android/app/build.gradle` then adds it (with `androidx.browser` 1.8 for the
  sign-in, prefab on) and passes `-DGEVR_DISCORD=ON`, which links its prefab
  package. Without it the game builds without Discord: `vr_discord.cpp`'s
  stubs, no Discord section in the panel. With it the APK grows about 9 MB
  (arm64 only). Its voice service and permissions are removed in the manifest.
- **Link (phase 3, built):** `vr_discord.cpp` calls `Client::Authorize` with a
  PKCE challenge (presence scopes); the browser comes back to the SDK's
  AuthenticationActivity; the code and verifier go to the Worker
  (`POST /v1/discord/link`), which exchanges them with the client secret
  (Worker secret `DISCORD_CLIENT_SECRET`, var `DISCORD_CLIENT_ID`) and reads
  the account (`/users/@me`). The Worker keeps the Discord user ID on the
  player; FriendsClient keeps the refresh token in `gevr_player.json` and gets a
  fresh access token each run (`POST /v1/discord/refresh`). A Discord account
  already linked to another player brings that player back (new secret, the
  other headset signed out), so signing in on a new headset restores your
  friends. `DELETE /v1/me/discord` unlinks.
- **Discord friends (built):** the SDK's relationships (type Friend) go to
  `PUT /v1/me/discord/friends`. Two linked players become friends, marked
  "Discord", only when **both** clients report each other (`discord_claims`),
  so no client can claim a stranger; the friendship goes when either claim does
  or either side unlinks.
- **Rich Presence (built):** the presence state and detail ("In a lobby",
  "Deathmatch, Statue (Full), 3/8") with a party size; the party ID is a hash,
  never a private lobby's code. Cleared while appearing offline. On mobile
  Discord only publishes presence for linked accounts.
- **An old Discord app on the Quest** (found 2026-11-04): with the Discord app
  installed, the SDK signs in through it, and an outdated build answered
  "Invalid scope: openid" (the same request works on the web). The current
  one from the Meta Quest store works. The panel says so under Link Discord
  and when a sign-in fails.
- **Discord invites (phase 4, built, test.9):** in a lobby, the Rich Presence
  carries a party (size, a hashed ID) and the join secret `gevr1:<lobby code>`,
  supported platform Android. Party privacy Public for a lobby this headset
  hosts as public or friends-only, so Discord friends may join from Discord;
  Private otherwise (invite only). The Friends panel's Discord section lists
  Discord friends (online first, 40 shown) with **Invite**
  (`Client::SendActivityInvite`); with no lobby, the launcher hosts a private
  one first, as the panel's own Invite does, and the invites go out once its
  secret is in the presence. Invites received
  (`SetActivityInviteCreatedCallback`) show at the top of the panel with Join
  (`AcceptActivityInvite`) and Ignore, and as a notice in a mission; Join from
  Discord's own app arrives through `SetActivityJoinCallback`. A secret that
  is not `gevr1:` and a valid code is refused. The join goes through the
  ordinary join by code (in a mission, the panel asks first).
- **Join links (phase 4, built):** `goldeneyevr.com/join?c=<code>` (the site's
  `join.html` and `join.js`) shows the code, how to join by hand and an
  **Open in GoldenEye VR** button to `goldeneyevr://join/<code>`, which the
  app takes (MainActivity `takeJoinLink`, an intent filter on MainActivity)
  into `data/gevr_pending_join.txt` for the launcher (checked every second
  while it runs). The Friends panel shows a lobby's link to pass on. A custom
  scheme, not an Android app link: no `assetlinks.json` to verify, and a
  browser just shows the page when the game is not installed.
- Not used: Discord's provisional accounts. They would keep the friends list
  on Discord's side; ours stays the source of truth.

## Not used: Meta Platform SDK

Group Presence, the invite panel and the friends API exist, but every player
needs an entitlement (store, release channel or key), the friends API needs a
manually reviewed Data Use Checkup, and the app would be registered with Meta.
Sideloaded builds only get these for the developer's own organisation.

## Phases

1. **Friends and presence** (built): registration, friend codes, requests,
   block, presence and its settings, the launcher Friends page. Tests: the
   Worker's `test/social.test.mjs`, `FriendsClientTest`, `test_friends_ui.py`
   (the sidebar and the in-game panel at headset font size), and the launcher
   and pause layout tests.
2. **Invites** (built): send and receive in the launcher, the in-mission
   notice, the in-game panel and the pending-join restart.
3. **Discord link** (built, test.7): SDK, sign-in, Discord friends, Rich
   Presence. Worker tests: `test/discord.test.mjs` (Discord's API faked);
   `FriendsClientTest.discordLink...`.
4. **Discord invites and join links** (built, test.9): Discord invites both
   ways, Join from Discord, the join page and the `goldeneyevr://` link.

Phases 1 and 2 ship without Discord. Protocol: none of this touches the game's
network protocol; the Worker API is versioned under `/v1`.

## Open questions

- Does the Quest browser hand `goldeneyevr://join/...` to the app? Only a
  headset can tell; joining by hand from the page works either way.
