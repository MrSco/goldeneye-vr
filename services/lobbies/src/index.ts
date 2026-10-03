import { DurableObject } from "cloudflare:workers";
import { runReport, utcDay } from "./report";

export interface Env {
  REGISTRY: DurableObjectNamespace<LobbyRegistry>;
  TURN_KEY_ID: string;
  TURN_KEY_API_TOKEN: string;
  TURN_MONTHLY_CAP?: string;
  REPORT_TO: string;
  CF_ACCOUNT_ID?: string;
  CF_ZONE_ID?: string;
  CF_ANALYTICS_TOKEN?: string;
  TURN_FREE_GB?: string;
  WORKERS_DAILY_LIMIT?: string;
  DO_DAILY_LIMIT?: string;
  EMAIL: { send(message: {
    from: string; to: string; subject: string; text: string;
    attachments: Array<{ content: string; filename: string; type: string; disposition: "attachment" }>;
  }): Promise<{ messageId: string }> };
}

type Visibility = "public" | "private";
type Phase = "waiting" | "warmup" | "in_progress";
type Lobby = {
  code: string; owner_hash: string; name: string; visibility: Visibility;
  version: number; stage: number; weapons: number; players: number;
  max_players: number; open: number; expires: number; phase: Phase;
  created_at: number; phase_changed_at: number;
};
type Join = { id: string; code: string; token_hash: string; offer: string | null; answer: string | null; expires: number };

const TTL = 45_000;
// The game's player slots (net_protocol.h GEVR_MAX_PLAYERS, protocol 16).
const MAX_PLAYERS = 8;
// Join rows live 90 seconds: room for every client slot twice (a retry, a rejoin after a host change).
const MAX_PENDING_JOINS = 2 * (MAX_PLAYERS - 1);
const WAITING_IDLE_TIMEOUT = 15 * 60_000;
const ALONE_IDLE_TIMEOUT = 30 * 60_000;
const MAX_LOBBY_LIFESPAN = 2 * 3600_000;
// Relay credentials issued per calendar month before the Worker stops handing them out.
// Cloudflare bills TURN egress past 1,000 GB a month; a credential covers at most one
// two-hour lobby, under 250 MB relayed in the worst case of four players, so 4000 keeps
// the worst month inside the free tier. A client's relayed traffic grows with the other
// players it hears: an eight-player lobby relays up to about 7/3 as much per credential.
// Games keep working without a relay: direct connections only.
const TURN_MONTHLY_CAP_DEFAULT = 4000;
const turnMonthlyCap = (env: Env) => env.TURN_MONTHLY_CAP === undefined ? TURN_MONTHLY_CAP_DEFAULT : Number(env.TURN_MONTHLY_CAP);
const addDays = (day: string, days: number) => utcDay(Date.parse(day + "T00:00:00Z") + days * 86_400_000);
export type StatsRow = { day: string; key: string; value: number };
const msUntilNextUtcMonth = (now = Date.now()) => {
  const date = new Date(now);
  return Date.UTC(date.getUTCFullYear(), date.getUTCMonth() + 1, 1) - now;
};
const ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
const json = (value: unknown, status = 200, extraHeaders: Record<string, string> = {}) =>
  Response.json(value, { status, headers: { "cache-control": "no-store", ...extraHeaders } });
const bad = (message: string, status = 400) => json({ error: message }, status);
const codeValue = () => Array.from(crypto.getRandomValues(new Uint8Array(8)), n => ALPHABET[n & 31]).join("");
const digest = async (value: string) => Array.from(new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(value))), b => b.toString(16).padStart(2, "0")).join("");
const validInt = (value: unknown, min: number, max: number) => Number.isInteger(value) && Number(value) >= min && Number(value) <= max;
const validName = (value: unknown) => typeof value === "string" && value.length >= 1 && value.length <= 32;
const validSdp = (value: unknown) => typeof value === "string" && value.length > 0 && value.length <= 4096 && value.startsWith("a=ice-ufrag:");

