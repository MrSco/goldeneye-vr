import { FORMAT, VERSION, uid, clone, clamp, escapeHTML as e, componentTypes, createNode, bounds, moveNodes, alignNodes, distributeNodes, History, validateProject, parseOBJ, parseGameModel, auditProject, handoffMarkdown, palettes } from "./model.js";
import { starterProject, template, screenKinds, stages, weapons, scenarios } from "./templates.js";
import { renderScene, nodeStyle, fontCSS } from "./render.js";
import { loadSaved, saveProject, downloadFile, embedAssets, readDataURL } from "./storage.js";

const app = document.querySelector("#app"), dialog = document.querySelector("#dialog");
let project = starterProject(), history, activeId, selected = new Set(), dockTab = "screens", inspectorTab = "element";
let preview = false, previewProject, designScreenId, role = "host", scale = 1, zoom = 0, grid = false, safeArea = false, snap = true;
let mobilePanel = "", multiSelect = false, pan = false;
const mobile = () => matchMedia("(max-width: 900px), (pointer: coarse)").matches;
let saveTimer, toastTimer, drag, clipboard = [], assetSearch = "", librarySearch = "", catalog = [], saveState = "Local workspace";
const currentProject = () => preview ? previewProject : project;
const screen = () => currentProject().screens.find(s => s.id === activeId) || currentProject().screens[0];
const selectedNodes = () => screen().nodes.filter(n => selected.has(n.id));
const isTyping = target => target.closest("input,textarea,select,[contenteditable]");
const titleFor = type => componentTypes.find(t => t[0] === type)?.[1] || type;
const symbols = { text:"T", button:"↗", panel:"▣", image:"▧", model:"◇", divider:"—", roster:"♙", player:"♟", "server-list":"☷", vote:"✓", scoreboard:"≡", loadout:"⌑", chat:"☏", countdown:"◷", badge:"●", tabs:"⊟", toggle:"◐", slider:"⊸", select:"⌄", input:"▤", progress:"▰" };
const safeName = value => String(value).replace(/[^a-z0-9-]+/gi, "-").toLowerCase().replace(/^-|-$/g, "") || "menu-project";

