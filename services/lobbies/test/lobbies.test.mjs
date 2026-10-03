import assert from "node:assert/strict";
import { after, before, test } from "node:test";
import { fileURLToPath } from "node:url";
// Use the runtime and bundler shipped with the lockfile's Wrangler version.
import { build } from "esbuild";
import { Miniflare, convertV4MiniflareOptions } from "miniflare";

let runtime, storage;
let requestId = 0;
const minute = 60_000;

before(async () => {
  const bundle = await build({
    entryPoints: [fileURLToPath(new URL("../src/index.ts", import.meta.url))],
    bundle: true, write: false, format: "esm", platform: "neutral",
    external: ["cloudflare:workers"],
  });
  runtime = new Miniflare(convertV4MiniflareOptions({
    name: "lobby-test", modules: true, script: bundle.outputFiles[0].text,
    compatibilityDate: "2026-09-27",
    durableObjects: { REGISTRY: { className: "LobbyRegistry", useSQLite: true } },
    bindings: { TURN_MONTHLY_CAP: "1" },
    unsafeInspectDurableObjects: true,
  }));
  // Initialize the real schema before manipulating timestamps locally.
  await request("GET", "/v1/activity");
  storage = await runtime.unsafeGetDurableObjectStorage("lobby-test", "LobbyRegistry", { name: "global-v1" });
});
after(async () => { await runtime?.dispose(); });

async function request(method, path, body, token = "") {
  const response = await runtime.dispatchFetch(`http://localhost${path}`, {
    method, headers: {
      "content-type": "application/json", authorization: `Bearer ${token}`,
      "cf-connecting-ip": `test-${++requestId}`,
    }, body: body === undefined ? undefined : JSON.stringify(body),
  });
  return { status: response.status, body: await response.json() };
}
async function create() {
  const result = await request("POST", "/v1/lobbies", {
    name: "A's game", visibility: "public", version: 6, stage: 1, weapons: 2, maxPlayers: 4,
  });
  assert.equal(result.status, 201);
  return result.body;
}
function update(lobby, state, token = lobby.ownerToken) {
  return request("PUT", `/v1/lobbies/${lobby.code}`, { players: 1, open: true, ...state }, token);
}
async function age(lobby, { phase = "warmup", players = 1, ageMinutes = 31, createdMinutes = 35, expired = false, phaseTimestamp = true } = {}) {
  const now = Date.now();
  await storage.exec("UPDATE lobbies SET phase=?, players=?, phase_changed_at=?, created_at=?, expires=? WHERE code=?",
    phase, players, phaseTimestamp ? now - ageMinutes * minute : 0,
    now - createdMinutes * minute, now + (expired ? -1_000 : 45_000), lobby.code);
}
async function gone(lobby) {
  assert.deepEqual(await storage.exec("SELECT code FROM lobbies WHERE code=?", lobby.code), []);
  assert.deepEqual(await storage.exec("SELECT id FROM joins WHERE code=?", lobby.code), []);
}

test("owner can rename on migration; omitted and invalid names preserve the name", async () => {
  const lobby = await create();
  assert.equal((await update(lobby, { name: "B's game" }, "wrong-token")).status, 404);
  assert.equal((await update(lobby, { name: "B's game" })).status, 200);
  assert.equal((await update(lobby, {})).status, 200);
  for (const name of ["", "x".repeat(40), null, 123])
    assert.equal((await update(lobby, { name })).status, 400);
  const activity = await request("GET", "/v1/activity");
  assert.equal(activity.body.lobbies.find(row => row.code === lobby.code).name, "B's game");
  const list = await request("GET", "/v1/lobbies?version=6");
  assert.equal(list.body.lobbies.find(row => row.code === lobby.code).name, "B's game");
  assert.equal((await update(lobby, { name: "x".repeat(32) })).status, 200);
});

test("solo warmup heartbeat returns 410 and removes pending joins after 30 minutes", async () => {
  const lobby = await create();
  assert.equal((await request("POST", `/v1/lobbies/${lobby.code}/joins`, { version: 6 })).status, 201);
  await age(lobby);
  assert.deepEqual(await update(lobby, { phase: "warmup" }), { status: 410, body: { error: "Lobby idle timeout" } });
  await gone(lobby);
});