export class LobbyRegistry extends DurableObject<Env> {
  constructor(ctx: DurableObjectState, env: Env) {
    super(ctx, env);
    ctx.blockConcurrencyWhile(async () => {
      this.ctx.storage.sql.exec("CREATE TABLE IF NOT EXISTS lobbies (code TEXT PRIMARY KEY, owner_hash TEXT NOT NULL, name TEXT NOT NULL, visibility TEXT NOT NULL, version INTEGER NOT NULL, stage INTEGER NOT NULL, weapons INTEGER NOT NULL, players INTEGER NOT NULL, max_players INTEGER NOT NULL, open INTEGER NOT NULL, expires INTEGER NOT NULL)");
      const columns = this.ctx.storage.sql.exec<{ name: string }>("PRAGMA table_info(lobbies)").toArray();
      if (!columns.some(column => column.name === "phase"))
        this.ctx.storage.sql.exec("ALTER TABLE lobbies ADD COLUMN phase TEXT NOT NULL DEFAULT 'waiting'");
      if (!columns.some(column => column.name === "created_at"))
        this.ctx.storage.sql.exec("ALTER TABLE lobbies ADD COLUMN created_at INTEGER NOT NULL DEFAULT 0");
      if (!columns.some(column => column.name === "phase_changed_at"))
        this.ctx.storage.sql.exec("ALTER TABLE lobbies ADD COLUMN phase_changed_at INTEGER NOT NULL DEFAULT 0");
      this.ctx.storage.sql.exec("CREATE TABLE IF NOT EXISTS joins (id TEXT PRIMARY KEY, code TEXT NOT NULL, token_hash TEXT NOT NULL, offer TEXT, answer TEXT, expires INTEGER NOT NULL)");
      this.ctx.storage.sql.exec("CREATE TABLE IF NOT EXISTS limits (key TEXT PRIMARY KEY, count INTEGER NOT NULL, reset INTEGER NOT NULL)");
      this.ctx.storage.sql.exec("CREATE INDEX IF NOT EXISTS lobbies_expires ON lobbies(expires)");
      this.ctx.storage.sql.exec("CREATE INDEX IF NOT EXISTS joins_code ON joins(code)");
      this.ctx.storage.sql.exec("CREATE TABLE IF NOT EXISTS stats (day TEXT NOT NULL, key TEXT NOT NULL, value INTEGER NOT NULL, PRIMARY KEY(day, key))");
      this.ctx.storage.sql.exec("CREATE TABLE IF NOT EXISTS alerts (metric TEXT PRIMARY KEY, period TEXT NOT NULL, level INTEGER NOT NULL)");
    });
  }

  // Today's daily maximums, cached so that sampling them on every request writes a row only when one rises.
  private peaks = new Map<string, number>();

  private bump(key: string, now = Date.now()) {
    this.ctx.storage.sql.exec(
      "INSERT INTO stats(day,key,value) VALUES(?,?,1) ON CONFLICT(day,key) DO UPDATE SET value=value+1", utcDay(now), key);
  }

  private peak(key: string, value: number, now = Date.now()) {
    const day = utcDay(now);
    const cacheKey = day + ":" + key;
    let known = this.peaks.get(cacheKey);
    if (known === undefined) {
      if (this.peaks.size > 16) this.peaks.clear();
      known = this.ctx.storage.sql.exec<{ value: number }>("SELECT value FROM stats WHERE day=? AND key=?", day, key).toArray()[0]?.value ?? -1;
      this.peaks.set(cacheKey, known);
    }
    if (value <= known) return;
    this.peaks.set(cacheKey, value);
    this.ctx.storage.sql.exec(
      "INSERT INTO stats(day,key,value) VALUES(?,?,?) ON CONFLICT(day,key) DO UPDATE SET value=MAX(value, excluded.value)", day, key, value);
  }

  async record(key: "turn_issued" | "turn_refused" | "reports"): Promise<void> {
    this.bump(key);
  }

  async statsRange(fromDay: string, toDay: string): Promise<StatsRow[]> {
    return this.ctx.storage.sql.exec<StatsRow>("SELECT day, key, value FROM stats WHERE day>=? AND day<=? ORDER BY day", fromDay, toDay).toArray();
  }

  /** Relay credentials issued this UTC month under TURN_MONTHLY_CAP. */
  async turnCredentialsThisMonth(): Promise<number> {
    const row = this.ctx.storage.sql.exec<{ count: number; reset: number }>(
      "SELECT count, reset FROM limits WHERE key=?", await digest("global:turn-month")).toArray()[0];
    return row && row.reset >= Date.now() ? row.count : 0;
  }

  /** The highest alert level already emailed for a metric in this period, 0 if none. */
  async alertLevelSent(metric: string, period: string): Promise<number> {
    const row = this.ctx.storage.sql.exec<{ period: string; level: number }>("SELECT period, level FROM alerts WHERE metric=?", metric).toArray()[0];
    return row && row.period === period ? row.level : 0;
  }

