"""Headless checks against production launcher widgets and bundled ImGui."""
from pathlib import Path
import shutil, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
out=Path(tempfile.gettempdir())/"gevr-launcher-ui-test.exe"
args=[shutil.which("g++") or "g++","-std=c++17","-O2","-Iport/vr","port/tests/launcher_ui_native.cpp"]
args += ["port/vr/imgui/"+s for s in ("imgui.cpp","imgui_draw.cpp","imgui_tables.cpp","imgui_widgets.cpp")]
args += ["-o",str(out)]
subprocess.run(args,cwd=root,check=True)
subprocess.run([str(out)],cwd=root,check=True)
