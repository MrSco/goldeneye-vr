import { DurableObject } from "cloudflare:workers";

interface Env {
  REGISTRY: DurableObjectNamespace<LobbyRegistry>;
  TURN_KEY_ID: string;
  TURN_KEY_API_TOKEN: string;
}

type Visibility = "public" | "private";
type Phase = "waiting" | "warmup" | "in_progress";
type Lobby = {
  code: string; owner_hash: string; name: string; visibility: Visibility;
  version: number; stage: number; weapons: number; players: number;
  max_players: number; open: number; expires: number; phase: Phase;
};
type Join = { id: string; code: string; token_hash: string; offer: string | null; answer: string | null; expires: number };

const TTL = 45_000;
const ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
const json = (value: unknown, status = 200) => Response.json(value, { status, headers: { "cache-control": "no-store" } });
const bad = (message: string, status = 400) => json({ error: message }, status);
const codeValue = () => Array.from(crypto.getRandomValues(new Uint8Array(8)), n => ALPHABET[n & 31]).join("");
const digest = async (value: string) => Array.from(new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(value))), b => b.toString(16).padStart(2, "0")).join("");
const validInt = (value: unknown, min: number, max: number) => Number.isInteger(value) && Number(value) >= min && Number(value) <= max;
const validSdp = (value: unknown) => typeof value === "string" && value.length > 0 && value.length <= 4096 && value.startsWith("a=ice-ufrag:");

export class LobbyRegistry extends DurableObject<Env> {
  constructor(ctx: DurableObjectState, env: Env) {
    super(ctx, env);
    ctx.blockConcurrencyWhile(async () => {
      this.ctx.storage.sql.exec("CREATE TABLE IF NOT EXISTS lobbies (code TEXT PRIMARY KEY, owner_hash TEXT NOT NULL, name TEXT NOT NULL, visibility TEXT NOT NULL, version INTEGER NOT NULL, stage INTEGER NOT NULL, weapons INTEGER NOT NULL, players INTEGER NOT NULL, max_players INTEGER NOT NULL, open INTEGER NOT NULL, expires INTEGER NOT NULL)");
      const columns = this.ctx.storage.sql.exec<{ name: string }>("PRAGMA table_info(lobbies)").toArray();
      if (!columns.some(column => column.name === "phase"))
        this.ctx.storage.sql.exec("ALTER TABLE lobbies ADD COLUMN phase TEXT NOT NULL DEFAULT 'waiting'");
      this.ctx.storage.sql.exec("CREATE TABLE IF NOT EXISTS joins (id TEXT PRIMARY KEY, code TEXT NOT NULL, token_hash TEXT NOT NULL, offer TEXT, answer TEXT, expires INTEGER NOT NULL)");
      this.ctx.storage.sql.exec("CREATE TABLE IF NOT EXISTS limits (key TEXT PRIMARY KEY, count INTEGER NOT NULL, reset INTEGER NOT NULL)");
      this.ctx.storage.sql.exec("CREATE INDEX IF NOT EXISTS lobbies_expires ON lobbies(expires)");
      this.ctx.storage.sql.exec("CREATE INDEX IF NOT EXISTS joins_code ON joins(code)");
    });
  }

  private cleanup() {
    const now = Date.now();
    this.ctx.storage.sql.exec("DELETE FROM lobbies WHERE expires < ?", now);
    this.ctx.storage.sql.exec("DELETE FROM joins WHERE expires < ? OR code NOT IN (SELECT code FROM lobbies)", now);
    this.ctx.storage.sql.exec("DELETE FROM limits WHERE reset < ?", now);
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
    if (!x || typeof x.name !== "string" || x.name.length < 1 || x.name.length > 32 || !["public", "private"].includes(String(x.visibility)) || !validInt(x.version, 1, 65535) || !validInt(x.stage, 0, 255) || !validInt(x.weapons, 0, 255) || !validInt(x.maxPlayers, 2, 4)) return bad("Invalid lobby settings");
    this.cleanup();
    let code: string;
    do { code = codeValue(); } while (this.lobby(code));
    const ownerToken = crypto.randomUUID() + crypto.randomUUID();
    this.ctx.storage.sql.exec("INSERT INTO lobbies(code,owner_hash,name,visibility,version,stage,weapons,players,max_players,open,expires,phase) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)", code, await digest(ownerToken), x.name, x.visibility, x.version, x.stage, x.weapons, 1, x.maxPlayers, 1, Date.now() + TTL, "waiting");
    return json({ code, ownerToken, ttlSeconds: TTL / 1000 }, 201);
  }

