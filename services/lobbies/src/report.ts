import type { Env, LobbyRegistry, StatsRow } from "./index";

export const utcDay = (ms = Date.now()) => new Date(ms).toISOString().slice(0, 10);
const DAY_MS = 86_400_000;
const GRAPHQL = "https://api.cloudflare.com/client/v4/graphql";
const LEVELS = [50, 80, 100];
const STAGES: Record<number, string> = {
  34: "Facility", 31: "Complex", 38: "Temple", 46: "Stack", 39: "Caverns", 48: "Library",
  45: "Basement", 50: "Caves", 32: "Egypt", 27: "Bunker II", 24: "Archives",
};

export type Website = { requests: number; bytes: number; cachedBytes: number; uniques: number; status4xx: number; status5xx: number; countries: Array<{ name: string; requests: number }> };
export type Workers = { lobbyRequests: number; lobbyErrors: number; lobbySubrequests: number; lobbyCpuP50Us: number; accountRequests: number };
export type Traffic = { egressBytes: number; ingressBytes: number };
export type Analytics = { website?: Website; workers?: Workers; durableObjectRequests?: number; turn?: Traffic; errors: string[] };
export type LobbyTotals = Record<string, number>;
export type Period = { from: string; to: string; analytics: Analytics; lobby: LobbyTotals };
export type Limits = { turnFreeGb: number; turnCap: number; workersDaily: number; durableObjectsDaily: number };
export type Month = { period: string; turn?: Traffic; turnCredentials: number; elapsed: number; projectedEgressBytes?: number };
export type Alert = { metric: string; period: string; level: number; message: string };
export type ReportInput = { day: Period; week?: { current: Period; previous: Period }; month: Month; limits: Limits; alerts: Alert[]; newAlerts: Alert[] };

/** Sums the Worker's daily counters over a range; peak_* keys keep the highest day. */
export function lobbyTotals(rows: StatsRow[]): LobbyTotals {
  const totals: LobbyTotals = {};
  for (const row of rows)
    totals[row.key] = row.key.startsWith("peak_") ? Math.max(totals[row.key] ?? 0, row.value) : (totals[row.key] ?? 0) + row.value;
  return totals;
}

async function gql<T>(env: Env, query: string, variables: Record<string, unknown>): Promise<T> {
  const response = await fetch(GRAPHQL, {
    method: "POST",
    headers: { authorization: `Bearer ${env.CF_ANALYTICS_TOKEN}`, "content-type": "application/json" },
    body: JSON.stringify({ query, variables }),
  });
  if (!response.ok) throw new Error(`HTTP ${response.status}`);
  const body = await response.json() as { data?: T; errors?: Array<{ message: string }> | null };
  if (body.errors?.length) throw new Error(body.errors.map(e => e.message).join("; "));
  if (!body.data) throw new Error("empty response");
  return body.data;
}

const WEBSITE_QUERY = `query ($zone: string, $from: Date, $to: Date) { viewer { zones(filter: { zoneTag: $zone }) {
  httpRequests1dGroups(limit: 40, filter: { date_geq: $from, date_leq: $to }) {
    sum { requests bytes cachedBytes responseStatusMap { edgeResponseStatus requests } countryMap { clientCountryName requests } }
    uniq { uniques } } } } }`;
const WORKERS_QUERY = `query ($account: string, $start: Time, $end: Time) { viewer { accounts(filter: { accountTag: $account }) {
  workersInvocationsAdaptive(limit: 1000, filter: { datetime_geq: $start, datetime_lt: $end }) {
    sum { requests errors subrequests } quantiles { cpuTimeP50 } dimensions { scriptName } } } } }`;
const DURABLE_OBJECTS_QUERY = `query ($account: string, $from: Date, $to: Date) { viewer { accounts(filter: { accountTag: $account }) {
  durableObjectsInvocationsAdaptiveGroups(limit: 1000, filter: { date_geq: $from, date_leq: $to }) { sum { requests } } } } }`;
