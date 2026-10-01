# GoldenEye VR lobby service

This Worker supplies the public lobby browser, unlisted codes, ICE signaling, and short-lived Cloudflare TURN credentials. The game traffic never passes through this Worker. Each headset sends its ENet packets through libjuice, which uses direct UDP when possible and Cloudflare TURN when direct connectivity fails (symmetric or carrier-grade NAT). TURN is a fallback, not a requirement: when `/turn` refuses (secrets missing, rate limit, monthly cap), the headset still joins with STUN candidates only, and the join fails only for the peers that hole punching cannot reach.

Cloudflare's STUN is free and unlimited; TURN egress is free up to 1,000 GB a month and billed past that. `TURN_MONTHLY_CAP` (a `vars` entry in `wrangler.jsonc`, default `4000`) caps the credentials issued per UTC calendar month; past it `/turn` answers 503 `Monthly relay budget used; direct connections only` until the month rolls over. One credential covers at most one two-hour lobby, well under 250 MB relayed even if every packet relays, so the default keeps the worst month inside the free tier. Set it to `0` to remove the cap. The headset asks for TURN over UDP on 3478 and 443; libjuice speaks UDP only, so there is no TCP or TLS relay path.

## Deploy

1. Create a Cloudflare Realtime TURN key in the Cloudflare dashboard. Keep its token and key ID server-side.
2. From this directory, run `npm ci`, then set the Worker secrets with `npx wrangler secret put TURN_KEY_ID` and `npx wrangler secret put TURN_KEY_API_TOKEN`. Enter values at the interactive prompts; do not put them in source or shell command arguments.
3. Run `npm run check`, `npx wrangler deploy --dry-run`, then `npm run deploy`. The Worker config attaches `lobbies.goldeneyevr.com` as a Custom Domain in the `goldeneyevr.com` zone. The Quest app calls that host.
4. Check `/` for the dashboard, `GET /v1/activity` for public activity, and `GET /v1/lobbies?version=6` for compatible open games before distributing the protocol-6 APK.

Debug reports use the Worker `send_email` binding. `REPORT_TO` is `info@goldeneyevr.com`, which must be verified as an account destination address in Cloudflare Email Routing before it can receive messages from this binding. The sender domain must be enabled for Email Service. Run `npm run check` and `npx wrangler deploy --dry-run` before deploying this change.

The service uses one SQLite-backed Durable Object for lobby coordination. Lobbies expire after 45 seconds without a host heartbeat; pending joins expire after 90 seconds. Public list responses exclude private games and owner tokens. `/v1/activity` reports private games only as an aggregate count; every other total and the published names, stages, phases, occupancy, and open spots cover public games only. `waiting`, `warmup`, and `in_progress` are independent of occupancy and joinability. The code is an unlisted join key for private games, not an account identity. Turn on Cloudflare request analytics and monitor Worker/DO limits and TURN egress as usage grows.

Heartbeats cannot extend a lobby past these limits:

- A `waiting` lobby with one player expires 15 minutes after creation.
- A `warmup` or `in_progress` lobby with one player expires 30 minutes after its last phase change.
- Every lobby expires 2 hours after creation, regardless of phase or occupancy.

An owner heartbeat that encounters an idle or lifetime limit returns HTTP 410 with `Lobby idle timeout` or `Lobby lifetime expired`. A lobby already removed by cleanup or expired by its 45-second heartbeat TTL returns HTTP 404. The app forgets either expired lobby and registers a new code when the host is still accepting players.

Owner-token holders may include an optional `name` in the heartbeat PUT body (1–32 characters, matching creation). A migrated host sends its own name so the dashboard identifies the current host. Omitting `name` preserves the existing name; no schema migration is needed.

The Android client pauses heartbeats and offer polling after 60 seconds without a native `refresh` or `phase` keepalive. In-game keepalives require a running OpenXR session; loss of focus to the Quest menu alone does not pause them. Returning to an expired lobby triggers registration again. Quit/relaunch waits up to 2 seconds for queued removal (1 second on the main looper), and shutdown waits up to 1.5 seconds; failed removal still expires by the heartbeat TTL. A host handing the match to another player queues `leave` first so the successor retains the lobby.

## Local check

Run `npm ci`, `npm run check`, and `npm test` for local type and runtime checks. The tests use Wrangler's bundled Miniflare and an isolated SQLite registry; they cover renaming, idle/lifetime limits, heartbeat expiry, and the monthly relay cap without contacting production. Android client regression tests run with `android/gradlew.bat -p android testDebugUnitTest` from the repository root and replace all lobby HTTP responses locally.

Run `npm run dev` and make requests to `http://127.0.0.1:8787/v1/lobbies`. TURN credential issuance needs the real server-side secrets and a valid lobby owner or join token. Android builds call the production hostname, so local API checks do not test headset connectivity.

## Endpoints

| Method | Path | Access | Purpose |
| --- | --- | --- | --- |
| POST | `/v1/lobbies` | Rate-limited | Create a public or private lobby; returns owner token and code |
| GET | `/v1/lobbies?version=6` | Rate-limited | List compatible open public games |
| GET | `/v1/lobbies/:code?version=6` | Code | Resolve an available game |
| GET | `/v1/activity` | Rate-limited | Public activity and aggregate private count |
| PUT, DELETE | `/v1/lobbies/:code` | Owner token | Refresh state (optional name) or remove game |
| POST | `/v1/lobbies/:code/joins` | Code | Start a join and receive a join token |
| PUT | `/v1/lobbies/:code/joins/:id/offer` | Join token | Submit ICE offer |
| GET | `/v1/lobbies/:code/joins` | Owner token | Poll offers |
| PUT | `/v1/lobbies/:code/joins/:id/answer` | Owner token | Submit ICE answer |
| GET | `/v1/lobbies/:code/joins/:id/answer` | Join token | Poll answer |
| POST | `/v1/lobbies/:code/turn` | Owner or join token; 120 requests per IP per hour; `TURN_MONTHLY_CAP` per month | Issue a short-lived TURN credential; 503 when refused, and the headset then connects without a relay |
| POST | `/v1/reports` | 3 per IP per hour, 100 global per day | Email an explicitly submitted debug report |

A report email's `Crash:` line is the Android exit record the headset offered (ISO UTC time, reason name, status, Android's description) or "Previous foreground run ended unexpectedly"; the attached log opens with `=== exit history ===`, the last 32 exit records with `[before install]` on any older than the installed APK. The headset only offers crashes newer than its install, so a record from an earlier version never arrives as a new report. A `tombstone-<id>.pb` attachment (native crashes and ANRs only; fatal-signal records have none) is Android's protobuf tombstone: decode it on any Quest with `adb push tombstone-<id>.pb /data/local/tmp/ && adb shell pbtombstone /data/local/tmp/tombstone-<id>.pb`, then symbolize the `libgevr.so` frames against that release's `android/app/build/outputs/native-debug-symbols/release/native-debug-symbols.zip` (release builds keep a symbol table; keep that zip per tag).

Authorization uses `Authorization: Bearer <token>`. The TURN key never leaves the Worker; issued per-peer credentials are short lived.
