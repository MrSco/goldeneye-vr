import test from "node:test";
import assert from "node:assert/strict";
import { clone, createNode, History, moveNodes, alignNodes, distributeNodes, validateProject, parseOBJ, parseGameModel, auditProject, handoffMarkdown } from "./model.js";
import { starterProject, stages } from "./templates.js";
import { interpolate, visibleNode } from "./render.js";

test("starter project preserves game player limits and has valid links", () => {
  const p = validateProject(starterProject());
  assert.equal(p.screens.length, 9);
  assert.equal(p.sample.players.length, 4);
  // port/src/net/net_match.c: twice the game's own limits, eight at most (protocol 16)
  assert.equal(stages.find(s => s[0] === "Egypt")[2], 4);
  assert.equal(stages.find(s => s[0] === "Facility")[2], 8);
  assert.ok(stages.every(s => s[2] >= 4 && s[2] <= 8));
  const ids = new Set(p.screens.map(s => s.id));
  assert.ok(p.screens.flatMap(s => s.nodes).every(n => !n.targetScreen || ids.has(n.targetScreen)));
});
test("group dragging snaps once and keeps relative offsets inside canvas", () => {
  const a = createNode("panel", 10,20,{w:100,h:50}), b = createNode("panel",140,40,{w:80,h:30});
  moveNodes([a,b],35,17,{width:300,height:150},8);
  assert.equal(a.x,48); assert.equal(b.x-a.x,130); assert.equal(b.y-a.y,20);
  moveNodes([a,b],200,200,{width:300,height:150},8);
  assert.ok(b.x+b.w<=300); assert.ok(a.y+a.h<=150);
});
test("alignment and distribution act on selected geometry", () => {
  const nodes=[0,40,100].map(x => createNode("button",x,0,{w:20,h:20}));
  distributeNodes(nodes,"x"); assert.equal(nodes[1].x,50);
  alignNodes(nodes,"bottom",{width:300,height:200}); assert.deepEqual(nodes.map(n=>n.y),[0,0,0]);
  alignNodes([nodes[0]],"right",{width:300,height:200}); assert.equal(nodes[0].x,280);
  assert.throws(()=>distributeNodes(nodes.slice(0,2),"x"),/three/);
});
test("undo isolates screen edits and shared asset metadata without recopying binary data", () => {
  const p=starterProject(), history=new History(p), original=p.screens[0].nodes[0].x;
  p.screens[0].nodes[0].x=120; p.assets[0]={...p.assets[0],runtimeAsset:"edited"}; history.push(p);
  const restored=history.undo(); assert.equal(restored.screens[0].nodes[0].x,original); assert.notEqual(restored.assets[0].runtimeAsset,"edited");
  assert.equal(history.redo().screens[0].nodes[0].x,120);
  const after=history.undo(); after.name="New direction"; history.push(after); assert.equal(history.redo(),undefined);
});
test("portable import rejects malformed geometry, version, assets, and data", () => {
  const p=starterProject(); assert.equal(validateProject(JSON.parse(JSON.stringify(p))).format,p.format);
  const invalid=clone(p); invalid.screens[0].nodes[0].w=NaN; assert.throws(()=>validateProject(invalid),/geometry/);
  assert.throws(()=>validateProject({...p,version:2}),/supported/);
  assert.throws(()=>validateProject({...p,assets:[{id:"bad",kind:"image",data:"https://remote.example/image.png"}]}),/embedded/);
  assert.throws(()=>validateProject({...p,sample:{...p.sample,players:[null]}}),/name/);
  const linked=clone(p); linked.screens[0].nodes[0].targetScreen="missing"; assert.ok(auditProject(linked).some(w=>w.includes("destination")));
});
test("OBJ supports polygon triangulation and negative indices", () => {
  const mesh=parseOBJ("v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf -4 -3 -2 -1\n");
  assert.deepEqual(mesh.faces,[[0,1,2],[0,2,3]]);
  assert.throws(()=>parseOBJ("v 0 0 0\nf 1 2 3"),/triangles/);
});
test("native model exports use world positions without double translation", () => {
  const mesh=parseGameModel({verts:[{pos:[10,0,0],mtx:0},{pos:[11,0,0],mtx:0},{pos:[10,1,0],mtx:0}],tris:[{v:[0,1,2]}],matrices:{"0":[10,0,0]}});
  assert.deepEqual(mesh.vertices[0],[10,0,0]);
});
test("handoff includes implementation bindings, asset references and direction", () => {
  const p=starterProject(), text=handoffMarkdown(p);
  assert.match(text,/netLobbySetReady/); assert.match(text,/NET_BALLOT_STAGE/); assert.match(text,/Rectangle: x/); assert.match(text,/four-player|player limits/);
});
test("prototype text and host/client/phase visibility match sample data", () => {
  const p=starterProject();
  assert.equal(interpolate("{{readyCount}} of {{playerCount}} · {{map}}",p),"3 of 4 · Facility");
  assert.equal(visibleNode({visibleWhen:"client",hidden:false},p,"host"),false);
  assert.equal(visibleNode({visibleWhen:"waiting",hidden:false},p,"host"),true);
  assert.equal(visibleNode({visibleWhen:"always",hidden:true},p,"host"),false);
});
