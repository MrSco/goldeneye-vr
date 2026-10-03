import { FORMAT, VERSION, uid, clone, palettes, createNode, createScreen, defaultPlayers } from "./model.js";

// These IDs and names follow port/src/net/net_match.c, rather than inventing maps.
export const stages = [
  ["Facility", 34, 8], ["Complex", 31, 8], ["Temple", 38, 8], ["Stack", 46, 8],
  ["Caverns", 39, 6], ["Library", 48, 8], ["Basement", 45, 8], ["Caves", 50, 8],
  ["Egypt", 32, 4], ["Bunker II", 27, 6], ["Archives", 24, 6],
];
export const weapons = ["Slappers only", "Pistols", "Throwing Knives", "Automatics", "Power Weapons", "Sniper Rifles", "Grenades", "Remote Mines", "Grenade Launchers", "Timed Mines", "Proximity Mines", "Rockets", "Lasers", "Golden Gun", "Custom"];
export const scenarios = ["Normal", "You Only Live Twice", "The Living Daylights", "The Man With The Golden Gun", "Licence To Kill", "Team 2v2", "Team 3v1", "Team 2v1", "Team 3v3", "Team 4v4"];
export const screenKinds = [
  ["lobby", "Pre-match lobby", "Get the squad together"],
  ["vote", "Map & weapon voting", "Decide the next operation"],
  ["scoreboard", "Round results", "Give the score room to breathe"],
  ["browser", "Game browser", "Find a session"],
  ["host", "Host / join", "Set up a match"],
  ["loadout", "Character & loadout", "Choose your equipment"],
  ["pause", "In-game pause", "Quick access in VR"],
  ["hud", "Match HUD", "Only the information you need"],
  ["settings", "Multiplayer settings", "Comfort, voice and gameplay"],
  ["blank", "Blank canvas", "Start from scratch"],
];
function header(screen, title, subtitle) {
  screen.nodes.push(
    createNode("image", 48, 40, { name: "Brand mark", w: 48, h: 48, assetId: "brand-icon", fill: "transparent", border: "transparent" }),
    createNode("text", 114, 42, { name: "Brand", text: "GOLDENEYE / ONLINE", fontSize: 21, w: 600, h: 30, color: "accent", font: "mono" }),
    createNode("text", 114, 72, { text: "MULTIPLAYER OPERATIONS", fontSize: 12, w: 600, h: 20, color: "muted", font: "mono" }),
    createNode("badge", 1030, 46, { w: 202, text: "●  ONLINE / EUROPE", fontSize: 13 }),
    createNode("divider", 48, 118, { w: 1184, h: 1 }),
    createNode("text", 48, 151, { name: "Screen title", text: title, w: 900, h: 60, fontSize: 46 }),
    createNode("text", 50, 219, { name: "Screen description", text: subtitle, w: 1000, h: 30, fontSize: 17, color: "muted" }),
  );
}
function footer(screen, text = "A  SELECT     B  BACK     ☰  MENU") {
  screen.nodes.push(createNode("divider", 48, 662, { w: 1184, h: 1 }), createNode("text", 48, 682, { text, w: 1184, h: 20, fontSize: 13, font: "mono", color: "muted" }));
}
export function template(kind) {
  const name = screenKinds.find(k => k[0] === kind)?.[1] || "Blank canvas";
  const s = createScreen(name, kind), add = (type, x, y, options) => s.nodes.push(createNode(type, x, y, options));
  if (kind === "blank") return s;
  if (kind === "lobby") {
    header(s, "YOUR NEXT OPERATION.", "Facility · Normal · Power Weapons · 10 minutes");
    add("roster", 48, 278, { w: 780, h: 280, binding: "netGetLobbyState().slots", notes: "Eight slots maximum (protocol 16). Host migration keeps player identity. Include empty-slot and disconnected states." });
    add("chat", 852, 278, { w: 380, h: 280, subtext: "Bond: One more round?\nNatalya: Ready when you are.\nSystem: Xenia joined the lobby.", binding: "voice + lobby events" });
    add("badge", 48, 598, { text: "3 / 4 READY", w: 160, h: 40, binding: "netLobbyCanLaunch()" });
    add("text", 228, 606, { text: "{{waitingPlayers}}", fontSize: 17, w: 500, h: 28, color: "muted" });
    add("button", 812, 584, { text: "CUSTOMIZE", w: 196, action: "navigate", binding: "local player loadout" });
    add("button", 1024, 584, { text: "READY UP", w: 208, fill: "accent", color: "background", action: "ready", binding: "netLobbySetReady" });
    footer(s, "PRIVATE CODE  GE007     ·     4 PLAYER SESSION                                      B  LEAVE LOBBY");
  } else if (kind === "vote") {
    header(s, "CHOOSE THE NEXT CHAPTER.", "One vote per operative. Your selection can change until the ballot closes.");
    add("countdown", 1016, 152, { w: 216, h: 92, text: "BALLOT CLOSES", count: 15, binding: "next-round countdown" });
    [["Facility", 48, 2, 34], ["Complex", 452, 1, 31], ["Temple", 856, 1, 38]].forEach(([text, x, votes, id], i) => {
      add("vote", x, 280, { w: 376, h: 312, text, subtext: "Normal · 4 operatives", votes, action: "vote", binding: "NET_BALLOT_STAGE", runtimeAsset: "stage:" + id, state: i === 0 ? "selected" : "default", notes: "Use a map screenshot or native stage preview. Preserve each player's own ballot and the native tie-break rule." });
    });
    add("select", 48, 606, { text: "NEXT WEAPONS", subtext: "Power Weapons", w: 560, h: 42, binding: "NET_BALLOT_WEAPONS", action: "cycle", notes: "Weapon ballot is independent of the map ballot." });
    add("button", 1024, 606, { text: "CONFIRM VOTE", w: 208, h: 42, action: "navigate", fill: "accent", color: "background" });
    footer(s, "A  VOTE     ·     MOST VOTES WINS     ·     TIES USE THE GAME'S EXISTING RULE");
  } else if (kind === "scoreboard") {
    header(s, "MISSION COMPLETE.", "Facility · Normal · Final standings");
    add("scoreboard", 48, 280, { w: 1184, h: 292, binding: "match player statistics", notes: "Results have a stable ranking. Distinguish tie, team win, disconnect and spectators." });
    add("badge", 48, 604, { text: "MI6 VICTORY", w: 208, h: 40, color: "success" });
    add("text", 284, 608, { text: "Most lethal  ·  James Bond", fontSize: 18, w: 480, h: 32 });
    add("button", 804, 590, { text: "BACK TO LOBBY", w: 220, action: "navigate" });
    add("button", 1040, 590, { text: "NEXT ROUND", w: 192, action: "navigate", fill: "accent", color: "background" });
    footer(s, "A  CONTINUE     ·     B  RETURN TO WARMUP");
  } else if (kind === "browser") {
    header(s, "FIND YOUR PEOPLE.", "Public sessions. Same game version. No IP addresses needed.");
    add("tabs", 48, 267, { text: "PUBLIC GAMES | PRIVATE CODE | LOCAL NETWORK", w: 720, h: 48 });
    add("server-list", 48, 338, { w: 1184, h: 242, binding: "LobbyClient public list", action: "navigate" });
    add("button", 48, 598, { text: "REFRESH", w: 172, action: "refresh" });
    add("button", 836, 598, { text: "HOST A GAME", w: 184, action: "navigate" });
    add("button", 1036, 598, { text: "JOIN GAME", w: 196, action: "navigate", fill: "accent", color: "background" });
    footer(s);
  } else if (kind === "host") {
    header(s, "SET THE TERMS.", "Your lobby, your rules. Invite up to three friends.");
    add("input", 48, 280, { text: "LOBBY NAME", subtext: "Bond's game", w: 550, binding: "VrMpName" });
    add("select", 48, 372, { text: "SCENARIO", subtext: "Normal", w: 550, action: "cycle", binding: "VrMpScenario" });
    add("select", 48, 464, { text: "WEAPON SET", subtext: "Power Weapons", w: 550, action: "cycle", binding: "VrMpWeaponSet" });
    add("select", 650, 280, { text: "MAP", subtext: "Facility", w: 582, action: "cycle", binding: "VrMpStage" });
    add("select", 650, 372, { text: "ROUND LENGTH", subtext: "10 minutes", w: 582, action: "cycle", binding: "VrMpLength" });
    add("toggle", 650, 464, { text: "Public lobby", value: 100, w: 582, binding: "lobby visibility", action: "toggle" });
    add("button", 982, 590, { text: "CREATE LOBBY", w: 250, fill: "accent", color: "background", action: "navigate" });
    footer(s);
  } else if (kind === "loadout") {
    header(s, "MAKE IT PERSONAL.", "Character, primary weapon, and your four-gun kit.");
    add("model", 48, 278, { text: "JAMES BOND", w: 352, h: 310, runtimeAsset: "character:James Bond", binding: "local character model", notes: "Import a preview OBJ or the JSON generated by tools/gevr_model_export.py. Keep this model reference for the native renderer." });
    add("loadout", 432, 278, { w: 800, h: 232, binding: "player four-gun loadout", subtext: "PP7 (Silenced)|KF7 Soviet|Sniper Rifle|Remote Mine" });
    add("select", 432, 532, { text: "CHARACTER", subtext: "James Bond", w: 380, binding: "VrMpChr", action: "cycle" });
    add("toggle", 852, 532, { text: "Dual wield", w: 380, binding: "match config dual_wield", action: "toggle" });
    add("button", 984, 598, { text: "SAVE LOADOUT", w: 248, fill: "accent", color: "background", action: "navigate" });
    footer(s);
  } else if (kind === "pause") {
    header(s, "TAKE A BREATH.", "The match continues. Stay connected to your squad.");
    ["RESUME MATCH", "LOBBY & NEXT MAP", "LOADOUT", "SETTINGS", "LEAVE MATCH"].forEach((text, i) => add("button", 48, 280 + i * 68, { text, w: 372, h: 52, action: "navigate", fill: i === 0 ? "accent" : "panel", color: i === 0 ? "background" : "text" }));
    add("scoreboard", 452, 280, { w: 780, h: 292, binding: "live match statistics" });
    footer(s);
  } else if (kind === "hud") {
    add("badge", 48, 40, { text: "MI6   12 : 8   JANUS", w: 310, h: 44 });
    add("countdown", 1040, 36, { text: "ROUND TIME", count: 180, w: 192, h: 94 });
    add("chat", 48, 500, { text: "COMMS", subtext: "Natalya eliminated Trevelyan\nBond: Cover the corridor.", w: 368, h: 150, opacity: 0.8 });
    add("text", 984, 606, { text: "PP7   /   07 · 28", w: 248, h: 50, fontSize: 30, align: "right", font: "mono" });
    add("progress", 984, 580, { text: "HEALTH", value: 75, w: 248, h: 14, color: "success" });
    s.notes = "HUD mockup only. Validate sightlines, arm placement and readable angular size on Quest. Keep the center of the view clear.";
  } else if (kind === "settings") {
    header(s, "DIAL IT IN.", "Keep multiplayer comfortable and readable.");
    add("toggle", 48, 290, { text: "Voice chat", value: 100, w: 540, action: "toggle", binding: "netVoice mute" });
    add("slider", 48, 380, { text: "Voice volume", value: 75, w: 540, action: "adjust", binding: "voice gain" });
    add("toggle", 48, 476, { text: "Show player names", value: 100, w: 540, action: "toggle" });
    add("select", 650, 290, { text: "TURNING", subtext: "Snap · 30°", w: 582, action: "cycle", binding: "VR settings" });
    add("slider", 650, 386, { text: "Comfort vignette", value: 30, w: 582, action: "adjust" });
    add("button", 1000, 590, { text: "SAVE & RETURN", w: 232, fill: "accent", color: "background", action: "navigate" });
    footer(s);
  }
  return s;
}
export function starterProject() {
  const screens = ["lobby", "vote", "scoreboard", "browser", "host", "loadout", "pause", "hud", "settings"].map(template);
  const byKind = Object.fromEntries(screens.map(s => [s.kind, s.id]));
  for (const s of screens) for (const n of s.nodes) {
    if (n.action !== "navigate") continue;
    const t = n.text;
    n.targetScreen = byKind[t.includes("LOADOUT") || t === "CUSTOMIZE" ? "loadout" : t.includes("SETTINGS") ? "settings" : t.includes("HOST") ? "host" : t.includes("NEXT ROUND") ? "vote" : t.includes("CONFIRM") ? "scoreboard" : t.includes("RESUME") ? "hud" : t.includes("LEAVE") ? "browser" : "lobby"];
  }
  return {
    format: FORMAT, version: VERSION, name: "GoldenEye · Multiplayer direction",
    notes: "A clearer multiplayer flow: find a game → assemble the squad → choose the next round → celebrate results. Keep original game assets and the gold/charcoal palette. Favor readable text and generous Quest pointer targets.",
    theme: clone(palettes.dossier), screens,
    assets: [
      { id: "brand-icon", name: "GoldenEye VR mark", kind: "image", data: "/repo-assets/icon.png", runtimeAsset: "launcher icon", source: "services/lobbies/public/favicon-64.png" },
      { id: "brand-banner", name: "GoldenEye VR banner", kind: "image", data: "/repo-assets/banner.png", runtimeAsset: "project banner", source: "docs/banner.png" },
      { id: "native-font", name: "ProggyClean · native launcher", kind: "font", data: "/repo-assets/native-ui.ttf", runtimeAsset: "ImGui default bitmap font", source: "port/vr/imgui/imgui_draw.cpp", license: "MIT / Copyright (c) 2004, 2005 Tristan Grimmer" },
    ],
    sample: { players: clone(defaultPlayers), localPlayer: "Xenia", map: "Facility", weaponSet: "Power Weapons", scenario: "Normal", phase: "waiting" },
  };
}
