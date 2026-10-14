"""Bean model lists under the game's node state (G_GEVR_NODE_STATE, DAB xblaMeshApplyNodeMode)."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'port/fast3d/gfx_pc.cpp').read_text()


def function(signature):
    start = source.index(signature)
    end, depth = source.index('{', start) + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


fixture = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#define _LANGUAGE_C
#include <PR/gbi.h>
#include "gevr_bean_room_state.h"
struct RGBA { uint8_t r, g, b, a; };
static struct { uint32_t other_mode_l, other_mode_h, palette_fmt; bool textures_changed[2], tex_lod, tex_detail;
                uint64_t combine_mode; struct RGBA prim_color; } rdp;
static struct { uint32_t geometry_mode; } rsp;
static bool s_beanNodeOn, s_beanNodeSaved, s_beanNodePrimTouched, s_beanModelCull;
static uint64_t s_beanNodeCombine;
static uint32_t s_beanNodeMode, s_beanNodeFog, s_beanNodeLod;
static uint8_t s_beanNodePrimA;
/* PRODUCTION */
static void op(uint32_t w0, uint32_t w1 = 0) { gfx_bean_node_state(w0, w1); }
static const uint32_t FOG_PRIM = GBL_c1(G_BL_CLR_FOG, G_BL_A_FOG, G_BL_CLR_IN, G_BL_1MA);
static uint32_t own(uint32_t m1, uint32_t m2) { return (uint32_t)(G_RM_PASS | m2 | (m1 & 0xffff)); }
static void node(uint32_t cycle, uint32_t mode, uint64_t combine) {
    rdp.other_mode_h = cycle; rdp.other_mode_l = mode; rdp.combine_mode = combine;
    rdp.prim_color = {10, 20, 30, 40}; rsp.geometry_mode = G_FOG;
    rdp.other_mode_h |= G_TL_LOD;   /* props' mipmapped G_CC_TRILERP */
}
int main() {
    const uint32_t chr = FOG_PRIM | G_RM_AA_ZB_OPA_SURF2;
    /* Without ON (a list drawn outside a node), SAVE leaves the list its own state. */
    node(G_CYC_2CYCLE, chr, 0x1234);
    op(GEVR_NODE_SAVE);
    assert(!s_beanNodeSaved);
    rdp.combine_mode = 0x99;
    op(GEVR_NODE_APPLY | GEVR_NODE_COMBINE, own(G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2));
    assert(rdp.combine_mode == 0x99);

    /* A chr node: opaque batches take its combiner and its whole mode (the shade). */
    node(G_CYC_2CYCLE, chr, 0x1234);
    op(GEVR_NODE_ON);
    op(GEVR_NODE_SAVE);
    assert(s_beanNodeSaved);
    rdp.combine_mode = 0x99; rdp.other_mode_l = own(G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2); rsp.geometry_mode = 0;
    op(GEVR_NODE_APPLY | GEVR_NODE_COMBINE, rdp.other_mode_l);
    assert(rdp.combine_mode == 0x1234 && rdp.other_mode_l == chr);
    /* no LOD blend inside the list (tile 1 is tile 0's picture): the GPU mesh path takes it */
    assert(!(rdp.other_mode_h & G_TL_LOD) && !rdp.tex_lod && (rdp.other_mode_h & G_CYC_2CYCLE));
    /* A cut-out: the node's shade cycle, the list's edge cycle, prim alpha 0 for it alone. */
    op(GEVR_NODE_APPLY | GEVR_NODE_COMBINE | GEVR_NODE_SPAN | GEVR_NODE_CUT, own(G_RM_AA_ZB_TEX_EDGE, G_RM_AA_ZB_TEX_EDGE2));
    assert(rdp.other_mode_l == (FOG_PRIM | G_RM_AA_ZB_TEX_EDGE2) && rdp.prim_color.a == 0);
    /* A translucent batch keeps its own combiner, under the node's shade cycle. */
    rdp.combine_mode = 0x77;
    op(GEVR_NODE_APPLY | GEVR_NODE_SPAN | GEVR_NODE_XLU, own(G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2));
    assert(rdp.combine_mode == 0x77 && rdp.other_mode_l == (FOG_PRIM | G_RM_AA_ZB_XLU_SURF2) && rdp.prim_color.a == 40);
    /* END: the node's state back for the game's next list. */
    rdp.prim_color = {96, 96, 96, 255};
    op(GEVR_NODE_END);
    assert(!s_beanNodeSaved && rdp.combine_mode == 0x1234 && rdp.other_mode_l == chr);
    assert(rsp.geometry_mode == G_FOG && rdp.prim_color.r == 10 && rdp.prim_color.a == 40);
    assert((rdp.other_mode_h & G_TL_LOD) && rdp.tex_lod);

    /* A fading node (PropType 8: XLU) keeps its blend for solid batches, cut-outs included. */
    const uint32_t fading = FOG_PRIM | G_RM_AA_ZB_XLU_SURF2;
    node(G_CYC_2CYCLE, fading, 0x55);
    op(GEVR_NODE_SAVE);
    op(GEVR_NODE_APPLY | GEVR_NODE_COMBINE | GEVR_NODE_SPAN | GEVR_NODE_CUT, own(G_RM_AA_ZB_TEX_EDGE, G_RM_AA_ZB_TEX_EDGE2));
    assert(rdp.other_mode_l == fading);
    op(GEVR_NODE_END);

    /* A one-cycle node (type 1: no shade, no fade) leaves the list its own state. */
    node(G_CYC_1CYCLE, G_RM_AA_ZB_OPA_SURF, 0x66);
    op(GEVR_NODE_SAVE);
    assert(!s_beanNodeSaved);
    op(GEVR_NODE_OFF);
    assert(!s_beanNodeOn);
    puts("PASS: Bean node state: chr shade, cut-out and translucent spans, fading nodes, one-cycle and unmarked lists");
}
'''
production = function('static void gfx_sp_set_other_mode(') + '\n' + function('static void gfx_bean_node_state(')
with tempfile.TemporaryDirectory() as temp:
    path = Path(temp)
    (path / 'test.cpp').write_text(fixture.replace('/* PRODUCTION */', production))
    exe = path / 'test.exe'
    compiler = shutil.which('g++') or shutil.which('clang++') or 'c++'
    subprocess.run([compiler, '-std=c++17', '-O1', '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'port/include'),
                    str(path / 'test.cpp'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
