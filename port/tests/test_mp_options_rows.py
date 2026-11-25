"""The in-match Multiplayer Options folder's rows (src/game/gevr_mpoptions.c gevrMpOptionsRowSet), compiled as
written: the original folder's order, the host's rows read-only at other seats, teams, votes, scenarios, a round
in progress, split-screen seats, a dedicated server's players, and no folder for co-op (Horde's rows to come)."""
from pathlib import Path
import shutil, subprocess, tempfile

root = Path(__file__).resolve().parents[2]
compiler = shutil.which('gcc') or shutil.which('cc') or shutil.which('clang')
if compiler is None:
    raise SystemExit('SKIP: no C compiler')
with tempfile.TemporaryDirectory(prefix='gevr-mpoptions-rows-') as tmp:
    exe = Path(tmp) / 'mpoptions_rows.exe'
    subprocess.run([compiler, '-std=c99', '-Wall', '-Werror', '-I' + str(root / 'src/game'),
                    str(root / 'port/tests/mpoptions_rows_native.c'), str(root / 'src/game/gevr_mpoptions.c'),
                    '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
