"""A folder for building the settings table (port/src/gevr_settings.c) natively:
the net headers it includes, as the few calls port/tests/settings_table_stubs.h
answers, and the table itself, copied in so its "net/..." includes find them."""
from pathlib import Path
import shutil


def make(temp, root):
    temp, root = Path(temp), Path(root)
    net = temp / 'net'
    net.mkdir(exist_ok=True)
    (net / 'net_core.h').write_text('#pragma once\n#include <stdbool.h>\n'
                                    '#ifdef __cplusplus\nextern "C" {\n#endif\n'
                                    'bool netIsActive(void);\nbool netIsHost(void);\n'
                                    '#ifdef __cplusplus\n}\n#endif\n')
    game = (root / 'port/src/net/net_game.h').read_text(encoding='utf8')
    cfg = game[game.index('enum { CFG_'):]
    cfg = cfg[:cfg.index('};') + 2]
    (net / 'net_game.h').write_text('#pragma once\n' + cfg + '\n#ifdef __cplusplus\nextern "C" {\n#endif\n'
                                    'int gevrNetConfigCoop(void);\nint gevrNetConfigPve(void);\n'
                                    'int gevrCoopFastReinforcements(void);\nvoid gevrNetConfigSet(int field, int value);\n'
                                    'int netActiveMovementSpeed(void);\n#ifdef __cplusplus\n}\n#endif\n')
    shutil.copy(root / 'port/src/net/net_rules.h', net / 'net_rules.h')
    shutil.copy(root / 'port/src/gevr_settings.c', temp / 'gevr_settings.c')
    return temp / 'gevr_settings.c'
