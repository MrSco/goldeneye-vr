"""Build the Windows game (docs/pc-platforms.md).

    python tools/build_pc.py [--build-dir DIR] [--config RelWithDebInfo] [--jobs N]

The compiler is LLVM-MinGW (clang, UCRT, x86-64), pinned below and fetched
once into a tools folder shared by every checkout: %LOCALAPPDATA%\\gevr-tools,
or GEVR_TOOLS. GEVR_LLVM_MINGW points at an unpacked copy instead. GCC can't
build the game (token pasting the NDK's clang accepts), and MSVC lacks the GNU
C the decomp is written in, so clang it is, as in Time Crisis II VR.

Steam Audio (voice chat's binaural rendering, net_spatial.c) is the Quest's
version, 4.8.1, from Valve's official SDK archive (checked against the SHA-256
in port/external/steamaudio/README.md); its Windows phonon.dll goes beside
the exe. CMake and Ninja come from PATH. SDL2, zlib and Opus are fetched by
CMake.

The build folder defaults to a short path under %LOCALAPPDATA%: object paths
past 250 characters break CMake's compiler checks under a long worktree path.
"""

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import urllib.request
import zipfile

LLVM_MINGW = 'llvm-mingw-20260922-ucrt-x86_64'
LLVM_MINGW_URL = f'https://github.com/mstorsjo/llvm-mingw/releases/download/20260922/{LLVM_MINGW}.zip'
STEAM_AUDIO = 'steamaudio-4.8.1'
STEAM_AUDIO_URL = 'https://github.com/ValveSoftware/steam-audio/releases/download/v4.8.1/steamaudio_4.8.1.zip'
STEAM_AUDIO_SHA256 = '4a0aa5ec1176f38f0b0993a37c2259d9e86f27e22d5e24f83ec4c3cb9a1d5449'
# Khronos's Windows OpenXR loader, the version of the headers in OpenXR/Include
OPENXR = 'openxr-loader-1.1.54'
OPENXR_URL = 'https://github.com/KhronosGroup/OpenXR-SDK/releases/download/release-1.1.54/OpenXR.Loader.1.1.54.nupkg'
OPENXR_SHA256 = '76d3a3eb3010d51da5cac769adda979b7f68331c67e8830933908ebbcdc97107'

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def tools_dir():
    base = os.environ.get('GEVR_TOOLS') or os.path.join(os.environ.get('LOCALAPPDATA', os.path.expanduser('~')), 'gevr-tools')
    os.makedirs(base, exist_ok=True)
    return base


def llvm_mingw():
    given = os.environ.get('GEVR_LLVM_MINGW')
    if given:
        return given
    dest = os.path.join(tools_dir(), LLVM_MINGW)
    if os.path.isfile(os.path.join(dest, 'bin', 'clang.exe')):
        return dest
    archive = dest + '.zip'
    if not os.path.isfile(archive):
        print(f'fetching {LLVM_MINGW_URL}', flush=True)
        part = archive + '.part'
        urllib.request.urlretrieve(LLVM_MINGW_URL, part)
        os.replace(part, archive)
    print(f'unpacking {archive}', flush=True)
    with zipfile.ZipFile(archive) as z:
        z.extractall(tools_dir())
    return dest


def fetch(url, archive):
    if not os.path.isfile(archive):
        print(f'fetching {url}', flush=True)
        part = archive + '.part'
        urllib.request.urlretrieve(url, part)
        os.replace(part, archive)