  async recordAlert(metric: string, period: string, level: number): Promise<void> {
    this.ctx.storage.sql.exec(
      "INSERT INTO alerts(metric,period,level) VALUES(?,?,?) ON CONFLICT(metric) DO UPDATE SET period=excluded.period, level=excluded.level", metric, period, level);
  }

  async publicStats(now = Date.now()): Promise<Response> {
    const today = utcDay(now);
    const from30 = addDays(today, -29);
    const from14 = addDays(today, -13);
    const sum = (where: string, ...args: (string | number)[]) =>
      this.ctx.storage.sql.exec<{ key: string; total: number }>(`SELECT key, SUM(value) AS total FROM stats WHERE ${where} GROUP BY key`, ...args).toArray()
        .reduce((totals, row) => (totals[row.key] = row.total, totals), {} as Record<string, number>);
    const max = (key: string, fromDay = "") =>
      this.ctx.storage.sql.exec<{ peak: number | null }>("SELECT MAX(value) AS peak FROM stats WHERE key=? AND day>=?", key, fromDay).one().peak ?? 0;
    const lobbies = (t: Record<string, number>) => (t.lobbies_public ?? 0) + (t.lobbies_private ?? 0);
    const all = sum("1=1");
    const last30 = sum("day>=?", from30);
    const day = sum("day=?", today);
    const since = this.ctx.storage.sql.exec<{ since: string | null }>("SELECT MIN(day) AS since FROM stats").one().since;
    const top = this.ctx.storage.sql.exec<{ key: string; total: number }>(
      "SELECT key, SUM(value) AS total FROM stats WHERE key LIKE 'stage:%' GROUP BY key ORDER BY total DESC, key LIMIT 1").toArray()[0];
    const byDay = new Map<string, Record<string, number>>();
    for (const row of this.ctx.storage.sql.exec<StatsRow>(
      "SELECT day, key, value FROM stats WHERE day>=? AND key IN ('lobbies_public','lobbies_private','matches_started')", from14).toArray()) {
      const entry = byDay.get(row.day) ?? {};
      entry[row.key] = row.value;
      byDay.set(row.day, entry);
    }
    const daily = Array.from({ length: 14 }, (_, i) => {
      const d = addDays(from14, i);
      const t = byDay.get(d) ?? {};
      return { day: d, lobbies: lobbies(t), matches: t.matches_started ?? 0 };
    });
    return json({
      since,
      today: { lobbies: lobbies(day), joins: day.join_attempts ?? 0, matches: day.matches_started ?? 0 },
      last30: {
        lobbies: lobbies(last30), joinAttempts: last30.join_attempts ?? 0, joinsConnected: last30.joins_connected ?? 0,
        matches: last30.matches_started ?? 0, peakPlayers: max("peak_players", from30),
      },
      allTime: {
        lobbies: lobbies(all), matches: all.matches_started ?? 0, joinsConnected: all.joins_connected ?? 0, peakPlayers: max("peak_players"),
      },
      topStage: top ? { stage: Number(top.key.slice(6)), matches: top.total } : null,
      daily,
    }, 200, { "cache-control": "public, max-age=300", "access-control-allow-origin": "*" });
  }

  private cleanup(now = Date.now()) {
    this.ctx.storage.sql.exec(
      "DELETE FROM lobbies WHERE expires < ? OR (phase = 'waiting' AND players = 1 AND created_at > 0 AND created_at < ?) OR (phase <> 'waiting' AND players = 1 AND phase_changed_at > 0 AND phase_changed_at < ?) OR (created_at > 0 AND created_at < ?)",
      now, now - WAITING_IDLE_TIMEOUT, now - ALONE_IDLE_TIMEOUT, now - MAX_LOBBY_LIFESPAN
    );
    this.ctx.storage.sql.exec("DELETE FROM joins WHERE expires < ? OR code NOT IN (SELECT code FROM lobbies)", now);
    this.ctx.storage.sql.exec("DELETE FROM limits WHERE reset < ?", now);
    this.samplePeaks(now);
  }

  private samplePeaks(now = Date.now()) {
    const live = this.ctx.storage.sql.exec<{ lobbies: number; players: number | null }>("SELECT COUNT(*) AS lobbies, SUM(players) AS players FROM lobbies").one();
    this.peak("peak_lobbies", live.lobbies, now);
    this.peak("peak_players", live.players ?? 0, now);
  }

