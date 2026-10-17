"""Execute production targeting, guard hit probability, muzzle and damage routing."""
from pathlib import Path
import subprocess, tempfile
from test_reinforcements import block
ROOT=Path(__file__).resolve().parents[2]
coop=(ROOT/'port/src/net/net_coop.c').read_text()
action=(ROOT/'src/game/chraction.c').read_text()
maths=(ROOT/'src/game/matrixmath.c').read_text()
production='typedef struct { s8 player; s32 until60; } CoopTarget;\nstatic CoopTarget s_target[COOP_MAX_SLOTS];\n'
production+='s32 g_gevrCoopGuardTick;\nvoid *g_gevrCoopGuardViewToWorld;\n'
production+='static s32 s_gevrShotsAimed,s_gevrShotsOff,s_gevrShotsHeld;static f32 s_gevrShotChance;\n'
production+='#define gevrGuardViewToWorld() (g_gevrCoopGuardViewToWorld ? g_gevrCoopGuardViewToWorld : currentPlayerGetViewToWorldMtxf())\n'
production+='\n'.join(block(coop,name) for name in ('static s32 coopSlotOf(', 'static int coopPlayerTargetable(',
    's32 gevrCoopGuardTarget(', 'void gevrCoopGuardProvoked(', 'int gevrCoopGuardDamageBlocked(',
    'static void coopSendGuardDamage(', 's32 gevrCoopForwardGuardDamage(', 'static void coopApplyDamage('))
production+='\n'+'\n'.join(block(maths,name) for name in ('void matrix_4x4_multiply_homogeneous(', 'void matrix_4x4_rotate_vector(', 'void mtx4RotateVecInPlace(', 'void mtx4TransformVecInPlace('))
production+='\n'+'\n'.join(block(action,name) for name in ('f32 chrlvGetAimLimitAngle(', 'void chrlvUpdateShotbondsum(ChrRecord *self, s32 *arg1, s32 *arg2, ITEM_IDS item)\n{', 's32 sub_GAME_7F02D630(ChrRecord *self, GUNHAND hand, coord3d *arg2)\n{'))
with tempfile.TemporaryDirectory(prefix='gevr-coop-combat-') as t:
    path=Path(t);source=path/'combat.c';exe=path/'combat'
    source.write_text((ROOT/'port/tests/coop_combat_native.c').read_text().replace('/* PRODUCTION */',production))
    flags=['-std=c11','-O1','-g','-fms-extensions','-Wno-builtin-declaration-mismatch','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie']
    flags+=['-D'+x for x in ('GEVR=1','PLATFORM_64BIT=1','_LANGUAGE_C=1','VERSION_US=1')]
    flags+=['-I'+str(ROOT/x) for x in ('.','include','src','src/game','port/include','port/src','port/src/net')]
    subprocess.run(['gcc',*flags,str(source),str(ROOT/'port/src/net/netbuf.c'),'-lm','-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