  async update(code: string, token: string, input: unknown): Promise<Response> {
    const lobby = this.lobby(code);
    if (!lobby || lobby.owner_hash !== await digest(token)) return bad("Lobby unavailable", 404);
    const x = input as Record<string, unknown>;
    if (!x || !validInt(x.players, 1, lobby.max_players) || typeof x.open !== "boolean" ||
        (x.phase !== undefined && !["waiting", "warmup", "in_progress"].includes(String(x.phase)))) return bad("Invalid lobby state");
    const phase = (x.phase || lobby.phase) as Phase;
    if (phase === "in_progress" && Number(x.players) < 2) return bad("An active match needs two players");
    this.ctx.storage.sql.exec("UPDATE lobbies SET players=?,open=?,phase=?,expires=? WHERE code=?", x.players, x.open ? 1 : 0, phase, Date.now() + TTL, code);
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
    const rows = this.ctx.storage.sql.exec<Lobby>("SELECT code,name,visibility,version,stage,weapons,players,max_players,open,phase FROM lobbies ORDER BY expires DESC").toArray();
    const counts = { public: 0, private: 0, waiting: 0, warmup: 0, inProgress: 0, players: 0 };
    for (const row of rows) {
      counts[row.visibility]++;
      if (row.phase === "in_progress") counts.inProgress++;
      else counts[row.phase]++;
      counts.players += row.players;
    }
    return json({ updatedAt: new Date().toISOString(), counts,
      lobbies: rows.filter(row => row.visibility === "public").slice(0, 64).map(row => ({
        code: row.code, name: row.name, version: row.version, stage: row.stage,
        weapons: row.weapons, players: row.players, maxPlayers: row.max_players,
        phase: row.phase, joinable: !!row.open && row.players < row.max_players
      })) });
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
    if (count >= 6) return bad("Lobby is busy", 429);
    const id = crypto.randomUUID();
    const joinToken = crypto.randomUUID() + crypto.randomUUID();
    this.ctx.storage.sql.exec("INSERT INTO joins VALUES(?,?,?,?,?,?)", id, code, await digest(joinToken), null, null, Date.now() + 90_000);
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
    const rows = this.ctx.storage.sql.exec<Join>("SELECT * FROM joins WHERE code=? AND offer IS NOT NULL AND answer IS NULL AND expires>? LIMIT 6", code, Date.now()).toArray();
    return json({ requests: rows.map(({ id, offer }) => ({ id, offer })) });
  }

  async answer(code: string, id: string, token: string, sdp: unknown): Promise<Response> {
    if (!await this.authorized(code, id, token, true)) return bad("Join unavailable", 404);
    if (!validSdp(sdp)) return bad("Invalid answer");
    this.ctx.storage.sql.exec("UPDATE joins SET answer=? WHERE id=?", sdp, id);
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

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    try {
      const url = new URL(request.url);
      const path = url.pathname.split("/").filter(Boolean);
      if (path[0] !== "v1") return bad("Not found", 404);
      const registry = env.REGISTRY.getByName("global-v1");
      const ip = request.headers.get("CF-Connecting-IP") || "unknown";
      const action = request.method === "GET" ? "read" : "write";
      if (!await registry.limit(ip, action, action === "read" ? 120 : 40)) return bad("Too many requests", 429);
      const token = request.headers.get("Authorization")?.replace(/^Bearer /i, "") || "";
      const body = request.method === "GET" || request.method === "DELETE" ? null : await request.json().catch(() => null);
      if (path.length === 2 && path[1] === "activity" && request.method === "GET") return registry.activity();
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
        if (!await registry.limit(ip, "turn-day", 24, 86_400_000) || !await registry.limit("all", "turn-day", 500, 86_400_000))
          return bad("Relay capacity reached; try again later", 429);
        return turnCredentials(env);
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
  }
} satisfies ExportedHandler<Env>;