test("dashboard cleanup removes solo warmup and in-progress lobbies, keeping multiplayer and recent phases", async () => {
  const oldWarmup = await create();
  const oldMatch = await create();
  const multiplayer = await create();
  const recent = await create();
  const legacy = await create();
  await age(oldWarmup);
  await age(oldMatch, { phase: "in_progress" });
  await age(multiplayer, { players: 2 });
  await age(recent, { ageMinutes: 29 });
  await age(legacy, { phaseTimestamp: false });
  const codes = (await request("GET", "/v1/activity")).body.lobbies.map(row => row.code);
  assert.ok(!codes.includes(oldWarmup.code));
  assert.ok(!codes.includes(oldMatch.code));
  for (const lobby of [multiplayer, recent, legacy]) assert.ok(codes.includes(lobby.code));
});

test("heartbeats do not reset the solo phase timer; phase transitions do", async () => {
  const lobby = await create();
  await age(lobby, { ageMinutes: 29 });
  const original = (await storage.exec("SELECT phase_changed_at FROM lobbies WHERE code=?", lobby.code))[0].phase_changed_at;
  assert.equal((await update(lobby, { phase: "warmup", name: "B's game" })).status, 200);
  assert.equal((await storage.exec("SELECT phase_changed_at FROM lobbies WHERE code=?", lobby.code))[0].phase_changed_at, original);
  assert.equal((await update(lobby, { phase: "in_progress", players: 2 })).status, 200);
  assert.ok((await storage.exec("SELECT phase_changed_at FROM lobbies WHERE code=?", lobby.code))[0].phase_changed_at > original);
});

test("15-minute waiting and 2-hour lifetime rules still return 410", async () => {
  const waiting = await create();
  await age(waiting, { phase: "waiting", createdMinutes: 16 });
  assert.deepEqual(await update(waiting, {}), { status: 410, body: { error: "Lobby idle timeout" } });
  const match = await create();
  await age(match, { phase: "in_progress", players: 2, createdMinutes: 121 });
  assert.deepEqual(await update(match, { players: 2 }), { status: 410, body: { error: "Lobby lifetime expired" } });
});

test("an expired heartbeat TTL cannot be revived by PUT", async () => {
  const lobby = await create();
  await age(lobby, { ageMinutes: 1, createdMinutes: 2, expired: true });
  assert.equal((await update(lobby, { phase: "warmup" })).status, 404);
  await gone(lobby);
});

test("relay credentials stop at the monthly cap and the client is told to go direct", async () => {
  const lobby = await create();
  // No TURN secrets in the test runtime: the first request passes the cap and fails at configuration.
  const first = await request("POST", `/v1/lobbies/${lobby.code}/turn`, {}, lobby.ownerToken);
  assert.equal(first.status, 503);
  assert.equal(first.body.error, "Relay is not configured");
  const second = await request("POST", `/v1/lobbies/${lobby.code}/turn`, {}, lobby.ownerToken);
  assert.equal(second.status, 503);
  assert.match(second.body.error, /monthly relay budget/i);
  const [row] = await storage.exec("SELECT count, reset FROM limits ORDER BY reset DESC LIMIT 1");
  assert.equal(row.count, 1);
  const now = Date.now();
  const nextMonth = Date.UTC(new Date(now).getUTCFullYear(), new Date(now).getUTCMonth() + 1, 1);
  assert.ok(Math.abs(row.reset - nextMonth) < 60_000, "cap window ends at the start of next UTC month");
});

test("an eight-player lobby takes seven joiners at once; nine players are refused", async () => {
  const settings = { name: "A's game", visibility: "public", version: 6, stage: 1, weapons: 2 };
  assert.equal((await request("POST", "/v1/lobbies", { ...settings, maxPlayers: 9 })).status, 400);
  const created = await request("POST", "/v1/lobbies", { ...settings, maxPlayers: 8 });
  assert.equal(created.status, 201);
  const lobby = created.body;
  assert.equal((await update(lobby, { players: 8 })).status, 200);
  assert.equal((await update(lobby, { players: 9 })).status, 400);
  assert.equal((await update(lobby, { players: 1 })).status, 200);
  const joins = [];
  for (let i = 0; i < 7; i++) {
    const join = await request("POST", `/v1/lobbies/${lobby.code}/joins`, { version: 6 });
    assert.equal(join.status, 201, `joiner ${i + 1}`);
    joins.push(join.body);
  }
  for (const join of joins)
    assert.equal((await request("PUT", `/v1/lobbies/${lobby.code}/joins/${join.id}/offer`, { sdp: "a=ice-ufrag:x" }, join.joinToken)).status, 200);
  const pending = await request("GET", `/v1/lobbies/${lobby.code}/joins`, undefined, lobby.ownerToken);
  assert.equal(pending.body.requests.length, 7);
});
