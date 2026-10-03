"""Headless checks against production pause widgets, input gate and bundled ImGui."""
from pathlib import Path
import shutil, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
source=(root/'port/src/input.c').read_text(encoding='utf-8')
start=source.index('    {',source.index('    /* Menu owns A/B, triggers and sticks.'))
end=source.index('\n#endif',start)
fixture=(root/'port/tests/pause_input_native.cpp').read_text(encoding='utf-8')
with tempfile.TemporaryDirectory(prefix='gevr-pause-input-') as temp:
    cpp=Path(temp)/'pause_input_native.cpp'
    exe=Path(temp)/'pause_input_native.exe'
    cpp.write_text(fixture.replace('/* INSERT_PAUSE_INPUT */',source[start:end]),encoding='utf-8')
    subprocess.run([shutil.which('g++') or 'g++','-std=c++17','-O2','-I'+str(root/'port/vr'),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
output=Path(tempfile.gettempdir())/'gevr-pause-ui-test'
args=[shutil.which('g++') or 'g++','-std=c++17','-O2','-Iport/vr','port/tests/pause_ui_native.cpp']
args += ['port/vr/imgui/'+s for s in ('imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp')]
subprocess.run(args+['-o',str(output)],cwd=root,check=True)
subprocess.run([str(output)],cwd=root,check=True)
