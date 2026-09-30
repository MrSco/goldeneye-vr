const stages = { 34:"Facility",31:"Complex",38:"Temple",46:"Stack",39:"Caverns",48:"Library",45:"Basement",50:"Caves",32:"Egypt",27:"Bunker II",24:"Archives" };
const weapons = ["Slappers only","Pistols","Throwing Knives","Automatics","Power Weapons","Sniper Rifles","Grenades","Remote Mines","Grenade Launchers","Timed Mines","Proximity Mines","Rockets","Lasers","Golden Gun"];
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
  for (const value of [stages[game.stage] || `Stage ${game.stage}`, weapons[game.weapons] || `Weapons ${game.weapons}`, `Protocol ${game.version}`]) {
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

byId("refresh").addEventListener("click", refresh);
document.addEventListener("visibilitychange", () => { if (!document.hidden) refresh(); });
setInterval(() => { if (!document.hidden) refresh(); }, 30_000);
refresh();
