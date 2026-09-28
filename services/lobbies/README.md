# GoldenEye VR lobby service

This Worker supplies the public lobby browser, unlisted codes, ICE signaling, and short-lived Cloudflare TURN credentials. The game traffic never passes through this Worker. Each headset sends its ENet packets through libjuice, which uses direct UDP when possible and Cloudflare TURN when direct connectivity fails.

## Deploy

1. Create a Cloudflare Realtime TURN key in the Cloudflare dashboard. Keep its token and key ID server-side.
2. From this directory, run `npm ci`, then set the Worker secrets with `npx wrangler secret put TURN_KEY_ID` and `npx wrangler secret put TURN_KEY_API_TOKEN`. Enter values at the interactive prompts; do not put them in source or shell command arguments.
3. Run `npm run check`, `npx wrangler deploy --dry-run`, then `npm run deploy`. The Worker config attaches `lobbies.goldeneyevr.com` as a Custom Domain in the `goldeneyevr.com` zone. The Quest app calls that host.
4. Check `/` for the dashboard, `GET /v1/activity` for public activity, and `GET /v1/lobbies?version=6` for compatible open games before distributing the protocol-6 APK.

The service uses one SQLite-backed Durable Object for lobby coordination. Lobbies expire after 45 seconds without a host heartbeat; pending joins expire after 90 seconds. Public list responses exclude private games and owner tokens. `/v1/activity` reports private games only as an aggregate count; every other total and the published names, stages, phases, occupancy, and open spots cover public games only. `waiting`, `warmup`, and `in_progress` are independent of occupancy and joinability. The code is an unlisted join key for private games, not an account identity. Turn on Cloudflare request analytics and monitor Worker/DO limits and TURN egress as usage grows.

## Local check

Run `npm run dev` and make requests to `http://127.0.0.1:8787/v1/lobbies`. TURN credential issuance needs the real server-side secrets and a valid lobby owner or join token. Android builds call the production hostname, so local API checks do not test headset connectivity.

## Endpoints

| Method | Path | Access | Purpose |
| --- | --- | --- | --- |
| POST | `/v1/lobbies` | Rate-limited | Create a public or private lobby; returns owner token and code |
| GET | `/v1/lobbies?version=6` | Rate-limited | List compatible open public games |
| GET | `/v1/lobbies/:code?version=6` | Code | Resolve an available game |
| GET | `/v1/activity` | Rate-limited | Public activity and aggregate private count |
| PUT, DELETE | `/v1/lobbies/:code` | Owner token | Refresh state or remove game |
| POST | `/v1/lobbies/:code/joins` | Code | Start a join and receive a join token |
| PUT | `/v1/lobbies/:code/joins/:id/offer` | Join token | Submit ICE offer |
| GET | `/v1/lobbies/:code/joins` | Owner token | Poll offers |
| PUT | `/v1/lobbies/:code/joins/:id/answer` | Owner token | Submit ICE answer |
| GET | `/v1/lobbies/:code/joins/:id/answer` | Join token | Poll answer |
| POST | `/v1/lobbies/:code/turn` | Owner or join token | Issue a short-lived TURN credential |

Authorization uses `Authorization: Bearer <token>`. The TURN key never leaves the Worker; issued per-peer credentials are short lived.
