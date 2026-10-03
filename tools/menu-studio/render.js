import { escapeHTML as e, clamp } from "./model.js";

export const cssColor = (project, value, fallback = "text") => project.theme[value] || (/^#[0-9a-f]{3,8}$/i.test(value) ? value : value === "transparent" ? "transparent" : project.theme[fallback]);
export function fontFamily(project, value) {
  if (value === "mono") return "ui-monospace, Consolas, monospace";
  if (value === "serif") return "Georgia, serif";
  const index = project.assets.findIndex(a => a.kind === "font" && a.id === value);
  return index >= 0 ? '"studio-font-' + index + '"' : '"Arial Narrow", "Segoe UI", Arial, sans-serif';
}
export function fontCSS(project) {
  return project.assets.map((a, i) => a.kind === "font" ? '@font-face{font-family:"studio-font-' + i + '";src:url("' + a.data.replace(/["\\]/g, "") + '")}' : "").join("\n");
}
export function interpolate(text, project) {
  const players=shownPlayers(project);
  const waiting = players.filter(p => !p.ready).map(p => p.name);
  const data = { ...project.sample, map: project.sample.mode === "coop" ? project.sample.mission : project.sample.map, sessionStatus: project.sample.mode === "coop" ? project.sample.difficulty : project.sample.sessionStatus, playerCount: players.length, readyCount: players.filter(p => p.ready).length, waitingPlayers: waiting.length ? "Waiting for " + waiting.join(", ") : "All operatives ready" };
  return String(text || "").replace(/\{\{(\w+)\}\}/g, (match, key) => Object.hasOwn(data, key) ? String(data[key]) : match);
}
function asset(project, node) { return project.assets.find(a => a.id === node.assetId); }
function badge(text, tone = "accent") { return '<span class="c-badge" style="color:var(--' + tone + ')">' + e(text) + '</span>'; }
function initials(name) { return e(String(name).split(/\s+/).map(s => s[0]).slice(0, 2).join("")); }
export function shownPlayers(project) { return project.sample.mode === "coop" ? project.sample.players.slice(0,4) : project.sample.players.slice(0,8); }
function roster(project, scoreboard = false) {
  if(project.uiStyle === "launcher") {
    const coop=project.sample.mode === "coop";
    const head=coop?["PLAYER","CHARACTER","STATUS","PING MS"]:["PLAYER","CHARACTER","PTS","KILLS","LOSSES","PING MS","READY"];
    const rows=shownPlayers(project).map(p=>'<tr><td>'+e(p.name)+(p.host?' *':'')+'</td><td>'+e(p.character)+'</td>'+ (coop?'<td class="'+(p.down?'c-bad':'c-good')+'">'+(p.down?'Down / revive':'Active')+'</td><td>'+p.ping+'</td>':'<td class="c-gold">'+p.score+'</td><td>'+p.kills+'</td><td>'+p.deaths+'</td><td>'+p.ping+'</td><td class="'+(p.ready?'c-good':'c-muted')+'">'+(p.ready?'Ready':'Waiting')+'</td>')+'</tr>');
    return '<table class="c-table"><thead><tr>'+head.map(h=>'<th>'+h+'</th>').join('')+'</tr></thead><tbody>'+rows.join('')+'</tbody></table>';
  }
  const players = project.sample.players;
  const head = scoreboard ? ["OPERATIVE", "TEAM", "K", "D", "SCORE", "PING"] : ["OPERATIVE", "CHARACTER", "TEAM", "STATE", "PING"];
  return '<table class="c-table"><thead><tr>'+head.map(h=>'<th>'+h+'</th>').join('')+'</tr></thead><tbody>'+players.map(p=>'<tr><td>'+e(p.name)+'</td><td>'+e(scoreboard?p.team:p.character)+'</td><td>'+e(scoreboard?p.kills:p.team)+'</td><td>'+e(scoreboard?p.deaths:p.ready?'Ready':'Waiting')+'</td><td>'+e(scoreboard?p.score:p.ping)+'</td>'+ (scoreboard?'<td>'+e(p.ping)+'</td>':'')+'</tr>').join('')+'</tbody></table>';
}
export function meshSVG(mesh, yaw = -25, pitch = 10, color = "#e0b040") {
  if (!mesh) return "";
  const mins = [0, 1, 2].map(i => Math.min(...mesh.vertices.map(v => v[i]))), maxs = [0, 1, 2].map(i => Math.max(...mesh.vertices.map(v => v[i])));
  const center = mins.map((v, i) => (v + maxs[i]) / 2), scale = 140 / (Math.max(...maxs.map((v, i) => v - mins[i])) || 1);
  const y = yaw * Math.PI / 180, p = pitch * Math.PI / 180;
  const points = mesh.vertices.map(v => {
    const [x, yy, z] = v.map((n, i) => (n - center[i]) * scale);
    const xx = x * Math.cos(y) + z * Math.sin(y), zz = -x * Math.sin(y) + z * Math.cos(y);
    return [150 + xx, 144 - (yy * Math.cos(p) - zz * Math.sin(p)), yy * Math.sin(p) + zz * Math.cos(p)];
  });
  const faces = mesh.faces.map(f => ({ points: f.map(i => points[i]), depth: f.reduce((a, i) => a + points[i][2], 0) / 3 })).sort((a, b) => a.depth - b.depth);
  const polygons = faces.map(f => '<polygon points="' + f.points.map(v => v[0].toFixed(2) + ',' + v[1].toFixed(2)).join(" ") + '" fill="' + color + '" fill-opacity="' + clamp(0.15 + (f.depth + 80) / 220, 0.12, 0.7).toFixed(2) + '" stroke="' + color + '" stroke-width=".35"/>').join("");
  return '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 300 288" role="img" aria-label="Imported 3D model geometry">' + polygons + '</svg>';
}
export function nodeContent(project, n) {
  const text = e(interpolate(n.text, project)), sub = e(interpolate(n.sampleKey ? project.sample[n.sampleKey] ?? n.subtext : n.subtext, project)), a = asset(project, n);
  switch (n.type) {
    case "text": return '<div class="c-text">' + text.replace(/\n/g, "<br>") + '</div>';
    case "panel": return '<div class="c-panel-label">' + (n.text === "Panel" ? "" : text) + '</div>';
    case "divider": return '<div class="c-divider"></div>';
    case "image": return a?.kind === "image" ? '<img class="c-image" src="' + e(a.data) + '" alt="' + e(a.name) + '" style="object-fit:' + (n.state === "selected" ? "contain" : "cover") + '">' : '<div class="c-asset-empty"><span>▧</span><b>' + text + '</b><small>' + e(n.runtimeAsset || "Choose an image or texture") + '</small></div>';
    case "model": return '<div class="c-model">' + (a?.kind === "model" ? meshSVG(a.mesh, n.yaw, n.pitch, project.theme.accent) : '<svg viewBox="0 0 240 230" aria-hidden="true"><g fill="none" stroke="currentColor" stroke-width="1"><circle cx="120" cy="48" r="23"/><path d="M97 78L70 100L60 150L85 158L95 120L98 166L90 222M143 78L170 100L180 150L155 158L145 120L142 166L150 222M97 78L143 78L142 166L120 178L98 166M120 178L120 220"/></g></svg>') + '<div class="c-model-title">' + text + '</div><small>' + e(a?.name || n.runtimeAsset || "Assign a model preview") + '</small></div>';
    case "objectives": return '<div class="c-objectives">'+project.sample.objectives.map((o,i)=>'<div><span>'+String.fromCharCode(65+i)+'. '+e(o.text)+'</span><b class="'+(o.status==="Complete"?"c-good":o.status==="Failed"?"c-bad":"c-gold")+'">'+e(o.status)+'</b></div>').join('')+'</div>';
    case "button": return '<button class="c-button" data-interact="button" ' + (n.state === "disabled" || n.state === "loading" ? "disabled" : "") + '>' + (n.state === "loading" ? "◌  " : "") + text + '</button>';
    case "roster": if(project.uiStyle === "launcher") return roster(project); return '<div class="c-component-heading">' + text + '<span>' + project.sample.players.length + ' / 4</span></div>' + roster(project);
    case "scoreboard": if(project.uiStyle === "launcher") return roster(project,true); return '<div class="c-component-heading">' + text + '<span>FINAL STANDINGS</span></div>' + roster(project, true);
    case "player": {
      const player = project.sample.players.find(p => p.name === n.text) || project.sample.players[0];
      return '<div class="c-player"><div class="c-player-portrait">' + (a?.kind === "image" ? '<img src="' + e(a.data) + '" alt="">' : '<span>' + initials(n.text) + '</span>') + '</div><b>' + text + '</b><small>' + e(player?.character || "Operative") + '</small>' + badge(player?.ready ? "READY" : "WAITING", player?.ready ? "success" : "muted") + '</div>';
    }
    case "vote": return '<button class="c-vote" data-interact="vote"><div class="c-map-image">' + (a?.kind === "image" ? '<img src="' + e(a.data) + '" alt="">' : '<svg viewBox="0 0 300 140" aria-hidden="true"><g fill="none" stroke="currentColor"><path d="M34 105V40H96V25H166V51H256V111H166V88H115V113H34M55 40V77H96V105M96 40H135V65H166M190 51V86H256M166 111V51M115 88V65H72"/><path d="M15 125H285M15 15V125M285 15V125" opacity=".3"/></g></svg>') + '<span class="c-map-index">0' + (Math.max(0, ["Facility", "Complex", "Temple"].indexOf(n.text)) + 1) + ' / OPERATION</span></div><div class="c-vote-body"><b>' + text + '</b><small>' + (sub || "Normal · 4 operatives") + '</small><div class="c-vote-bottom"><span>' + (n.state === "selected" ? "✓ YOUR VOTE" : "SELECT MAP") + '</span><span>' + n.votes + ' VOTES</span></div></div></button>';
    case "loadout": return '<div class="c-component-heading">' + text + '<span>4 GUN KIT</span></div><div class="c-loadout">' + (n.subtext || "PP7 (Silenced)|KF7 Soviet|Sniper Rifle|Remote Mine").split("|").map((weapon, i) => '<div><span class="c-slot">' + (i + 1) + '</span><svg viewBox="0 0 130 55" aria-hidden="true"><path d="M20 13H108V25H65L58 43H39L44 26H20Z" fill="none" stroke="currentColor" stroke-width="2"/><path d="M66 25V33H78V25" fill="none" stroke="currentColor"/></svg><b>' + e(weapon.trim()) + '</b><small>' + (i === 0 ? "PRIMARY" : "SLOT " + (i + 1)) + '</small></div>').join("") + '</div>';
    case "chat": return '<div class="c-component-heading">' + text + '<span>◖))</span></div><div class="c-chat">' + (n.subtext || "System: Lobby connected.\nBond: Ready for the next operation.\nNatalya: Let's go.").split("\n").map(line => '<p>' + e(line) + '</p>').join("") + '</div><div class="c-chat-footer"><i></i> VOICE CONNECTED <span>' + project.sample.players.filter(p => p.voice).length + ' ACTIVE</span></div>';
    case "countdown": return '<div class="c-countdown"><small>' + text + '</small><b>' + (n.count >= 60 ? Math.floor(n.count / 60) + ":" + String(n.count % 60).padStart(2, "0") : String(n.count).padStart(2, "0")) + '<span>' + (n.count >= 60 ? "" : " s") + '</span></b></div>';
    case "badge": return '<div class="c-status">' + text + '</div>';
    case "tabs": return '<div class="c-tabs">' + n.text.split("|").map((t, i) => '<button data-interact="tab" data-index="' + i + '" class="' + (Number(n.value || 0) === i ? "active" : "") + '">' + e(t.trim()) + '</button>').join("") + '</div>';
    case "toggle": return '<button class="c-toggle" data-interact="toggle"><span>' + text + '</span><i class="' + (n.value > 0 ? "on" : "") + '"><b></b></i></button>';
    case "slider": return '<div class="c-slider"><label>' + text + '<span>' + n.value + '%</span></label><input data-interact="slider" type="range" min="0" max="100" value="' + n.value + '"></div>';
    case "select": if(n.options) return '<label class="c-choice"><span>'+text+'</span><select data-interact="choose">'+n.options.split('|').map(o=>'<option '+(e(o)===sub?'selected':'')+'>'+e(o)+'</option>').join('')+'</select></label>'; return '<button class="c-select" data-interact="cycle"><small>' + text + '</small><b>' + (sub || "Power Weapons") + '</b><span>⌄</span></button>';
    case "input": return '<label class="c-input"><small>' + text + '</small><input data-interact="input" placeholder="Type here…" value="' + e(n.subtext) + '"></label>';
    case "progress": return '<div class="c-progress"><div style="width:' + clamp(n.sampleKey ? project.sample[n.sampleKey] ?? n.value : n.value, 0, 100) + '%"></div></div>';
    case "server-list": return '<div class="c-component-heading">' + text + '<span>3 SESSIONS FOUND</span></div><table class="c-table c-servers"><thead><tr><th>SESSION</th><th>MAP</th><th>MODE</th><th>PLAYERS</th><th>PING</th></tr></thead><tbody>' + [["Bond's game", "Facility", "Normal", "3 / 4", "24"], ["Sunday operatives", "Complex", "Team 2v2", "2 / 4", "38"], ["Golden hour", "Temple", "Golden Gun", "1 / 4", "52"]].map((row, i) => '<tr data-interact="server" data-index="' + i + '" tabindex="0"><td><b>' + e(row[0]) + '</b>' + (i === 0 ? '<small>WARMUP</small>' : "") + '</td>' + row.slice(1).map(c => '<td>' + e(c) + '</td>').join("") + '</tr>').join("") + '</tbody></table>';
    default: return text;
  }
}
export function visibleNode(n, project, role) {
  const mode=project.sample.mode || "deathmatch";
  return !n.hidden && (!n.visibleWhen || n.visibleWhen === "always" || [role,mode,role+"-"+mode,project.sample.phase].includes(n.visibleWhen));
}
export function nodeStyle(project, n) {
  return {
    left: n.x + "px", top: n.y + "px", width: n.w + "px", height: n.h + "px",
    color: cssColor(project, n.color), background: cssColor(project, n.fill, "panel"),
    borderColor: cssColor(project, n.border, "line"), borderRadius: n.radius + "px",
    opacity: n.opacity, fontSize: n.fontSize + "px", textAlign: n.align, fontFamily: fontFamily(project, n.font),
  };
}
export function renderScene(project, screen, { preview = false, role = "host" } = {}) {
  const fragment = document.createDocumentFragment();
  for (const n of screen.nodes) {
    if ((preview || project.uiStyle === "launcher") && !visibleNode(n, project, role)) continue;
    const element = document.createElement("div");
    element.className = "scene-node type-" + n.type + " state-" + n.state + (n.locked ? " locked" : "") + (n.hidden ? " hidden-node" : "");
    element.dataset.id = n.id;
    element.dataset.type = n.type;
    element.setAttribute("aria-label", n.name);
    Object.assign(element.style, nodeStyle(project, n));
    element.innerHTML = nodeContent(project, n);
    if(preview && n.editableBy && n.editableBy !== "everyone" && n.editableBy !== role) element.querySelectorAll("button,input,select").forEach(c=>c.disabled=true);
    fragment.append(element);
  }
  return fragment;
}
