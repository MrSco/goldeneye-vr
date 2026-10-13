"""Run production cinematic player ticks and intro exits, including reloads.

The animation/renderer fixture deliberately displaces the body and loses its
floor tile. --baseline-ref proves the same checks fail before the fix.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
from test_reinforcements import block

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-ref')
    baseline = parser.parse_args().baseline_ref
    if baseline:
        source = subprocess.check_output(['git', 'show', baseline + ':src/game/bondview2.c'],
                                         cwd=ROOT, text=True)
    else:
        source = (ROOT / 'src/game/bondview2.c').read_text()
    tick = block(source, 's32 playerTick(PropRecord *prop)')
    # Keep the complete production cinematic branch, including chrTick,
    # collision copyback and its return; unrelated gameplay rendering is omitted.
    tick = tick[tick.index('    index = getPlayerPointerIndex(prop);'):]
    tick = tick[:tick.index('    if (chr == NULL)')]
    helpers = '\n'.join(block(source, signature) for signature in (
        'static s32 gevrCoopIntroActive(', 'static void gevrCoopFinishIntro(')
        if signature in source)
    fallback = block(source, 'else if (g_CameraMode == CAMERAMODE_SWIRL)')
    fallback = fallback[fallback.rindex('        else\n'):]
    fallback = block(fallback, '        else\n')
    fixture = (ROOT / 'port/tests/coop_intro_native.c').read_text()
    for marker, code in {
        'COLLISION_TYPE': block((ROOT / 'src/game/bondview.h').read_text(), 'struct collision434 {') + ';',
        'HELPERS': helpers,
        'TICK': tick,
        'ADVANCE': block(source, 'void bondviewAdvanceCameraMode('),
        'REMOVE': block(source, 'void bondviewRemovePlayerBody('),
        'BOUNDS': block(source, 'void bondviewUpdatePlayerCollisionBounds('),
        'NO_SWIRL': fallback,
    }.items():
        fixture = fixture.replace('/* INSERT_' + marker + ' */', code)
    with tempfile.TemporaryDirectory(prefix='gevr-coop-intro-') as temp:
        cfile, exe = Path(temp) / 'intro.c', Path(temp) / 'intro.exe'
        cfile.write_text(fixture)
        args = [shutil.which('gcc') or 'gcc', '-std=c11', '-O2', '-fms-extensions',
                '-Wno-builtin-declaration-mismatch']
        args += ['-D' + v for v in ('GEVR=1', 'PLATFORM_64BIT=1', '_LANGUAGE_C=1',
                                   'VERSION=2', 'VERSION_US=1', 'LANG_US=1')]
        args += ['-I' + str(ROOT / p) for p in ('port/include', 'include', 'src', '.')]
        compiled = subprocess.run(args + [str(cfile), '-lm', '-o', str(exe)],
                                  capture_output=True, text=True)
        if compiled.returncode:
            raise RuntimeError(compiled.stderr)
        result = subprocess.run([str(exe)], capture_output=True, text=True)
        if baseline:
            assert result.returncode, 'baseline unexpectedly passed'
            print('CONFIRMED: co-op intro regression fails on ' + baseline + ': ' + result.stderr.strip())
        else:
            if result.returncode:
                raise RuntimeError(result.stderr)
            print(result.stdout.strip())


if __name__ == '__main__':
    main()
