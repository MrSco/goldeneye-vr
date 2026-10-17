"""Run the real companion packet sender/receiver and validation without ENet."""
from pathlib import Path
import shutil
import subprocess
import tempfile
from test_reinforcements import block

ROOT = Path(__file__).resolve().parents[2]
fixture = (ROOT / "port/tests/ally_network_native.c").read_text()
validation = block((ROOT / "src/game/gevr_ally.c").read_text(), "s32 gevrAllyStateValid(")
fixture = fixture.replace("/* INSERT_VALIDATION */", validation)
with tempfile.TemporaryDirectory(prefix="gevr-ally-network-") as temp:
    source, exe = Path(temp)/"test.c", Path(temp)/"test"
    source.write_text(fixture)
    args = [shutil.which("gcc") or "gcc", "-std=c11", "-O2", "-fms-extensions"]
    args += ["-D" + v for v in ("GEVR=1", "PLATFORM_64BIT=1", "_LANGUAGE_C=1",
                                "VERSION=2", "VERSION_US=1", "LANG_US=1")]
    args += ["-I" + str(ROOT / p) for p in (".", "port/include", "include", "src", "src/game", "port/src", "port/src/net")]
    result = subprocess.run(args + [str(source), str(ROOT/"port/src/net/net_ally.c"),
                                   str(ROOT/"port/src/net/netbuf.c"), "-o", str(exe)], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr)
    subprocess.run([str(exe)], check=True)