const TURN_QUERY = `query ($account: string, $from: Date, $to: Date) { viewer { accounts(filter: { accountTag: $account }) {
  callsTurnUsageAdaptiveGroups(limit: 1, filter: { date_geq: $from, date_leq: $to }) { sum { egressBytes ingressBytes } } } } }`;

type Rows<K extends string, R> = { viewer: { zones?: Array<Record<K, R[]>>; accounts?: Array<Record<K, R[]>> } };
const rowsOf = <K extends string, R>(data: Rows<K, R>, key: K): R[] => (data.viewer.zones ?? data.viewer.accounts ?? [])[0]?.[key] ?? [];

async function queryTurn(env: Env, from: string, to: string): Promise<Traffic> {
  const rows = rowsOf(await gql<Rows<"callsTurnUsageAdaptiveGroups", { sum: Traffic }>>(env, TURN_QUERY, { account: env.CF_ACCOUNT_ID, from, to }), "callsTurnUsageAdaptiveGroups");
  return rows.reduce((t, r) => ({ egressBytes: t.egressBytes + r.sum.egressBytes, ingressBytes: t.ingressBytes + r.sum.ingressBytes }), { egressBytes: 0, ingressBytes: 0 });
}

/** Cloudflare analytics for whole UTC days from..to. Each dataset is queried on its own so one failure leaves the rest. */
export async function queryAnalytics(env: Env, from: string, to: string): Promise<Analytics> {
  const result: Analytics = { errors: [] };
  if (!env.CF_ANALYTICS_TOKEN || !env.CF_ACCOUNT_ID) {
    result.errors.push("Cloudflare analytics not configured (CF_ANALYTICS_TOKEN, CF_ACCOUNT_ID)");
    return result;
  }
  const end = new Date(Date.parse(to + "T00:00:00Z") + DAY_MS).toISOString();
  const tasks: Array<[string, () => Promise<void>]> = [
    ["Workers", async () => {
      type Row = { sum: { requests: number; errors: number; subrequests: number }; quantiles: { cpuTimeP50: number }; dimensions: { scriptName: string } };
      const rows = rowsOf(await gql<Rows<"workersInvocationsAdaptive", Row>>(env, WORKERS_QUERY, { account: env.CF_ACCOUNT_ID, start: from + "T00:00:00Z", end }), "workersInvocationsAdaptive");
      const lobby = rows.filter(r => r.dimensions.scriptName === "gevr-lobbies");
      const lobbyRequests = lobby.reduce((n, r) => n + r.sum.requests, 0);
      result.workers = {
        lobbyRequests,
        lobbyErrors: lobby.reduce((n, r) => n + r.sum.errors, 0),
        lobbySubrequests: lobby.reduce((n, r) => n + r.sum.subrequests, 0),
        lobbyCpuP50Us: lobbyRequests ? lobby.reduce((n, r) => n + r.quantiles.cpuTimeP50 * r.sum.requests, 0) / lobbyRequests : 0,
        accountRequests: rows.reduce((n, r) => n + r.sum.requests, 0),
      };
    }],
    ["Durable Objects", async () => {
      const rows = rowsOf(await gql<Rows<"durableObjectsInvocationsAdaptiveGroups", { sum: { requests: number } }>>(env, DURABLE_OBJECTS_QUERY, { account: env.CF_ACCOUNT_ID, from, to }), "durableObjectsInvocationsAdaptiveGroups");
      result.durableObjectRequests = rows.reduce((n, r) => n + r.sum.requests, 0);
    }],
    ["TURN", async () => { result.turn = await queryTurn(env, from, to); }],
  ];
  if (env.CF_ZONE_ID) tasks.push(["Website", async () => {
    type Row = { sum: { requests: number; bytes: number; cachedBytes: number; responseStatusMap: Array<{ edgeResponseStatus: number; requests: number }>; countryMap: Array<{ clientCountryName: string; requests: number }> }; uniq: { uniques: number } };
    const rows = rowsOf(await gql<Rows<"httpRequests1dGroups", Row>>(env, WEBSITE_QUERY, { zone: env.CF_ZONE_ID, from, to }), "httpRequests1dGroups");
    const countries = new Map<string, number>();
    const site: Website = { requests: 0, bytes: 0, cachedBytes: 0, uniques: 0, status4xx: 0, status5xx: 0, countries: [] };
    for (const row of rows) {
      site.requests += row.sum.requests;
      site.bytes += row.sum.bytes;
      site.cachedBytes += row.sum.cachedBytes;
      site.uniques += row.uniq.uniques;
      for (const s of row.sum.responseStatusMap) {
        if (s.edgeResponseStatus >= 400 && s.edgeResponseStatus < 500) site.status4xx += s.requests;
        if (s.edgeResponseStatus >= 500) site.status5xx += s.requests;
      }
      for (const c of row.sum.countryMap) countries.set(c.clientCountryName, (countries.get(c.clientCountryName) ?? 0) + c.requests);
    }
    site.countries = [...countries].map(([name, requests]) => ({ name, requests })).sort((a, b) => b.requests - a.requests).slice(0, 5);
    result.website = site;
  }]);
  else result.errors.push("Website: CF_ZONE_ID not set");
  const settled = await Promise.allSettled(tasks.map(([, run]) => run()));
  settled.forEach((outcome, i) => {
    if (outcome.status === "rejected") result.errors.push(`${tasks[i][0]}: ${String(outcome.reason instanceof Error ? outcome.reason.message : outcome.reason)}`);
  });
  return result;
}

