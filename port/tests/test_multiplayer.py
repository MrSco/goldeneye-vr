"""Windows native regression checks. Run: python port/tests/test_multiplayer.py
Requires gcc on PATH and the pinned Steam Audio archive cached in TEMP (or downloads it).
Builds the production packet serializer, rules, and binaural adapter into a temporary DLL.
"""
import ctypes as C
import hashlib
import math
import re
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[2]
SDK_SHA = "4a0aa5ec1176f38f0b0993a37c2259d9e86f27e22d5e24f83ec4c3cb9a1d5449"
CACHE = Path(tempfile.gettempdir())
ARCHIVE = CACHE / "gevr-steamaudio-4.8.1.zip"
NATIVE = CACHE / "gevr-steamaudio-test"

def build():
    if not ARCHIVE.exists():
        urllib.request.urlretrieve("https://github.com/ValveSoftware/steam-audio/releases/download/v4.8.1/steamaudio_4.8.1.zip", ARCHIVE)
    if hashlib.sha256(ARCHIVE.read_bytes()).hexdigest() != SDK_SHA:
        raise RuntimeError("Steam Audio archive checksum mismatch")
    NATIVE.mkdir(exist_ok=True)
    with zipfile.ZipFile(ARCHIVE) as z:
        for name in ("phonon.dll", "phonon.lib"):
            (NATIVE/name).write_bytes(z.read("steamaudio/lib/windows-x64/"+name))
    args = [shutil.which("gcc") or "gcc", "-shared", "-O2", "-fms-extensions", "-Wno-builtin-declaration-mismatch"]
    args += ["-D"+v for v in ("GEVR=1", "PLATFORM_64BIT=1", "GEVR_STEAMAUDIO=1", "_LANGUAGE_C=1", "VERSION=2", "VERSION_US=1", "LANG_US=1")]
    args += ["-I"+v for v in (".", "port/include", "include", "src", "port/src", "port/external/steamaudio/include")]
    args += ["port/tests/multiplayer_native.c", "port/src/net/netbuf.c", "port/src/net/net_spatial.c", "port/src/net/net_match.c", str(NATIVE/"phonon.lib"), "-o", str(NATIVE/"multiplayer_native.dll")]
    result = subprocess.run(args, cwd=ROOT, capture_output=True, text=True)
    if result.returncode: raise RuntimeError(result.stderr)
    global DLL_DIRECTORY
    DLL_DIRECTORY = os.add_dll_directory(str(NATIVE))
    dll = C.CDLL(str(NATIVE/"multiplayer_native.dll"))
    dll.test_gain.argtypes = [C.c_int, C.c_float]; dll.test_gain.restype = C.c_float
    dll.test_ping.argtypes = [C.c_uint32]*3
    dll.test_group.argtypes = [C.c_int]*6
    dll.test_roster.argtypes = [C.c_int,C.POINTER(C.c_uint8),C.POINTER(C.c_uint8)]
    dll.test_spatial_render.argtypes = [C.c_uint,C.POINTER(C.c_float),C.c_uint,C.POINTER(C.c_float),C.c_int,C.POINTER(C.c_float)]
    return dll

