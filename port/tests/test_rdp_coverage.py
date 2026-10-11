"""Execute the renderer's alpha decision on opaque, cutout and glass modes.

Goldfinger Cartel uses 0x0c182078 with a zero-alpha combiner. Coverage-selected
opaque modes must draw it as solid, while alpha masks and glass retain alpha.
"""
from pathlib import Path
import os
import shutil
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[2]
rom_path = os.environ.get('GEVR_GF64_ROM')
if rom_path:
    # Read Cartel's display lists independently from the player's own ROM.
    import test_romcampaign as campaign
    rom = Path(rom_path).read_bytes()
    expected = campaign.expect(rom, campaign.GF)
    _, at, size = next(v for v in expected['files'].values() if v[0] == 'bg/bg_dam_all_p.seg')
    bg = rom[at:at+size]
    rows = struct.unpack_from('>I', bg, 4)[0] & 0xffffff
    records = 1
    while struct.unpack_from('>I', bg, rows+records*24+4)[0]:
        records += 1
    # The final nonzero row holds end offsets, followed by an all-zero row.
    count, matched = records-2, 0
    for room in range(1, count+1):
        primary = struct.unpack_from('>I', bg, rows+room*24+4)[0] & 0xffffff
        dl = zlib.decompressobj(-15).decompress(bg[primary+2:])
        commands = [struct.unpack_from('>II', dl, i) for i in range(0, len(dl), 8)]
        if (0xb900031d, 0x0c182078) in commands and (0xfc26a004, 0x1ffc93ff) in commands:
            matched += 1
    assert count == 128 and matched > 0, (count, matched)
    print(f'Cartel: {count} primary lists decoded; {matched} use coverage with zero-alpha combining')
source = (ROOT / 'port/fast3d/gfx_pc.cpp').read_text()
start = source.index('static bool gfx_rdp_uses_combiner_alpha(')
end = source.index('\n}\n', start) + 3
fixture = r'''
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#include <PR/gbi.h>
/* PRODUCTION */
int main(void) {
    /* Actual Cartel opaque room mode, with and without fog. */
    assert(!gfx_rdp_uses_combiner_alpha(0x0c182078u));
    assert(!gfx_rdp_uses_combiner_alpha(G_RM_FOG_SHADE_A | 0x00182078u));
    assert(!gfx_rdp_uses_combiner_alpha(G_RM_PASS | G_RM_AA_ZB_OPA_SURF2));
    assert(!gfx_rdp_uses_combiner_alpha(G_RM_PASS | G_RM_AA_ZB_OPA_DECAL2));
    assert(!gfx_rdp_uses_combiner_alpha(G_RM_PASS | G_RM_AA_OPA_SURF2));
    assert(gfx_rdp_uses_combiner_alpha(G_RM_PASS | G_RM_AA_ZB_TEX_EDGE2));
    assert(gfx_rdp_uses_combiner_alpha(G_RM_FOG_SHADE_A | G_RM_AA_ZB_TEX_EDGE2));
    assert(gfx_rdp_uses_combiner_alpha(G_RM_PASS | G_RM_AA_ZB_XLU_SURF2));
    assert(gfx_rdp_uses_combiner_alpha(G_RM_FOG_SHADE_A | G_RM_AA_ZB_XLU_SURF2 | Z_UPD));
    assert(gfx_rdp_uses_combiner_alpha(G_RM_PASS | G_RM_AA_ZB_XLU_DECAL2));
    assert(gfx_rdp_uses_combiner_alpha(G_RM_PASS | G_RM_AA_ZB_XLU_LINE2));
    puts("PASS: Cartel opaque coverage, fog, cutouts, glass and depth-writing transparency");
}
'''.replace('/* PRODUCTION */', source[start:end])
assert 'gfx_rdp_uses_combiner_alpha(rdp.other_mode_l)' in source
with tempfile.TemporaryDirectory(prefix='rdp-coverage-') as directory:
    out = Path(directory)
    (out / 'coverage.c').write_text(fixture)
    subprocess.run([shutil.which('gcc') or 'gcc', '-D_LANGUAGE_C', '-I'+str(ROOT/'include'),
                    str(out/'coverage.c'), '-o', str(out/'coverage.exe')], check=True)
    subprocess.run([str(out/'coverage.exe')], check=True)
