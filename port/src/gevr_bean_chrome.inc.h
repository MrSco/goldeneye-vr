/* The intro's chrome picture lives in the player's cartridge. Keep its
 * texture/tile/combiner setup and generate UVs from Bean's fitted normals,
 * rather than drawing the release's placeholder as a flat grey surface. */
static int emitChrome(const struct gevrEmitVert *v, int nverts, ModelNode *host,
                      void **gdlOut, void **vtxOut)
{
    if (nodeOp(host) != MODELNODE_OPCODE_DL || !host->Data || nverts <= 0)
        return 0;
    ModelRoData_DisplayListRecord *r = &host->Data->DisplayList;
    uintptr_t addr = (uintptr_t)r->Primary;
    if (!addr || !r->BaseAddr) return 0;
    const Gfx *native = addr >> 24 == 5 ? (const Gfx *)((const u8 *)r->BaseAddr + (addr & 0xffffff))
                                      : (const Gfx *)addr;
    int prefix = 0, texture = 0, generated = 0;
    for (; prefix < 128; prefix++) {
        unsigned op = (native[prefix].words.w0 >> 24) & 255;
        if (op == (G_VTX & 255)) break;
        if (op == (G_ENDDL & 255) || op == (G_DL & 255)) return 0;
        texture |= op == (G_SETTIMG & 255);
        generated |= op == (G_SETGEOMETRYMODE & 255) && (native[prefix].words.w1 & G_TEXTURE_GEN);
    }
    if (prefix == 128 || !texture || !generated) return 0;
    int triangles = nverts / 3;
    Gfx *list = malloc((size_t)(prefix + triangles + (triangles + 4) / 5 + 8) * sizeof(Gfx));
    Vtx *verts = malloc((size_t)nverts * sizeof(Vtx));
    if (!list || !verts) { free(list); free(verts); return 0; }
    Gfx *g = list;
    for (int i = 0; i < prefix; i++)
        if (((native[i].words.w0 >> 24) & 255) != (G_MTX & 255)) *g++ = native[i];
    gSPClearGeometryMode(g++, G_FOG | G_CULL_BOTH | G_TEXTURE_GEN_LINEAR);
    gSPSetGeometryMode(g++, G_LIGHTING | G_TEXTURE_GEN | G_SHADE | G_SHADING_SMOOTH | G_ZBUFFER);
    gDPSetAlphaCompare(g++, G_AC_NONE);
    gDPSetRenderMode(g++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
    for (int i = 0; i < nverts; i++) {
        const struct gevrEmitVert *src = &v[i];
        Vtx *dst = &verts[i];
        memset(dst, 0, sizeof(*dst));
        float len = sqrtf(src->n[0]*src->n[0] + src->n[1]*src->n[1] + src->n[2]*src->n[2]);
        for (int k = 0; k < 3; k++) {
            dst->v.ob[k] = posClamp(src->p[k]);
            dst->n.n[k] = len > 1e-6f ? (s8)(src->n[k] * 127.0f / len) : 0;
        }
        dst->n.a = 255;
    }
    for (int t = 0; t < triangles;) {
        int take = triangles - t;
        if (take > 5) take = 5;
        gSPVertex(g++, osVirtualToPhysical(&verts[t * 3]), take * 3, 0);
        for (int j = 0; j < take; j++) gSP1Triangle(g++, j*3, j*3+1, j*3+2, 0);
        t += take;
    }
    gSPClearGeometryMode(g++, G_LIGHTING | G_TEXTURE_GEN);
    gSPEndDisplayList(g++);
    *gdlOut = list;
    *vtxOut = verts;
    return 1;
}
