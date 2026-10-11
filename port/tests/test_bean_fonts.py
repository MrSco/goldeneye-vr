"""Production Bean font crops; optional real alps3/doc0 atlas sweep."""
from pathlib import Path
import shutil, subprocess, sys, tempfile

ROOT=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="bean-fonts-") as temporary:
    out=Path(temporary); exe=out/'fonts.exe'
    subprocess.run([shutil.which('gcc') or 'gcc','-std=c11','-O2','-fms-extensions','-include','stdarg.h',
                    '-DPLATFORM_64BIT','-D_LANGUAGE_C','-DGEVR','-DVERSION_US',
                    *['-I'+str(ROOT/p) for p in ('.','include','src','src/game','port/include')],
                    str(ROOT/'port/tests/gevr_bean_font_test.c'),
                    str(ROOT/'port/src/gevr_bean_tex.c'),str(ROOT/'port/src/gevr_bean_logic.c'),
                    '-lm','-o',str(exe)],check=True)
    args=[str(exe)]
    if len(sys.argv)>1:
        assets=Path(sys.argv[1])
        for name in ('alps3','doc0'):
            target=out/'added-content/xbla/files/misc'/name/'default.dt'
            target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(assets/'misc'/name/'default.dt',target)
        args+=['real-atlases']
    subprocess.run(args,cwd=out,check=True)