def build_core():
    # Leave production types, state, and tested functions intact. Replace only
    # unrelated game/world function bodies: the headless fixture has no renderer.
    source=(ROOT/"port/src/net/net_core.c").read_text(encoding="utf-8")
    masked=re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',lambda m:"".join("\n" if c=="\n" else " " for c in m.group()),source,flags=re.S)
    keep={"netResetLobbyState","netIsHost","netGetState","netGetConnectedPlayerCount","netGetMaxPlayers","netPlayerInRound","netVoiceSameGroup","netVoiceModeForPair","netTeamRosterReady","netGetSlotTeam","netGetSlotPing","netSlotIsSpectator","netVoiceSlotSpectating","netTeamScore","netSetSlotTeam","netLobbySetConfig","netLobbySetReady","netHostRoundEnded","netStageEligible","netLatchRoundSettings","netValidConfig","netWriteRoundSettings","netReadRoundSettings","netSendMatchSnapshot","netHostLost","netForgetPlayerScore","netClearVotes","netBroadcastBuf","netBroadcastPacket","netBroadcastLobbyState","netCancelRound","netSendCountdown"}
    keep.update({"netLobbyCanLaunch","netRoundRosterReady","netLocalReady","gevrNetSetReady","netActiveFunFlags","netActiveLineMode","netActiveGunSize","gevrNetConfigGet","gevrNetConfigSet","netScheduleRound","netAllLoaded","netReadyProgress","netHostStartRoundNow","netHostContinue","netIsActive","netDamageAllowed","netApplyAmmoPacket","netObjectByIndex","netSnapshotObjectType","netSendAmmoState"})
    keep.update({"netInvalidateHitSlot","netClearHostHits","netSetHostEqualization","netGetHostEqualization","netHostBaseDelayMs","netGetSlotHostDelayMs","netHostEqualizationText","netTransitionRoundPhase","netBroadcastRoundPhase","netObserveHitMove","netHitReportAllowed","netProcessHitReport","netExecuteHostHit","netQueueHostHit","netDrainHostHits","netSlotOccupied","netSendHitReport"})
    keep.update({"netStartPad","netStartPadShare","netBroadcastVotes","netClearVotes","netTeamScore","netGetSlotTeam","netSetSlotTeam","netVoiceModeForPair"})
    keep.update({"netConfigSlots","netLobbyMinPlayers", "netCoopSession", "gevrCoopFastReinforcements"})
    keep.update({"netReceiveLobbyReady","netHostCanStartRound","netBeginRoundReset","netBroadcastRoundPhase"})
    keep.update({"netLobbySlotConnected","netWarmupSettingsChanged","netTryHostStartRequest","netHostRoundTick","netHostReturnToLobby","netStageLoaded","netWarmupSecondsLeft","netHostStartRequested","netHostRequestVotes","netRoundNoticeText","netBroadcastRoundNotice","netReceiveRoundNotice","netGetVote","netResolveVotes","netTallyBallot","netRotationPick","netBallotSize","netHostKickPlayer","netHostDropSlot","netClientKickDisconnected","netBroadcastAllVotes"})
    keep.update({"netHostCanKickPlayer","netReceiveClientCaps","netSendClientCaps","netHostRemoveOldForNoRadar"})
    keep.update({"netSlotOwned","gevrNetOwnsSlot","netActingSlot","netSendHitReportAs","netSendOwnedMove","netIsRemotePlayerActive","netRemoteTrigger","netSlotIsBot","netGetHumanPlayerCount","netReleaseBotSlot","netAddBot","netUpdateBots","netBotPoints","netBotToReplace","netJoinSlot","gevrNetSlotIsBot","gevrNetBotRowsEditable"})
    keep.update({"netMapShotTime","netResetCombatEpoch","netWriteCombatIdentity","netReadCombatIdentity","netImportCombatIdentity","netSendClockTo","netClockTick","netReceiveClock","netExplosiveWeapon","netAcceptHit","netReceiveHitReport","netBeginLocalShot","netEndLocalShot","netMakeLocalHit","netNextLife","netAcceptRespawn","netSendRespawnEvent","netSendLocalPlayerMove","netLocalIsSpectator","netReceiveDamageEvent","netReceiveRespawn","netCombatClocksReady"})
    replacements=[]
    for m in re.finditer(r"^[A-Za-z_][A-Za-z_ \t*]*?\s+([A-Za-z_]\w*)\([^;]*?\)\s*\{",masked,re.M):
        if m.group(1) in keep: continue
        start=m.end();depth=1;end=start
        while depth:
            if masked[end]=="{":depth+=1
            elif masked[end]=="}":depth-=1
            end+=1
        replacements.append((start,end-1))
    for start,end in reversed(replacements):source=source[:start]+(" fixtureDamage(target,attacker,weapon,dmg,vx,vz); " if "static void netApplyDamage(" in source[max(0,start-170):start] else "")+source[end:]
    subset=NATIVE/"net_core_test_subset.inc";subset.write_text(source,encoding="utf-8")
    fixture=(ROOT/"port/tests/net_core_native.c").read_text(encoding="utf-8").replace('../src/net/net_core.c',subset.as_posix())
    generated=NATIVE/"net_core_native.c";generated.write_text(fixture,encoding="utf-8")
    args=[shutil.which("gcc") or "gcc","-shared","-O2","-fms-extensions","-Wno-builtin-declaration-mismatch"]
    args += ["-D"+v for v in ("GEVR=1","PLATFORM_64BIT=1","_LANGUAGE_C=1","VERSION=2","VERSION_US=1","LANG_US=1")]
    args += ["-I"+str(ROOT/v) for v in (".","port/include","include","src","src/game","port/src","port/src/net")]
    args += [str(generated),str(ROOT/"port/src/net/netbuf.c"),str(ROOT/"port/src/net/net_match.c"),"-o",str(NATIVE/"net_core_native.dll")]
    result=subprocess.run(args,cwd=ROOT,capture_output=True,text=True)
    if result.returncode: raise RuntimeError(result.stderr)
    return C.CDLL(str(NATIVE/"net_core_native.dll"))

