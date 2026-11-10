"""The Windows game's release download (docs/pc-platforms.md).

    python tools/package_pc.py [VERSION] [--out DIR] [--build-dir DIR] [--no-build]

VERSION defaults to android/app/build.gradle's versionName (the version the
exes report, CMakeLists.txt reads the same line). Builds with
tools/build_pc.py (RelWithDebInfo) and packs
GoldenEyeVR-VERSION-windows-x64.zip:

- GoldenEyeVR.exe (flat: monitor, keyboard and mouse, gamepad) and
  GoldenEyeVR-VR.exe (PC VR through the active OpenXR runtime), each without
  its debug info;
- Steam Audio's phonon.dll and Khronos's openxr_loader.dll;
- the launch scripts, as Time Crisis II VR's package has them
  (other_projects/time-crisis-2-vr tools/build_pc.py, pc/Start-SteamVR.ps1):
  "Play VR.cmd" (the active runtime: Quest Link, Virtual Desktop, ...),
  "Play SteamVR.cmd" + Start-SteamVR.ps1 (SteamVR's runtime for that launch
  only, through XR_RUNTIME_JSON); the flat exe runs directly;
- README.txt (port/pc/package), the licences and a VERSION file.

The debug info stays beside the zip as GoldenEyeVR-VERSION-windows-x64.debug
and GoldenEyeVR-VR-VERSION-windows-x64.debug, for reading a crash's addresses:
they go to the reports repo's `symbols` release with the Quest's symbols zip
(STATUS.md, "How to work on it"), not into the game's release. No game data
goes in: the player supplies the ROM. The updater (port/pc/pc_updater.cpp)
looks for exactly this zip name and replaces every file the zip holds.
"""

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import build_pc  # noqa: E402

EXES = ('GoldenEyeVR.exe', 'GoldenEyeVR-VR.exe')
DLLS = ('phonon.dll', 'openxr_loader.dll')

# Time Crisis II VR's scripts, with our exe's name (CRLF: they are batch files)
PLAY_VR = '@echo off\ncd /d "%~dp0"\nstart "" GoldenEyeVR-VR.exe\n'
PLAY_STEAMVR = '@echo off\npowershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-SteamVR.ps1"\n'
START_STEAMVR = """$ErrorActionPreference = 'Stop'
try {
    $steam = (Get-ItemProperty 'HKCU:\\Software\\Valve\\Steam').SteamPath
    $runtime = Join-Path $steam 'steamapps\\common\\SteamVR\\steamxr_win64.json'
    if (!(Test-Path -LiteralPath $runtime)) {
        throw 'SteamVR was not found in the standard Steam library. Start your VR software and use Play VR.cmd, or set XR_RUNTIME_JSON to your SteamVR runtime manifest.'
    }
    $env:XR_RUNTIME_JSON = $runtime
    Set-Location -LiteralPath $PSScriptRoot
    & (Join-Path $PSScriptRoot 'GoldenEyeVR-VR.exe')
} catch {
    Write-Host $_.Exception.Message
    Read-Host 'Press Enter to close'
}
"""


def app_version():
    """android/app/build.gradle's versionName, as CMakeLists.txt reads it."""
    with open(os.path.join(ROOT, 'android', 'app', 'build.gradle'), encoding='utf-8') as f:
        m = re.search(r'versionName "([^"]+)"', f.read())
    if not m:
        sys.exit('android/app/build.gradle has no versionName')
    return m.group(1)


def write_text(path, text, newline='\r\n'):
    with open(path, 'w', newline=newline, encoding='utf-8') as f:
        f.write(text)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('version', nargs='?', help="default: android/app/build.gradle's versionName")
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'pc-dist'))
    key = hashlib.sha1(ROOT.lower().encode()).hexdigest()[:8]
    ap.add_argument('--build-dir', default=os.path.join(os.environ.get('LOCALAPPDATA', os.path.expanduser('~')), 'gevr-pc', key))
    ap.add_argument('--no-build', action='store_true', help='pack what the build folder holds')
    args = ap.parse_args()
    version = args.version or app_version()
    if version[:1] in 'vV':
        version = version[1:]   # the tag is vX.Y.Z, the zip and VERSION say X.Y.Z

    if not args.no_build:
        subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'build_pc.py'), '--build-dir', args.build_dir], check=True)
    for f in EXES + DLLS:
        if not os.path.isfile(os.path.join(args.build_dir, f)):
            sys.exit(f'{args.build_dir} has no {f} (build_pc.py builds both exes and copies the DLLs beside them)')
    objcopy = os.path.join(build_pc.llvm_mingw(), 'bin', 'llvm-objcopy.exe')

    name = f'GoldenEyeVR-{version}-windows-x64'
    os.makedirs(args.out, exist_ok=True)
    stage = os.path.join(args.out, name)
    if os.path.isdir(stage):
        shutil.rmtree(stage)
    os.makedirs(os.path.join(stage, 'licenses'))

    debugs = []
    for exe in EXES:
        base = exe[:-4]
        debug = os.path.join(args.out, f'{base}-{version}-windows-x64.debug')
        built = os.path.join(args.build_dir, exe)
        subprocess.run([objcopy, '--only-keep-debug', built, debug], check=True)
        subprocess.run([objcopy, '--strip-debug', f'--add-gnu-debuglink={debug}', built,
                        os.path.join(stage, exe)], check=True)
        debugs.append(debug)
    for dll in DLLS:
        shutil.copy2(os.path.join(args.build_dir, dll), stage)
    write_text(os.path.join(stage, 'Play VR.cmd'), PLAY_VR)
    write_text(os.path.join(stage, 'Play SteamVR.cmd'), PLAY_STEAMVR)
    write_text(os.path.join(stage, 'Start-SteamVR.ps1'), START_STEAMVR)
    shutil.copy2(os.path.join(ROOT, 'port', 'pc', 'package', 'README.txt'), stage)
    for img in ('gevr_icon.rgba', 'gevr_logo.rgba'):   # the window's icon, the headers' logo
        shutil.copy2(os.path.join(ROOT, 'port', 'pc', 'package', img), stage)
    shutil.copy2(os.path.join(ROOT, 'LICENSE'), os.path.join(stage, 'LICENSE.txt'))
    shutil.copy2(os.path.join(ROOT, 'CREDITS.md'), stage)
    for f in os.listdir(os.path.join(ROOT, 'port', 'licenses')):
        shutil.copy2(os.path.join(ROOT, 'port', 'licenses', f), os.path.join(stage, 'licenses', f))
    shutil.copy2(os.path.join(ROOT, 'port', 'external', 'steamaudio', 'LICENSE.md'),
                 os.path.join(stage, 'licenses', 'steam-audio.md'))
    write_text(os.path.join(stage, 'VERSION'), version + '\n', newline='\n')

    zpath = os.path.join(args.out, name + '.zip')
    with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for base, _, files in os.walk(stage):
            for fn in sorted(files):
                full = os.path.join(base, fn)
                z.write(full, os.path.join(name, os.path.relpath(full, stage)))
    h = hashlib.sha256(open(zpath, 'rb').read()).hexdigest()
    print(f'{zpath}\n  {os.path.getsize(zpath) // 1024} KB, SHA-256 {h}')
    for d in debugs:
        print(f'{d} (to the reports repo\'s symbols release)')


if __name__ == '__main__':
    main()
