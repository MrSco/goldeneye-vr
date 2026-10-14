"""Goldfinger cast decoding and production menu/cast allocation regression.

All model and ROM-window bytes are synthetic. --baseline-ref verifies the
reported capacity failure against the original cast initializer.
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
    front = (ROOT / 'src/game/front.c').read_text()
    campaign = (ROOT / 'port/src/gevr_romcampaign.c').read_text()
    viewport = (ROOT / 'src/game/viewport.c').read_text()
    fixture = (ROOT / 'port/tests/title_cast_native.c').read_text()
    binder = '\n'.join(block(campaign, signature) for signature in (
        'static uint32_t be32at(', 'static uint16_t be16at(',
        'static const uint8_t *atVram(', 'static int readCastTable(',
        'const struct intro_char *gevrRomCastTable('))
    globals_ = '\n'.join(block(front, signature) + ';' for signature in (
        'struct intro_random_rifles\n', 'struct intro_random_pistols\n',
        'struct intro_random_rifles random_rifles_in_intro =',
        'struct intro_random_pistols random_pistols_in_intro ='))
    init = block(front, 'void init_menu18_displaycast(void)')
    if baseline:
        original = subprocess.check_output(['git', 'show', baseline + ':src/game/front.c'],
                                           cwd=ROOT, text=True)
        init = block(original, 'void init_menu18_displaycast(void)')
    substitutions = {
        'BINDER': binder, 'GLOBALS': globals_,
        'CAST_HELPER': block(front, 'static const struct intro_char *gevrCastTable('),
        'INIT': init,
        'ADVANCE': block(front, 'void interface_menu18_displaycast(void)'),
        'MENU_INIT': block((ROOT / 'src/game/initmenus.c').read_text(), 'void init_menus_or_reset(void)'),
        'DEPTH_SET': block(viewport, 'void zbufSetBuffer('),
        'DEPTH_INIT': block(viewport, 'Gfx *zbufInit('),
    }
    for marker, source in substitutions.items():
        fixture = fixture.replace('/* INSERT_' + marker + ' */', source)
    with tempfile.TemporaryDirectory(prefix='gevr-title-cast-') as tmp:
        source, exe = Path(tmp) / 'test.c', Path(tmp) / 'test'
        source.write_text(fixture)
        compiler = shutil.which('gcc') or 'gcc'
        compiler_headers = subprocess.check_output([compiler, '-print-file-name=include'], text=True).strip()
        args = [compiler, '-std=c11', '-O1', '-g',
                '-include', 'stddef.h', '-include', compiler_headers + '/stdarg.h', '-fms-extensions',
                '-Wno-builtin-declaration-mismatch']
        args += ['-D' + value for value in ('GEVR=1', 'PLATFORM_64BIT=1', '_LANGUAGE_C=1',
                                          'VERSION=2', 'VERSION_US=1', 'LANG_US=1')]
        args += ['-I' + str(ROOT / path) for path in ('.', 'port/include', 'include', 'src', 'src/game')]
        result = subprocess.run(args + [str(source), str(ROOT / 'port/src/gevr_romlayout.c'),
                                       str(ROOT / 'port/src/gevr_romcheck.c'),
                                       str(ROOT / 'port/src/gevr_rom_manifest.c'), '-lz', '-lm',
                                       '-o', str(exe)], capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stderr)
        result = subprocess.run([str(exe)], capture_output=True, text=True)
        if baseline:
            assert result.returncode and 'cast model capacity' in result.stderr, result.stderr
            print('CONFIRMED: ' + baseline + ' fails the cast model capacity regression')
        else:
            if result.returncode:
                raise RuntimeError(result.stderr)
            print(result.stdout.strip())


if __name__ == '__main__':
    main()