const levelOf = (ratio: number) => LEVELS.filter(level => ratio * 100 >= level).pop() ?? 0;
const pct = (ratio: number) => {
  const value = ratio * 100;
  return (value >= 10 ? value.toFixed(0) : value >= 0.1 ? value.toFixed(1) : value > 0 ? value.toPrecision(1) : "0") + "%";
};

export function checkThresholds(day: Period, month: Month, limits: Limits): Alert[] {
  const alerts: Alert[] = [];
  const add = (metric: string, period: string, ratio: number, describe: (level: number) => string) => {
    const level = levelOf(ratio);
    if (level) alerts.push({ metric, period, level, message: describe(level) });
  };
  const free = limits.turnFreeGb * 1e9;
  if (month.turn && free > 0)
    add("turn_egress", month.period, month.turn.egressBytes / free,
      level => `TURN egress at ${level}% of free tier (${bytes(month.turn!.egressBytes)} of ${limits.turnFreeGb} GB this month)`);
  // A projection from the first days of a month swings too far to act on.
  if (month.projectedEgressBytes !== undefined && month.elapsed >= 0.2 && free > 0)
    add("turn_projection", month.period, month.projectedEgressBytes / free,
      level => `TURN egress projected at ${level}% of free tier (about ${bytes(month.projectedEgressBytes!)} by month end)`);
  if (limits.turnCap > 0)
    add("turn_credentials", month.period, month.turnCredentials / limits.turnCap,
      level => `TURN credentials at ${level}% of the monthly cap (${num(month.turnCredentials)} of ${num(limits.turnCap)})`);
  const workers = day.analytics.workers;
  if (workers && limits.workersDaily > 0)
    add("worker_requests", day.from, workers.accountRequests / limits.workersDaily,
      level => `Worker requests at ${level}% of the daily limit (${num(workers.accountRequests)} of ${num(limits.workersDaily)} on ${day.from})`);
  const doRequests = day.analytics.durableObjectRequests;
  if (doRequests !== undefined && limits.durableObjectsDaily > 0)
    add("do_requests", day.from, doRequests / limits.durableObjectsDaily,
      level => `Durable Object requests at ${level}% of the daily limit (${num(doRequests)} of ${num(limits.durableObjectsDaily)} on ${day.from})`);
  const site = day.analytics.website;
  if (site && site.requests >= 100 && site.status5xx / site.requests > 0.01)
    alerts.push({ metric: "website_5xx", period: day.from, level: 100,
      message: `Website 5xx rate ${pct(site.status5xx / site.requests)} on ${day.from} (${num(site.status5xx)} of ${num(site.requests)} requests)` });
  return alerts;
}

