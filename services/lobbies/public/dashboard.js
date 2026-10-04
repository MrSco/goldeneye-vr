const stages = { 34:"Facility",31:"Complex",38:"Temple",46:"Stack",39:"Caverns",48:"Library",45:"Basement",50:"Caves",32:"Egypt",27:"Bunker II",24:"Archives",22:"Statue",41:"Cradle" };
const weapons = ["Slappers only","Pistols","Throwing Knives","Automatics","Power Weapons","Sniper Rifles","Grenades","Remote Mines","Grenade Launchers","Timed Mines","Proximity Mines","Rockets","Lasers","Golden Gun"];
const missions = { 33:"Dam",34:"Facility",35:"Runway",36:"Surface I",9:"Bunker I",20:"Silo",26:"Frigate",43:"Surface II",27:"Bunker II",22:"Statue",24:"Archives",29:"Streets",30:"Depot",25:"Train",37:"Jungle",23:"Control",39:"Caverns",41:"Cradle",28:"Aztec",32:"Egyptian" };
const difficulties = ["Agent","Secret Agent","00 Agent","007"];
// A co-op game lists 0x80 | where the party is (90: its menus, else a mission's level id),
// and its difficulty in the weapons' place (the game's net_core.c netGetLobbyStage).
const COOP_STAGE = 0x80, COOP_MENUS = 90;

function stageLabel(stage) {
  if (stage & COOP_STAGE) {
    const where = stage & 0x7F;
    if (where === COOP_MENUS) return "Co-op campaign";
    if (missions[where]) return `Co-op: ${missions[where]}`;
  }
  return stages[stage] || `Stage ${stage}`;
}

function settingsLabel(stage, set) {
  if (stage & COOP_STAGE)
    return [(stage & 0x7F) === COOP_MENUS ? "Co-op campaign, in the menus" : stageLabel(stage), difficulties[set] || `Difficulty ${set}`];
  return [stageLabel(stage), weapons[set] || `Weapons ${set}`];
}

const byId = id => document.getElementById(id);
let loading = false;

function timeAgo(ms) {
  const diffSec = Math.max(0, Math.floor((Date.now() - ms) / 1000));
  if (diffSec < 60) return `${diffSec}s ago`;
  const diffMin = Math.floor(diffSec / 60);
  if (diffMin < 60) return `${diffMin}m ago`;
  const diffHours = Math.floor(diffMin / 60);
  const remMin = diffMin % 60;
  return remMin ? `${diffHours}h ${remMin}m ago` : `${diffHours}h ago`;
}

function durationStr(ms) {
  const diffSec = Math.max(0, Math.floor((Date.now() - ms) / 1000));
  if (diffSec < 60) return `${diffSec}s`;
  const diffMin = Math.floor(diffSec / 60);
  if (diffMin < 60) return `${diffMin}m`;
  const diffHours = Math.floor(diffMin / 60);
  const remMin = diffMin % 60;
  return remMin ? `${diffHours}h ${remMin}m` : `${diffHours}h`;
}

function gameCard(game) {
  const card = document.createElement("article");
  card.className = "game";
  const top = document.createElement("div");
  top.className = "game-top";
  const title = document.createElement("h3");
  title.textContent = game.name;
  const badge = document.createElement("span");
  badge.className = "badge" + (game.phase === "in_progress" ? " playing" : "");
  badge.textContent = { waiting:"In lobby", warmup:"Warmup", in_progress:"In progress" }[game.phase] || "Live";
  top.append(title, badge);
  const meta = document.createElement("p");
  meta.className = "meta";
  for (const value of [...settingsLabel(game.stage, game.weapons), `Protocol ${game.version}`]) {
    const label = document.createElement("span");
    label.textContent = value;
    meta.append(label);
  }
  const timeInfo = document.createElement("div");
  timeInfo.className = "game-time";
  const hostedTime = game.createdAt ? timeAgo(game.createdAt) : null;
  const phaseDuration = game.phaseChangedAt ? durationStr(game.phaseChangedAt) : null;
  const phaseLabel = { waiting: "In lobby", warmup: "Warmup", in_progress: "Playing" }[game.phase] || "Live";
  if (hostedTime && phaseDuration) {
    timeInfo.textContent = `Hosted ${hostedTime} · ${phaseLabel} for ${phaseDuration}`;
  } else if (hostedTime) {
    timeInfo.textContent = `Hosted ${hostedTime}`;
  }
  const bottom = document.createElement("div");
  bottom.className = "game-bottom";
  const occupancy = document.createElement("strong");
  occupancy.textContent = `${game.players}/${game.maxPlayers} players`;
  const availability = document.createElement("span");
  availability.className = game.joinable ? "" : "closed";
  availability.textContent = game.joinable ? `${game.maxPlayers - game.players} open ${game.maxPlayers - game.players === 1 ? "spot" : "spots"}` : "Not accepting joins";
  bottom.append(occupancy, availability);
  card.append(top, meta, timeInfo, bottom);
  return card;
}

