import assert from "node:assert/strict";
import { after, before, test } from "node:test";
import { fileURLToPath } from "node:url";
import { build } from "esbuild";
import { Miniflare, convertV4MiniflareOptions } from "miniflare";

const bundle = async (entry) => (await build({
  entryPoints: [fileURLToPath(new URL(entry, import.meta.url))],
  bundle: true, write: false, format: "esm", platform: "neutral", external: ["cloudflare:workers"],
})).outputFiles[0].text;
const report = await import("data:text/javascript;base64," + Buffer.from(await bundle("../src/report.ts")).toString("base64"));

const limits = { turnFreeGb: 1000, turnCap: 4000, workersDaily: 100_000, durableObjectsDaily: 100_000 };
const day = (analytics = {}, lobby = {}) => ({
  from: "2026-10-02", to: "2026-10-02",
  analytics: {
    errors: [],
    website: { requests: 9470, bytes: 97_120_000, cachedBytes: 26_100_000, uniques: 274, status4xx: 390, status5xx: 0, countries: [{ name: "US", requests: 4100 }, { name: "GB", requests: 900 }] },
    workers: { lobbyRequests: 6660, lobbyErrors: 0, lobbySubrequests: 13_410, lobbyCpuP50Us: 998, accountRequests: 6790 },
    durableObjectRequests: 13_320,
    turn: { egressBytes: 158_364, ingressBytes: 856_524 },
    ...analytics,
  },
  lobby: { lobbies_public: 9, lobbies_private: 3, join_attempts: 30, joins_connected: 27, matches_started: 8, "stage:34": 5, "stage:32": 3, peak_lobbies: 3, peak_players: 7, reports: 1, turn_issued: 30, ...lobby },
});
const month = (egressBytes, extra = {}) => ({ period: "2026-10", turn: { egressBytes, ingressBytes: 0 }, turnCredentials: 34, elapsed: 0.1, projectedEgressBytes: egressBytes / 0.1, ...extra });
const levels = alerts => Object.fromEntries(alerts.map(a => [a.metric, a.level]));

test("lobby totals add counters and keep the busiest day's peaks", () => {
  assert.deepEqual(report.lobbyTotals([
    { day: "2026-10-01", key: "lobbies_public", value: 2 }, { day: "2026-10-02", key: "lobbies_public", value: 3 },
    { day: "2026-10-01", key: "peak_players", value: 6 }, { day: "2026-10-02", key: "peak_players", value: 4 },
  ]), { lobbies_public: 5, peak_players: 6 });
});

test("thresholds fire at 50, 80 and 100 percent and not below", () => {
  assert.deepEqual(levels(report.checkThresholds(day(), month(490e9), limits)), {});
  assert.deepEqual(levels(report.checkThresholds(day(), month(500e9), limits)), { turn_egress: 50 });
  assert.deepEqual(levels(report.checkThresholds(day(), month(800e9), limits)), { turn_egress: 80 });
  assert.deepEqual(levels(report.checkThresholds(day(), month(1000e9), limits)), { turn_egress: 100 });
  const busy = day({ workers: { lobbyRequests: 0, lobbyErrors: 0, lobbySubrequests: 0, lobbyCpuP50Us: 0, accountRequests: 85_000 }, durableObjectRequests: 51_000 });
  assert.deepEqual(levels(report.checkThresholds(busy, month(0, { turnCredentials: 3999 }), limits)),
    { turn_credentials: 80, worker_requests: 80, do_requests: 50 });
  const failing = day({ website: { requests: 1000, bytes: 0, cachedBytes: 0, uniques: 0, status4xx: 0, status5xx: 20, countries: [] } });
  assert.match(report.checkThresholds(failing, month(0), limits)[0].message, /Website 5xx rate 2\.0%/);
});

test("the month-end projection alerts only once a fifth of the month has passed", () => {
  assert.deepEqual(levels(report.checkThresholds(day(), month(100e9, { elapsed: 0.1, projectedEgressBytes: 1000e9 }), limits)), {});
  assert.deepEqual(levels(report.checkThresholds(day(), month(300e9, { elapsed: 0.25, projectedEgressBytes: 1200e9 }), limits)), { turn_projection: 100 });
});

