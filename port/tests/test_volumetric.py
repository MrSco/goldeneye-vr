"""Native tests for analytical 3D volumetric smoke & explosions (issue #249)."""
from pathlib import Path
import shutil, subprocess, tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='gevr-volumetric-') as temp:
    exe = Path(temp) / 'volumetric_native.exe'
    cc = shutil.which('gcc') or 'gcc'
    subprocess.run(
        [
            cc, '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror=implicit-function-declaration',
            '-Iport/include',
            'port/src/gevr_volumetric.c',
            'port/tests/volumetric_native.c',
            '-lm', '-o', str(exe),
        ],
        cwd=root,
        check=True,
    )
    subprocess.run([str(exe)], cwd=root, check=True)