def build_fixture(name, source=None):
    fixture=(ROOT/f"port/tests/{name}.c").read_text(encoding="utf-8")
    if name == "coop_drop_native":
        def function(path, signature):
            text=(ROOT/path).read_text(encoding="utf-8")
            start=text.index(signature);brace=text.index("{",start);depth=1;end=brace+1
            while depth:
                if text[end]=="{":depth+=1
                elif text[end]=="}":depth-=1
                end+=1
            return text[start:end]
        functions=[]
        for path, signatures in (
            ("src/game/matrixmath.c", ("void matrix_4x4_set_identity(", "void matrix_4x4_copy(",
                "void matrix_4x4_set_position(", "void matrix_scalar_multiply(", "void matrix_4x4_multiply_homogeneous(")),
            ("src/game/chrprop.c", ("void chrpropReparent(", "void chrpropDetach(", "void chrpropDelist(",
                "void chrpropActivate(", "void chrpropEnable(")),
            ("src/game/chr.c", ("PropRecord *chrGetEquippedWeaponProp(",)),
            ("src/game/propobj.c", ("void sub_GAME_7F03FDA8(", "void propobjSetDropped(", "void objDetach(", "s32 objDrop(")),
            ("port/src/net/net_coop.c", ("static u8 coopWeaponOf(", "static void coopSyncHand(")),
        ):
            functions.extend(function(path, signature) for signature in signatures)
        fixture=fixture.replace("/* INSERT_PRODUCTION_DROP_FUNCTIONS */", "\n\n".join(functions))
        fixture=fixture.replace("/* INSERT_PRODUCTION_DROP_TICK */", function("src/game/chr.c", "        if (chr->hidden & CHRHIDDEN_DROP_HELD_ITEMS)"))
        puppet=function("port/src/net/net_coop.c", "void netCoopPuppetTick(")
        dying=next(line.strip() for line in puppet.splitlines() if "bool dying =" in line)
        hands="\n".join(line.strip() for line in puppet.splitlines() if "coopSyncHand(chr," in line)
        fixture=fixture.replace("/* INSERT_PRODUCTION_DYING */", dying)
        fixture=fixture.replace("/* INSERT_PRODUCTION_SYNC_HANDS */", hands)
    elif name == "inventory_native":
        init_source=source if source is not None else (ROOT/'src/game/inititemslots.c').read_text(encoding='utf-8')
        init_path=NATIVE/f"{name}_source.c";init_path.write_text(init_source,encoding='utf-8')
        fixture=fixture.replace('../../src/game/inititemslots.c',init_path.as_posix())
        inventory=(ROOT/'src/game/bondinv.c').read_text(encoding='utf-8')
        body=inventory[inventory.index('void bondinvReinitInv(void)'):inventory.index('/**',inventory.index('void bondinvReinitInv(void)'))]
        fixture=fixture.replace('/* INSERT_PRODUCTION_REINIT */',body)
    elif name == "hand_native":
        panel=(ROOT/'src/game/bondview2.c').read_text(encoding='utf8')
        start=panel.index('#define GEVR_WP_MAX 96')
        end=panel.index('extern u16 *bondinvGetNameByIndex',start)
        region=panel[start:end]
        # The grip gestures and hand reload (#111) sit in this range; the fixture does not use them.
        cut=region.find("/*\n * GEVR PC's grip gestures")
        if cut>=0:
            region=region[:cut]+region[region.index("/*\n * The panel is a wheel of categories",cut):]
        fixture=fixture.replace('/* INSERT_HAND_INVENTORY */',region)
        inventory=(ROOT/'src/game/bondinv.c').read_text(encoding='utf8')
        start=inventory.index('int bondinvAddWeaponByProp(')
        end=inventory.index('#ifdef GEVR',inventory.index('    return added;',start))
        fixture=fixture.replace('/* INSERT_PICKUP */',inventory[start:end])
        start=panel.index('static s32 gevrMultiplayerCuff(')
        end=panel.index('#endif',start)  # Preserve ALL_BONDS conditional inside the switch.
        end=panel.index('void bondviewSelectCuff(',end)
        helper=panel[start:end].rsplit('#endif',1)[0]
        fixture=fixture.replace('/* INSERT_CUFF */',helper)
        start=panel.index('void bondviewSelectCuff(')
        end=panel.index('Gfx *bondviewRenderWatch(',start)
        body=panel[start:end].split('/**',1)[0]
        fixture=fixture.replace('/* INSERT_SELECT_CUFF */',body)
        gun=(ROOT/'src/game/gun.c').read_text(encoding='utf8')
        start=gun.index('ITEM_IDS get_next_weapon_in_cycle_for_hand(')
        end=gun.index('void gunRequestHandWeaponChange(',start)
        fixture=fixture.replace('/* INSERT_NEXT_WEAPON */',gun[start:end])
        start=gun.index('void gunRequestHandWeaponChange(')
        end=gun.index('// Unused',start)
        fixture=fixture.replace('/* INSERT_HAND_REQUEST */',gun[start:end])
        fire=(ROOT/'src/game/gunfire.c').read_text(encoding='utf8')
        start=fire.index('                /* Stereo hands are independent;')
        end=fire.index('                currentPlayerUnEquipWeaponWrapper(hand,',start)
        fixture=fixture.replace('/* INSERT_PAIR_VALIDATION */',fire[start:end])
        start=fire.index('        if ((handptr->weapon_action_state == GUN_ANIM_STATE_IDLE)')
        end=fire.index('    if (handptr->weapon_action_state == GUN_ANIM_STATE_TRIGGER_PRESS)',start)
        body=fire[start:end].rstrip()
        # The final brace closes the outer idle state, outside this tested block.
        fixture=fixture.replace('/* INSERT_DEPLETION */',body[:-1])
        inp=(ROOT/'port/src/input.c').read_text(encoding='utf8')
        start=inp.index('        {',inp.index('// Logical dominant/off-hand buttons'))
        end=inp.index('        if (gevrReturnPrompt)',start)
        fixture=fixture.replace('/* INSERT_CYCLING_INPUT */',inp[start:end])
    elif name == "fun_native":
        def function(path, signature):
            text=(ROOT/path).read_text(encoding="utf-8")
            start=text.index(signature);brace=text.index("{",start);depth=1;end=brace+1
            while depth:
                if text[end]=="{":depth+=1
                elif text[end]=="}":depth-=1
                end+=1
            return text[start:end]
        fixture=fixture.replace("/* INSERT_CHEAT_QUERY */",function("src/game/cheat.c","bool cheatIsActive(CHEAT_ID cheat)"))
        fixture=fixture.replace("/* INSERT_LINE_QUERY */",function("src/game/debugmenu_handler.c","s32 get_debug_VisCVG_flag(void)"))
        fixture=fixture.replace("/* INSERT_GUN_QUERY */",function("src/game/bondview2.c","static f32 gevrGunSizeFactor(void)\n{"))
        fixture=fixture.replace("/* INSERT_REMOTE_HAND */",function("port/src/net/net_player_sync.c","static void netSyncCopyHand("))
        body=function("src/game/chr_b.c","struct Model *makeonebody(")
        prefix=body[body.index("{")+1:body.index("    if (bodyHeader->RootNode == 0)")]
        fixture=fixture.replace("/* INSERT_BODY_SCALE */",prefix)
    elif source is not None:
        old=NATIVE/f"{name}_source.c";old.write_text(source,encoding="utf-8")
        fixture=fixture.replace('../../src/game/vtxstore.c',old.as_posix())
    elif name == "vtxstore_native":
        fixture=fixture.replace('../../src/game/vtxstore.c',(ROOT/'src/game/vtxstore.c').as_posix())
    elif name == "botnav_native":
        fixture=fixture.replace('../../src/game/gevr_botnav.c',(ROOT/'src/game/gevr_botnav.c').as_posix())
    elif name == "objects_native":
        fixture=fixture.replace('../src/net/net_objects.c',(ROOT/'port/src/net/net_objects.c').as_posix())
    path=NATIVE/f"{name}.c";path.write_text(fixture,encoding="utf-8")
    args=[shutil.which("gcc") or "gcc","-shared","-O2","-fms-extensions","-Wno-builtin-declaration-mismatch"]
    args += ["-D"+v for v in ("GEVR=1","PLATFORM_64BIT=1","_LANGUAGE_C=1","VERSION=2","VERSION_US=1","LANG_US=1")]
    args += ["-I"+str(ROOT/v) for v in (".","port/include","include","src","src/game","port/src","port/src/net")]
    args += [str(path),"-o",str(NATIVE/f"{name}.dll")]
    result=subprocess.run(args,cwd=ROOT,capture_output=True,text=True)
    if result.returncode: raise RuntimeError(result.stderr)
    return C.CDLL(str(NATIVE/f"{name}.dll"))