test("the daily report reads cleanly", () => {
  const m = month(2_923_200, { elapsed: 0.08, projectedEgressBytes: 36_540_000 });
  const { subject, text } = report.buildReport({ day: day(), month: m, limits, alerts: [], newAlerts: [] });
  assert.equal(subject, "[GEVR daily] Fri 2 Oct 2026: 12 lobbies, 8 matches");
  for (const line of [
    "  Lobbies hosted          12 (9 public, 3 private)",
    "  Most played             Facility (5)",
    "  Peak lobbies / players  3 / 7",
    "  Data transfer           97.1 MB (27% cached)",
    "  Top countries           US 4,100 · GB 900",
    "  Median CPU              1.0 ms",
    "  Egress / ingress        158 KB / 857 KB",
    "  TURN egress             2.9 MB of 1,000 GB free (0.0003%)",
    "  TURN credentials        34 of 4,000 (0.9%)",
    "  None. Everything is under 50% of its limit.",
  ]) assert.ok(text.includes(line), `missing: ${line}\n---\n${text}`);
  assert.ok(!text.includes("LAST 7 DAYS"));
  assert.ok(!text.includes("UNAVAILABLE"));
});

test("Mondays add a week-over-week section; missing analytics are reported, not fatal", () => {
  const current = { ...day(), from: "2026-09-28", to: "2026-10-04" };
  const previous = { ...day({}, { lobbies_public: 6, lobbies_private: 2 }), from: "2026-09-21", to: "2026-09-27" };
  const broken = day({ website: undefined, errors: ["Website: HTTP 403"] });
  const alert = { metric: "turn_egress", period: "2026-10", level: 80, message: "TURN egress at 80% of free tier (800 GB of 1000 GB this month)" };
  const { subject, text } = report.buildReport({ day: broken, week: { current, previous }, month: month(800e9), limits, alerts: [alert], newAlerts: [alert] });
  assert.equal(subject, "[GEVR weekly] Fri 2 Oct 2026: 12 lobbies, 8 matches, 1 alert");
  assert.ok(text.includes("LAST 7 DAYS (2026-09-28 to 2026-10-04) vs the 7 days before"));
  assert.ok(text.includes("  Lobbies hosted          12  (+50% vs 8)"), text);
  assert.ok(text.includes("  NEW TURN egress at 80% of free tier"));
  assert.ok(text.includes("WEBSITE (goldeneyevr.com)\n  unavailable"));
  assert.ok(text.includes("CLOUDFLARE ANALYTICS UNAVAILABLE\n  Website: HTTP 403"));
});

// End to end: the cron handler in workerd, with GraphQL mocked and EMAIL captured over RPC.
let runtime, worker;
const emails = [];
const graphql = [];
let turnEgress = 0;
let analyticsDown = false;

function graphqlResponse(query) {
  if (query.includes("httpRequests1dGroups")) return { viewer: { zones: [{ httpRequests1dGroups: [{
    sum: { requests: 9470, bytes: 97_120_000, cachedBytes: 26_100_000, responseStatusMap: [{ edgeResponseStatus: 200, requests: 9080 }, { edgeResponseStatus: 404, requests: 390 }], countryMap: [{ clientCountryName: "US", requests: 4100 }] },
    uniq: { uniques: 274 } }] }] } };
  if (query.includes("workersInvocationsAdaptive")) return { viewer: { accounts: [{ workersInvocationsAdaptive: [
    { sum: { requests: 6660, errors: 0, subrequests: 13_410 }, quantiles: { cpuTimeP50: 998 }, dimensions: { scriptName: "gevr-lobbies" } },
    { sum: { requests: 130, errors: 0, subrequests: 0 }, quantiles: { cpuTimeP50: 500 }, dimensions: { scriptName: "other" } }] }] } };
  if (query.includes("durableObjectsInvocationsAdaptiveGroups")) return { viewer: { accounts: [{ durableObjectsInvocationsAdaptiveGroups: [{ sum: { requests: 13_320 } }] }] } };
  if (query.includes("callsTurnUsageAdaptiveGroups")) return { viewer: { accounts: [{ callsTurnUsageAdaptiveGroups: [{ sum: { egressBytes: turnEgress, ingressBytes: 1000 } }] }] } };
  throw new Error("unexpected query " + query);
}