const num = (n: number) => Math.round(n).toLocaleString("en-US");
function bytes(n: number): string {
  for (const [unit, size] of [["TB", 1e12], ["GB", 1e9], ["MB", 1e6], ["KB", 1e3]] as const)
    if (n >= size) return `${(n / size).toFixed(n / size >= 100 ? 0 : 1)} ${unit}`;
  return `${num(n)} B`;
}
const WEEKDAYS = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];
const MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];
const longDay = (day: string) => {
  const d = new Date(day + "T00:00:00Z");
  return `${WEEKDAYS[d.getUTCDay()]} ${d.getUTCDate()} ${MONTHS[d.getUTCMonth()]} ${d.getUTCFullYear()}`;
};
const plural = (n: number, one: string, many: string) => `${num(n)} ${n === 1 ? one : many}`;
const change = (now: number, before: number) =>
  before === 0 ? (now === 0 ? "no change" : "new") : `${now >= before ? "+" : ""}${(((now - before) / before) * 100).toFixed(0)}%`;
const row = (label: string, value: string) => `  ${label.padEnd(24)}${value}`;
const lobbiesOf = (t: LobbyTotals) => (t.lobbies_public ?? 0) + (t.lobbies_private ?? 0);

function topStage(t: LobbyTotals): string {
  const best = Object.entries(t).filter(([key]) => key.startsWith("stage:")).sort((a, b) => b[1] - a[1])[0];
  if (!best) return "none";
  const id = Number(best[0].slice(6));
  return `${STAGES[id] ?? `Stage ${id}`} (${num(best[1])})`;
}

function periodLines(p: Period): string[] {
  const t = p.lobby, a = p.analytics;
  const lines = [
    "LOBBIES",
    row("Lobbies hosted", `${num(lobbiesOf(t))} (${num(t.lobbies_public ?? 0)} public, ${num(t.lobbies_private ?? 0)} private)`),
    row("Join attempts", num(t.join_attempts ?? 0)),
    row("Joins connected", num(t.joins_connected ?? 0)),
    row("Matches started", num(t.matches_started ?? 0)),
    row("Most played", topStage(t)),
    row("Peak lobbies / players", `${num(t.peak_lobbies ?? 0)} / ${num(t.peak_players ?? 0)}`),
    row("Debug reports", num(t.reports ?? 0)),
    "",
    "WEBSITE (goldeneyevr.com)",
  ];
  if (a.website) {
    const s = a.website;
    lines.push(
      row("Requests", num(s.requests)),
      row("Data transfer", `${bytes(s.bytes)} (${s.bytes ? pct(s.cachedBytes / s.bytes) : "0%"} cached)`),
      row("Unique visitors", num(s.uniques)),
      row("4xx / 5xx", `${num(s.status4xx)} / ${num(s.status5xx)}`),
      row("Top countries", s.countries.map(c => `${c.name} ${num(c.requests)}`).join(" · ") || "none"));
  } else lines.push("  unavailable");
  lines.push("", "LOBBY WORKER (gevr-lobbies)");
  if (a.workers) {
    const w = a.workers;
    lines.push(
      row("Invocations", `${num(w.lobbyRequests)} (errors ${num(w.lobbyErrors)})`),
      row("Subrequests", num(w.lobbySubrequests)),
      row("Median CPU", `${(w.lobbyCpuP50Us / 1000).toFixed(1)} ms`),
      row("All Workers requests", num(w.accountRequests)));
  } else lines.push("  unavailable");
  lines.push(row("Durable Object requests", a.durableObjectRequests === undefined ? "unavailable" : num(a.durableObjectRequests)));
  lines.push("", "TURN RELAY");
  lines.push(row("Egress / ingress", a.turn ? `${bytes(a.turn.egressBytes)} / ${bytes(a.turn.ingressBytes)}` : "unavailable"));
  lines.push(row("Credentials issued", `${num(t.turn_issued ?? 0)} (${num(t.turn_refused ?? 0)} refused at cap)`));
  return lines;
}

