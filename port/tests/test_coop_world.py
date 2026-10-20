"""Execute co-op world packets and the production mission-effect handlers.

No ROM, renderer or socket is needed. The fixture provides native AI lists,
props and engine boundaries; packet parsing and replay use production code.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
from test_reinforcements import block

ROOT = Path(__file__).resolve().parents[2]


def main():
    coop = (ROOT / 'port/src/net/net_coop.c').read_text()
    core = (ROOT / 'port/src/net/net_core.c').read_text()
    ai = (ROOT / 'src/game/chrai.c').read_text()
    props = (ROOT / 'src/game/propobj.c').read_text()
    blast = (ROOT / 'src/game/explosion.c').read_text()
    player = (ROOT / 'src/game/bondview2.c').read_text()
    effects = coop[coop.index('/* ---- Shared effects from'):coop.index('/*\n * Mission gadgets the whole party')]
    handlers = ('DestroyObject', 'MoveObject', 'SetObjectFlags', 'UnsetObjectFlags',
                'SetObjectFlags2', 'UnsetObjectFlags2', 'TvChangeScreenBank', 'SwitchSky',
                'ObjectRocketLaunch', 'SfxPlay', 'SfxStopChannel', 'SfxSetChannelVolume',
                'SfxFadeChannelVolume', 'SfxEmitFromObject', 'SfxEmitFromPad',
                'MusicPlaySlot', 'MusicStopSlot')
    ai_cases = '\n'.join(block(ai, '                case AI_' + name + ':') for name in handlers)
    # The network preamble is exercised before the renderer allocates a blast.
    create = block(blast, 'explosionCreate(PropRecord *arg0,')
    start = create.index('#ifdef GEVR')
    end = create.index('\n#endif\n\n#if defined(VERSION_US)', start) + len('\n#endif')
    parts = {
        'EFFECTS': effects,
        'HEADER': block(coop, 'static void coopHeader(struct netbuf *b, u8 type)\n{'),
        'AI_CASES': ai_cases,
        'GLASS': block(props, 'void sub_GAME_7F04DCB4('),
        'OBJECTS': '\n'.join(block(core, sig) for sig in (
            'static bool netSnapshotObjectType(', 'static ObjectRecord *netObjectByIndex(',
            'static int netObjectIndex(', 'static void netSendObjectEvent(',
            'void netSendHostGlassBreak(', 'static void netSendWorldSnapshot(')),
        'RECEIVE_OBJECT': block(core, '        case NET_MSG_OBJECT_STATE:'),
        'RECEIVE_SNAPSHOT': block(core, '        case NET_MSG_WORLD_SNAPSHOT:'),
        'SEND_BLAST': '\n'.join(block(core, sig) for sig in (
            'void netSendExplosionAs(', 'void netSendExplosion(')),
        'CREATE_BLAST': create[start:end],
        'RECEIVE_BLAST': block(blast, 'void gevrNetExplosionReceive('),
        'TICK_BLAST': block(player, 'void bondviewPlayerTickExplode('),
    }
    fixture = (ROOT / 'port/tests/coop_world_native.c').read_text()
    for marker, code in parts.items():
        fixture = fixture.replace('/* INSERT_' + marker + ' */', code)
    # Verify the exercised handlers are actually wired into the game paths.
    assert 'gevrCoopScriptEffect(AiListp, Offset);' in ai
    assert 'coopHostClock(now);' in block(coop, 'void netCoopHostTick(')
    assert 'coopClockReset();' in block(coop, 'void netCoopPlayerJoined(')
    assert 'coopSendScriptSnapshot(slot);' in block(coop, 'static void coopSendRoster(')
    assert 'coopScriptReset();' in block(coop, 'void netCoopStageLoaded(')
    route = core[core.index('        case NET_MSG_CHR_STATE:'):core.index('        case NET_MSG_COOP_HIT:')]
    assert 'case NET_MSG_COOP_CLOCK:' in route and 'case NET_MSG_COOP_SCRIPT:' in route
    assert 'peer != s_server_peer' in route and 'netIsHost()' in route
    with tempfile.TemporaryDirectory(prefix='gevr-coop-world-') as temp:
        source, exe = Path(temp) / 'world.c', Path(temp) / 'world'
        source.write_text(fixture)
        flags = ['-std=c11', '-O1', '-g', '-fms-extensions',
                 '-Wno-builtin-declaration-mismatch', '-Wno-incompatible-pointer-types', '-fsanitize=address,undefined',
                 '-fno-omit-frame-pointer', '-no-pie']
        flags += ['-D' + v for v in ('GEVR=1', 'PLATFORM_64BIT=1', '_LANGUAGE_C=1',
                                     'VERSION=2', 'VERSION_US=1', 'LANG_US=1')]
        flags += ['-I' + str(ROOT / p) for p in ('.', 'include', 'src', 'src/game',
                                                'port/include', 'port/src', 'port/src/net')]
        result = subprocess.run([shutil.which('gcc') or 'gcc', *flags, str(source),
                                 str(ROOT / 'port/src/net/netbuf.c'), '-lm', '-o', str(exe)],
                                capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stderr)
        subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