function toast(message) {
  const element = document.querySelector("#toast");
  element.textContent = message; element.classList.add("visible");
  clearTimeout(toastTimer); toastTimer = setTimeout(() => element.classList.remove("visible"), 4000);
}
async function attempt(operation) {
  try { await operation(); } catch (error) { console.error(error); toast(error.message || "Something went wrong."); }
}
function saveSoon() {
  clearTimeout(saveTimer);
  saveState = "Saving…";
  if (document.querySelector("#saved-status")) document.querySelector("#saved-status").textContent = saveState;
  saveTimer = setTimeout(async () => {
    try { await saveProject(project); saveState = "● Saved locally"; }
    catch { saveState = "Export to save"; toast("Browser storage is unavailable or full. Export your project to keep it."); }
    if (document.querySelector("#saved-status")) document.querySelector("#saved-status").textContent = saveState;
  }, 500);
}
function commit({ repaint = true } = {}) {
  history.push(project); saveSoon();
  if (repaint) render();
  else updateSelection();
}
function shell() {
  return '<div class="app-shell ' + (mobilePanel ? 'panel-' + mobilePanel : '') + '">' +
    '<header class="topbar"><div class="brand-lockup"><svg class="brand-mark" viewBox="0 0 40 40" aria-hidden="true"><g fill="none" stroke="currentColor" stroke-width="1.5"><circle cx="20" cy="20" r="17"/><circle cx="20" cy="20" r="9"/><path d="M3 20h8M29 20h8M20 3v8M20 29v8"/></g><circle cx="20" cy="20" r="3" fill="currentColor"/></svg><div class="brand-name">MENU STUDIO<span>GOLDENEYE VR / DESIGN WORKSPACE</span></div></div>' +
    '<input id="project-name" class="project-name" aria-label="Project name" value="' + e(project.name) + '">' +
    '<div class="top-actions"><span id="saved-status" class="saved-indicator">' + saveState + '</span><button id="help-button">Guide</button><button id="import-button">Import</button><button id="export-button" class="primary">Export project ↗</button></div></header>' +
    '<main class="workspace"><button class="panel-backdrop" data-do="close-panel" aria-label="Close panel"></button><aside class="dock"><div class="mobile-panel-head"><b>DESIGN TOOLS</b><button data-do="close-panel" aria-label="Close tools">✕</button></div><nav class="dock-tabs" aria-label="Editor panels">' +
    [["screens","Screens"],["library","Add"],["assets","Assets"],["layers","Layers"]].map(([id,title]) => '<button data-dock="' + id + '" class="' + (dockTab === id ? "active" : "") + '">' + title + '</button>').join("") +
    '</nav><div id="dock-content" class="dock-content"></div><div class="dock-footer"><button data-do="add-screen">＋ Screen</button><button data-do="flow">Flow ↗</button></div></aside>' +
    '<section class="main-area"><div class="canvas-toolbar"><div class="canvas-title">' + e(screen().name) + '<small>' + (preview ? "PROTOTYPE" : "DESIGN") + '</small></div>' +
    '<div class="canvas-tools">' + (preview ? '<select id="preview-role" aria-label="Preview role"><option value="host">Host</option><option value="client">Client</option></select><select id="preview-phase" aria-label="Preview phase">' + ["waiting","warmup","in_progress","results"].map(p => '<option ' + (currentProject().sample.phase === p ? "selected" : "") + '>' + p + '</option>').join("") + '</select>' : "") +
    '<button data-do="grid" class="' + (grid ? "active" : "") + '" title="Toggle 16 pixel grid">Grid</button><button data-do="safe" class="' + (safeArea ? "active" : "") + '" title="Show canvas safe area">Safe area</button><button data-do="preview" class="' + (preview ? "active" : "") + '">' + (preview ? "← Back to edit" : "▷ Play flow") + '</button></div></div>' +
    '<div class="edit-tools"><div class="tool-cluster"><button data-do="undo" title="Undo (Ctrl/⌘ Z)" ' + (history.index === 0 || preview ? "disabled" : "") + '>↶</button><button data-do="redo" title="Redo (Ctrl/⌘ Shift Z)" ' + (history.index === history.items.length - 1 || preview ? "disabled" : "") + '>↷</button><span class="tool-divider"></span>' +
    '<button class="mobile-only" data-do="multi-select" aria-pressed="' + multiSelect + '">Select many</button><button data-do="pan" aria-pressed="' + pan + '">' + (pan ? 'Pan ✓' : 'Pan') + '</button><button data-do="duplicate" title="Duplicate selection (Ctrl/⌘ D)">Duplicate</button><button data-do="group" title="Group selected elements">Group</button><button data-do="delete" title="Delete selection">Delete</button></div><div class="tool-cluster">' +
    '<button data-align="left" title="Align left">⊢</button><button data-align="center" title="Align horizontal centers">↔</button><button data-align="right" title="Align right">⊣</button><button data-align="top" title="Align top">⊤</button><button data-align="middle" title="Align vertical centers">↕</button><button data-align="bottom" title="Align bottom">⊥</button><button data-do="distribute-x" title="Distribute horizontally">⋯</button><button data-do="snap" class="' + (snap ? "active" : "") + '" title="Snap to 8 pixel increments">Snap</button></div></div>' +
    '<div id="viewport" class="canvas-viewport ' + (pan ? 'pan-mode' : '') + '"><div id="canvas-holder" class="canvas-holder"><div id="scene" class="scene" aria-label="Editable menu canvas"></div><div id="selection-overlay"></div></div></div>' +
    '<div class="canvas-caption"><span><b>' + screen().width + ' × ' + screen().height + '</b> &nbsp; / &nbsp; ' + (preview ? '<span class="preview-banner">Local simulation · click controls to try the flow</span>' : 'Shift-click to select · drag to move · pull the corner to resize') + '</span><div class="zoom-control"><button data-do="fit" class="quiet">Fit</button><input id="zoom" type="range" min="20" max="250" value="' + Math.round((zoom || scale) * 100) + '" aria-label="Canvas zoom"><button class="zoom-step" data-do="zoom-out" aria-label="Zoom out">−</button><button class="zoom-step" data-do="zoom-in" aria-label="Zoom in">＋</button><span id="zoom-label">' + Math.round(scale * 100) + '%</span></div></div></section>' +
    '<aside id="inspector" class="inspector"><div class="inspector-head"><span>' + (selected.size ? selected.size + ' SELECTED' : 'PROPERTIES') + '</span><button data-do="inspector-tab">' + (inspectorTab === "element" ? "Project" : "Element") + '</button><button class="mobile-only" data-do="close-panel" aria-label="Close properties">✕</button></div><div id="inspector-content" class="inspector-content"></div></aside></main>' +
    '<nav class="mobile-nav" aria-label="Mobile editor">' + [["screens","Screens"],["library","Add"],["layers","Layers"],["properties","Edit"],["more","More"]].map(([id,label]) => '<button data-mobile-panel="' + id + '" class="' + (mobilePanel === id ? 'active' : '') + '">' + label + '</button>').join('') + '</nav><footer class="statusbar"><span>LOCAL WORKSPACE &nbsp; / &nbsp; ' + project.screens.length + ' SCREENS &nbsp; / &nbsp; ' + project.assets.length + ' ASSETS</span><span>Design the experience. Export the direction.</span></footer></div>';
}
function render() {
  app.innerHTML = shell();
  renderDock(); renderInspector(); paintScene();
  if (preview) document.querySelector("#preview-role").value = role;
  fitCanvas();
}
function paintScene() {
  const scene = document.querySelector("#scene"), p = currentProject(), s = screen();
  scene.className = "scene" + (grid ? " show-grid" : "") + (safeArea ? " show-safe" : "") + (preview ? " preview" : "");
  scene.style.width = s.width + "px"; scene.style.height = s.height + "px";
  for (const [key,value] of Object.entries(p.theme)) scene.style.setProperty("--" + key, value);
  scene.replaceChildren(renderScene(p, s, { preview, role }));
  let fonts = document.querySelector("#imported-fonts");
  if (!fonts) { fonts = document.createElement("style"); fonts.id = "imported-fonts"; document.head.append(fonts); }
  fonts.textContent = fontCSS(p);
  updateSelection();
}
function fitCanvas() {
  const viewport = document.querySelector("#viewport");
  if (!viewport) return;
  const s = screen(), padding = mobile() ? 24 : 72;
  scale = zoom || clamp(Math.min((viewport.clientWidth - padding) / s.width, (viewport.clientHeight - padding) / s.height), 0.12, 2.5);
  const holder = document.querySelector("#canvas-holder");
  holder.style.width = s.width * scale + "px"; holder.style.height = s.height * scale + "px";
  document.querySelector("#scene").style.transform = "scale(" + scale + ")";
  document.querySelector("#zoom-label").textContent = Math.round(scale * 100) + "%";
  document.querySelector("#zoom").value = Math.round(scale * 100);
  updateSelection();
}
function updateSelection() {
  const scene = document.querySelector("#scene"), overlay = document.querySelector("#selection-overlay");
  if (!scene || !overlay) return;
  scene.querySelectorAll(".scene-node").forEach(el => el.classList.toggle("selected", !preview && selected.has(el.dataset.id)));
  overlay.replaceChildren();
  if (preview) return;
  const nodes = selectedNodes(), b = bounds(nodes);
  if (!b) return;
  const box = document.createElement("div");
  box.className = "selection-box";
  Object.assign(box.style, { left: b.x * scale + "px", top: b.y * scale + "px", width: b.w * scale + "px", height: b.h * scale + "px" });
  const label = document.createElement("span"); label.className = "selection-label"; label.textContent = Math.round(b.w) + " × " + Math.round(b.h); box.append(label);
  if (nodes.every(n => !n.locked)) { const handle = document.createElement("div"); handle.className = "resize-handle"; handle.dataset.resize = "true"; handle.setAttribute("aria-label", "Resize selected elements"); box.append(handle); }
  overlay.append(box);
}
function renderDock() {
  const target = document.querySelector("#dock-content"), s = screen();
  if (dockTab === "screens") {
    target.innerHTML = '<div class="section-title">Your screens <span>' + project.screens.length + '</span></div>' + project.screens.map((item, i) => '<button class="screen-row ' + (activeId === item.id ? "active" : "") + '" data-screen="' + e(item.id) + '"><span class="screen-number">' + String(i + 1).padStart(2,"0") + '</span><span><strong>' + e(item.name) + '</strong><small>' + item.nodes.length + ' elements · ' + item.width + ' × ' + item.height + '</small></span><span class="screen-more">›</span></button>').join("") +
      '<p class="dock-note">A connected starting point, ready for your direction. Every element can move.</p><div class="button-row"><button data-do="duplicate-screen">Copy screen</button><button data-do="delete-screen" class="danger">Remove</button></div>';
  } else if (dockTab === "library") {
    const groups = [...new Set(componentTypes.map(t => t[2]))];
    target.innerHTML = '<div class="section-title">Add an element</div><input id="library-search" class="search-input" placeholder="Search components…" aria-label="Search components" value="' + e(librarySearch) + '">' +
      groups.map(group => '<div class="library-group"><h3>' + group + '</h3><div class="component-grid">' + componentTypes.filter(t => t[2] === group && t[1].toLowerCase().includes(librarySearch.toLowerCase())).map(([type,title]) => '<button class="component-tile" data-add="' + type + '" draggable="true"><span class="component-symbol">' + symbols[type] + '</span>' + title + '</button>').join("") + '</div></div>').join("") +
      '<p class="dock-note">Click to add, or drag onto the canvas. Customize content and behavior in Properties.</p>';
  } else if (dockTab === "layers") {
    target.innerHTML = '<div class="section-title">Draw order <span>FRONT → BACK</span></div>' + [...s.nodes].reverse().map(n => '<div class="layer-row ' + (selected.has(n.id) ? "active" : "") + (n.hidden ? " muted" : "") + '"><small>' + symbols[n.type] + '</small><button class="layer-name" data-layer="' + e(n.id) + '">' + e(n.name) + (n.group ? " ⊞" : "") + '</button><button class="icon" data-lock="' + e(n.id) + '" title="' + (n.locked ? "Unlock" : "Lock") + '">' + (n.locked ? "▣" : "◇") + '</button><button class="icon" data-hide="' + e(n.id) + '" title="' + (n.hidden ? "Show" : "Hide") + '">' + (n.hidden ? "○" : "●") + '</button></div>').join("") +
      '<div class="button-row"><button data-do="forward">↑ Forward</button><button data-do="backward">↓ Backward</button></div><div class="button-row"><button data-do="ungroup">Ungroup</button><button data-do="distribute-y">Space vertically</button></div>';
  } else {
    const matches = catalog.filter(a => (a.name + " " + a.reference).toLowerCase().includes(assetSearch.toLowerCase())).slice(0, 35);
    target.innerHTML = '<div class="section-title">Project assets</div><button data-do="import-assets" class="full-width">＋ Import images, fonts, models</button><p class="dock-note">Imports stay in this browser and travel inside your exported project. Use your own game assets or captures.</p>' +
      project.assets.map(a => '<div class="asset-card" draggable="true" data-asset="' + e(a.id) + '"><div class="asset-preview">' + (a.kind === "image" ? '<img src="' + e(a.data) + '" alt="">' : a.kind === "font" ? "Aa" : "◇") + '</div><strong>' + e(a.name) + '</strong><small>' + e(a.kind + (a.runtimeAsset ? " · " + a.runtimeAsset : "")) + '</small><div class="asset-card-actions"><button data-use-asset="' + e(a.id) + '">Use</button><button data-reference-asset="' + e(a.id) + '">Reference</button><button data-remove-asset="' + e(a.id) + '" title="Remove asset">×</button></div></div>').join("") +
      '<div class="section-title" style="margin-top:24px">Native asset references</div><input id="asset-search" class="search-input" placeholder="Search textures, models, fonts…" aria-label="Search native assets" value="' + e(assetSearch) + '">' +
      '<p class="dock-note">Names from the repository. Attach a local preview to see ROM-backed visuals.</p>' +
      matches.map(a => '<div class="reference-row"><div>' + e(a.name) + '<code>' + e(a.reference) + '</code></div><button data-native-ref="' + e(a.reference) + '" title="Set reference on selected element">Use</button></div>').join("") + (catalog.length && matches.length === 35 ? '<p class="dock-note">Refine the search to see more references.</p>' : "");
  }
}
function options(values, value) { return values.map(item => { const pair = Array.isArray(item) ? item : [item,item]; return '<option value="' + e(pair[0]) + '" ' + (String(pair[0]) === String(value) ? "selected" : "") + '>' + e(pair[1]) + '</option>'; }).join(""); }
function field(name, label, value, type = "text", choices) {
  const control = choices ? '<select data-prop="' + name + '">' + options(choices, value) + '</select>' : type === "textarea" ? '<textarea data-prop="' + name + '">' + e(value) + '</textarea>' : '<input data-prop="' + name + '" type="' + type + '" value="' + e(value) + '"' + (type === "number" ? ' step="' + (name === "opacity" ? ".05" : "1") + '"' : "") + '>';
  return '<label><span class="form-label">' + label + '</span>' + control + '</label>';
}
function renderInspector() {
  const target = document.querySelector("#inspector-content"), s = screen(), nodes = selectedNodes(), n = nodes[0];
  if (inspectorTab === "project") {
    target.innerHTML = '<section class="inspector-section"><h3>Art direction</h3><label><span class="form-label">Project notes</span><textarea id="project-notes">' + e(project.notes) + '</textarea></label><p class="help-text">Describe the purpose, priorities and mood. These notes are included in the handoff.</p></section>' +
      '<section class="inspector-section"><h3>Shared palette</h3><select id="palette-preset" aria-label="Palette preset"><option value="">Choose a preset…</option>' + options([["dossier","GoldenEye dossier"],["mi6","MI6 blue"],["terminal","Operations green"]],"") + '</select><div class="theme-swatches">' + Object.entries(project.theme).map(([key,value]) => '<label>' + key + '<input data-theme="' + e(key) + '" type="color" value="' + value + '"></label>').join("") + '</div></section>' +
      '<section class="inspector-section"><h3>This screen</h3><label><span class="form-label">Name</span><input id="screen-name" value="' + e(s.name) + '"></label><div class="form-row"><label><span class="form-label">Width</span><input data-screen-size="width" type="number" min="240" max="4096" value="' + s.width + '"></label><label><span class="form-label">Height</span><input data-screen-size="height" type="number" min="240" max="4096" value="' + s.height + '"></label></div><label><span class="form-label">Design & behavior notes</span><textarea id="screen-notes">' + e(s.notes) + '</textarea></label></section>' +
      '<section class="inspector-section"><h3>Preview data & handoff</h3><button data-do="sample-data" class="full-width">Edit sample players & match</button><div class="button-row"><button data-do="review">Review layout</button><button data-do="export-notes">Export notes</button></div><div class="button-row"><button data-do="export-image">Export PNG</button><button data-do="export-svg">Export SVG</button></div><div class="button-row"><button data-do="new-project" class="danger">New project</button></div></section>';
    return;
  }
  if (!n) {
    target.innerHTML = '<div class="empty-inspector"><span class="empty-icon">↖</span><b>Select something to shape it.</b><p>Click an element on the canvas or pick it from Layers.</p></div><section class="inspector-section"><h3>A useful starting point</h3><p class="help-text">Move a player roster, resize a ballot card, change the headlines. Play the flow to try ready states and votes.</p><button data-do="inspector-tab" class="full-width">Edit theme & screen notes</button><div class="button-row"><button data-do="review">Review</button><button data-do="export-image">Export PNG</button></div></section>';
    return;
  }
  const colors = ["text","accent","muted","success","danger","background","panel","line","transparent"];
  const fonts = [["sans","UI sans (preview)"],["mono","UI monospace (preview)"],["serif","Serif (preview)"], ...project.assets.filter(a => a.kind === "font").map(a => [a.id,a.name])];
  target.innerHTML = (nodes.length > 1 ? '<p class="help-text">Editing ' + nodes.length + ' elements. Shared property changes apply to the selection.</p>' : "") +
    '<section class="inspector-section"><h3>' + e(titleFor(n.type)) + '</h3>' + field("name","Layer name",n.name) +
    '<div class="form-row">' + field("x","X",n.x,"number") + field("y","Y",n.y,"number") + '</div><div class="form-row">' + field("w","Width",n.w,"number") + field("h","Height",n.h,"number") + '</div><div class="button-row"><button data-do="forward">↑ Forward</button><button data-do="backward">↓ Back</button></div></section>' +
    '<section class="inspector-section"><h3>Content & style</h3>' + field("text","Text / title",n.text,"textarea") + field("subtext","Detail / items (one chat line, or | between items)",n.subtext,"textarea") +
    '<p class="help-text">Dynamic copy: {{map}}, {{weaponSet}}, {{playerCount}}, {{readyCount}}, {{phase}}.</p>' +
    field("font","Font",n.font,"text",fonts) + '<div class="form-row">' + field("fontSize","Size",n.fontSize,"number") + field("align","Alignment",n.align,"text",["left","center","right"]) + '</div>' +
    '<div class="form-row">' + field("color","Text color",n.color,"text",colors) + field("fill","Fill",n.fill,"text",colors) + '</div><div class="form-row">' + field("border","Border",n.border,"text",colors) + field("radius","Corner radius",n.radius,"number") + '</div>' +
    '<div class="form-row">' + field("opacity","Opacity 0–1",n.opacity,"number") + field("state","Visual state",n.state,"text",["default","hover","focused","selected","disabled","loading","error"]) + '</div>' +
    (["vote","countdown","slider","toggle","progress","tabs"].includes(n.type) ? field(n.type === "vote" ? "votes" : n.type === "countdown" ? "count" : "value", n.type === "vote" ? "Sample votes" : n.type === "countdown" ? "Seconds" : "Value", n.type === "vote" ? n.votes : n.type === "countdown" ? n.count : n.value,"number") : "") + '</section>' +
    '<section class="inspector-section"><h3>Assets & native references</h3>' + field("assetId","Preview asset",n.assetId,"text",[["","None"], ...project.assets.filter(a => n.type === "model" ? a.kind === "model" : a.kind === "image").map(a => [a.id,a.name])]) +
    field("runtimeAsset","Game asset / texture / font reference",n.runtimeAsset) + (n.type === "model" ? '<div class="form-row">' + field("yaw","Model yaw",n.yaw,"number") + field("pitch","Model pitch",n.pitch,"number") + '</div>' : "") +
    '<button data-do="browse-assets" class="full-width" style="margin-top:12px">Browse / import assets</button></section>' +
    '<section class="inspector-section"><h3>Behavior & implementation</h3>' + field("targetScreen","On click → screen",n.targetScreen,"text",[["","Stay on this screen"], ...project.screens.map(s => [s.id,s.name])]) +
    field("action","Action",n.action,"text",[["","No action"],["navigate","Navigate"],["ready","Toggle ready"],["vote","Cast a vote"],["toggle","Toggle option"],["cycle","Cycle option"],["adjust","Adjust value"],["refresh","Refresh games"],["custom","Custom native action"]]) +
    field("binding","Native data / function binding",n.binding) + field("visibleWhen","Show when",n.visibleWhen,"text",["always","host","client","waiting","warmup","in_progress","results"]) +
    field("notes","Purpose & implementation notes",n.notes,"textarea") +
    '<label class="form-label"><input data-prop="locked" type="checkbox" ' + (n.locked ? "checked" : "") + '> Lock position</label><label class="form-label"><input data-prop="hidden" type="checkbox" ' + (n.hidden ? "checked" : "") + '> Hide element</label></section>';
}
function select(id, additive = false, individual = false) {
  const n = screen().nodes.find(n => n.id === id);
  if (!n) return;
  if (!additive) selected.clear();
  const ids = n.group && !individual ? screen().nodes.filter(item => item.group === n.group).map(item => item.id) : [id];
  const remove = additive && selected.has(id);
  for (const item of ids) remove ? selected.delete(item) : selected.add(item);
  inspectorTab = "element"; updateSelection(); renderInspector(); if (dockTab === "layers") renderDock();
}
function addComponent(type, x, y, overrides = {}) {
  if (preview) return toast("Return to edit mode to add elements.");
  if (mobile()) mobilePanel = "";
  const n = createNode(type, x ?? 128, y ?? 280, overrides);
  n.x = clamp(n.x, 0, screen().width - Math.min(n.w, screen().width)); n.y = clamp(n.y, 0, screen().height - Math.min(n.h, screen().height));
  n.w = Math.min(n.w, screen().width); n.h = Math.min(n.h, screen().height);
  screen().nodes.push(n); selected = new Set([n.id]); inspectorTab = "element"; commit();
}
function duplicateSelection() {
  if (preview || !selected.size) return;
  const copies = selectedNodes().map(n => ({ ...clone(n), id: uid(), name: n.name + " copy", x: clamp(n.x + 24,0,Math.max(0,screen().width - n.w)), y: clamp(n.y + 24,0,Math.max(0,screen().height - n.h)), locked: false }));
  const groups = new Map(); for (const n of copies) if (n.group) { if (!groups.has(n.group)) groups.set(n.group,uid("group")); n.group = groups.get(n.group); }
  screen().nodes.push(...copies); selected = new Set(copies.map(n => n.id)); commit();
}
function removeSelection() {
  if (preview) return;
  const removable = new Set(selectedNodes().filter(n => !n.locked).map(n => n.id));
  screen().nodes = screen().nodes.filter(n => !removable.has(n.id)); selected.clear(); commit();
}
function changeLayer(direction) {
  if (preview) return;
  const nodes = screen().nodes;
  const indices = nodes.map((n,i) => selected.has(n.id) ? i : -1).filter(i => i >= 0);
  if (direction > 0) indices.reverse();
  for (const i of indices) {
    const j = i + direction;
    if (j >= 0 && j < nodes.length && !selected.has(nodes[j].id)) [nodes[i],nodes[j]] = [nodes[j],nodes[i]];
  }
  commit();
}
function modal(title, body) {
  dialog.innerHTML = '<div class="dialog-heading"><h2>' + e(title) + '</h2><button data-close aria-label="Close dialog">×</button></div>' + body;
  dialog.showModal();
}
function chooseScreen() {
  modal("What are we designing?", '<p class="dialog-copy">Add a screen, then make it yours. All components stay editable.</p><div class="template-grid">' + screenKinds.map(([kind,title,description]) => '<button class="template-choice" data-template="' + kind + '"><b>' + title + '</b><small>' + description + '</small></button>').join("") + '</div>');
}
function addScreen(kind) {
  if (preview) setPreview(false);
  const s = template(kind); project.screens.push(s); activeId = s.id; selected.clear(); dialog.close(); commit();
}
function setPreview(enabled) {
  pan = false; mobilePanel = "";
  if (enabled) { previewProject = clone(project); designScreenId = activeId; selected.clear(); }
  else { activeId = designScreenId || activeId; previewProject = null; }
  preview = enabled; render();
}
async function portableProject() { return embedAssets(project); }
async function exportProject() {
  clearTimeout(saveTimer);
  const portable = await portableProject();
  downloadFile(safeName(project.name) + ".gevr-menu.json", JSON.stringify(portable,null,2));
  saveSoon(); toast("Project exported. Send this JSON back with your direction.");
}
async function sceneSVG() {
  const p = await portableProject(), s = p.screens.find(s => s.id === activeId) || p.screens[0];
  const wrapper = document.createElement("div");
  wrapper.setAttribute("xmlns","http://www.w3.org/1999/xhtml");
  wrapper.className = "scene preview"; wrapper.style.width = s.width + "px"; wrapper.style.height = s.height + "px"; wrapper.style.position = "relative";
  for (const [key,value] of Object.entries(p.theme)) wrapper.style.setProperty("--" + key,value);
  wrapper.append(renderScene(p,s,{ preview:true,role }));
  const css = globalThis.STUDIO_CSS || await fetch("studio.css").then(r => { if (!r.ok) throw new Error("Cannot load export styles."); return r.text(); });
  const style = document.createElement("style"); style.textContent = css + "\n" + fontCSS(p); wrapper.prepend(style);
  const serialized = new XMLSerializer().serializeToString(wrapper);
  return { svg:'<svg xmlns="http://www.w3.org/2000/svg" width="' + s.width + '" height="' + s.height + '" viewBox="0 0 ' + s.width + ' ' + s.height + '"><foreignObject width="100%" height="100%">' + serialized + '</foreignObject></svg>', width:s.width,height:s.height };
}
async function exportVisual(png = false) {
  const { svg,width,height } = await sceneSVG(), name = safeName(screen().name);
  if (!png) { downloadFile(name + ".svg",svg,"image/svg+xml"); return toast("Screen exported as SVG."); }
  const img = new Image();
  img.src = "data:image/svg+xml;charset=utf-8," + encodeURIComponent(svg);
  await img.decode();
  const canvas = document.createElement("canvas"); canvas.width = width; canvas.height = height;
  canvas.getContext("2d").drawImage(img,0,0);
  const blob = await new Promise(resolve => canvas.toBlob(resolve,"image/png"));
  if (!blob) throw new Error("PNG export failed.");
  downloadFile(name + ".png",blob,"image/png"); toast("Screen exported as PNG.");
}
async function importAssets(files) {
  if (preview) return toast("Return to edit mode before importing assets.");
  for (const file of files) {
    if (project.assets.length >= 100) throw new Error("Maximum 100 assets per project.");
    if (file.size > 8 * 1024 * 1024) { toast(file.name + " exceeds the 8 MB preview limit."); continue; }
    const ext = file.name.split(".").at(-1).toLowerCase(), a = { id: uid("asset"), name:file.name, source:"local import",runtimeAsset:"" };
    if (["png","jpg","jpeg","webp","gif"].includes(ext)) {
      a.kind = "image"; a.data = await readDataURL(file);
      const image = new Image(); image.src = a.data; await image.decode();
    } else if (["ttf","otf","woff","woff2"].includes(ext)) {
      a.kind = "font"; a.data = await readDataURL(new Blob([await file.arrayBuffer()],{type:"font/" + ext}));
      await new FontFace("import-check", 'url("' + a.data + '")').load();
    } else if (["obj","json"].includes(ext)) {
      a.kind = "model"; a.mesh = ext === "obj" ? parseOBJ(await file.text()) : parseGameModel(JSON.parse(await file.text()));
      a.runtimeAsset = ext === "json" ? JSON.parse(await file.text()).model || "" : "";
    } else { toast("Unsupported preview format: " + file.name); continue; }
    const total = project.assets.reduce((sum,item) => sum + (item.data?.length || JSON.stringify(item.mesh || {}).length),0);
    if (total + (a.data?.length || JSON.stringify(a.mesh || {}).length) > 64 * 1024 * 1024) throw new Error("Project previews exceed 64 MB. Use smaller captures or runtime references.");
    project.assets.push(a);
  }
  dockTab = "assets"; commit(); toast("Assets imported. Select an element and choose Use, or drag an asset onto the canvas.");
}
function useAsset(id, x, y, forceNew = false) {
  const a = project.assets.find(a => a.id === id);
  if (!a || preview) return;
  const nodes = selectedNodes();
  if (nodes.length && !forceNew) {
    for (const n of nodes) {
      if (a.kind === "font") n.font = a.id;
      else { n.assetId = a.id; if (a.runtimeAsset) n.runtimeAsset = a.runtimeAsset; }
    }
    commit();
  } else if (a.kind === "font") addComponent("text",x,y,{text:"YOUR TYPOGRAPHY",font:a.id});
  else addComponent(a.kind === "model" ? "model" : "image",x,y,{assetId:a.id,runtimeAsset:a.runtimeAsset,text:a.name});
}
function previewAction(n, target) {
  const p = currentProject();
  if (n.state === "disabled" || n.state === "loading") return;
  const action = n.action || target.dataset.interact;
  if (action === "ready") {
    const me = p.sample.players.find(player => player.name === p.sample.localPlayer) || p.sample.players[0]; if (me) me.ready = !me.ready;
    const count = p.sample.players.filter(player => player.ready).length;
    for (const node of screen().nodes) {
      if (node.binding === "netLobbyCanLaunch()") node.text = count + " / " + p.sample.players.length + " READY";
      if (node.action === "ready") node.text = me?.ready ? "UNREADY" : "READY UP";
    }
  } else if (action === "vote") {
    for (const node of screen().nodes.filter(node => node.type === "vote" && node.binding === n.binding)) {
      if (node.state === "selected") node.votes = Math.max(0,node.votes - 1);
      node.state = "default";
    }
    n.state = "selected"; n.votes += 1;
    if (/WEAPON/i.test(n.binding)) p.sample.weaponSet = n.text; else p.sample.map = n.text;
  } else if (action === "toggle") n.value = n.value ? 0 : 100;
  else if (action === "cycle") {
    const choices = /map/i.test(n.text) ? stages.map(s => s[0]) : /scenario/i.test(n.text) ? scenarios : /character/i.test(n.text) ? p.sample.players.map(player => player.character) : /length/i.test(n.text) ? ["5 minutes","10 minutes","20 minutes"] : weapons;
    n.subtext = choices[(choices.indexOf(n.subtext) + 1) % choices.length];
    if (/WEAPON/i.test(n.binding)) p.sample.weaponSet = n.subtext;
  } else if (target.dataset.interact === "tab") n.value = Number(target.dataset.index);
  else if (action === "custom") toast(n.notes || n.binding || "Custom action: add its behavior in Properties.");
  else if (action === "refresh") toast("Local mock data refreshed. This prototype does not contact the live lobby service.");
  if (n.targetScreen) { activeId = n.targetScreen; render(); }
  else paintScene();
}
function showFlow() {
  const links = project.screens.map(s => '<div class="flow-line"><strong>' + e(s.name) + '</strong>' + s.nodes.filter(n => n.targetScreen || n.action).map(n => '<div>' + e(n.text || n.name) + ' → ' + e(project.screens.find(t => t.id === n.targetScreen)?.name || n.action) + '</div>').join("") + '</div>').join("");
  modal("The experience, screen by screen", '<p class="dialog-copy">Destinations and actions are editable in each element’s Properties. Play flow runs a local simulation.</p>' + links);
}
function showHelp() {
  modal("A workspace for your multiplayer direction", '<p class="dialog-copy">Start with the lobby → vote → results screens, or add your own. The aim is to give the native redesign a clear layout, behavior and visual direction.</p><ol class="dialog-list"><li>Select and drag anything. Shift-click for multiple elements; Alt-click selects one member of a group. Resize with the gold corner handle.</li><li>Add reusable components from <b>Add</b>. Use <b>Layers</b> to reorder, lock and hide elements. Undo/redo and copy/paste work across screens.</li><li>Set titles, colors, fonts, preview states and visibility in Properties. Add bindings and notes to explain what each element should do.</li><li>Use <b>Assets</b> for local PNG/JPEG/WebP/GIF images, TTF/OTF/WOFF fonts, OBJ models, or model JSON from <code>tools/gevr_model_export.py</code>. Models preview geometry; materials and animation stay native.</li><li>Game assets that are loaded from the ROM are listed as references. Import your own captures or exports to preview them. No ROM is uploaded to a server.</li><li>Try <b>Play flow</b> to click between screens, toggle ready, vote and test controls. Sample data is editable from Project Properties.</li><li><b>Export project</b> saves a portable JSON with layout, imported previews, theme, actions and notes. Send that file back for implementation. Export PNG/SVG for discussion and Markdown for a written brief.</li></ol><p class="dialog-copy">Shortcuts: Ctrl/⌘ Z undo · Shift Z redo · D duplicate · C/V copy/paste · G group · Shift G ungroup · arrows nudge · Shift arrows move 8px · Delete remove · Esc clear selection.</p><p class="dialog-copy">On phones, use the bottom tabs for screens, components, layers and properties. Zoom in for precise changes; Pan lets you scroll the enlarged canvas. Select many works by tapping elements. Landscape gives you more room.<br><br>Autosave uses this browser’s storage. Export your work before clearing browser data or moving to another device.</p>');
}
async function doAction(action) {
  if (action === "preview") return setPreview(!preview);
  if (action === "fit") { zoom = 0; return fitCanvas(); }
  if (action === "close-panel") { mobilePanel = ""; return render(); }
  if (action === "zoom-in" || action === "zoom-out") { zoom = clamp(scale * (action === "zoom-in" ? 1.4 : 1 / 1.4), .2, 2.5); return fitCanvas(); }
  if (action === "pan") { pan = !pan; return render(); }
  if (action === "multi-select") { multiSelect = !multiSelect; return render(); }
  if (action === "help") return showHelp();
  if (action === "import-project") return document.querySelector("#project-file").click();
  if (action === "grid" || action === "safe" || action === "snap") { if (action === "grid") grid = !grid; if (action === "safe") safeArea = !safeArea; if (action === "snap") snap = !snap; return render(); }
  if (action === "properties") { mobilePanel = mobilePanel === "properties" ? "" : "properties"; return render(); }
  if (action === "flow") return showFlow();
  if (action === "inspector-tab") { inspectorTab = inspectorTab === "element" ? "project" : "element"; return render(); }
  if (action === "browse-assets") { dockTab = "assets"; if (mobile()) { mobilePanel = "assets"; return render(); } return renderDock(); }
  if (action === "export-image") return exportVisual(true);
  if (action === "export-svg") return exportVisual(false);
  if (action === "export-notes") return downloadFile(safeName(project.name) + "-handoff.md",handoffMarkdown(project),"text/markdown");
  if (action === "review") {
    const warnings = auditProject(project);
    return modal("Layout review", '<p class="dialog-copy">Check these before handing the design over. Actual headset readability and native interactions need playtesting.</p><ul class="review-list">' + (warnings.length ? warnings.map(w => '<li>' + e(w) + '</li>').join("") : "<li>No automatic layout warnings.</li>") + '</ul>');
  }
  if (preview) return toast("Return to edit mode to change the design.");
  if (action === "undo" || action === "redo") {
    const changed = action === "undo" ? history.undo() : history.redo();
    if (changed) { project = changed; if (!project.screens.some(s => s.id === activeId)) activeId = project.screens[0].id; selected.clear(); saveSoon(); render(); }
  } else if (action === "duplicate") duplicateSelection();
  else if (action === "delete") removeSelection();
  else if (action === "forward") changeLayer(1);
  else if (action === "backward") changeLayer(-1);
  else if (action === "add-screen") chooseScreen();
  else if (action === "group" || action === "ungroup") {
    if (action === "group" && selected.size < 2) return toast("Select at least two elements to group.");
    const group = action === "group" ? uid("group") : "";
    for (const n of selectedNodes()) n.group = group;
    commit();
  } else if (action.startsWith("distribute-")) { distributeNodes(selectedNodes().filter(n => !n.locked),action.endsWith("x") ? "x" : "y"); commit(); }
  else if (action === "duplicate-screen") {
    const s = clone(screen()), previousId = s.id; s.id = uid("screen"); s.name += " copy";
    const groups = new Map();
    for (const n of s.nodes) { n.id = uid(); if (n.targetScreen === previousId) n.targetScreen = s.id; if (n.group) { if (!groups.has(n.group)) groups.set(n.group,uid("group")); n.group = groups.get(n.group); } }
    project.screens.push(s); activeId = s.id; selected.clear(); commit();
  } else if (action === "delete-screen") {
    if (project.screens.length < 2) return toast("Keep at least one screen in the project.");
    modal("Remove " + screen().name + "?", '<p class="dialog-copy">The screen and its elements will be removed. Undo can restore them. Links to it will be cleared.</p><button data-confirm-delete class="danger">Remove screen</button>');
  } else if (action === "import-assets") document.querySelector("#asset-files").click();
  else if (action === "sample-data") modal("Preview data", '<p class="dialog-copy">Edit mock players and match fields. This data drives roster, scoreboard and dynamic text previews. It does not change the game.</p><textarea id="sample-json" class="json-editor" spellcheck="false">' + e(JSON.stringify(project.sample,null,2)) + '</textarea><div class="button-row"><button data-save-sample class="primary">Apply sample data</button></div>');
  else if (action === "new-project") modal("Start a fresh direction?", '<p class="dialog-copy">Export the current project first if you want a separate copy. This resets the design to the starter screens.</p><button data-confirm-new class="primary">Create starter project</button>');
}

