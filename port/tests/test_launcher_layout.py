"""Multi-size layout tests for gevr_layout_ui.h (PC window, VR panel, pause overlay)."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import settings_stub_dir

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='gevr-launcher-layout-') as temp:
    temp = Path(temp)
    table = settings_stub_dir.make(temp, root)
    obj = temp / 'gevr_settings.o'
    subprocess.run([shutil.which('gcc') or 'gcc', '-std=gnu11', '-O1', '-I' + str(temp), '-Iport/include', '-c',
                    str(table), '-o', str(obj)], cwd=root, check=True)
    exe = temp / 'launcher_layout.exe'
    args = [shutil.which('g++') or 'g++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-I' + str(temp), '-Iport/vr',
            '-Iport/include', '-Iport/tests', 'port/tests/launcher_layout_native.cpp', str(obj)]
    args += ['port/vr/imgui/' + s for s in ('imgui.cpp', 'imgui_draw.cpp', 'imgui_tables.cpp', 'imgui_widgets.cpp')]
    subprocess.run(args + ['-o', str(exe)], cwd=root, check=True)
    subprocess.run([str(exe)], cwd=root, check=True)