function weekLines(current: Period, previous: Period): string[] {
  const metric = (label: string, pick: (p: Period) => number | undefined, format = num) => {
    const now = pick(current), before = pick(previous);
    return row(label, now === undefined ? "unavailable" : `${format(now)}${before === undefined ? "" : `  (${change(now, before)} vs ${format(before)})`}`);
  };
  return [
    `LAST 7 DAYS (${current.from} to ${current.to}) vs the 7 days before`,
    metric("Lobbies hosted", p => lobbiesOf(p.lobby)),
    metric("Join attempts", p => p.lobby.join_attempts ?? 0),
    metric("Joins connected", p => p.lobby.joins_connected ?? 0),
    metric("Matches started", p => p.lobby.matches_started ?? 0),
    metric("Peak players", p => p.lobby.peak_players ?? 0),
    row("Most played", topStage(current.lobby)),
    metric("Website requests", p => p.analytics.website?.requests),
    metric("Website transfer", p => p.analytics.website?.bytes, bytes),
    metric("Lobby Worker requests", p => p.analytics.workers?.lobbyRequests),
    metric("TURN egress", p => p.analytics.turn?.egressBytes, bytes),
  ];
}

export function buildReport(input: ReportInput): { subject: string; text: string } {
  const { day, week, month, limits } = input;
  const weekly = week !== undefined;
  const subject = `[GEVR ${weekly ? "weekly" : "daily"}] ${longDay(day.from)}: ${plural(lobbiesOf(day.lobby), "lobby", "lobbies")}, ${plural(day.lobby.matches_started ?? 0, "match", "matches")}${input.newAlerts.length ? `, ${plural(input.newAlerts.length, "alert", "alerts")}` : ""}`;
  const lines = [`GoldenEye VR ${weekly ? "weekly" : "daily"} report for ${longDay(day.from)} (UTC)`, ""];
  lines.push(...periodLines(day), "");
  const free = limits.turnFreeGb * 1e9;
  lines.push(`MONTH TO DATE (${month.period}, ${pct(month.elapsed)} of the month)`);
  lines.push(row("TURN egress", month.turn ? `${bytes(month.turn.egressBytes)} of ${num(limits.turnFreeGb)} GB free (${pct(month.turn.egressBytes / free)})` : "unavailable"));
  if (month.projectedEgressBytes !== undefined) lines.push(row("Month-end projection", `about ${bytes(month.projectedEgressBytes)} (${pct(month.projectedEgressBytes / free)})`));
  lines.push(row("TURN credentials", limits.turnCap > 0 ? `${num(month.turnCredentials)} of ${num(limits.turnCap)} (${pct(month.turnCredentials / limits.turnCap)})` : `${num(month.turnCredentials)} (no cap)`));
  if (day.analytics.workers) lines.push(row("Workers daily limit", `${num(day.analytics.workers.accountRequests)} of ${num(limits.workersDaily)} yesterday (${pct(day.analytics.workers.accountRequests / limits.workersDaily)})`));
  if (day.analytics.durableObjectRequests !== undefined) lines.push(row("DO daily limit", `${num(day.analytics.durableObjectRequests)} of ${num(limits.durableObjectsDaily)} yesterday (${pct(day.analytics.durableObjectRequests / limits.durableObjectsDaily)})`));
  lines.push("");
  if (week) lines.push(...weekLines(week.current, week.previous), "");
  lines.push("ALERTS");
  if (!input.alerts.length) lines.push("  None. Everything is under 50% of its limit.");
  const isNew = new Set(input.newAlerts.map(a => a.metric));
  for (const alert of input.alerts) lines.push(`  ${isNew.has(alert.metric) ? "NEW " : ""}${alert.message}`);
  const errors = [...new Set([...day.analytics.errors, ...(week?.current.analytics.errors ?? [])])];
  if (errors.length) lines.push("", "CLOUDFLARE ANALYTICS UNAVAILABLE", ...errors.map(e => `  ${e}`));
  lines.push("", "Lobby counts come from the lobby Worker; website, Worker and TURN figures from Cloudflare analytics.");
  return { subject, text: lines.join("\n") };
}

