"""The Windows game's release download (docs/pc-platforms.md).

    python tools/package_pc.py VERSION [--out DIR] [--build-dir DIR]

Builds with tools/build_pc.py (RelWithDebInfo) and packs
GoldenEyeVR-VERSION-windows-x64.zip: the exe without its debug info,
Steam Audio's phonon.dll, README.txt (port/pc/package), the licences and a
VERSION file. The debug info stays beside it as
GoldenEyeVR-VERSION-windows-x64.debug, for reading a crash's addresses (keep
it with the release's symbols; it is not published), as the dedicated
server's packaging does. No game data goes in: the player supplies the ROM.
"""

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import build_pc  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('version')
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'pc-dist'))
    key = hashlib.sha1(ROOT.lower().encode()).hexdigest()[:8]
    ap.add_argument('--build-dir', default=os.path.join(os.environ.get('LOCALAPPDATA', os.path.expanduser('~')), 'gevr-pc', key))
    args = ap.parse_args()

    subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'build_pc.py'), '--build-dir', args.build_dir], check=True)
    objcopy = os.path.join(build_pc.llvm_mingw(), 'bin', 'llvm-objcopy.exe')

    name = f'GoldenEyeVR-{args.version}-windows-x64'
    os.makedirs(args.out, exist_ok=True)
    stage = os.path.join(args.out, name)
    if os.path.isdir(stage):
        shutil.rmtree(stage)
    os.makedirs(os.path.join(stage, 'licenses'))

    exe = os.path.join(args.build_dir, 'GoldenEyeVR.exe')
    debug = os.path.join(args.out, name + '.debug')
    subprocess.run([objcopy, '--only-keep-debug', exe, debug], check=True)
    subprocess.run([objcopy, '--strip-debug', f'--add-gnu-debuglink={debug}', exe,
                    os.path.join(stage, 'GoldenEyeVR.exe')], check=True)
    shutil.copy2(os.path.join(args.build_dir, 'phonon.dll'), stage)
    shutil.copy2(os.path.join(ROOT, 'port', 'pc', 'package', 'README.txt'), stage)
    shutil.copy2(os.path.join(ROOT, 'LICENSE'), os.path.join(stage, 'LICENSE.txt'))
    shutil.copy2(os.path.join(ROOT, 'CREDITS.md'), stage)
    for f in os.listdir(os.path.join(ROOT, 'port', 'licenses')):
        shutil.copy2(os.path.join(ROOT, 'port', 'licenses', f), os.path.join(stage, 'licenses', f))
    shutil.copy2(os.path.join(ROOT, 'port', 'external', 'steamaudio', 'LICENSE.md'),
                 os.path.join(stage, 'licenses', 'steam-audio.md'))
    with open(os.path.join(stage, 'VERSION'), 'w', newline='\n') as f:
        f.write(args.version + '\n')

    zpath = os.path.join(args.out, name + '.zip')
    with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for base, _, files in os.walk(stage):
            for fn in sorted(files):
                full = os.path.join(base, fn)
                z.write(full, os.path.join(name, os.path.relpath(full, stage)))
    h = hashlib.sha256(open(zpath, 'rb').read()).hexdigest()
    print(f'{zpath}\n  {os.path.getsize(zpath) // 1024} KB, SHA-256 {h}\n{debug} (keep with the symbols)')


if __name__ == '__main__':
    main()
