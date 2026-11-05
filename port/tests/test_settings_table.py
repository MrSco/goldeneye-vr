"""The settings table (port/src/gevr_settings.c) with its game and headset calls stubbed."""
from pathlib import Path
import shutil, subprocess, tempfile
import settings_stub_dir
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='gevr-settings-') as temp:
    temp=Path(temp)
    table=settings_stub_dir.make(temp, root)
    exe=temp/'settings_table_native.exe'
    cc=shutil.which('gcc') or 'gcc'
    subprocess.run([cc,'-std=gnu11','-O1','-Wall','-Werror=implicit-function-declaration','-I'+str(temp),'-Iport/include',
                    '-Iport/tests',str(table),'port/tests/settings_table_native.c','-lm','-o',str(exe)],cwd=root,check=True)
    subprocess.run([str(exe)],cwd=root,check=True)
