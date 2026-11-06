"""The launcher's frame with the production pages cut from vr_launcher.cpp and the
real settings table (port/src/gevr_settings.c): every page fits, no scrolling."""
from pathlib import Path
import shutil, subprocess, tempfile
import settings_stub_dir
root = Path(__file__).resolve().parents[2]
source = (root / 'port/vr/vr_launcher.cpp').read_text(encoding='utf-8')


def balanced(text, opening):
    end, depth = opening + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return end


def function(signature):
    start = source.index(signature)
    return source[start:balanced(source, source.index('{', start))]


# the frame: from the tab rows to the line under START
frame_start = source.index('        const bool updLine = upd.state == "available"')
lam = source.index('gevrLauncherTabRows(launcherTabs, [&](int tab, int sub) {', frame_start)
lam_end = balanced(source, source.index('{', lam)) + len(');')
assert source[lam_end - 2:lam_end] == ');'
page = source[frame_start:lam_end]
# where the page drew, for the checks
page = page.replace('            ImGui::EndChild();\n        });',
                    '            { ImGuiWindow *w = ImGui::GetCurrentWindow(); pageBottom = w->DC.CursorMaxPos.y; '
                    'pageLimit = w->ClipRect.Max.y; pageRight = w->DC.CursorMaxPos.x; pageLimitX = w->ClipRect.Max.x; '
                    'pageWidth = ImGui::GetContentRegionAvail().x; pageTab = tab; pageSub = sub; }\n'
                    '            ImGui::EndChild();\n        });')
assert 'pageBottom = ' in page
update = source[source.index('        // Update line: only when there is something to say'):source.index('        const bool campaignReady')]
footer = ('        if (launcherTabs.top != GEVR_LTAB_MULTIPLAYER) footerTop = ImGui::GetCursorPosY();\n'
          '        if (launcherTabs.top != GEVR_LTAB_MULTIPLAYER) {\n        ImGui::Separator();\n' + update +
          '        int campaign = 0;\n        gevrLauncherStartRow(&campaign, true, true, true, true, false);\n'
          '        startBottom = ImGui::GetItemRectMax().y;\n        }\n')
row = source[source.index('enum {\n    GEVR_START_ROW_NONE,'):source.index('static void gevrModsPage(')]
cheats = function('static void gevrCheatsPage(')
# Multiplayer's Audio and Host > Fun, as gevrMultiplayerPage draws them
audio_at = source.index('    auto audioOptions = [&]() {')
audio = source[audio_at:balanced(source, source.index('{', audio_at)) + 1]
assert audio.endswith('};')
fun_at = source.index('if (ImGui::BeginTabItem("Fun")) {')
fun = source[source.index('{', fun_at) + 1:balanced(source, source.index('{', fun_at)) - 1]
fun = fun.replace('ImGui::EndTabItem();', '')
fixture = (root / 'port/tests/launcher_frame_native.cpp').read_text(encoding='utf-8')
fixture = fixture.replace('/* AUDIO_OPTIONS */', audio).replace('/* FUN_PAGE */', fun)
fixture = fixture.replace('/* CHEATS */', cheats).replace('/* START_ROW */', row).replace('/* FRAME */', page + '\n' + footer)
with tempfile.TemporaryDirectory(prefix='gevr-launcher-frame-') as temp:
    temp = Path(temp)
    table = settings_stub_dir.make(temp, root)
    generated = temp / 'launcher_frame.cpp'
    generated.write_text(fixture, encoding='utf-8')
    obj = temp / 'gevr_settings.o'
    subprocess.run([shutil.which('gcc') or 'gcc', '-std=gnu11', '-O1', '-I' + str(temp), '-Iport/include', '-c', str(table),
                    '-o', str(obj)], cwd=root, check=True)
    exe = temp / 'launcher_frame.exe'
    args = [shutil.which('g++') or 'g++', '-std=c++17', '-O1', '-I' + str(temp), '-Iport/vr', '-Iport/include', '-Iport/tests',
            str(generated), str(obj)]
    args += ['port/vr/imgui/' + s for s in ('imgui.cpp', 'imgui_draw.cpp', 'imgui_tables.cpp', 'imgui_widgets.cpp')]
    subprocess.run(args + ['-o', str(exe)], cwd=root, check=True)
    subprocess.run([str(exe)], cwd=root, check=True)
