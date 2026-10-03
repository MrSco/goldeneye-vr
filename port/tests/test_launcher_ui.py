"""Headless checks against production launcher widgets and bundled ImGui."""
from pathlib import Path
import shutil, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
out=Path(tempfile.gettempdir())/"gevr-launcher-ui-test.exe"
launcher=(root/'port/vr/vr_launcher.cpp').read_text(encoding='utf-8')
start=launcher.index('ImGui::BeginTable("controls-columns"')
opening=launcher.rfind('[&]() {',0,start)+len('[&]() ')
end=opening+1;depth=1
while depth:
    depth+=(launcher[end]=='{')-(launcher[end]=='}');end+=1
fixture=(root/'port/tests/launcher_ui_native.cpp').read_text(encoding='utf-8')
generated=Path(tempfile.gettempdir())/'gevr-launcher-ui-test.cpp'
controls=launcher[opening:end].replace('ImGui::EndTable();',
    'watchControlsRect=ImRect(ImGui::GetItemRectMin(),ImGui::GetItemRectMax());ImGui::EndTable();')
generated.write_text(fixture.replace('/* INSERT_CONTROLS */','void gevrTestControls() '+controls),encoding='utf-8')
args=[shutil.which("g++") or "g++","-std=c++17","-O2","-Iport/vr","-Iport/include",str(generated)]
args += ["port/vr/imgui/"+s for s in ("imgui.cpp","imgui_draw.cpp","imgui_tables.cpp","imgui_widgets.cpp")]
args += ["-o",str(out)]
subprocess.run(args,cwd=root,check=True)
subprocess.run([str(out)],cwd=root,check=True)
