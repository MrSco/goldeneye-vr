"""Headless layout checks for the Friends sidebar and the in-game panel (production headers, bundled ImGui)."""
from pathlib import Path
import shutil, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='gevr-friends-ui-') as temp:
    exe=Path(temp)/'friends_ui_native.exe'
    args=[shutil.which('g++') or 'g++','-std=c++17','-O2','-Iport/vr','-Iport/include','port/tests/friends_ui_native.cpp']
    args+=['port/vr/imgui/'+s for s in ('imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp')]
    subprocess.run(args+['-o',str(exe)],cwd=root,check=True)
    subprocess.run([str(exe)],cwd=root,check=True)
