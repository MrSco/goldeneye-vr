"""Exercise production pose acceptance with valid but untracked OpenXR output."""
from pathlib import Path
import argparse
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-ref', help='Reproduce the freeze in earlier pose acceptance code')
args = parser.parse_args()
source = (ROOT/'port/vr/vr_input.cpp').read_text(encoding='utf-8')
if args.source_ref:
    source = subprocess.check_output(['git','show',args.source_ref+':port/vr/vr_input.cpp'],cwd=ROOT,text=True,encoding='utf-8')
start = source.index('        // Pose',source.index('XrResult update_vr_controllers('))
end = source.index('        // Actions bool',start)
production = 'static XrResult sample(int hand) { auto& state=gControllerStates[hand]; XrPath handPath=gHandPaths[hand]; XrTime predicted_time=1;\n'+source[start:end]+'return XR_SUCCESS; }'
fixture = (ROOT/'port/tests/controller_tracking_native.cpp').read_text(encoding='utf-8')
with tempfile.TemporaryDirectory(prefix='gevr-tracking-') as directory:
    temp=Path(directory);file,exe=temp/'test.cpp',temp/'test.exe'
    file.write_text(fixture.replace('/* PRODUCTION */',production),encoding='utf-8')
    subprocess.run([shutil.which('g++') or 'g++','-std=c++17','-O2','-I'+str(ROOT/'OpenXR/Include'),str(file),'-o',str(exe)],check=True)
    result=subprocess.run([str(exe)])
    if args.source_ref:
        assert result.returncode!=0,'Earlier updater unexpectedly accepts inferred poses'
        print(f'CONFIRMED: {args.source_ref} freezes valid untracked controller updates')
    else:
        result.check_returncode()
