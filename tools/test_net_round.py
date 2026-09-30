"""Build the production multiplayer logic tests with the configured NDK, run on Quest.

Requires a configured release native build and one adb device. This does not install
or launch the game, change settings, or read the ROM. Output stays in a temp folder.
"""
import json
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
databases = list((root / 'android/app/.cxx/RelWithDebInfo').glob('*/arm64-v8a/compile_commands.json'))
if not databases:
    raise SystemExit('Configure/build the Android release target first.')
db = max(databases, key=lambda p: p.stat().st_mtime)
entry = next(e for e in json.loads(db.read_text()) if e['file'].replace('\\', '/').endswith('/net_core.c'))
args = entry.get('arguments') or shlex.split(entry['command'].replace('\\', '/'))
flags = []
i = 1
while i < len(args):
    a = args[i]
    if a in ('-c', '-o'):
        i += 2
        continue
    if a != '-DNDEBUG':
        flags.append(a)
    i += 1
with tempfile.TemporaryDirectory(prefix='gevr-net-tests-') as temp:
    binary = Path(temp) / 'net_round_test'
    sources = [root/'tools/tests/net_round_test.c', root/'port/src/net/netbuf.c', root/'port/src/net/net_match.c']
    subprocess.run([args[0], *flags, *map(str, sources), '-Wl,--gc-sections', '-llog', '-lm', '-o', str(binary)], cwd=entry['directory'], check=True)
    destination = '/data/local/tmp/gevr-net-round-test'
    subprocess.run(['adb', 'push', str(binary), destination], check=True)
    subprocess.run(['adb', 'shell', 'chmod', '700', destination], check=True)
    subprocess.run(['adb', 'shell', destination], check=True)