def steam_audio():
    """The Windows x64 phonon.dll and phonon.lib of the Quest's Steam Audio."""
    dest = os.path.join(tools_dir(), STEAM_AUDIO)
    if os.path.isfile(os.path.join(dest, 'phonon.dll')) and os.path.isfile(os.path.join(dest, 'phonon.lib')):
        return dest
    archive = os.path.join(tools_dir(), STEAM_AUDIO + '.zip')
    fetch(STEAM_AUDIO_URL, archive)
    h = hashlib.sha256()
    with open(archive, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    if h.hexdigest() != STEAM_AUDIO_SHA256:
        os.remove(archive)
        sys.exit(f'{archive}: SHA-256 {h.hexdigest()} is not Steam Audio 4.8.1\'s')
    os.makedirs(dest, exist_ok=True)
    with zipfile.ZipFile(archive) as z:
        for name in z.namelist():
            if '/lib/windows-x64/' in name and name.rsplit('/', 1)[-1] in ('phonon.dll', 'phonon.lib'):
                with z.open(name) as src, open(os.path.join(dest, name.rsplit('/', 1)[-1]), 'wb') as out:
                    shutil.copyfileobj(src, out)
    if not os.path.isfile(os.path.join(dest, 'phonon.dll')):
        sys.exit(f'{archive}: no lib/windows-x64/phonon.dll in it')
    return dest


def sha256_of(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def openxr_loader():
    """openxr_loader.lib and .dll for x64 Windows (PC VR, GoldenEyeVR-VR)."""
    dest = os.path.join(tools_dir(), OPENXR)
    if all(os.path.isfile(os.path.join(dest, f)) for f in ('openxr_loader.lib', 'openxr_loader.dll')):
        return dest
    archive = os.path.join(tools_dir(), OPENXR + '.nupkg')
    fetch(OPENXR_URL, archive)
    if sha256_of(archive) != OPENXR_SHA256:
        os.remove(archive)
        sys.exit(f'{archive}: not the pinned OpenXR loader 1.1.54')
    os.makedirs(dest, exist_ok=True)
    with zipfile.ZipFile(archive) as z:
        for name in ('native/x64/release/lib/openxr_loader.lib', 'native/x64/release/bin/openxr_loader.dll'):
            with z.open(name) as src, open(os.path.join(dest, name.rsplit('/', 1)[-1]), 'wb') as out:
                shutil.copyfileobj(src, out)
    return dest


def main():
    ap = argparse.ArgumentParser()
    key = hashlib.sha1(ROOT.lower().encode()).hexdigest()[:8]
    ap.add_argument('--build-dir', default=os.path.join(os.environ.get('LOCALAPPDATA', os.path.expanduser('~')), 'gevr-pc', key))
    ap.add_argument('--config', default='RelWithDebInfo')
    ap.add_argument('--jobs', type=int, default=0)
    ap.add_argument('--configure-only', action='store_true')
    args = ap.parse_args()

    for tool in ('cmake', 'ninja'):
        if not shutil.which(tool):
            sys.exit(f'{tool} is not on PATH')
    mingw = llvm_mingw()
    phonon = steam_audio().replace(os.sep, '/')
    openxr = openxr_loader().replace(os.sep, '/')
    bin_dir = os.path.join(mingw, 'bin').replace(os.sep, '/')   # CMake reads backslashes as escapes
    env = dict(os.environ)
    # llvm-mingw first: a gcc elsewhere on PATH (Strawberry Perl ships one) must not be picked up
    env['PATH'] = bin_dir + os.pathsep + env['PATH']

    os.makedirs(args.build_dir, exist_ok=True)
    print(f'build folder: {args.build_dir}', flush=True)
    configure = [
        'cmake', '-S', ROOT, '-B', args.build_dir, '-G', 'Ninja',
        '-DGEVR_PLATFORM=pc',
        f'-DCMAKE_BUILD_TYPE={args.config}',
        f'-DCMAKE_C_COMPILER={bin_dir + "/x86_64-w64-mingw32-clang.exe"}',
        f'-DCMAKE_CXX_COMPILER={bin_dir + "/x86_64-w64-mingw32-clang++.exe"}',
        f'-DCMAKE_RC_COMPILER={bin_dir + "/x86_64-w64-mingw32-windres.exe"}',
        f'-DGEVR_STEAMAUDIO_WIN={phonon}',
        f'-DGEVR_OPENXR_WIN={openxr}',
    ]
    subprocess.run(configure, check=True, env=env)
    if args.configure_only:
        return
    build = ['cmake', '--build', args.build_dir, '--target', 'GoldenEyeVR', 'GoldenEyeVR-VR']
    if args.jobs:
        build += ['-j', str(args.jobs)]
    subprocess.run(build, check=True, env=env)
    print(os.path.join(args.build_dir, 'GoldenEyeVR.exe'))
    print(os.path.join(args.build_dir, 'GoldenEyeVR-VR.exe'))


if __name__ == '__main__':
    main()
