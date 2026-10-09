"""Native regressions for issues 161, 162 and Natalya's enemy selection (163)."""
from pathlib import Path
import argparse
import shutil
import subprocess
import tempfile
from test_reinforcements import block

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline', choices=('mines','documents','natalya'),
                    help='Verify this regression fails against the HEAD implementation')
parser.add_argument('--baseline-ref',default='HEAD',help='Git reference containing the unfixed code')
options = parser.parse_args()
baseline = options.baseline
coop = (ROOT / 'port/src/net/net_coop.c').read_text()
ai = (ROOT / 'src/game/chrai.c').read_text()
objective = (ROOT / 'src/game/objective_status.c').read_text()
action = (ROOT / 'src/game/chraction.c').read_text()
items = '\n\n'.join((
    block((ROOT / 'src/game/bondinv.c').read_text(), 'bool bondinvHasPropInInv('),
    block(coop,'int gevrCoopTeammateHolds('),
    block(ai,'s32 chraiitemsize('),
    block(objective,'static s32 gevrCoopAppendHeldTag('),
    block(objective,'s32 gevrCoopHeldObjectiveTags('),
    block(coop,'int gevrCoopThrownMissionItem('),
    block((ROOT / 'port/src/net/net_player_sync.c').read_text(),'static void netSyncCopyHand('),
))
fixture = (ROOT / 'port/tests/campaign_items_native.c').read_text()
fixture = fixture.replace('/* INSERT_ITEMS */',items)
fixture = fixture.replace('/* INSERT_COLLECTED_CASE */',block(ai,'case AI_IFBondCollectedObject:\n                {'))
fixture = fixture.replace('/* INSERT_HELD_CASE */',block(coop,'case NET_COOP_EVENT_HELD:').replace('NET_COOP_EVENT_HELD','5'))
fixture = fixture.replace('/* INSERT_SCAN */', '\n'.join((block(action,'bool chrIsDead('),
                                                          block(action,'bool sub_GAME_7F033B38('))))
if baseline:
    replacements = {
        'mines': [('port/src/net/net_player_sync.c','static void netSyncCopyHand(')],
        'documents': [('port/src/net/net_coop.c','int gevrCoopTeammateHolds('),
                      ('src/game/objective_status.c','s32 gevrCoopHeldObjectiveTags('),
                      ('src/game/chrai.c','case AI_IFBondCollectedObject:\n                {')],
        'natalya': [('src/game/chraction.c','bool sub_GAME_7F033B38(')],
    }
    for path, signature in replacements[baseline]:
        old = subprocess.check_output(['git','show',options.baseline_ref+':'+path],cwd=ROOT,text=True)
        fixture = fixture.replace(block((ROOT/path).read_text(),signature), block(old,signature))
    fixture = fixture.replace('documents(); mines(); natalya();', baseline+'();')
with tempfile.TemporaryDirectory(prefix='gevr-campaign-items-') as temp:
    source, exe = Path(temp)/'test.c', Path(temp)/'test.exe'
    source.write_text(fixture)
    args=[shutil.which('gcc') or 'gcc','-std=c11','-O2','-fms-extensions',
          '-Wno-builtin-declaration-mismatch','-Wno-incompatible-pointer-types']
    args+=['-D'+v for v in ('GEVR=1','PLATFORM_64BIT=1','_LANGUAGE_C=1','VERSION=2','VERSION_US=1','LANG_US=1')]
    args+=['-I'+str(ROOT/p) for p in ('.','port/include','include','src','src/game','port/src','port/src/net')]
    result=subprocess.run(args+[str(source),str(ROOT/'port/src/net/netbuf.c'),'-lm','-o',str(exe)],capture_output=True,text=True)
    if result.returncode: raise RuntimeError(result.stderr)
    result = subprocess.run([str(exe)], capture_output=True, text=True)
    if baseline:
        assert result.returncode, 'Baseline unexpectedly passed'
        print('CONFIRMED: '+options.baseline_ref+' fails the '+baseline+' regression: '+result.stderr.splitlines()[0])
    else:
        if result.returncode: raise RuntimeError(result.stderr)
        print(result.stdout.strip())