app.addEventListener("click", event => attempt(async () => {
  const target = event.target.closest("button,[data-interact]");
  if (!target) return;
  if (target.dataset.mobilePanel) {
    const panel = target.dataset.mobilePanel;
    if (panel === "more") return modal("Your workspace", '<div class="mobile-menu"><button data-do="help">Guide</button><button data-do="import-project">Import project</button><button data-do="browse-assets">Assets & imports</button><button data-mobile-project>Theme & screen notes</button><button data-do="flow">Screen flow</button><button data-do="export-image">Save PNG</button><button data-do="export-notes">Save handoff notes</button></div><p class="dialog-copy">Zoom for detail. Turn on Pan to scroll around the canvas. Select many lets you tap several elements to group or align them. Landscape gives you more room.</p>');
    mobilePanel = mobilePanel === panel ? "" : panel;
    if (["screens","library","layers"].includes(panel)) dockTab = panel;
    render(); return;
  }
  if (target.id === "help-button") return showHelp();
  if (target.id === "import-button") return document.querySelector("#project-file").click();
  if (target.id === "export-button") return exportProject();
  if (target.dataset.dock) { dockTab = target.dataset.dock; render(); return; }
  if (target.dataset.screen) { activeId = target.dataset.screen; selected.clear(); mobilePanel = ""; zoom = 0; render(); return; }
  if (target.dataset.add) return addComponent(target.dataset.add);
  if (target.dataset.do) return doAction(target.dataset.do);
  if (target.dataset.align && !preview) { alignNodes(selectedNodes().filter(n => !n.locked),target.dataset.align,screen()); commit(); return; }
  if (target.dataset.layer) { select(target.dataset.layer,event.shiftKey || multiSelect,event.altKey); if (mobile() && !multiSelect) { mobilePanel = ""; render(); } return; }
  if (target.dataset.lock || target.dataset.hide) {
    if (preview) return;
    const n = screen().nodes.find(n => n.id === (target.dataset.lock || target.dataset.hide));
    const key = target.dataset.lock ? "locked" : "hidden"; n[key] = !n[key]; commit(); return;
  }
  if (target.dataset.useAsset) return useAsset(target.dataset.useAsset);
  if (target.dataset.referenceAsset) {
    const a = project.assets.find(a => a.id === target.dataset.referenceAsset);
    return modal("Native asset reference", '<p class="dialog-copy">Keep the identifier the game will use for this preview: texture name, model file, font table or source path.</p><input id="asset-reference" value="' + e(a.runtimeAsset) + '" aria-label="Native asset reference"><div class="button-row"><button data-save-reference="' + e(a.id) + '" class="primary">Save reference</button></div>');
  }
  if (target.dataset.removeAsset && !preview) {
    const id = target.dataset.removeAsset;
    project.assets = project.assets.filter(a => a.id !== id);
    for (const s of project.screens) for (const n of s.nodes) { if (n.assetId === id) n.assetId = ""; if (n.font === id) n.font = "sans"; }
    commit(); return;
  }
  if (target.dataset.nativeRef) {
    if (preview || !selected.size) return toast("Select an element first, then assign the native reference.");
    for (const n of selectedNodes()) n.runtimeAsset = target.dataset.nativeRef;
    commit(); return;
  }
  if (preview && target.dataset.interact) {
    if (["input","slider"].includes(target.dataset.interact)) return;
    const n = screen().nodes.find(n => n.id === target.closest(".scene-node")?.dataset.id);
    if (n) previewAction(n,target);
  }
}));
app.addEventListener("change", event => attempt(async () => {
  const target = event.target;
  if (target.id === "preview-role") { role = target.value; return paintScene(); }
  if (target.id === "preview-phase") { previewProject.sample.phase = target.value; return paintScene(); }
  if (preview) {
    const n = screen().nodes.find(n => n.id === target.closest(".scene-node")?.dataset.id);
    if (n && target.dataset.interact === "slider") { n.value = Number(target.value); paintScene(); }
    if (n && target.dataset.interact === "input") n.subtext = target.value;
    return;
  }
  if (target.dataset.prop) {
    const key = target.dataset.prop, numeric = target.type === "number";
    const value = target.type === "checkbox" ? target.checked : numeric ? Number(target.value) : target.value;
    if (numeric && !Number.isFinite(value)) return toast("Enter a valid number.");
    const limits = { w:[8,screen().width],h:[2,screen().height],x:[0,screen().width],y:[0,screen().height],fontSize:[8,200],opacity:[0,1],radius:[0,100],value:[0,100],votes:[0,16],yaw:[-360,360],pitch:[-180,180],count:[0,3600] };
    for (const n of selectedNodes()) {
      if (n.locked && ["x","y","w","h"].includes(key)) continue;
      n[key] = numeric && limits[key] ? clamp(value,...limits[key]) : value;
      if (["x","w"].includes(key)) n.x = Math.min(n.x,screen().width - n.w);
      if (["y","h"].includes(key)) n.y = Math.min(n.y,screen().height - n.h);
    }
    commit();
  } else if (target.dataset.theme) { project.theme[target.dataset.theme] = target.value; commit(); }
  else if (target.dataset.screenSize) {
    const value = Number(target.value); if (!Number.isFinite(value)) throw new Error("Enter a valid canvas size.");
    screen()[target.dataset.screenSize] = clamp(Math.round(value),240,4096); commit();
  } else if (target.id === "palette-preset" && palettes[target.value]) { project.theme = clone(palettes[target.value]); commit(); }
  else if (target.id === "project-name") { project.name = target.value || "Untitled direction"; commit(); }
  else if (target.id === "project-notes") { project.notes = target.value; commit(); }
  else if (target.id === "screen-name") { screen().name = target.value || "Untitled screen"; commit(); }
  else if (target.id === "screen-notes") { screen().notes = target.value; commit(); }
}));
app.addEventListener("input", event => {
  if (event.target.id === "zoom") { zoom = Number(event.target.value)/100; fitCanvas(); }
  if (event.target.id === "library-search" || event.target.id === "asset-search") {
    const input = event.target, pos = input.selectionStart;
    if (input.id === "library-search") librarySearch = input.value; else assetSearch = input.value;
    renderDock(); const replacement = document.getElementById(input.id); replacement.focus(); replacement.setSelectionRange(pos,pos);
  }
});
app.addEventListener("dblclick", event => {
  if (preview) return;
  const node = event.target.closest(".scene-node");
  if (node) { select(node.dataset.id,false,true); document.querySelector('[data-prop="text"]')?.focus(); }
});
app.addEventListener("pointerdown", event => {
  if (preview || pan || drag || event.button !== 0 || !event.isPrimary || isTyping(event.target)) return;
  const handle = event.target.closest("[data-resize]"), element = event.target.closest(".scene-node");
  if (!handle && !element) { if (event.target.closest("#scene")) { selected.clear(); updateSelection(); renderInspector(); } return; }
  if (element && !selected.has(element.dataset.id)) select(element.dataset.id,event.shiftKey || multiSelect,event.altKey);
  else if (element && (event.shiftKey || multiSelect)) { select(element.dataset.id,true,event.altKey); return; }
  const nodes = selectedNodes().filter(n => !n.locked);
  if (!nodes.length) return;
  drag = { pointerId:event.pointerId, kind:handle ? "resize" : "move", startX:event.clientX,startY:event.clientY,nodes:nodes.map(n => ({id:n.id,x:n.x,y:n.y,w:n.w,h:n.h})), bounds:bounds(nodes), changed:false };
  app.setPointerCapture(event.pointerId);
  event.preventDefault();
});
window.addEventListener("pointermove", event => {
  if (!drag || drag.pointerId !== event.pointerId) return;
  let dx = (event.clientX - drag.startX)/scale, dy = (event.clientY - drag.startY)/scale;
  if (Math.hypot(event.clientX - drag.startX, event.clientY - drag.startY) < 5 && !drag.changed) return;
  drag.changed = true;
  const nodes = drag.nodes.map(origin => { const n = screen().nodes.find(n => n.id === origin.id); Object.assign(n,origin); return n; });
  if (drag.kind === "move") moveNodes(nodes,dx,dy,screen(),snap && !event.altKey ? 8 : 0);
  else {
    if (snap && !event.altKey) { dx = Math.round(dx/8)*8; dy = Math.round(dy/8)*8; }
    const b = drag.bounds;
    const width = clamp(b.w + dx,24,screen().width - b.x), height = clamp(b.h + dy,24,screen().height - b.y);
    for (const n of nodes) { n.x = Math.round(b.x + (n.x - b.x)*width/b.w); n.y = Math.round(b.y + (n.y - b.y)*height/b.h); n.w = Math.max(2,Math.round(n.w*width/b.w)); n.h = Math.max(2,Math.round(n.h*height/b.h)); }
  }
  for (const n of nodes) Object.assign(document.querySelector('[data-id="' + CSS.escape(n.id) + '"]').style,nodeStyle(project,n));
  updateSelection();
});
window.addEventListener("pointerup", event => { if (!drag || drag.pointerId !== event.pointerId) return; const changed = drag.changed; drag = null; if (app.hasPointerCapture(event.pointerId)) app.releasePointerCapture(event.pointerId); if (changed) commit(); });
window.addEventListener("pointercancel", event => { if (drag && drag.pointerId === event.pointerId) { for (const origin of drag.nodes) Object.assign(screen().nodes.find(n => n.id === origin.id),origin); drag = null; paintScene(); } });
app.addEventListener("dragstart", event => {
  const component = event.target.closest("[data-add]"), asset = event.target.closest("[data-asset]");
  if (component) event.dataTransfer.setData("application/x-gevr-component",component.dataset.add);
  else if (asset) event.dataTransfer.setData("application/x-gevr-asset",asset.dataset.asset);
});
app.addEventListener("dragover", event => { if (event.target.closest("#viewport") && !preview) event.preventDefault(); });
app.addEventListener("drop", event => attempt(async () => {
  if (!event.target.closest("#viewport") || preview) return;
  event.preventDefault();
  const rect = document.querySelector("#scene").getBoundingClientRect(), x = Math.round((event.clientX - rect.left)/scale), y = Math.round((event.clientY - rect.top)/scale);
  const type = event.dataTransfer.getData("application/x-gevr-component"), id = event.dataTransfer.getData("application/x-gevr-asset");
  if (type) addComponent(type,x,y);
  else if (id) useAsset(id,x,y,true);
  else if (event.dataTransfer.files.length) await importAssets(event.dataTransfer.files);
}));
window.addEventListener("keydown", event => attempt(async () => {
  if (dialog.open || isTyping(event.target)) return;
  const mod = event.ctrlKey || event.metaKey, key = event.key.toLowerCase();
  if (event.key === "Escape") { selected.clear(); updateSelection(); renderInspector(); return; }
  if (preview) return;
  if (mod && ["z","y","d","g","c","v","a","s"].includes(key)) event.preventDefault();
  if (mod && key === "z") return doAction(event.shiftKey ? "redo" : "undo");
  if (mod && key === "y") return doAction("redo");
  if (mod && key === "s") return exportProject();
  if (mod && key === "d") return duplicateSelection();
  if (mod && key === "g") return doAction(event.shiftKey ? "ungroup" : "group");
  if (mod && key === "a") { selected = new Set(screen().nodes.filter(n => !n.locked && !n.hidden).map(n => n.id)); updateSelection(); renderInspector(); return; }
  if (mod && key === "c") { clipboard = clone(selectedNodes()); toast("Selection copied. Paste onto any screen."); return; }
  if (mod && key === "v" && clipboard.length) {
    const groupMap = new Map();
    const nodes = clipboard.map(n => { const copy = {...clone(n),id:uid(),x:clamp(n.x+24,0,screen().width-n.w),y:clamp(n.y+24,0,screen().height-n.h),locked:false}; if (copy.group) { if (!groupMap.has(copy.group)) groupMap.set(copy.group,uid("group")); copy.group=groupMap.get(copy.group); } return copy; });
    screen().nodes.push(...nodes); selected = new Set(nodes.map(n => n.id)); commit(); return;
  }
  if (event.key === "Delete" || event.key === "Backspace") { event.preventDefault(); return removeSelection(); }
  if (["ArrowLeft","ArrowRight","ArrowUp","ArrowDown"].includes(event.key) && selected.size) {
    event.preventDefault(); const amount = event.shiftKey ? 8 : 1;
    moveNodes(selectedNodes().filter(n => !n.locked),event.key === "ArrowLeft" ? -amount : event.key === "ArrowRight" ? amount : 0,event.key === "ArrowUp" ? -amount : event.key === "ArrowDown" ? amount : 0,screen()); commit();
  }
}));
dialog.addEventListener("click", event => attempt(async () => {
  const target = event.target.closest("button");
  if (!target) return;
  if (target.hasAttribute("data-close")) return dialog.close();
  if (target.dataset.do) { dialog.close(); return doAction(target.dataset.do); }
  if (target.hasAttribute("data-mobile-project")) { dialog.close(); inspectorTab = "project"; mobilePanel = "properties"; return render(); }
  if (target.dataset.template) return addScreen(target.dataset.template);
  if (target.hasAttribute("data-save-sample")) {
    const candidate = { ...project,sample:JSON.parse(document.querySelector("#sample-json").value) };
    project = validateProject(candidate); dialog.close(); commit();
  }
  if (target.dataset.saveReference) {
    project.assets = project.assets.map(a => a.id === target.dataset.saveReference ? {...a,runtimeAsset:document.querySelector("#asset-reference").value} : a);
    dialog.close(); commit();
  }
  if (target.hasAttribute("data-confirm-delete")) {
    const removed = activeId; project.screens = project.screens.filter(s => s.id !== removed);
    for (const s of project.screens) for (const n of s.nodes) if (n.targetScreen === removed) n.targetScreen = "";
    activeId = project.screens[0].id; selected.clear(); dialog.close(); commit();
  }
  if (target.hasAttribute("data-confirm-new")) { project = starterProject(); activeId = project.screens[0].id; selected.clear(); dialog.close(); commit(); }
}));
document.querySelector("#asset-files").addEventListener("change", event => attempt(async () => { await importAssets(event.target.files); event.target.value = ""; }));
document.querySelector("#project-file").addEventListener("change", event => attempt(async () => {
  const file = event.target.files[0]; if (!file) return;
  if (file.size > 90 * 1024 * 1024) throw new Error("Project exceeds the 90 MB import limit.");
  const imported = validateProject(JSON.parse(await file.text()));
  if (preview) setPreview(false);
  project = imported; activeId = project.screens[0].id; selected.clear(); commit(); event.target.value = ""; toast("Project imported. All screens, assets and direction restored.");
}));
window.addEventListener("resize",fitCanvas);
window.addEventListener("beforeunload",() => { if (saveTimer) saveProject(project).catch(() => {}); });

app.innerHTML = '<div style="padding:40px;color:#dfb45c">Opening your design workspace…</div>';
try { const saved = await loadSaved(); if (saved) { project = validateProject(saved); saveState = "● Saved locally"; } } catch { saveState = "Export to save"; }
history = new History(project); activeId = project.screens[0].id; render();
try { catalog = globalThis.STUDIO_CATALOG || await fetch("catalog.json").then(r => r.ok ? r.json() : []); if (dockTab === "assets") renderDock(); } catch { catalog = []; }
// Useful for automated tests and local design inspection; contains no live game state.
globalThis.MenuStudio = { getProject:() => clone(project), getScreen:() => clone(screen()), select:id => select(id), addComponent, setPreview, exportProject, exportVisual, audit:() => auditProject(project) };