def build_line_renderer():
    source=(ROOT/"port/fast3d/gfx_opengl.cpp").read_text(encoding="utf-8")
    start=source.index("static void gevr_draw_world_lines(")
    end=source.index("static void gevr_issue_draw(",start)
    fixture=(ROOT/"port/tests/line_renderer_native.cpp").read_text(encoding="utf-8").replace("/* INSERT_LINE_RENDERER */",source[start:end])
    path=NATIVE/"line_renderer_native.cpp";path.write_text(fixture,encoding="utf-8")
    out=NATIVE/"line_renderer_native.dll"
    subprocess.run([shutil.which("g++") or "g++","-shared","-O2","-std=c++17","-static","-I"+str(ROOT/"port/include"),str(path),"-o",str(out)],check=True)
    return C.CDLL(str(out))

class MultiplayerNativeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = build()
        cls.core = build_core()
        cls.allocator = build_fixture("vtxstore_native")
        cls.objects = build_fixture("objects_native")
        cls.coop_drops = build_fixture("coop_drop_native")
        cls.inventory = build_fixture("inventory_native")
        cls.fun = build_fixture("fun_native")
        cls.hands = build_fixture("hand_native")
        cls.botnav = build_fixture("botnav_native")
        cls.line_renderer = build_line_renderer()
        if cls.lib.test_spatial_init() != 1: raise RuntimeError(f"Actual Steam Audio initialization failed: {cls.lib.test_spatial_error()}")
    @classmethod
    def tearDownClass(cls): cls.lib.test_spatial_shutdown()
    def render(self,direction,positioned=1,slot=1,frames=2048):
        self.lib.test_spatial_reset(slot)
        data = (C.c_float*frames)(*(12000*math.sin(i*.43)+3000*math.sin(i*.17) for i in range(frames)))
        out = (C.c_float*(frames*2))()
        self.lib.test_spatial_render(slot,data,frames,(C.c_float*3)(*direction),positioned,out)
        return list(data),list(out)
    def test_clock_offsets_and_bounds(self): self.assertEqual(self.lib.test_clock_math(),0)
    def test_timestamp_and_life_packet_roundtrips(self): self.assertEqual(self.lib.test_timing_protocol(),0)
    def test_owner_damage_and_respawn_packets(self): self.assertEqual(self.core.test_core_owner_packets(),0)
    def test_clock_exchange_authentication(self): self.assertEqual(self.core.test_core_clock_exchange(),0)
    def test_shot_timestamp_replay_and_respawn_validation(self): self.assertEqual(self.core.test_core_timed_hits(),0)
    def test_late_join_round_and_migration_identity(self): self.assertEqual(self.core.test_core_combat_identity(),0)
    def test_host_delay_timing_and_trades(self): self.assertEqual(self.core.test_core_host_delay(),0)
    def test_host_delay_lifecycle_and_overflow(self): self.assertEqual(self.core.test_core_host_delay_lifecycle(),0)
    def test_warmup_score_boundary(self): self.assertEqual(self.core.test_core_score_boundary(),0)
    def test_pickup_ownership_and_equipment(self): self.assertEqual(self.hands.test_pickup_ownership(),0)
    def test_character_sleeve_selection(self): self.assertEqual(self.hands.test_character_sleeves(),0)
    def test_tap_hold_grip_and_menu_input_routing(self): self.assertEqual(self.hands.test_hand_input(),0)
    def test_independent_hand_lists_and_cycling(self): self.assertEqual(self.hands.test_hand_cycles(),0)
    def test_weapon_wheel_categories_and_stick(self): self.assertEqual(self.hands.test_weapon_wheel(),0)
    def test_per_hand_depletion_and_switch_animation(self): self.assertEqual(self.hands.test_hand_depletion(),0)
    def test_native_fun_preferences_and_scaling(self): self.assertEqual(self.fun.test_fun_visuals(),0)
    def test_fun_pending_authority_late_join_migration(self): self.assertEqual(self.core.test_core_fun(),0)
    def test_coop_fast_reinforcements_host_authority_and_migration(self): self.assertEqual(self.core.test_core_coop_fast(),0)
    def test_launch_consent_and_scenario_transition(self): self.assertEqual(self.core.test_core_launch_consent(),0)
    def test_in_game_ready_packets_and_start(self): self.assertEqual(self.core.test_core_menu_ready(),0)
    def test_coop_ignores_team_scenario_for_ready(self): self.assertEqual(self.core.test_core_coop_team_scenario_ready(),0)
    def test_coop_guard_drops_detach_and_activate(self): self.assertEqual(self.coop_drops.test_coop_guard_drops(),0)
    def test_coop_guard_hand_removal_and_replacement(self): self.assertEqual(self.coop_drops.test_coop_guard_hand_changes(),0)
    def test_coop_guard_pickups_remain_individual(self): self.assertEqual(self.coop_drops.test_coop_guard_pickups_are_local(),0)
    def test_solo_warmup_pending_options_restart(self): self.assertEqual(self.core.test_core_solo_restart(),0)
    def test_connected_roster_and_spectator_load_ack(self): self.assertEqual(self.core.test_core_connected_roster(),0)
    def test_first_and_next_round_warmup_timers(self): self.assertEqual(self.core.test_core_warmup_lifecycle(),0)
    def test_late_join_countdown_and_voted_map_warmup(self): self.assertEqual(self.core.test_core_warmup_join_and_votes(),0)
    def test_ready_vote_prompts_and_authenticated_notices(self): self.assertEqual(self.core.test_core_round_prompts(),0)
    def test_host_kick_and_client_launcher_exit(self): self.assertEqual(self.core.test_core_host_kick(),0)
    def test_no_radar_removes_only_older_apps(self): self.assertEqual(self.core.test_core_no_radar_caps(),0)
    def test_line_renderer_restores_gl_state(self): self.assertEqual(self.line_renderer.test_line_renderer(),0)
    def test_line_edges_and_vertex_ring_offset(self): self.assertEqual(self.lib.test_line_indices(),0)
    def test_gauge_clearance_and_symmetry(self): self.assertEqual(self.lib.test_gauge_geometry(),0)
    def test_host_pending_and_active_teams(self): self.assertEqual(self.core.test_core_teams(),0)
    def test_closing_door_escape_preserves_entry_collision(self): self.assertEqual(self.lib.test_door_escape(),0)
    def test_combat_allocator_canaries_and_merge(self): self.assertEqual(self.allocator.test_vtxstore(),0)
    def test_inventory_reset_and_respawn_bounds(self): self.assertEqual(self.inventory.test_inventory_bounds(),0)
    def test_ammo_spawn_reset_and_late_join_transforms(self): self.assertEqual(self.objects.test_ammo_lifecycle(),0)
    def test_ammo_protocol_and_invalid_transforms(self): self.assertEqual(self.lib.test_ammo_protocol(),0)
    def test_ammo_batches_are_atomic_and_stage_scoped(self): self.assertEqual(self.core.test_core_ammo_packets(),0)
    def test_live_friendly_fire_and_round_teams(self): self.assertEqual(self.core.test_core_friendly_fire(),0)
    def test_friendly_fire_rules(self):
        for scenario in range(10):
            with self.subTest(scenario=scenario):
                self.assertEqual(self.lib.test_damage(scenario,0,0,0,0),scenario<5)
                self.assertEqual(self.lib.test_damage(scenario,1,0,0,0),1)
                self.assertEqual(self.lib.test_damage(scenario,0,1,0,0),1)
                self.assertEqual(self.lib.test_damage(scenario,0,0,0,1),1)
    def test_late_join_snapshot(self): self.assertEqual(self.core.test_core_late_join_snapshot(),0)
    def test_eight_slots_packets_teams_and_pads(self): self.assertEqual(self.core.test_core_eight_slots(),0)
    def test_bot_roster(self): self.assertEqual(self.core.test_core_bot_roster(),0)
    def test_bot_join_and_election(self): self.assertEqual(self.core.test_core_bot_join(),0)
    def test_bot_owner(self): self.assertEqual(self.core.test_core_bot_owner(),0)
    def test_bot_floor_routes(self): self.assertEqual(self.botnav.test_botnav(),0)
    def test_host_player_count_any_stage(self): self.assertEqual(self.core.test_core_player_count(),0)
    def test_live_config_and_round_snapshot(self): self.assertEqual(self.core.test_core_live_voice(),0)
    def test_scores_survive_departures(self): self.assertEqual(self.core.test_core_scores_after_departure(),0)
    def test_ping_measurements_age_and_wrap(self):
        self.assertEqual(self.lib.test_ping(42,100,5100),42)
        self.assertEqual(self.lib.test_ping(42,100,5101),65535)
        self.assertEqual(self.lib.test_ping(42,0,100),65535)
        self.assertEqual(self.lib.test_ping(99999,100,200),65534)
        self.assertEqual(self.lib.test_ping(42,0xfffffffe,2),42)
    def test_latency_expiry_and_host_migration(self): self.assertEqual(self.core.test_core_latency_and_migration(),0)
    def test_packet_roundtrip_and_all_truncations(self): self.assertEqual(self.lib.test_protocol(),0)
    def test_invalid_config_and_player_counts(self): self.assertEqual(self.lib.test_config_validation(),0)
    def test_proximity_bright_dot_and_floor(self):
        for distance,gain in ((0,1),(3000,1),(4000,.3666666667),(4500,.2125),(5000,.1333333333),(6000,.10),(8000,.10),(100000,.10)):
            with self.subTest(distance=distance): self.assertAlmostEqual(self.lib.test_gain(0,distance),gain,places=6)
    def test_couch_full_gain(self):
        for distance in (0,4000,8000,100000): self.assertEqual(self.lib.test_gain(1,distance),1)
    def test_active_voice_eligibility(self):
        self.assertEqual(self.lib.test_group(1,0,0,0,0,1),1)
        self.assertEqual(self.lib.test_group(1,5,0,0,0,1),1)
        self.assertEqual(self.lib.test_group(1,5,0,0,1,1),1)
        self.assertEqual(self.lib.test_group(1,5,0,0,2,2),1)
    def test_spectators_and_eliminated_share_group(self):
        self.assertEqual(self.lib.test_group(1,5,1,1,0,1),1)
        self.assertEqual(self.lib.test_group(1,0,1,0,0,0),0)
        self.assertEqual(self.lib.test_group(1,5,0,1,0,0),0)
    def test_lobby_and_results_allow_everyone(self):
        self.assertEqual(self.lib.test_group(0,5,1,0,0,1),1)
    def test_exact_rosters_and_unassigned(self):
        slots = self.lib.test_max_players()
        self.assertEqual(slots, 8)
        for mode,teams in ((5,[0,0,1,1]),(6,[0,0,0,1]),(7,[0,0,1]),(8,[0,0,0,1,1,1]),(9,[0,0,0,0,1,1,1,1])):
            connected = (C.c_uint8*slots)(*([1]*len(teams)+[0]*(slots-len(teams))))
            chosen = (C.c_uint8*slots)(*(teams+[2]*(slots-len(teams))))
            self.assertEqual(self.lib.test_roster(mode,connected,chosen),1)
            chosen[0]=2; self.assertEqual(self.lib.test_roster(mode,connected,chosen),0)
            chosen[0]=1; self.assertEqual(self.lib.test_roster(mode,connected,chosen),0)
    def test_team_capacity_and_friendly_fire(self):
        for mode,red,blue in ((5,2,2),(6,3,1),(7,2,1),(8,3,3),(9,4,4)):
            self.assertEqual(self.lib.test_capacity(mode,0),red); self.assertEqual(self.lib.test_capacity(mode,1),blue)
        for mode in (0,4,10): self.assertEqual(self.lib.test_capacity(mode,0),0)
        self.assertEqual(self.lib.test_points(0,1,3),3); self.assertEqual(self.lib.test_points(0,0,3),-3)
    def test_online_team_sizes_play_the_games_2v2(self):
        for mode,game in ((0,0),(5,5),(6,6),(7,7),(8,5),(9,5)):
            self.assertEqual(self.lib.test_game_scenario(mode),game)
    def test_binaural_front_back_and_elevation(self):
        outputs=[self.render(d)[1][512:] for d in ((0,0,-1),(0,0,1),(0,1,0),(0,-1,0))]
        for i in range(4):
            self.assertGreater(sum(abs(v) for v in outputs[i]),1000)
            for j in range(i): self.assertGreater(sum(abs(a-b) for a,b in zip(outputs[i],outputs[j])),1000)
    def test_left_right_positioning(self):
        # A measured human HRTF is naturally asymmetric. Check ear dominance,
        # not exact mirrored filters, which would reject valid binaural cues.
        a=self.render((-1,0,0))[1][512:];b=self.render((1,0,0))[1][512:]
        self.assertGreater(sum(x*x for x in a[::2]),sum(x*x for x in a[1::2])*2)
        self.assertGreater(sum(x*x for x in b[1::2]),sum(x*x for x in b[::2])*2)
    def test_centered_voice_block_delay(self):
        data,out=self.render((0,0,-1),positioned=0)
        self.assertEqual(out[:512],[0]*512)
        for i in range(256,len(data)):
            self.assertAlmostEqual(out[2*i],data[i-256],places=3)
            self.assertEqual(out[2*i],out[2*i+1])
    def test_slot_reset_removes_previous_speech(self):
        self.render((1,0,0));self.lib.test_spatial_reset(1)
        silence=(C.c_float*512)();out=(C.c_float*1024)()
        self.lib.test_spatial_render(1,silence,512,(C.c_float*3)(0,0,-1),1,out)
        self.assertEqual(list(out),[0]*1024)
    def test_arbitrary_buffer_sizes_preserve_samples(self):
        self.lib.test_spatial_reset(2)
        chunks=[1,127,441,19,256,73]
        data=(C.c_float*sum(chunks))(*(float(i) for i in range(sum(chunks))))
        output=[];at=0
        for n in chunks:
            chunk=(C.c_float*n)(*data[at:at+n]);out=(C.c_float*(n*2))()
            self.lib.test_spatial_render(2,chunk,n,(C.c_float*3)(0,0,-1),0,out)
            output.extend(out);at+=n
        self.assertEqual(output[:512],[0]*512)
        self.assertEqual(output[512::2],list(data)[:-256])

if __name__ == "__main__": unittest.main(verbosity=2)