export async function runReport(env: Env, registry: DurableObjectStub<LobbyRegistry>, now: Date, turnCap: number): Promise<{ report: { subject: string; text: string }; alerts: Alert[] }> {
  const today = utcDay(now.getTime());
  const yesterday = utcDay(now.getTime() - DAY_MS);
  const monthStart = Date.UTC(now.getUTCFullYear(), now.getUTCMonth(), 1);
  const monthEnd = Date.UTC(now.getUTCFullYear(), now.getUTCMonth() + 1, 1);
  const weekly = now.getUTCDay() === 1;
  const limits: Limits = {
    turnFreeGb: Number(env.TURN_FREE_GB ?? 1000),
    turnCap,
    workersDaily: Number(env.WORKERS_DAILY_LIMIT ?? 100_000),
    durableObjectsDaily: Number(env.DO_DAILY_LIMIT ?? 100_000),
  };
  const period = async (from: string, to: string): Promise<Period> => {
    const [analytics, rows] = await Promise.all([queryAnalytics(env, from, to), registry.statsRange(from, to)]);
    return { from, to, analytics, lobby: lobbyTotals(rows) };
  };
  const monthTurn = async (): Promise<Traffic | undefined> => {
    if (!env.CF_ANALYTICS_TOKEN || !env.CF_ACCOUNT_ID) return undefined;
    try { return await queryTurn(env, utcDay(monthStart), today); }
    catch (error) { console.error(JSON.stringify({ event: "usage_report_turn_error", message: String(error) })); return undefined; }
  };
  const [day, current, previous, turn, turnCredentials] = await Promise.all([
    period(yesterday, yesterday),
    weekly ? period(utcDay(now.getTime() - 7 * DAY_MS), yesterday) : undefined,
    weekly ? period(utcDay(now.getTime() - 14 * DAY_MS), utcDay(now.getTime() - 8 * DAY_MS)) : undefined,
    monthTurn(),
    registry.turnCredentialsThisMonth(),
  ]);
  const elapsed = Math.min(1, Math.max(0, (now.getTime() - monthStart) / (monthEnd - monthStart)));
  const month: Month = {
    period: today.slice(0, 7), turn, turnCredentials, elapsed,
    projectedEgressBytes: turn && elapsed > 0 ? turn.egressBytes / elapsed : undefined,
  };
  const alerts = checkThresholds(day, month, limits);
  const newAlerts: Alert[] = [];
  for (const alert of alerts)
    if (await registry.alertLevelSent(alert.metric, alert.period) < alert.level) newAlerts.push(alert);
  const report = buildReport({ day, week: current && previous ? { current, previous } : undefined, month, limits, alerts, newAlerts });
  await env.EMAIL.send({ from: "reports@goldeneyevr.com", to: env.REPORT_TO, subject: report.subject, text: report.text, attachments: [] });
  if (newAlerts.length) {
    const [first, ...rest] = newAlerts;
    await env.EMAIL.send({
      from: "reports@goldeneyevr.com", to: env.REPORT_TO,
      subject: `[GEVR ALERT] ${first.message.replace(/ \(.*$/, "")}${rest.length ? ` (+${rest.length} more)` : ""}`,
      text: ["Usage crossed a threshold. Details:", "", ...newAlerts.map(a => `- ${a.message}`), "",
        "Each alert is sent once per level (50%, 80%, 100%) per period. The daily report has the full picture."].join("\n"),
      attachments: [],
    });
    for (const alert of newAlerts) await registry.recordAlert(alert.metric, alert.period, alert.level);
  }
  console.log(JSON.stringify({ event: "usage_report_sent", subject: report.subject, alerts: newAlerts.map(a => `${a.metric}:${a.level}`) }));
  return { report, alerts: newAlerts };
}