  async limit(ip: string, action: string, ceiling: number, windowMs = 60_000): Promise<boolean> {
    const key = await digest(ip + ":" + action);
    const now = Date.now();
    const row = this.ctx.storage.sql.exec<{ count: number; reset: number }>("SELECT count, reset FROM limits WHERE key = ?", key).toArray()[0];
    if (!row || row.reset < now) {
      this.ctx.storage.sql.exec("INSERT INTO limits(key,count,reset) VALUES(?,1,?) ON CONFLICT(key) DO UPDATE SET count=1, reset=excluded.reset", key, now + windowMs);
      return true;
    }
    if (row.count >= ceiling) return false;
    this.ctx.storage.sql.exec("UPDATE limits SET count=count+1 WHERE key=?", key);
    return true;
  }

  private lobby(code: string): Lobby | undefined {
    this.cleanup();
    return this.ctx.storage.sql.exec<Lobby>("SELECT * FROM lobbies WHERE code=?", code).toArray()[0];
  }

  async create(input: unknown): Promise<Response> {
    const x = input as Record<string, unknown>;
    if (!x || !validName(x.name) || !["public", "private"].includes(String(x.visibility)) || !validInt(x.version, 1, 65535) || !validInt(x.stage, 0, 255) || !validInt(x.weapons, 0, 255) || !validInt(x.maxPlayers, 2, MAX_PLAYERS)) return bad("Invalid lobby settings");
    this.cleanup();
    let code: string;
    do { code = codeValue(); } while (this.lobby(code));
    const ownerToken = crypto.randomUUID() + crypto.randomUUID();
    const now = Date.now();
    this.ctx.storage.sql.exec(
      "INSERT INTO lobbies(code,owner_hash,name,visibility,version,stage,weapons,players,max_players,open,expires,phase,created_at,phase_changed_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
      code, await digest(ownerToken), x.name, x.visibility, x.version, x.stage, x.weapons, 1, x.maxPlayers, 1, now + TTL, "waiting", now, now
    );
    this.bump(x.visibility === "private" ? "lobbies_private" : "lobbies_public", now);
    this.samplePeaks(now);
    return json({ code, ownerToken, ttlSeconds: TTL / 1000 }, 201);
  }

  async update(code: string, token: string, input: unknown): Promise<Response> {
    // Authenticate before cleanup so the owner can receive the timeout reason.
    const ownerHash = await digest(token);
    const lobby = this.ctx.storage.sql.exec<Lobby>("SELECT * FROM lobbies WHERE code=?", code).toArray()[0];
    if (!lobby || lobby.owner_hash !== ownerHash) return bad("Lobby unavailable", 404);
    const now = Date.now();
    if (lobby.phase !== "waiting" && lobby.players === 1 && lobby.phase_changed_at > 0 && now - lobby.phase_changed_at > ALONE_IDLE_TIMEOUT) {
      this.ctx.storage.sql.exec("DELETE FROM lobbies WHERE code=?", code);
      this.ctx.storage.sql.exec("DELETE FROM joins WHERE code=?", code);
      return bad("Lobby idle timeout", 410);
    }
    if (lobby.created_at > 0) {
      if (lobby.phase === "waiting" && lobby.players === 1 && now - lobby.created_at > WAITING_IDLE_TIMEOUT) {
        this.ctx.storage.sql.exec("DELETE FROM lobbies WHERE code=?", code);
        this.ctx.storage.sql.exec("DELETE FROM joins WHERE code=?", code);
        return bad("Lobby idle timeout", 410);
      }
      if (now - lobby.created_at > MAX_LOBBY_LIFESPAN) {
        this.ctx.storage.sql.exec("DELETE FROM lobbies WHERE code=?", code);
        this.ctx.storage.sql.exec("DELETE FROM joins WHERE code=?", code);
        return bad("Lobby lifetime expired", 410);
      }
    }
    if (lobby.expires < now) {
      this.ctx.storage.sql.exec("DELETE FROM lobbies WHERE code=?", code);
      this.ctx.storage.sql.exec("DELETE FROM joins WHERE code=?", code);
      return bad("Lobby unavailable", 404);
    }
    this.cleanup(now);
    const x = input as Record<string, unknown>;
    // The host may change the player count after registering (any stage takes 2..8).
    // Clients before protocol 16 do not send it and keep the count they created with.
    if (x && x.maxPlayers !== undefined && !validInt(x.maxPlayers, 2, MAX_PLAYERS)) return bad("Invalid lobby state");
    const maxPlayers = x && x.maxPlayers !== undefined ? Number(x.maxPlayers) : lobby.max_players;
    if (!x || !validInt(x.players, 1, maxPlayers) || typeof x.open !== "boolean" ||
        (x.name !== undefined && !validName(x.name)) ||
        (x.phase !== undefined && !["waiting", "warmup", "in_progress"].includes(String(x.phase)))) return bad("Invalid lobby state");
    const phase = (x.phase || lobby.phase) as Phase;
    if (phase === "in_progress" && Number(x.players) < 2) return bad("An active match needs two players");
    const phaseChangedAt = (phase !== lobby.phase) ? now : (lobby.phase_changed_at || now);
    const createdAt = lobby.created_at || (lobby.expires - TTL);
    this.ctx.storage.sql.exec(
      "UPDATE lobbies SET players=?,open=?,phase=?,expires=?,created_at=?,phase_changed_at=?,name=?,max_players=? WHERE code=?",
      x.players, x.open ? 1 : 0, phase, now + TTL, createdAt, phaseChangedAt, x.name ?? lobby.name, maxPlayers, code
    );
    if (phase === "in_progress" && lobby.phase !== "in_progress") {
      this.bump("matches_started", now);
      this.bump("stage:" + lobby.stage, now);
    }
    this.samplePeaks(now);
    return json({ ok: true });
  }