before(async () => {
  runtime = new Miniflare(convertV4MiniflareOptions({
    workers: [{
      name: "lobby-report-test", modules: true, script: await bundle("../src/index.ts"), compatibilityDate: "2026-09-27",
      durableObjects: { REGISTRY: { className: "LobbyRegistry", useSQLite: true } },
      bindings: { REPORT_TO: "info@example.com", CF_ACCOUNT_ID: "acct", CF_ZONE_ID: "zone", CF_ANALYTICS_TOKEN: "token", TURN_MONTHLY_CAP: "4000" },
      serviceBindings: { EMAIL: { name: "email-capture", entrypoint: "Email" } },
      outboundService: async request => {
        const body = await request.json();
        graphql.push({ url: request.url, authorization: request.headers.get("authorization"), query: body.query, variables: body.variables });
        if (analyticsDown) return new Response("down", { status: 502 });
        return Response.json({ data: graphqlResponse(body.query), errors: null });
      },
    }, {
      name: "email-capture", modules: true, compatibilityDate: "2026-09-27",
      script: `import { WorkerEntrypoint } from "cloudflare:workers";
        export class Email extends WorkerEntrypoint {
          async send(message) {
            await this.env.SINK.fetch("http://sink/", { method: "POST", body: JSON.stringify(message) });
            return { messageId: "test" };
          }
        }
        export default { fetch() { return new Response("ok"); } };`,
      serviceBindings: { SINK: async request => { emails.push(await request.json()); return new Response("ok"); } },
    }],
  }));
  worker = await runtime.getWorker("lobby-report-test");
});
after(async () => { await runtime?.dispose(); });

// Stats are recorded on today's date; the report covers the day before its run, so run "tomorrow".
const tomorrowAt14 = () => { const d = new Date(); return new Date(Date.UTC(d.getUTCFullYear(), d.getUTCMonth(), d.getUTCDate() + 1, 14)); };
const run = async () => {
  emails.length = 0;
  const scheduledTime = tomorrowAt14();
  await worker.scheduled({ scheduledTime, cron: "0 14 * * *" });
  return [...emails];
};

test("the cron run emails a report with lobby counters and Cloudflare figures", async () => {
  const created = await worker.fetch("http://localhost/v1/lobbies", {
    method: "POST", headers: { "content-type": "application/json", "cf-connecting-ip": "host" },
    body: JSON.stringify({ name: "A's game", visibility: "public", version: 6, stage: 34, weapons: 2, maxPlayers: 4 }),
  });
  assert.equal(created.status, 201);
  turnEgress = 158_364;
  const sent = await run();
  assert.equal(sent.length, 1, "no alert below 50%");
  const [digest] = sent;
  assert.equal(digest.from, "reports@goldeneyevr.com");
  assert.equal(digest.to, "info@example.com");
  assert.match(digest.subject, /^\[GEVR (daily|weekly)\] \w{3} \d{1,2} \w{3} \d{4}: 1 lobby, 0 matches$/);
  assert.match(digest.text, /Lobbies hosted {10}1 \(1 public, 0 private\)/);
  assert.match(digest.text, /Requests {16}9,470/);
  assert.match(digest.text, /Invocations {13}6,660 \(errors 0\)/);
  assert.match(digest.text, /All Workers requests {4}6,790/);
  assert.ok(!digest.text.includes("UNAVAILABLE"), digest.text);
  assert.ok(graphql.every(q => q.url === "https://api.cloudflare.com/client/v4/graphql" && q.authorization === "Bearer token"));
  const zone = graphql.find(q => q.query.includes("httpRequests1dGroups"));
  assert.equal(zone.variables.zone, "zone");
  assert.equal(zone.variables.from, new Date().toISOString().slice(0, 10));
  console.log("--- sample daily report ---\nSubject: " + digest.subject + "\n\n" + digest.text);
});

test("an alert is emailed once per level, then again when usage climbs a level", async () => {
  turnEgress = 600e9;
  let sent = await run();
  const alerts = sent.filter(e => e.subject.startsWith("[GEVR ALERT]"));
  assert.equal(alerts.length, 1);
  assert.match(alerts[0].subject, /^\[GEVR ALERT\] TURN egress (at|projected at) /);
  assert.match(alerts[0].text, /TURN egress at 50% of free tier \(600 GB of 1000 GB this month\)/);
  assert.match(sent[0].text, /NEW TURN egress at 50% of free tier/);
  console.log("--- sample alert ---\nSubject: " + alerts[0].subject + "\n\n" + alerts[0].text);

  sent = await run();
  assert.equal(sent.length, 1, "same level again: report only");
  assert.match(sent[0].text, /\n {2}TURN egress at 50% of free tier/, "still listed, not marked new");

  turnEgress = 850e9;
  sent = await run();
  assert.equal(sent.length, 2);
  assert.match(sent[1].text, /TURN egress at 80% of free tier/);
});

test("a Cloudflare analytics outage still sends the report with the Worker's own counts", async () => {
  analyticsDown = true;
  try {
    const [digest, ...rest] = await run();
    assert.equal(rest.length, 0);
    assert.match(digest.text, /Lobbies hosted {10}1 \(1 public, 0 private\)/);
    assert.match(digest.text, /CLOUDFLARE ANALYTICS UNAVAILABLE\n {2}.*HTTP 502/);
    assert.match(digest.text, /TURN egress {13}unavailable/);
  } finally { analyticsDown = false; }
});
