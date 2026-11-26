"""Headless checks against production launcher widgets and bundled ImGui."""
from pathlib import Path
import shutil, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
out=Path(tempfile.gettempdir())/"gevr-launcher-ui-test.exe"
launcher=(root/'port/vr/vr_launcher.cpp').read_text(encoding='utf-8')
fixture=(root/'port/tests/launcher_ui_native.cpp').read_text(encoding='utf-8')
generated=Path(tempfile.gettempdir())/'gevr-launcher-ui-test.cpp'
generated.write_text(fixture,encoding='utf-8')
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

# the Host page (Game, Advanced) and the Favorites page: the match itself is set up in the game
# (docs/in-game-match-setup.md), so no match, roster or co-op rules pages are left to check
launcher_host=launcher[launcher.index('void gevrMultiplayerPage('):launcher.index('/* The launcher is no longer pumping')]
for gone in ('BeginTabItem("Match")','BeginTabItem("Lobby")','gevrLobbyRoster','"Ready"','"Start Hosting"','loadoutRows'):
    assert gone not in launcher, gone+': the in-game setup replaced it'
assert 'BeginTabItem("Game")' in launcher_host and 'BeginTabItem("Advanced")' in launcher_host
assert 'ImGui::Button("Host"' in launcher_host and 'gevrMpStartHosting();\n            gevrMpLaunch(startMatch);' in launcher_host
fixture=(root/'port/tests/launcher_compact_native.cpp').read_text(encoding='utf-8')
generated.write_text(fixture.replace('/* INSERT_OPTIONS */','\n'.join(function(signature) for signature in
    ('static void gevrMovementSpeedOptions(bool', 'static void gevrMatchLiveOptions() {', 'static void gevrHostAdvancedOptions(',
     'static void gevrFavoriteOptions(', 'static void gevrHostLobbyOptions('))),encoding='utf-8')
obj=Path(tempfile.gettempdir())/'gevr-launcher-net-match.o'
subprocess.run([shutil.which('gcc') or 'gcc','-O2','-D_LANGUAGE_C','-Wno-builtin-declaration-mismatch','-I.','-Iport/include','-Iinclude','-Isrc',
                '-c','port/src/net/net_match.c','-o',str(obj)],cwd=root,check=True)
subprocess.run(args[:-2]+['-Iport/src/net',str(obj),'-o',str(out)],cwd=root,check=True)
subprocess.run([str(out)],cwd=root,check=True)
