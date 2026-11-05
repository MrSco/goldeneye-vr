"""The settings table (port/src/gevr_settings.c) with its game and headset calls stubbed."""
from pathlib import Path
import shutil, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='gevr-settings-') as temp:
    temp=Path(temp)
    net=temp/'net'; net.mkdir()
    # the net layer's calls the table makes; net_rules.h is the real one
    (net/'net_core.h').write_text('#include <stdbool.h>\nbool netIsActive(void);\nbool netIsHost(void);\n')
    game=(root/'port/src/net/net_game.h').read_text(encoding='utf8')
    cfg=game[game.index('enum { CFG_'):]
    cfg=cfg[:cfg.index('};')+2]
    (net/'net_game.h').write_text('#pragma once\n'+cfg+'\nint gevrCoopActive(void);\nint gevrCoopFastReinforcements(void);\n'
                                  'void gevrNetConfigSet(int field, int value);\nint netActiveMovementSpeed(void);\n')
    shutil.copy(root/'port/src/net/net_rules.h', net/'net_rules.h')
    # compiled from the temp folder, so its "net/..." includes find the stubs first
    shutil.copy(root/'port/src/gevr_settings.c', temp/'gevr_settings.c')
    exe=temp/'settings_table_native.exe'
    cc=shutil.which('gcc') or 'gcc'
    subprocess.run([cc,'-std=gnu11','-O1','-Wall','-Werror=implicit-function-declaration','-I'+str(temp),'-Iport/include',
                    str(temp/'gevr_settings.c'),'port/tests/settings_table_native.c','-lm','-o',str(exe)],cwd=root,check=True)
    subprocess.run([str(exe)],cwd=root,check=True)
