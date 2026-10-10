"""Exercise production lost-tracking poses and scope placement across XR redraws."""
from pathlib import Path
import argparse
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-ref', help='Check the regression against earlier production code')
parser.add_argument('--scope-only', action='store_true', help='Isolate the scope attachment regression')
args = parser.parse_args()


def read(path):
    if args.source_ref:
        return subprocess.check_output(['git', 'show', f'{args.source_ref}:{path}'], cwd=ROOT, text=True, encoding='utf-8')
    return (ROOT / path).read_text(encoding='utf-8')


def function(source, signature):
    match = re.search(re.escape(signature) + r'[^;{]*\{', source)
    assert match, signature
    start, opening = match.start(), match.end() - 1
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


inp = read('port/vr/vr_input.cpp')
xr = read('port/vr/vr_openxr.cpp')
# Head lookup and space conversion are new; old getters simply do not use them.
current_xr = (ROOT / 'port/vr/vr_openxr.cpp').read_text(encoding='utf-8')
current_inp = (ROOT / 'port/vr/vr_input.cpp').read_text(encoding='utf-8')
production = [function(xr, 'static void vr_quat_to_mat3('),
              function(current_xr, 'extern "C" int gevrVrFrameHeadPose('),
              function(inp, 'static inline int gevrPhysHand(')]
if 'static XrPosef gevrPlayPoseToView(' in inp:
    production.append(function(inp, 'static XrPosef gevrPlayPoseToView('))
production.extend(function(inp, signature) for signature in (
    'extern "C" void gevrVrSnapshotControllers(',
    'extern "C" int gevrVrGripPoseCamera(',
    'extern "C" int gevrVrGripPoseSteady(',
    'extern "C" int gevrVrGripPose('))
production.extend(function(current_inp, signature) for signature in (
    'extern "C" int gevrVrGripUsable(', 'extern "C" int gevrVrGripTracked('))
production.append(function(xr, 'extern "C" int gevrVrRedrawHandDelta('))
if 'static bool vr_scope_gun_pose(' in xr:
    production.append(function(xr, 'static bool vr_scope_gun_pose('))
else:
    # Execute the old submission branch, rather than a hand-written imitation.
    branch = function(xr, 'if (sc.gunValid)')
    production.append('''static bool vr_scope_gun_pose(int, const GevrScopeState& sc,
        float gp[3], float axes[3][3]) {
        float rx,ry,rz,ux,uy,uz,bx,by,bz;
        if (!sc.gunValid) return false;
        ''' + branch + '''
        axes[0][0]=rx; axes[0][1]=ry; axes[0][2]=rz;
        axes[1][0]=ux; axes[1][1]=uy; axes[1][2]=uz;
        axes[2][0]=bx; axes[2][1]=by; axes[2][2]=bz;
        return true;
        }''')
fixture = (ROOT / 'port/tests/grip_redraw_native.cpp').read_text(encoding='utf-8')
fixture = fixture.replace('/* PRODUCTION */', '\n'.join(production))
with tempfile.TemporaryDirectory(prefix='gevr-grip-redraw-') as temp:
    source, exe = Path(temp) / 'test.cpp', Path(temp) / 'test.exe'
    source.write_text(fixture, encoding='utf-8')
    subprocess.run([shutil.which('g++') or 'g++', '-std=c++17', '-O2',
                    '-I' + str(ROOT / 'port/include'), str(source), '-o', str(exe)], check=True)
    subprocess.run([str(exe)] + (['scope'] if args.scope_only else []), check=True)