  async remove(code: string, token: string): Promise<Response> {
    const lobby = this.lobby(code);
    if (!lobby || lobby.owner_hash !== await digest(token)) return bad("Lobby unavailable", 404);
    this.ctx.storage.sql.exec("DELETE FROM lobbies WHERE code=?", code);
    this.ctx.storage.sql.exec("DELETE FROM joins WHERE code=?", code);
    return json({ ok: true });
  }

  async list(version: number): Promise<Response> {
    this.cleanup();
    const rows = this.ctx.storage.sql.exec<Lobby>("SELECT * FROM lobbies WHERE visibility='public' AND version=? AND open=1 AND players<max_players ORDER BY expires DESC LIMIT 64", version).toArray();
    return json({ lobbies: rows.map(({ code, name, stage, weapons, players, max_players, phase }) => ({ code, name, stage, weapons, players, maxPlayers: max_players, phase })) });
  }

  async activity(): Promise<Response> {
    this.cleanup();
    const rows = this.ctx.storage.sql.exec<Lobby>("SELECT code,name,visibility,version,stage,weapons,players,max_players,open,phase,created_at,phase_changed_at,expires FROM lobbies ORDER BY expires DESC").toArray();
    const counts = { public: 0, private: 0, waiting: 0, warmup: 0, inProgress: 0, players: 0 };
    for (const row of rows) {
      counts[row.visibility]++;
      if (row.visibility === "private") continue;
      if (row.phase === "in_progress") counts.inProgress++;
      else counts[row.phase]++;
      counts.players += row.players;
    }
    return json({ updatedAt: new Date().toISOString(), counts,
      lobbies: rows.filter(row => row.visibility === "public").slice(0, 64).map(row => ({
        code: row.code, name: row.name, version: row.version, stage: row.stage,
        weapons: row.weapons, players: row.players, maxPlayers: row.max_players,
        phase: row.phase, joinable: !!row.open && row.players < row.max_players,
        createdAt: row.created_at || (row.expires - TTL),
        phaseChangedAt: row.phase_changed_at || (row.expires - TTL)
      })) }, 200, { "access-control-allow-origin": "*" });
  }

  async resolve(code: string, version: number): Promise<Response> {
    const lobby = this.lobby(code);
    if (!lobby || lobby.version !== version || !lobby.open || lobby.players >= lobby.max_players) return bad("Lobby unavailable", 404);
    return json({ code, name: lobby.name, stage: lobby.stage, weapons: lobby.weapons, players: lobby.players, maxPlayers: lobby.max_players });
  }

  async join(code: string, version: number): Promise<Response> {
    const lobby = this.lobby(code);
    if (!lobby || lobby.version !== version || !lobby.open || lobby.players >= lobby.max_players) return bad("Lobby unavailable", 404);
    const count = this.ctx.storage.sql.exec<{ n: number }>("SELECT COUNT(*) AS n FROM joins WHERE code=?", code).one().n;
    if (count >= MAX_PENDING_JOINS) return bad("Lobby is busy", 429);
    const id = crypto.randomUUID();
    const joinToken = crypto.randomUUID() + crypto.randomUUID();
    this.ctx.storage.sql.exec("INSERT INTO joins VALUES(?,?,?,?,?,?)", id, code, await digest(joinToken), null, null, Date.now() + 90_000);
    this.bump("join_attempts");
    return json({ id, joinToken }, 201);
  }

