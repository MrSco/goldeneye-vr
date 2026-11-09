"""Build the Windows game (docs/pc-platforms.md).

    python tools/build_pc.py [--build-dir DIR] [--config RelWithDebInfo] [--jobs N]

The compiler is LLVM-MinGW (clang, UCRT, x86-64), pinned below and fetched
once into a tools folder shared by every checkout: %LOCALAPPDATA%\\gevr-tools,
or GEVR_TOOLS. GEVR_LLVM_MINGW points at an unpacked copy instead. GCC can't
build the game (token pasting the NDK's clang accepts), and MSVC lacks the GNU
C the decomp is written in, so clang it is, as in Time Crisis II VR.

CMake and Ninja come from PATH. SDL2, zlib and Opus are fetched by CMake.
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
    ]
    subprocess.run(configure, check=True, env=env)
    if args.configure_only:
        return
    build = ['cmake', '--build', args.build_dir, '--target', 'GoldenEyeVR']
    if args.jobs:
        build += ['-j', str(args.jobs)]
    subprocess.run(build, check=True, env=env)
    print(os.path.join(args.build_dir, 'GoldenEyeVR.exe'))


if __name__ == '__main__':
    main()
