"""Check inventory-driven mission gadgets in either hand and gadget-use results."""
from pathlib import Path
import argparse
import shutil
import subprocess
import tempfile
from test_multiplayer import ROOT, NATIVE, build_fixture

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-ref',help='Verify the slot regression against earlier inventory code')
args=parser.parse_args()
NATIVE.mkdir(exist_ok=True)
source=None
if args.source_ref:
    source=subprocess.check_output(['git','show',args.source_ref+':src/game/bondview2.c'],cwd=ROOT,text=True,encoding='utf-8')
hands=build_fixture('hand_native',source,source_ref=args.source_ref)
result=hands.test_mission_gadget_slots()
if args.source_ref:
    assert result, 'Earlier inventory unexpectedly supports both hands'
    print(f'CONFIRMED: {args.source_ref} fails mission gadget slot checks at line {result}')
    raise SystemExit(0)
for test in ('test_mission_gadget_slots','test_mission_gadget_ownership','test_watch_laser_slots','test_watch_laser_exit','test_hand_cycles','test_weapon_wheel','test_hand_depletion',
             'test_hand_input','test_pickup_ownership','test_character_sleeves'):
    result=getattr(hands,test)()
    assert result==0, f'{test} failed at line {result}'
print('PASS: mission inventory, both hand lists/slots, single gadget ownership, pending/stale requests, stow/retake, names and hand input')


def function(text,signature):
    import re
    match=re.search(re.escape(signature)+r'[^;{]*\{',text)
    assert match,signature
    end=match.end();depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[match.start():end]


gun=(ROOT/'src/game/gunfire.c').read_text(encoding='utf-8')
production='\n'.join(function(gun,s) for s in ('static void gevrEquipGadgetResult(',
    'static void analyzeGEKeyInHand(','void analyzeGEKey(',
    'static void giveWeaponCaseItemsInHand(','void give_weapon_case_items('))
use=function(gun,'if (handptr->weapon_action_state == GUN_ANIM_STATE_USE_ITEM)')
fixture=(ROOT/'port/tests/mission_gadget_use_native.c').read_text(encoding='utf-8')
fixture=fixture.replace('/* PRODUCTION */',production).replace('/* USE_ITEM */',use)
with tempfile.TemporaryDirectory(prefix='gevr-mission-gadgets-') as temp:
    source,exe=Path(temp)/'test.c',Path(temp)/'test.exe'
    source.write_text(fixture,encoding='utf-8')
    flags=['-std=c11','-O2','-fms-extensions','-DGEVR=1','-D_LANGUAGE_C']
    flags+=['-I'+str(ROOT/p) for p in ('.','port/include','include','src','src/game')]
    subprocess.run([shutil.which('gcc') or 'gcc',*flags,str(source),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