  private async authorized(code: string, id: string, token: string, owner: boolean): Promise<Join | undefined> {
    const join = this.ctx.storage.sql.exec<Join>("SELECT * FROM joins WHERE id=? AND code=? AND expires>?", id, code, Date.now()).toArray()[0];
    if (!join) return undefined;
    if (owner) return this.lobby(code)?.owner_hash === await digest(token) ? join : undefined;
    return join.token_hash === await digest(token) ? join : undefined;
  }

  async offer(code: string, id: string, token: string, sdp: unknown): Promise<Response> {
    if (!await this.authorized(code, id, token, false)) return bad("Join unavailable", 404);
    if (!validSdp(sdp)) return bad("Invalid offer");
    this.ctx.storage.sql.exec("UPDATE joins SET offer=? WHERE id=?", sdp, id);
    return json({ ok: true });
  }

  async requests(code: string, token: string): Promise<Response> {
    if (this.lobby(code)?.owner_hash !== await digest(token)) return bad("Lobby unavailable", 404);
    const rows = this.ctx.storage.sql.exec<Join>("SELECT * FROM joins WHERE code=? AND offer IS NOT NULL AND answer IS NULL AND expires>? LIMIT ?", code, Date.now(), MAX_PENDING_JOINS).toArray();
    return json({ requests: rows.map(({ id, offer }) => ({ id, offer })) });
  }

  async answer(code: string, id: string, token: string, sdp: unknown): Promise<Response> {
    const join = await this.authorized(code, id, token, true);
    if (!join) return bad("Join unavailable", 404);
    if (!validSdp(sdp)) return bad("Invalid answer");
    this.ctx.storage.sql.exec("UPDATE joins SET answer=? WHERE id=?", sdp, id);
    if (join.answer === null) this.bump("joins_connected");
    return json({ ok: true });
  }

  async pollAnswer(code: string, id: string, token: string): Promise<Response> {
    const join = await this.authorized(code, id, token, false);
    if (!join) return bad("Join unavailable", 404);
    return json({ answer: join.answer });
  }

  async mayIssueTurn(code: string, id: string | null, token: string): Promise<boolean> {
    if (id) return !!await this.authorized(code, id, token, false);
    return this.lobby(code)?.owner_hash === await digest(token);
  }
}

async function turnCredentials(env: Env): Promise<Response> {
  if (!env.TURN_KEY_ID || !env.TURN_KEY_API_TOKEN) return bad("Relay is not configured", 503);
  const response = await fetch(`https://rtc.live.cloudflare.com/v1/turn/keys/${encodeURIComponent(env.TURN_KEY_ID)}/credentials/generate-ice-servers`, {
    method: "POST", headers: { authorization: `Bearer ${env.TURN_KEY_API_TOKEN}`, "content-type": "application/json" }, body: JSON.stringify({ ttl: 86400 })
  });
  if (!response.ok) return bad("Relay temporarily unavailable", 503);
  const data = await response.json() as { iceServers?: Array<{ username?: string; credential?: string }> };
  const turn = data.iceServers?.find(x => x.username && x.credential);
  if (!turn) return bad("Relay temporarily unavailable", 503);
  return json({ host: "turn.cloudflare.com", port: 3478, username: turn.username, credential: turn.credential });
}

async function boundedText(request: Request, max: number): Promise<string> {
  if (!request.body) throw new Error("Empty request");
  const reader = request.body.getReader();
  const chunks: Uint8Array[] = [];
  let size = 0;
  try {
    for (;;) {
      const { value, done } = await reader.read();
      if (done) break;
      size += value.byteLength;
      if (size > max) throw new Error("Report too large");
      chunks.push(value);
    }
  } finally { reader.releaseLock(); }
  const bytes = new Uint8Array(size);
  let offset = 0;
  for (const chunk of chunks) { bytes.set(chunk, offset); offset += chunk.length; }
  return new TextDecoder().decode(bytes);
}

