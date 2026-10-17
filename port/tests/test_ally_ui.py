"""Exercise the actual Game rules page with bundled ImGui at launcher scale."""
from pathlib import Path
import shutil
import subprocess
import tempfile
from test_reinforcements import block

ROOT = Path(__file__).resolve().parents[2]
launcher = (ROOT / "port/vr/vr_launcher.cpp").read_text()
rules = block(launcher, "} else if (rulesPage) {")
rules = rules[rules.index("{"):]
rules = rules.replace('ImGui::Checkbox("AI ally (Natalya)", &ally)',
                      'allyCheckbox("AI ally (Natalya)", &ally)')
rules = rules.replace('ImGui::Button("Done", ImVec2(-1, 0))', 'done("Done", ImVec2(-1, 0))')
rules = rules.replace('ImGui::EndChild();', 'scrollMax=ImGui::GetScrollMaxY(); ImGui::EndChild();')
fixture = (ROOT / "port/tests/ally_ui_native.cpp").read_text()
fixture = fixture.replace("/* INSERT_RULES */", "static void drawRules() { const ImVec4 gold(1,1,0,1); " + rules + " }")
settings = (ROOT / "port/vr/vr_settings.cpp").read_text()
start = settings.index('    fprintf(f, "Ally=%d')
save = settings[start:settings.index(');', start) + 2]
start = settings.index('            else if (strcmp(key, "Ally")')
load = settings[start:settings.index('            else if (strcmp(key, "FastReinforcements")', start)]
load = load.replace('else if', 'if', 1)
fixture = fixture.replace("/* INSERT_SETTINGS */", "static void saveAlly(FILE *f) {" + save + "}\n"
                          'static void loadAlly(const char *line) { char key[64]; int ival; '
                          'if (sscanf(line, "%63[^=]=%d", key, &ival) == 2) {' + load + '}}')
with tempfile.TemporaryDirectory(prefix="gevr-ally-ui-") as tmp:
    source, exe = Path(tmp)/"test.cpp", Path(tmp)/"test"
    source.write_text(fixture)
    args = [shutil.which("g++") or "g++", "-std=c++17", "-O2", "-Iport/vr", "-Iport/include", str(source)]
    args += ["port/vr/imgui/" + name for name in ("imgui.cpp", "imgui_draw.cpp", "imgui_tables.cpp", "imgui_widgets.cpp")]
    subprocess.run(args + ["-o", str(exe)], cwd=ROOT, check=True)
    subprocess.run([str(exe)], check=True)
