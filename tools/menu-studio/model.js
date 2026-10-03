export const FORMAT = "gevr-menu-studio";
export const VERSION = 1;
export const uid = (prefix = "node") => prefix + "-" + (crypto.randomUUID ? crypto.randomUUID() : Array.from(crypto.getRandomValues(new Uint8Array(16)), b => b.toString(16).padStart(2,"0")).join(""));
export const clone = value => structuredClone(value);
export const clamp = (n, min, max) => Math.max(min, Math.min(max, n));
export const escapeHTML = text => String(text ?? "").replace(/[&<>"']/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]);
export const palettes = {
  // vr_launcher.cpp: StyleColorsDark(), gold/good/bad, with opaque WindowBg.
  // Frame/Button alpha is composited over #0f0f0f from imgui_draw.cpp.
  launcher: { accent: "#e0b040", heading: "#ffd678", background: "#0f0f0f", panel: "#0f0f0f", text: "#ffffff", muted: "#808080", line: "#3e3e47", frame: "#1d2f49", button: "#23456d", selected: "#3368ad", hover: "#4296fa", success: "#80e680", danger: "#f28066" },
  dossier: { accent: "#e0b040", background: "#0b0a08", panel: "#181612", text: "#ede7da", muted: "#a89f8c", line: "#393125", success: "#93d6a5", danger: "#ef806c" },
  mi6: { accent: "#74aecb", background: "#0a1119", panel: "#131f2a", text: "#e4edf2", muted: "#92a4af", line: "#2b4152", success: "#93d6a5", danger: "#ef806c" },
  terminal: { accent: "#9ecb88", background: "#0b110c", panel: "#162018", text: "#e0ebdc", muted: "#93a38e", line: "#334532", success: "#a6dc93", danger: "#ef806c" },
};
export const componentTypes = [
  ["text", "Text", "Typography"], ["button", "Button", "Controls"], ["panel", "Panel", "Layout"],
  ["image", "Image / texture", "Assets"], ["model", "3D model", "Assets"], ["divider", "Divider", "Layout"],
  ["roster", "Player roster", "Multiplayer"], ["player", "Player card", "Multiplayer"],
  ["server-list", "Game browser", "Multiplayer"], ["vote", "Map / weapon ballot", "Multiplayer"],
  ["objectives", "Mission objectives", "Multiplayer"], ["scoreboard", "Scoreboard", "Multiplayer"], ["loadout", "Weapon loadout", "Multiplayer"],
  ["chat", "Chat / voice panel", "Multiplayer"], ["countdown", "Countdown", "Multiplayer"],
  ["badge", "Status badge", "Multiplayer"], ["tabs", "Navigation tabs", "Controls"],
  ["toggle", "Toggle", "Controls"], ["slider", "Slider", "Controls"], ["select", "Option selector", "Controls"],
  ["input", "Text field", "Controls"], ["progress", "Progress bar", "Controls"],
  ["gauge", "Health / armour arc", "Multiplayer"],
  ["radar", "Live radar", "Multiplayer"],
];
export const defaultRadar = {visible:true,blips:[{x:0,y:0,r:255,g:255,b:255,a:160},{x:-.45,y:-.35,r:255,g:255,b:0,a:160},{x:.3,y:.6,r:255,g:255,b:0,a:160},{x:.8,y:-.6,r:255,g:255,b:0,a:96}]};
export const defaultPlayers = [
  { name: "MrSco", character: "James Bond", team: "Red", ready: true, host: true, ping: 0, kills: 12, deaths: 4, score: 12, voice: true },
  { name: "Natalya", character: "Natalya", team: "Red", ready: true, host: false, ping: 32, kills: 9, deaths: 6, score: 9, voice: true },
  { name: "Trevelyan", character: "Trevelyan", team: "Blue", ready: true, host: false, ping: 24, kills: 7, deaths: 8, score: 7, voice: false },
  { name: "Oddjob", character: "Oddjob", team: "Red", ready: true, host: false, ping: 41, kills: 6, deaths: 7, score: 6, voice: true },
  { name: "Boris", character: "Boris", team: "Blue", ready: true, host: false, ping: 57, kills: 5, deaths: 8, score: 5, voice: false },
  { name: "Ourumov", character: "Ourumov", team: "Red", ready: true, host: false, ping: 28, kills: 3, deaths: 6, score: 3, voice: true },
  { name: "Mayday", character: "Mayday", team: "Blue", ready: true, host: false, ping: 36, kills: 2, deaths: 5, score: 2, voice: false },
  { name: "Xenia", character: "Xenia", team: "Blue", ready: false, host: false, ping: 46, kills: 4, deaths: 10, score: 4, voice: true },
];
export function createNode(type, x = 80, y = 160, overrides = {}) {
  const defaults = {
    text: [400, 56, "YOUR HEADLINE"], button: [240, 56, "CONTINUE"], panel: [420, 260, "Panel"],
    image: [320, 200, "Image"], model: [320, 320, "Model preview"], divider: [500, 2, ""],
    roster: [760, 296, "FIELD OPERATIVES"], player: [240, 240, "James Bond"],
    "server-list": [1120, 344, "AVAILABLE GAMES"], vote: [352, 344, "Facility"],
    scoreboard: [1120, 332, "ROUND RESULTS"], loadout: [760, 260, "YOUR LOADOUT"],
    chat: [320, 240, "SQUAD COMMS"], countdown: [220, 96, "NEXT ROUND"],
    badge: [176, 36, "PUBLIC LOBBY"], tabs: [500, 48, "LOBBY | LOADOUT | SETTINGS"],
    toggle: [280, 52, "Voice chat"], slider: [320, 60, "Voice volume"],
    select: [320, 64, "Weapon set"], input: [320, 64, "Lobby name"],
    objectives: [1244, 222, "MISSION OBJECTIVES"], progress: [320, 36, "Loading"],
    gauge: [60, 86, "HEALTH"],
    radar: [68, 68, "LIVE RADAR"],
  }[type];
  if (!defaults) throw new Error("Unknown component type: " + type);
  return {
    id: uid(), type, name: componentTypes.find(t => t[0] === type)[1], x, y,
    w: defaults[0], h: defaults[1], text: defaults[2], subtext: "", fontSize: 28.6,
    font: "native-font", color: "text", fill: type === "text" || type === "divider" ? "transparent" : type === "button" ? "button" : "panel",
    border: type === "text" ? "transparent" : "line", radius: 0, opacity: 1, align: "left",
    assetId: "", runtimeAsset: "", targetScreen: "", action: "", binding: "",
    visibleWhen: "always", state: "default", locked: false, hidden: false, group: "",
    notes: "", options: "", sampleKey: "", editableBy: "everyone", tabTargets: [], min: 0, max: 100, unit: "%",
    value: 70, votes: 2, yaw: -25, pitch: 10, count: 15, ...overrides,
  };
}
export function createScreen(name = "Untitled screen", kind = "blank") {
  return { id: uid("screen"), name, kind, width: 1280, height: 720, notes: "", nodes: [] };
}
export function bounds(nodes) {
  if (!nodes.length) return null;
  const x = Math.min(...nodes.map(n => n.x)), y = Math.min(...nodes.map(n => n.y));
  return { x, y, w: Math.max(...nodes.map(n => n.x + n.w)) - x, h: Math.max(...nodes.map(n => n.y + n.h)) - y };
}
export function moveNodes(nodes, dx, dy, screen, snap = 0) {
  const b = bounds(nodes);
  if (!b) return;
  dx = clamp(dx, -b.x, screen.width - b.x - b.w);
  dy = clamp(dy, -b.y, screen.height - b.y - b.h);
  if (snap) { dx = Math.round((b.x + dx) / snap) * snap - b.x; dy = Math.round((b.y + dy) / snap) * snap - b.y; }
  dx = clamp(dx, -b.x, screen.width - b.x - b.w);
  dy = clamp(dy, -b.y, screen.height - b.y - b.h);
  for (const n of nodes) { n.x = Math.round(n.x + dx); n.y = Math.round(n.y + dy); }
}
export function alignNodes(nodes, direction, screen) {
  const b = nodes.length > 1 ? bounds(nodes) : { x: 0, y: 0, w: screen.width, h: screen.height };
  for (const n of nodes) {
    if (direction === "left") n.x = b.x;
    if (direction === "center") n.x = b.x + (b.w - n.w) / 2;
    if (direction === "right") n.x = b.x + b.w - n.w;
    if (direction === "top") n.y = b.y;
    if (direction === "middle") n.y = b.y + (b.h - n.h) / 2;
    if (direction === "bottom") n.y = b.y + b.h - n.h;
  }
}
export function distributeNodes(nodes, axis) {
  if (nodes.length < 3) throw new Error("Select at least three elements to distribute.");
  const pos = axis === "x" ? "x" : "y", size = axis === "x" ? "w" : "h";
  const sorted = [...nodes].sort((a, b) => a[pos] - b[pos]);
  const first = sorted[0], last = sorted.at(-1);
  const gap = (last[pos] + last[size] - first[pos] - sorted.reduce((sum, n) => sum + n[size], 0)) / (nodes.length - 1);
  let cursor = first[pos];
  for (const n of sorted) { n[pos] = Math.round(cursor); cursor += n[size] + gap; }
}
export class History {
  constructor(project) { this.items = [this.snapshot(project)]; this.index = 0; }
  snapshot(project) {
    // Asset bytes and geometry are immutable; avoid copying large imports for
    // every keystroke. Metadata objects are still isolated for undo/redo.
    return { ...clone({ ...project, assets: [] }), assets: project.assets.map(a => ({ ...a })) };
  }
  push(project) {
    if (JSON.stringify(this.items[this.index]) === JSON.stringify(project)) return;
    this.items.splice(this.index + 1);
    this.items.push(this.snapshot(project));
    if (this.items.length > 60) this.items.shift();
    this.index = this.items.length - 1;
  }
  undo() { if (this.index > 0) return this.snapshot(this.items[--this.index]); }
  redo() { if (this.index < this.items.length - 1) return this.snapshot(this.items[++this.index]); }
}
export function validateProject(input) {
  if (!input || input.format !== FORMAT || input.version !== VERSION) throw new Error("This is not a supported Menu Studio project (version 1).");
  if (!Array.isArray(input.screens) || !input.screens.length || input.screens.length > 60) throw new Error("A project must have 1–60 screens.");
  const project = clone(input), ids = new Set(), types = new Set(componentTypes.map(t => t[0]));
  project.uiStyle = project.uiStyle || "legacy";
  if (!["legacy", "launcher"].includes(project.uiStyle)) throw new Error("Invalid UI style.");
  for (const screen of project.screens) {
    if (typeof screen.id !== "string" || ids.has(screen.id)) throw new Error("Screen IDs must be unique.");
    ids.add(screen.id);
    for (const key of ["width", "height"]) if (!Number.isFinite(screen[key]) || screen[key] < 240 || screen[key] > 4096) throw new Error("Invalid canvas dimensions.");
    if (!Array.isArray(screen.nodes) || screen.nodes.length > 500) throw new Error("Too many elements in a screen.");
    const nodeIds = new Set();
    for (let i = 0; i < screen.nodes.length; i++) {
      const n = screen.nodes[i];
      if (!types.has(n.type) || typeof n.id !== "string" || nodeIds.has(n.id)) throw new Error("Invalid or duplicate element.");
      nodeIds.add(n.id);
      for (const key of ["x", "y", "w", "h"]) if (!Number.isFinite(n[key])) throw new Error("Invalid element geometry.");
      if (n.w < 1 || n.h < 1 || n.w > 8192 || n.h > 8192 || Math.abs(n.x) > 8192 || Math.abs(n.y) > 8192) throw new Error("Element dimensions are out of range.");
      screen.nodes[i] = { ...createNode(n.type), ...n };
      if (typeof screen.nodes[i].options !== "string" || screen.nodes[i].options.length > 12000) throw new Error("Invalid option list.");
      if (!Array.isArray(screen.nodes[i].tabTargets) || screen.nodes[i].tabTargets.length > 60 || !screen.nodes[i].tabTargets.every(t => typeof t === "string" && t.length < 200)) throw new Error("Invalid tab destinations.");
      if (!["everyone","host","client"].includes(screen.nodes[i].editableBy)) throw new Error("Invalid control authority.");
      const key = screen.nodes[i].sampleKey;
      if (typeof key !== "string" || (key && (!/^[a-z][a-z0-9_]{0,63}$/i.test(key) || ["__proto__","prototype","constructor"].includes(key)))) throw new Error("Invalid sample field.");
      for (const key of ["min","max"]) if (!Number.isFinite(screen.nodes[i][key])) throw new Error("Invalid slider range.");
      if (screen.nodes[i].max < screen.nodes[i].min) throw new Error("Invalid slider range.");
      for (const [key, low, high, fallback] of [["opacity",0,1,1],["fontSize",8,200,20],["radius",0,100,4],["value",0,100,70],["votes",0,16,2],["yaw",-360,360,-25],["pitch",-180,180,10],["count",0,3600,15]]) {
        const value = Number(n[key] ?? fallback);
        if (!Number.isFinite(value)) throw new Error("Invalid element property: " + key);
        screen.nodes[i][key] = clamp(value, low, high);
      }
    }
  }
  project.theme = { ...(project.uiStyle === "launcher" ? palettes.launcher : palettes.dossier), ...project.theme };
  for (const [key, value] of Object.entries(project.theme)) {
    if (!/^#[0-9a-f]{6}$/i.test(value)) throw new Error("Invalid theme color: " + key);
  }
  project.assets = Array.isArray(project.assets) ? project.assets : [];
  if (project.assets.length > 100) throw new Error("Maximum 100 imported assets per project.");
  const assetIds = new Set();
  for (const a of project.assets) {
    if (!a || typeof a.id !== "string" || assetIds.has(a.id) || !["image", "font", "model"].includes(a.kind)) throw new Error("Invalid or duplicate asset.");
    assetIds.add(a.id);
    if (a.kind === "image" && a.data && !/^data:image\/(png|jpeg|webp|gif);base64,/i.test(a.data) && !/^\/repo-assets\/(banner|icon|launcher-icon)\.png$/.test(a.data)) throw new Error("Images must be embedded PNG, JPEG, WebP or GIF previews.");
    if (a.kind === "font" && !/^data:(font\/[a-z0-9.-]+|application\/[a-z0-9.-]+);base64,/i.test(a.data || "") && a.data !== "/repo-assets/native-ui.ttf") throw new Error("Fonts must be embedded font files.");
    if (a.kind === "model") validateMesh(a.mesh);
  }
  project.sample = { players: clone(defaultPlayers), health: 100, armour: 50, radar:clone(defaultRadar), map: "Facility", weaponSet: "Power Weapons", scenario: "Normal", phase: "waiting", mode: "deathmatch", objectives: [], ...project.sample };
  const radar=project.sample.radar;
  if(!radar || typeof radar.visible!=="boolean" || !Array.isArray(radar.blips) || radar.blips.length>8 || !radar.blips.every(b=>b && Number.isFinite(b.x) && Number.isFinite(b.y) && Math.hypot(b.x,b.y)<=1.001 && ["r","g","b","a"].every(k=>Number.isInteger(b[k]) && b[k]>=0 && b[k]<=255)))throw new Error("Invalid sample radar.");
  if (!["deathmatch","coop"].includes(project.sample.mode)) throw new Error("Invalid sample mode.");
  if (!Array.isArray(project.sample.objectives) || project.sample.objectives.length > 10 || !project.sample.objectives.every(o => o && typeof o.text === "string" && ["Incomplete","Complete","Failed"].includes(o.status))) throw new Error("Invalid objectives.");
  if (!Array.isArray(project.sample.players) || project.sample.players.length > 16) throw new Error("Invalid sample roster.");
  for (const p of project.sample.players) {
    if (!p || typeof p.name !== "string") throw new Error("Every sample player needs a name.");
    for (const key of ["character","team"]) if (typeof p[key] !== "string") throw new Error("Sample player " + key + " must be text.");
    for (const key of ["ping","kills","deaths","score"]) if (!Number.isFinite(p[key])) throw new Error("Sample player statistics must be numbers.");
  }
  return project;
}
export function validateMesh(mesh) {
  if (!mesh || !Array.isArray(mesh.vertices) || !Array.isArray(mesh.faces) || !mesh.vertices.length || !mesh.faces.length || mesh.vertices.length > 30000 || mesh.faces.length > 30000) throw new Error("Model must contain 1–30,000 vertices and triangles.");
  if (!mesh.vertices.every(v => Array.isArray(v) && v.length === 3 && v.every(n => Number.isFinite(n)))) throw new Error("Invalid model vertices.");
  if (!mesh.faces.every(f => Array.isArray(f) && f.length === 3 && f.every(n => Number.isInteger(n) && n >= 0 && n < mesh.vertices.length))) throw new Error("Invalid model triangles.");
  return mesh;
}
export function parseOBJ(text) {
  const vertices = [], faces = [];
  for (const line of text.split(/\r?\n/)) {
    const fields = line.trim().split(/\s+/);
    if (fields[0] === "v") vertices.push(fields.slice(1, 4).map(Number));
    if (fields[0] === "f") {
      const indices = fields.slice(1).map(f => { const n = Number(f.split("/")[0]); return n > 0 ? n - 1 : vertices.length + n; });
      for (let i = 1; i < indices.length - 1; i++) faces.push([indices[0], indices[i], indices[i + 1]]);
    }
  }
  return validateMesh({ vertices, faces });
}
export function parseGameModel(input) {
  if (input.vertices && input.faces) return validateMesh(input);
  if (!input.verts || !input.tris) throw new Error("Use an OBJ or JSON from tools/gevr_model_export.py.");
  const vertices = input.verts.map(v => {
    const p = v.pos || v.local || v.xyz;
    const translation = input.matrices?.[String(v.mtx)] || [0, 0, 0];
    if (!Array.isArray(p)) throw new Error("The model vertex is missing its position.");
    return v.pos ? [...p] : p.map((n, i) => n + translation[i]);
  });
  return validateMesh({ vertices, faces: input.tris.map(t => t.v) });
}
export function auditProject(project) {
  const warnings = [], screens = new Set(project.screens.map(s => s.id));
  for (const s of project.screens) {
    for (const n of s.nodes) {
      const label = s.name + " / " + n.name;
      if (n.x < 0 || n.y < 0 || n.x + n.w > s.width || n.y + n.h > s.height) warnings.push(label + ": extends outside the canvas.");
      if (n.tabTargets?.some(t=>t && !screens.has(t))) warnings.push(label + ": tab destination screen is missing.");
      if (n.targetScreen && !screens.has(n.targetScreen)) warnings.push(label + ": destination screen is missing.");
      if (n.type === "button" && !n.targetScreen && !n.action) warnings.push(label + ": choose a destination or describe its action.");
      if (n.type === "image" || n.type === "model") if (!n.assetId && !n.runtimeAsset) warnings.push(label + ": choose an asset or add a runtime reference.");
      if (["button", "toggle", "select", "input"].includes(n.type) && n.h < 44) warnings.push(label + ": small pointer target; check in VR.");
      if (n.type === "text" && n.fontSize < 18) warnings.push(label + ": small text; check headset readability.");
    }
  }
  return warnings;
}
export function handoffMarkdown(project) {
  const output = ["# " + project.name, "", project.notes || "", "", "## Implementation contract", "",
    "Canvas coordinates are logical pixels with origin at the top left. Each element has an explicit bounding rectangle, draw order, theme tokens, content, interaction, visibility rule and runtime binding.",
    "The mockup simulates data locally. Backend actions, voting authority, player limits and actual VR hit testing must be implemented against the existing native multiplayer system.",
    "", "## Theme", "", ...Object.entries(project.theme).map(([k, v]) => "- " + k + ": " + v), "",
    "## Assets", "", ...project.assets.map(a => "- " + a.name + " (" + a.kind + "): " + (a.runtimeAsset || a.source || "imported local preview")), "",
    "## Screens"];
  for (const s of project.screens) {
    output.push("", "### " + s.name + " (" + s.width + " × " + s.height + ")", "", s.notes || "");
    for (const [i, n] of s.nodes.entries()) {
      output.push("", "- " + (i + 1) + ". **" + n.name + "** / " + n.type + " / " + n.id,
        "  - Rectangle: x " + n.x + ", y " + n.y + ", width " + n.w + ", height " + n.h,
        "  - Content: " + n.text + (n.subtext ? " / " + n.subtext : ""),
        "  - Binding: " + (n.binding || "static") + "; visibility: " + n.visibleWhen + "; state: " + n.state,
        "  - Options: " + (n.options || "none") + "; editable by: " + n.editableBy + "; sample field: " + (n.sampleKey || "none"),
        "  - Interaction: " + (n.action || "none") + (n.targetScreen ? " → " + (project.screens.find(s => s.id === n.targetScreen)?.name || n.targetScreen) : ""),
        "  - Runtime asset: " + (n.runtimeAsset || project.assets.find(a => a.id === n.assetId)?.runtimeAsset || "none"),
        "  - Notes: " + (n.notes || "none"));
    }
  }
  const warnings = auditProject(project);
  output.push("", "## Review notes", "", ...(warnings.length ? warnings.map(w => "- " + w) : ["No automatic layout warnings."]), "");
  return output.join("\n");
}
