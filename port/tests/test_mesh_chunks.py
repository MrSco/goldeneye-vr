"""Run the production room mesh builder, with conservative bounds checks."""
from pathlib import Path
import os, shutil, subprocess, tempfile
ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'port/src/gevr_bean_world.c').read_text()
code = source[source.index('GevrStaticMesh *gevrStaticMeshBuild('):source.index('/* G_GEVR_MESH ahead')]
with tempfile.TemporaryDirectory(prefix='gevr-chunks-') as temp:
    c = Path(temp) / 'chunks.c'
    c.write_text((ROOT / 'port/tests/gevr_mesh_chunks_test.c').read_text().replace('/* PRODUCTION MESH BUILDER */', code))
    exe = Path(temp) / 'chunks.exe'
    sanitize = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie'] if os.environ.get('BEAN_SANITIZE') else []
    subprocess.run([shutil.which('gcc') or 'cc', '-std=c11', '-O2', '-DPLATFORM_64BIT=1', '-D_LANGUAGE_C',
                    *sanitize, '-I'+str(ROOT / 'include'), '-I'+str(ROOT / 'port/include'), str(c), '-lm', '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
