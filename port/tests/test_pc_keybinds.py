"""Phase 2 tests: PC keybinds capture, save/load round-trip, defaults, duplicates,
and no-scroll multi-size layout fit for Keyboard & mouse and Gamepad pages."""
from pathlib import Path
import shutil, subprocess, tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='gevr-pc-keybinds-') as temp:
    exe = Path(temp) / 'pc_keybinds.exe'
    args = [shutil.which('g++') or 'g++', '-std=c++17', '-O1', '-Iport/vr', '-Iport/include', '-Iport/tests',
            'port/tests/pc_keybinds_native.cpp']
    args += ['port/vr/imgui/' + s for s in ('imgui.cpp', 'imgui_draw.cpp', 'imgui_tables.cpp', 'imgui_widgets.cpp')]
    subprocess.run(args + ['-o', str(exe)], cwd=root, check=True)
    subprocess.run([str(exe)], cwd=root, check=True)