function decodeBase64(value: unknown, maxBytes: number): Uint8Array {
  if (typeof value !== "string" || value.length > Math.ceil(maxBytes / 3) * 4 + 4 || !/^[A-Za-z0-9+/]*={0,2}$/.test(value))
    throw new Error("Invalid attachment");
  const raw = atob(value);
  if (raw.length > maxBytes) throw new Error("Attachment too large");
  return Uint8Array.from(raw, c => c.charCodeAt(0));
}

async function decompressBounded(gzip: Uint8Array, max: number): Promise<Uint8Array> {
  const stream = new Blob([gzip]).stream().pipeThrough(new DecompressionStream("gzip"));
  const reader = stream.getReader();
  const parts: Uint8Array[] = [];
  let size = 0;
  try {
    for (;;) {
      const { value, done } = await reader.read();
      if (done) break;
      size += value.length;
      if (size > max) throw new Error("Log too large");
      parts.push(value);
    }
  } finally { await reader.cancel().catch(() => {}); }
  const result = new Uint8Array(size);
  let offset = 0;
  for (const part of parts) { result.set(part, offset); offset += part.length; }
  return result;
}

async function report(request: Request, env: Env, registry: DurableObjectStub<LobbyRegistry>, ip: string): Promise<Response> {
  if (!await registry.limit(ip, "report", 3, 3_600_000)
      || !await registry.limit("all", "report-day", 100, 86_400_000)) return bad("Report limit reached", 429);
  let input: Record<string, unknown>;
  try { input = JSON.parse(await boundedText(request, 4_200_000)) as Record<string, unknown>; }
  catch { return bad("Invalid or oversized report"); }
  const field = (name: string, max: number) => typeof input?.[name] === "string" && (input[name] as string).length <= max;
  if (!input || !["manual", "crash"].includes(String(input.kind)) || !field("version", 40)
      || !field("build", 64) || !field("device", 120) || !field("player", 64) || !field("note", 500)
      || !field("crash_summary", 500))
    return bad("Invalid report details");
  let gzip: Uint8Array, tombstone: Uint8Array, plain: Uint8Array;
  try {
    gzip = decodeBase64(input.log_gz_b64, 2_500_000);
    tombstone = decodeBase64(input.tombstone_b64, 1_000_000);
    plain = await decompressBounded(gzip, 3_000_000);
  } catch { return bad("Invalid report attachments"); }
  // The client redacts before compression; enforce the same text boundary on the server.
  const log = new TextDecoder().decode(plain)
    .replace(/(?<![0-9.])(?!127\.)\d{1,3}(?:\.\d{1,3}){3}(?![0-9.])/g, "[redacted address]")
    .replace(/^.*(?:lobbyCommand|launcher: lobbyCommand).*(?:offer|answer)\|.*$/gim, "[redacted signaling]");
  const cleaned = new TextEncoder().encode(log);
  const id = crypto.randomUUID().slice(0, 8);
  const safe = (value: unknown) => String(value).replace(/[\r\n\t]/g, " ");
  const title = `[GEVR ${input.kind}] v${safe(input.version)} build ${safe(input.build)} - ${safe(input.player)} - ${id}`;
  const attachments: Array<{ content: string; filename: string; type: string; disposition: "attachment" }> = [];
  const encode = (bytes: Uint8Array) => {
    let out = "";
    for (let i = 0; i < bytes.length; i += 8192)
      out += String.fromCharCode(...bytes.subarray(i, i + 8192));
    return btoa(out);
  };
  if (cleaned.length <= 750_000) {
    attachments.push({ content: encode(cleaned), filename: `gevr-${id}.txt`, type: "text/plain", disposition: "attachment" });
  } else {
    // Recompress after the server's redaction pass.
    const compressed = await new Response(new Blob([cleaned]).stream().pipeThrough(new CompressionStream("gzip"))).arrayBuffer();
    attachments.push({ content: encode(new Uint8Array(compressed)), filename: `gevr-${id}.txt.gz`, type: "application/gzip", disposition: "attachment" });
  }
  if (tombstone.length) attachments.push({ content: encode(tombstone), filename: `tombstone-${id}.pb`, type: "application/octet-stream", disposition: "attachment" });
  try {
    await env.EMAIL.send({
      from: "reports@goldeneyevr.com", to: env.REPORT_TO, subject: title,
      text: `Report: ${id}\nKind: ${safe(input.kind)}\nVersion: ${safe(input.version)}\nBuild: ${safe(input.build)}\nDevice: ${safe(input.device)}\nPlayer: ${safe(input.player)}\nNote: ${safe(input.note)}\nCrash: ${safe(input.crash_summary)}\nTombstone: ${tombstone.length ? "attached protobuf" : "unavailable"}`,
      attachments
    });
  } catch (error) {
    console.error(JSON.stringify({ event: "report_email_error", code: (error as { code?: string }).code, message: String(error) }));
    return bad("Report delivery unavailable", 503);
  }
  await registry.record("reports");
  return json({ ok: true, id }, 201);
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    try {
      if (request.method === "OPTIONS") {
        return new Response(null, {
          status: 204,
          headers: {
            "access-control-allow-origin": "*",
            "access-control-allow-methods": "GET, POST, PUT, DELETE, OPTIONS",
            "access-control-allow-headers": "Content-Type, Authorization",
            "access-control-max-age": "86400",
          }
        });
      }
      const url = new URL(request.url);
      const path = url.pathname.split("/").filter(Boolean);
      if (path[0] !== "v1") return bad("Not found", 404);
      const registry = env.REGISTRY.getByName("global-v1");
      const ip = request.headers.get("CF-Connecting-IP") || "unknown";
      if (path.length === 2 && path[1] === "reports" && request.method === "POST")
        return report(request, env, registry, ip);
      const action = request.method === "GET" ? "read" : "write";
      if (!await registry.limit(ip, action, action === "read" ? 120 : 40)) return bad("Too many requests", 429);
      const token = request.headers.get("Authorization")?.replace(/^Bearer /i, "") || "";
      const body = request.method === "GET" || request.method === "DELETE" ? null : await request.json().catch(() => null);
      if (path.length === 2 && path[1] === "activity" && request.method === "GET") return registry.activity();
      if (path.length === 2 && path[1] === "stats" && request.method === "GET") return registry.publicStats();
      if (path[1] !== "lobbies") return bad("Not found", 404);
      if (path.length === 2 && request.method === "POST") return registry.create(body);
      if (path.length === 2 && request.method === "GET") return registry.list(Number(url.searchParams.get("version")));
      const code = path[2];
      if (!code || !/^[A-Z2-9]{8}$/.test(code)) return bad("Invalid code");
      if (path.length === 3 && request.method === "GET") return registry.resolve(code, Number(url.searchParams.get("version")));
      if (path.length === 3 && request.method === "PUT") return registry.update(code, token, body);
      if (path.length === 3 && request.method === "DELETE") return registry.remove(code, token);
      if (path[3] === "turn" && path.length === 4 && request.method === "POST") {
        const id = typeof (body as Record<string, unknown>)?.id === "string" ? String((body as Record<string, unknown>).id) : null;
        if (!await registry.mayIssueTurn(code, id, token)) return bad("Not authorized", 403);
        if (!await registry.limit(ip, "turn-hour", 120, 3_600_000))
          return bad("Too many relay requests; try again later", 429);
        const cap = turnMonthlyCap(env);
        if (cap > 0 && !await registry.limit("global", "turn-month", cap, msUntilNextUtcMonth())) {
          await registry.record("turn_refused");
          return bad("Monthly relay budget used; direct connections only", 503);
        }
        const credentials = await turnCredentials(env);
        if (credentials.ok) await registry.record("turn_issued");
        return credentials;
      }
      if (path[3] === "joins" && path.length === 4 && request.method === "POST") return registry.join(code, Number((body as Record<string, unknown>)?.version));
      if (path[3] === "joins" && path.length === 4 && request.method === "GET") return registry.requests(code, token);
      if (path[3] === "joins" && path[4] && path[5] === "offer" && request.method === "PUT") return registry.offer(code, path[4], token, (body as Record<string, unknown>)?.sdp);
      if (path[3] === "joins" && path[4] && path[5] === "answer" && request.method === "PUT") return registry.answer(code, path[4], token, (body as Record<string, unknown>)?.sdp);
      if (path[3] === "joins" && path[4] && path[5] === "answer" && request.method === "GET") return registry.pollAnswer(code, path[4], token);
      return bad("Not found", 404);
    } catch (error) {
      console.error(JSON.stringify({ event: "lobby_error", message: String(error) }));
      return bad("Service unavailable", 503);
    }
  },

  async scheduled(controller: ScheduledController, env: Env): Promise<void> {
    await runReport(env, env.REGISTRY.getByName("global-v1"), new Date(controller.scheduledTime), turnMonthlyCap(env));
  }
} satisfies ExportedHandler<Env>;
