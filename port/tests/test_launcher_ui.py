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
# the Gestures page's own branch, to check it fits the panel without scrolling
gstart=launcher.index('} else if (gesturesPage) {')+len('} else if (gesturesPage) ')
gend=gstart+1;depth=1
while depth:
    depth+=(launcher[gend]=='{')-(launcher[gend]=='}');gend+=1
gestures='void gevrTestGestures() '+launcher[gstart:gend]
generated.write_text(fixture.replace('/* INSERT_CONTROLS */','void gevrTestControls() '+controls)
                     .replace('/* INSERT_GESTURES */',gestures),encoding='utf-8')
args=[shutil.which("g++") or "g++","-std=c++17","-O2","-Iport/vr","-Iport/include",str(generated)]
args += ["port/vr/imgui/"+s for s in ("imgui.cpp","imgui_draw.cpp","imgui_tables.cpp","imgui_widgets.cpp")]
args += ["-o",str(out)]
subprocess.run(args,cwd=root,check=True)
subprocess.run([str(out)],cwd=root,check=True)

def function(signature):
    start=launcher.index(signature);opening=launcher.index('{',start);end=opening+1;depth=1
    while depth:
        depth+=(launcher[end]=='{')-(launcher[end]=='}');end+=1
    return launcher[start:end]

start=launcher.index('ImGui::BeginTable("comfort-columns"')
opening=launcher.rfind('[&]() {',0,start)+len('[&]() ')
end=opening+1;depth=1
while depth:
    depth+=(launcher[end]=='{')-(launcher[end]=='}');end+=1
comfort=launcher[opening:end]
start=launcher.index('if (VrMpMode == NET_MODE_COOP) {',launcher.index('if (ImGui::BeginTabItem("Match"))'))
opening=launcher.index('{',start);end=opening+1;depth=1
while depth:
    depth+=(launcher[end]=='{')-(launcher[end]=='}');end+=1
coop=launcher[opening:end]
fixture=(root/'port/tests/launcher_compact_native.cpp').read_text(encoding='utf-8')
generated.write_text(fixture.replace('/* INSERT_OPTIONS */','\n'.join(function(signature) for signature in
    ('static void gevrMatchOptions(bool', 'static void gevrMovementSpeedOptions(bool', 'static void gevrMatchLiveOptions() {')))
    .replace('/* INSERT_COMFORT */',comfort).replace('/* INSERT_COOP */',coop),encoding='utf-8')
obj=Path(tempfile.gettempdir())/'gevr-launcher-net-match.o'
subprocess.run([shutil.which('gcc') or 'gcc','-O2','-D_LANGUAGE_C','-Wno-builtin-declaration-mismatch','-I.','-Iport/include','-Iinclude','-Isrc',
                '-c','port/src/net/net_match.c','-o',str(obj)],cwd=root,check=True)
subprocess.run(args[:-2]+['-Iport/src/net',str(obj),'-o',str(out)],cwd=root,check=True)
subprocess.run([str(out)],cwd=root,check=True)
