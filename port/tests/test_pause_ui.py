"""Headless checks against production pause widgets, input gate and bundled ImGui."""
from pathlib import Path
import shutil, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
output=Path(tempfile.gettempdir())/'gevr-pause-ui-test'
args=[shutil.which('g++') or 'g++','-std=c++17','-O2','-Iport/vr','port/tests/pause_ui_native.cpp']
args += ['port/vr/imgui/'+s for s in ('imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp')]
subprocess.run(args+['-o',str(output)],cwd=root,check=True)
subprocess.run([str(output)],cwd=root,check=True)
