import { FORMAT, VERSION, clone, palettes, createNode, createScreen, defaultPlayers } from "./model.js";

// Protocol 17: all deathmatch stages support eight players; campaign parties four.
export const stages = [["Facility",34],["Complex",31],["Temple",38],["Stack",46],["Caverns",39],["Library",48],["Basement",45],["Caves",50],["Egypt",32],["Bunker II",27],["Archives",24]].map(([name,id])=>[name,id,8]);
export const weapons = ["Slappers only", "Pistols", "Throwing Knives", "Automatics", "Power Weapons", "Sniper Rifles", "Grenades", "Remote Mines", "Grenade Launchers", "Timed Mines", "Proximity Mines", "Rockets", "Lasers", "Golden Gun", "Custom"];
export const scenarios = ["Normal", "You Only Live Twice", "The Living Daylights", "The Man With The Golden Gun", "Licence To Kill", "Team 2v2", "Team 3v1", "Team 2v1", "Team 3v3", "Team 4v4"];
const characters=["James Bond", "Natalya", "Trevelyan", "Xenia", "Ourumov", "Boris", "Valentin", "Mishkin", "Mayday", "Jaws", "Oddjob", "Baron Samedi", "Russian Soldier", "Russian Infantry", "Scientist", "Female Scientist", "Russian Commandant", "Janus Marine", "Naval Officer", "Helicopter Pilot", "St. Petersburg Guard", "Female Civilian", "Civilian", "Civilian 2", "Civilian 3", "Siberian Guard", "Arctic Commando", "Siberian Guard 2", "Siberian Special Forces", "Jungle Commando", "Janus Special Forces", "Moonraker Elite", "Female Moonraker Elite", "Rosika", "Karl", "Martin", "Mark", "Dave", "Duncan", "B", "Steve E", "Grant", "Graeme", "Ken", "Alan", "Pete", "Shaun", "Dwayne", "Des", "Chris", "Lee", "Neil", "Jim", "Robin", "Steve H", "Terrorist", "Biker", "Joel", "Scott", "Joe", "Sally", "Marion", "Mandy", "Vivien"];
const items=["PP7", "PP7 (Silenced)", "DD44 Dostovei", "Cougar Magnum", "Golden Gun", "Klobb", "ZMG (9mm)", "D5K Deutsche", "D5K (Silenced)", "Phantom", "KF7 Soviet", "AR33 Assault Rifle", "RC-P90", "Shotgun", "Automatic Shotgun", "Sniper Rifle", "Moonraker Laser", "Grenade Launcher", "Rocket Launcher", "Hand Grenade", "Throwing Knife", "Hunting Knife", "Remote Mine", "Timed Mine", "Proximity Mine"];
const health=["-10 (Hero)", "-4 (Veteran)", "-3 (Veteran)", "-2 (Veteran)", "-1 (Veteran)", "Normal", "+1 (Novice)", "+2 (Novice)", "+3 (Novice)", "+4 (Novice)", "+10 (Rookie)"];
export const screenKinds = [["match","Match / lobby","Scores, party and next round"],["rules","Match rules","Host rules and fun options"],["player","Player","Character, team and loadout"],["audio","Audio","Volumes and microphone"],["blank","Blank canvas","Start from scratch"]];
export function template(kind="match") {
  // Old template names remain usable by existing callers and exports.
  kind=({lobby:"match",pause:"match",scoreboard:"match",vote:"match",settings:"audio",loadout:"player"})[kind]||kind;
  const s=createScreen(screenKinds.find(k=>k[0]===kind)?.[1]||"Blank canvas",kind);s.height=960;
  const add=(type,x,y,p={})=>{const n=createNode(type,x,y,p);s.nodes.push(n);return n;};
  if(kind==="blank")return s;
  add("image",18,18,{w:64,h:64,assetId:"brand-icon",runtimeAsset:"launcher_icon.rgba",text:"Launcher icon",border:"transparent",state:"selected"});
  add("text",100,32,{w:240,h:40,text:"GOLDENEYE VR",name:"Screen title",color:"heading"});
  ["health","armour"].forEach((key,i)=>{
    add("text",352+i*190,22,{w:170,h:30,text:key.toUpperCase()+"  {{"+key+"}}%",name:key+" header",fontSize:21.45,color:"accent",binding:"gevrPauseLocalVitals"});
    add("progress",352+i*190,60,{w:170,h:14,name:key+" bar",value:i?50:100,sampleKey:key,color:i?"#3366ad":"#f23f14",binding:"gevrPauseLocalVitals"});
  });
  add("text",742,22,{w:518,h:36,text:"{{map}}",color:"accent",binding:"netGetLobbyStage() / netCoopStageName()"});
  add("text",742,60,{w:518,h:36,text:"{{sessionStatus}}",color:"muted"});
  add("divider",18,98,{w:1244,h:2});
  add("tabs",18,122,{w:792,h:48,text:"Match | Rules | Player | Audio",value:["match","rules","player","audio"].indexOf(kind),action:"tab",binding:"GevrPauseUi.tab",border:"transparent",fill:"transparent"});
  const control=(label,value,options,binding,x,y,w=602,extra={})=>add("select",x,y,{text:label,subtext:value,options:options.join("|"),binding,sampleKey:"",editableBy:"host",action:"choose",w,h:48,...extra});
  if(kind==="match") {
    control("MAP","Facility",stages.map(s=>s[0]),"gevrNetConfigSet(CFG_STAGE)",18,206,398,{visibleWhen:"deathmatch",sampleKey:"map"});
    control("WEAPONS","Lasers",weapons,"gevrNetConfigSet(CFG_WEAPON_SET)",440,206,398,{visibleWhen:"deathmatch",sampleKey:"weaponSet"});
    control("SCENARIO","Normal",scenarios,"gevrNetConfigSet(CFG_SCENARIO)",862,206,398,{visibleWhen:"deathmatch",sampleKey:"scenario"});
    add("text",18,206,{w:1244,h:48,text:"CO-OP MISSION   {{mission}} · {{difficulty}}",color:"accent",visibleWhen:"coop",binding:"netCoopStageName / netDifficultyName"});
    add("text",18,276,{w:1244,h:40,text:"PLAYERS & SCORES  ({{playerCount}} / 8)",color:"accent",visibleWhen:"deathmatch"});
    add("scoreboard",18,320,{w:1244,h:382,text:"",binding:"get_points_for_mp_player / kill_counts / netGetSlotPing",visibleWhen:"deathmatch"});
    add("text",18,276,{w:1244,h:40,text:"PARTY  ({{playerCount}} / 4)",color:"accent",visibleWhen:"coop"});
    add("roster",18,320,{w:1244,h:224,text:"",visibleWhen:"coop",binding:"netSlotOccupied / gevrCoopDowned / netGetSlotPing"});
    add("text",18,574,{w:1244,h:40,text:"MISSION OBJECTIVES",color:"accent",visibleWhen:"coop"});
    add("objectives",18,618,{w:1244,h:222,visibleWhen:"coop",binding:"gevrPauseObjective"});
    add("text",18,722,{w:500,h:40,text:"NEXT ROUND",color:"accent",visibleWhen:"deathmatch"});
    control("NEXT ROUND","Vote",["Vote","Shuffle","Playlist"],"gevrNetConfigSet(CFG_NEXT_ROUND)",742,716,518,{visibleWhen:"deathmatch",sampleKey:"nextRound"});
    control("NEXT MAP","No vote",["No vote",...stages.map(s=>s[0])],"netSetLocalVote(NET_BALLOT_STAGE)",18,772,604,{visibleWhen:"deathmatch",editableBy:"everyone",sampleKey:"mapVote",notes:"Independent map ballot. Does not change the active map."});
    control("NEXT WEAPONS","No vote",["No vote",...weapons],"netSetLocalVote(NET_BALLOT_WEAPONSET)",660,772,602,{visibleWhen:"deathmatch",editableBy:"everyone",sampleKey:"weaponVote",notes:"Independent weapon ballot. Does not change the active weapon set."});
    add("text",18,832,{w:1244,h:32,text:"Votes are independent; the host starts the next round.",color:"muted",visibleWhen:"deathmatch"});
  } else if(kind==="rules") {
    const fields=[
      ["PLAYERS","8",["2","3","4","5","6","7","8"],"gevrNetConfigSet(CFG_MAX_PLAYERS)"],
      ["LENGTH","10 minutes",["No limit", "5 minutes", "10 minutes", "20 minutes", "First to 5", "First to 10", "First to 20"],"gevrNetConfigSet(CFG_GAME_LENGTH)"],
      ["HEALTH","Normal",health,"gevrNetConfigSet(CFG_HEALTH)"],
      ["DUAL WIELD","Off",["Off","Doubles","Any two guns"],"gevrNetConfigSet(CFG_DUAL_WIELD)"],
      ["LOADOUTS","Off",null,"gevrNetConfigSet(CFG_LOADOUTS)"],
      ["FRIENDLY FIRE","Off",null,"gevrNetConfigSet(CFG_FRIENDLY_FIRE)"],
      ["HOST EQUALIZATION","On",null,"netSetHostEqualization"],
      ["HOST DELAY CAP","50 ms",["20 ms","40 ms","50 ms","60 ms","80 ms"],"netSetHostEqualization"],
      ["DK MODE","Off",null,"rowFunStep"],["PAINTBALL","Off",null,"rowFunStep"],["LINE MODE","Off",null,"rowFunStep"],
      ["GUN SIZE","Normal",["Normal","Tiny","Big"],"rowGunSizeStep"],
      ...Array.from({length:4},(_,i)=>["CUSTOM "+(i+1),"PP7",items,"gevrNetConfigSet(CFG_CUSTOM0..3)"])
    ];
    fields.forEach(([label,value,opts,binding],i)=>{
      const x=18+i%2*642,y=212+Math.floor(i/2)*76,visibleWhen=["PLAYERS","LENGTH","HEALTH","DUAL WIELD","LOADOUTS"].includes(label)||label.startsWith("CUSTOM")?"deathmatch":"always";
      if(opts)control(label,value,opts,binding,x,y,602,{visibleWhen,sampleKey:"",notes:label.startsWith("CUSTOM")?"Shown when weapon set is Custom":""});
      else add("toggle",x,y,{w:602,h:48,text:label,value:value==="On"?100:0,editableBy:"host",visibleWhen,binding,action:"toggle"});
    });
  } else if(kind==="player") {
    control("CHARACTER","James Bond",characters,"netLobbySetCharacter",18,212,602,{editableBy:"everyone",sampleKey:""});
    control("YOUR TEAM","Unassigned",["Unassigned","Red","Blue"],"netLobbySetTeam",660,212,602,{editableBy:"everyone",sampleKey:"",visibleWhen:"deathmatch"});
    add("toggle",18,288,{w:602,h:48,text:"NEXT ROUND READY",value:100,action:"ready",binding:"netLobbySetReady",visibleWhen:"client-deathmatch"});
    const guns=items;
    for(let i=0;i<4;i++)control("LOADOUT "+(i+1),["PP7","KF7 Soviet","Sniper Rifle","Remote Mine"][i],guns,"netLobbySetLoadout",18+i%2*642,364+Math.floor(i/2)*76,602,{editableBy:"everyone",sampleKey:"",visibleWhen:"deathmatch",notes:"Only editable when the host enables player loadouts."});
    for(let i=0;i<2;i++)add("toggle",18+i*642,516,{w:602,h:48,text:i?"FAV SET":"FAV MAP",value:0,action:"toggle",binding:i?"rowFavSetStep":"rowFavMapStep",visibleWhen:"deathmatch"});
  } else if(kind==="audio") {
    ["MUSIC","SFX","VOICE"].forEach((text,i)=>add("slider",18,212+i*76,{w:1100,h:48,text,value:i?100:36,binding:["set_mTrack2Vol / musicTrack1ApplySeqpVol / musicTrack3ApplySeqpVol","gevrSndApplySfxVolume / VrSfxVolume","VrVoiceVolume / vrSettingsSave"][i],action:"adjust"}));
    control("VOICE MODE","Couch",["Couch","Proximity"],"gevrNetConfigSet(CFG_VOICE_MODE)",18,440,1100,{sampleKey:""});
    add("toggle",18,516,{w:1100,h:48,text:"MIC",value:100,binding:"netVoiceSetMuted",action:"toggle"});
  }
  if(kind!=="match")add("text",18,816,{w:1244,h:40,text:kind==="audio"?"Volumes and microphone are yours. The host chooses voice mode.":kind==="rules"?"Host settings. Round rules and fun options apply on the next load.":"Your character, team and loadout. The match continues while this window is open.",color:"muted"});
  add("divider",18,864,{w:1244,h:2});
  add("button",18,886,{w:244,h:52,text:"Resume",action:"custom",binding:"gevrNativePauseResume",notes:"Close the window. A held trigger must be released before firing."});
  add("button",282,886,{w:300,h:52,text:"Start match",action:"custom",binding:"START MATCH",editableBy:"host",visibleWhen:"host-deathmatch"});
  add("button",282,886,{w:300,h:52,text:"Ready up",action:"ready",binding:"netLobbySetReady",visibleWhen:"client-deathmatch"});
  add("button",600,886,{w:330,h:52,text:"Return to lobby",action:"custom",binding:"RETURN TO LOBBY",editableBy:"host",visibleWhen:"deathmatch"});
  add("button",600,886,{w:330,h:52,text:"End mission…",action:"custom",binding:"netCoopMissionEnded",editableBy:"host",visibleWhen:"host-coop",notes:"Confirm ending the mission for the whole party."});
  add("button",948,886,{w:314,h:52,text:"Leave session…",color:"danger",action:"custom",binding:"gevrLobbySessionStopped / gevrRestartToLauncher",notes:"Confirm disconnecting this headset and returning to the launcher."});
  s.notes="Launcher window in-match: opaque 1280×960 surface; default ProggyClean font at 2.2 scale; white labels, gold values, blue pointer targets. Native fields remain bound to existing host restrictions and net configuration setters. Export this layout to guide native rearrangement.";
  return s;
}
export function starterProject() {
  const screens=["match","rules","player","audio"].map(template);
  for(const s of screens)for(const n of s.nodes)if(n.type==="tabs")n.tabTargets=screens.map(s=>s.id);
  return {format:FORMAT,version:VERSION,uiStyle:"launcher",name:"GoldenEye VR · Shared pause window",notes:"One launcher-style window for eight-player deathmatch and four-player co-op. Direct laser targets; scores, independent ballots and host actions on Match. Co-op replaces ballots with party status and objectives. Match continues while the window is open. JSON exports describe layout for native implementation; they are not loaded by the game automatically.",theme:clone(palettes.launcher),screens,
    assets:[{id:"brand-icon",name:"Native launcher icon",kind:"image",data:"/repo-assets/launcher-icon.png",runtimeAsset:"launcher_icon.rgba",source:"android/app/src/main/assets/launcher_icon.rgba"},{id:"native-font",name:"ProggyClean · native launcher",kind:"font",data:"/repo-assets/native-ui.ttf",runtimeAsset:"ImGui default bitmap font",source:"port/vr/imgui/imgui_draw.cpp",license:"MIT / Tristan Grimmer"}],
    sample:{mode:"deathmatch",players:clone(defaultPlayers),localPlayer:"MrSco",health:100,armour:50,map:"Facility",weaponSet:"Lasers",scenario:"Normal",phase:"warmup",sessionStatus:"Warmup / next round",mission:"Dam",difficulty:"Agent",mapVote:"No vote",weaponVote:"No vote",nextRound:"Vote",objectives:[{text:"Neutralize all alarms",status:"Complete"},{text:"Bungee jump from platform",status:"Incomplete"}]}};
}