async function refresh() {
  if (loading) return;
  loading = true;
  byId("refresh").disabled = true;
  try {
    const response = await fetch("/v1/activity", { cache:"no-store" });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const data = await response.json();
    byId("public-count").textContent = data.counts.public;
    byId("match-count").textContent = data.counts.inProgress;
    byId("player-count").textContent = data.counts.players;
    byId("private-count").textContent = data.counts.private;
    const games = byId("games");
    games.replaceChildren(...data.lobbies.map(gameCard));
    if (!data.lobbies.length) {
      const empty = document.createElement("div");
      empty.className = "empty";
      const headline = document.createElement("strong");
      headline.textContent = "No public games right now";
      const detail = document.createElement("span");
      detail.textContent = "Start hosting in your headset, or find players on Discord.";
      empty.append(headline, detail);
      games.append(empty);
    }
    byId("status").classList.remove("error");
    byId("status").textContent = `Updated ${new Date(data.updatedAt).toLocaleTimeString()}`;
  } catch {
    byId("status").classList.add("error");
    byId("status").textContent = "Live activity is temporarily unavailable. Try refreshing.";
  } finally {
    loading = false;
    byId("refresh").disabled = false;
  }
}

const SVG_NS = "http://www.w3.org/2000/svg";
const reduceMotion = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
const formatCount = n => Math.round(n).toLocaleString();
const shortDay = day => new Date(`${day}T00:00:00Z`).toLocaleDateString(undefined, { day: "numeric", month: "short", timeZone: "UTC" });
const longDay = day => new Date(`${day}T00:00:00Z`).toLocaleDateString(undefined, { day: "numeric", month: "short", year: "numeric", timeZone: "UTC" });
let statsShown = false;

function countUp(element, value) {
  if (statsShown || reduceMotion || value === 0) {
    element.textContent = formatCount(value);
    return;
  }
  const start = performance.now();
  const step = now => {
    const progress = Math.min(1, (now - start) / 900);
    element.textContent = formatCount(value * (1 - Math.pow(1 - progress, 3)));
    if (progress < 1) requestAnimationFrame(step);
  };
  requestAnimationFrame(step);
}

function drawBars(daily) {
  const svg = byId("record-bars");
  svg.querySelectorAll("rect").forEach(rect => rect.remove());
  const max = Math.max(1, ...daily.map(d => d.lobbies));
  const slot = 700 / daily.length;
  const width = slot * 0.62;
  daily.forEach((d, i) => {
    const height = d.lobbies ? Math.max(4, (d.lobbies / max) * 116) : 2;
    const rect = document.createElementNS(SVG_NS, "rect");
    rect.setAttribute("x", (i * slot + (slot - width) / 2).toFixed(1));
    rect.setAttribute("y", (120 - height).toFixed(1));
    rect.setAttribute("width", width.toFixed(1));
    rect.setAttribute("height", height.toFixed(1));
    rect.setAttribute("rx", "2");
    if (i === daily.length - 1) rect.classList.add("today");
    else if (!d.lobbies) rect.classList.add("empty");
    const title = document.createElementNS(SVG_NS, "title");
    title.textContent = `${shortDay(d.day)}: ${d.lobbies} ${d.lobbies === 1 ? "lobby" : "lobbies"}, ${d.matches} ${d.matches === 1 ? "match" : "matches"}`;
    rect.append(title);
    svg.append(rect);
  });
}

async function loadStats() {
  try {
    const response = await fetch("/v1/stats");
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const stats = await response.json();
    if (!stats.since) {
      byId("record").hidden = true;
      return;
    }
    byId("record").hidden = false;
    countUp(byId("stat-lobbies"), stats.allTime.lobbies);
    countUp(byId("stat-matches"), stats.allTime.matches);
    countUp(byId("stat-joins"), stats.allTime.joinsConnected);
    countUp(byId("stat-peak"), stats.allTime.peakPlayers);
    byId("record-since").textContent = `Tracking since ${longDay(stats.since)}`;
    byId("record-today").textContent = `Today: ${stats.today.lobbies} ${stats.today.lobbies === 1 ? "lobby" : "lobbies"} · ${stats.today.matches} ${stats.today.matches === 1 ? "match" : "matches"}`;
    byId("record-first-day").textContent = shortDay(stats.daily[0].day);
    drawBars(stats.daily);
    const top = byId("record-top");
    if (stats.topStage) {
      const name = document.createElement("strong");
      name.textContent = stageLabel(stats.topStage.stage);
      top.replaceChildren("Most played: ", name, ` · ${formatCount(stats.topStage.matches)} ${stats.topStage.matches === 1 ? "match" : "matches"}`);
    } else top.textContent = "No matches played yet. Be the first.";
    statsShown = true;
  } catch {
    byId("record").hidden = true;
  }
}

loadStats();
setInterval(() => { if (!document.hidden) loadStats(); }, 300_000);

byId("refresh").addEventListener("click", refresh);
document.addEventListener("visibilitychange", () => { if (!document.hidden) refresh(); });
setInterval(() => { if (!document.hidden) refresh(); }, 30_000);
refresh();
