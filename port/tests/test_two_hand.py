"""Exercise production two-hand grip/aim and sniper-club replacement for either slot."""
from pathlib import Path
import argparse
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-ref', help='Read production code from a Git ref')
args = parser.parse_args()


def read(path):
    if args.source_ref:
        return subprocess.check_output(['git', 'show', f'{args.source_ref}:{path}'], cwd=ROOT, text=True)
    return (ROOT / path).read_text()


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


view = read('src/game/bondview2.c')
gun = read('src/game/gunfire.c')
math = read('src/game/matrixmath.c')
buttons = read('port/vr/vr_input.cpp')
definitions = re.findall(r'^#define GEVR_TWOHAND_[^\n]+', view, re.M)
for name in ('GEVR_UNITS_PER_METRE', 'GEVR_VIEWMODEL_CM', 'GEVR_GRIP_TO_ORIGIN_CM'):
    definitions.append(re.search(r'^#define ' + name + r'\s+[^\n]+', view, re.M).group())
definitions.append(re.search(r'^#define GEVR_RECOIL_TWOHAND_SHARE[^\n]+', buttons, re.M).group())
definitions.append(re.search(r'static const f32 s_gevrTwoHandPalm\[3\] = [^;]+;', view).group())
production = [function(buttons, 'static inline int gevrPhysHand('),
              function(buttons, 'extern "C" void vrRecoilKick(').replace('extern "C" ', '')]
production.extend(function(math, signature) for signature in (
    'void matrix_4x4_set_rotation_around_xyz(', 'void matrix_4x4_set_identity('))
production.extend(function(view, signature) for signature in (
    'static s32 gevrGripAxesRaw(', 'static s32 gevrGripAxes(',
    's32 gevrStereoGunMatrix(s32 handnum, Mtxf *out)',
    's32 gevrStereoTwoHandItem(', 'static s32 gevrTwoHandGunCandidate(',
    's32 gevrStereoTwoHandGun(void)', 's32 gevrStereoTwoHandSupportCtrl(void)',
    'static s32 gevrTwoHandIsHandgun(', 'static s32 gevrTwoHandBarrel(',
    's32 gevrStereoTwoHandUpdate(void)', 's32 gevrStereoTwoHandGrip(void)',
    's32 gevrStereoTwoHandClass(void)', 's32 gevrStereoTwoHandMatrix(',
    'static void gevrTwoHandAim(const f32 pos[3], f32 right[3], f32 up[3], f32 back[3])\n{'))
production.extend(function(gun, signature) for signature in (
    'static s32 gevrUnarmedModelItem(', 'static void gevrUnarmedModelUpdate('))
fixture = (ROOT / 'port/tests/two_hand_native.c').read_text()
fixture = fixture.replace('/* DEFINITIONS */', '\n'.join(definitions))
fixture = fixture.replace('/* PRODUCTION */', '\n'.join(production))
with tempfile.TemporaryDirectory(prefix='gevr-two-hand-') as temp:
    source, exe = Path(temp) / 'test.c', Path(temp) / 'test'
    source.write_text(fixture)
    subprocess.run([shutil.which('gcc') or 'gcc', '-std=c11', '-O2', '-fms-extensions', '-D_LANGUAGE_C',
                    '-I'+str(ROOT), '-I'+str(ROOT/'include'), '-I'+str(ROOT/'src'), '-I'+str(ROOT/'port/include'),
                    str(source), '-lm', '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
