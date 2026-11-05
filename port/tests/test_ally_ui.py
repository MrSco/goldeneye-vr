"""The AI ally's goldeneye-vr.ini lines, cut from vr_settings.cpp: round trip and bounds.

Its launcher rows are the settings table's (test_settings_table.py, test_launcher_frame.py)."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
fixture = (ROOT / "port/tests/ally_ui_native.cpp").read_text()
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
    subprocess.run([shutil.which("g++") or "g++", "-std=c++17", "-O2", str(source), "-o", str(exe)], cwd=ROOT, check=True)
    subprocess.run([str(exe)], check=True)
