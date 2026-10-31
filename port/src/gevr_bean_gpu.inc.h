/* GPU skinning follows DAB's xblaMeshGpuPalette design. The fitted native-rig
 * influences are already in bone-local space; keep those unchanged in a VBO. */

/*
 * Each distinct vertex once in the GPU buffers (m->gpuVertices, gpuIndices).
 * A Bean skin's triangles each carry their own three corners - the CPU pose's
 * and the list's layout - so the skinning vertex shader ran three times a
 * triangle, in every bin: a pile of 40 bodies held the GPU at 98% and the
 * headset at 53 fps of 120 (Bunker II, 2026-10-30). Equal vertices are equal
 * byte for byte (calloc'd, every field set from one source vertex). The
 * caller owns the two arrays; 0 when there is nothing to share or no memory.
 */
static int beanSkinShare(GevrStaticMesh *m)
{
    const GevrSkinVertex *v = m->vertices;
    const uint32_t n = m->numVertices;
    uint32_t size = 16, unique = 0;
    while (size < n * 2)
        size <<= 1;
    uint32_t *table = calloc(size, sizeof(*table));   /* unique index + 1 */
    uint32_t *remap = malloc((size_t)n * sizeof(*remap));
    GevrSkinVertex *out = malloc((size_t)n * sizeof(*out));
    uint32_t *indices = malloc((size_t)m->numIndices * sizeof(*indices));
    if (!table || !remap || !out || !indices || !n)
        goto fail;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *bytes = (const uint8_t *)&v[i];
        uint32_t h = 2166136261u;
        for (size_t k = 0; k < sizeof(*v); k++)
            h = (h ^ bytes[k]) * 16777619u;
        for (uint32_t s = h & (size - 1);; s = (s + 1) & (size - 1)) {
            if (!table[s]) {
                out[unique] = v[i];
                table[s] = ++unique;
                remap[i] = unique - 1;
                break;
            }
            if (!memcmp(&out[table[s] - 1], &v[i], sizeof(*v))) {
                remap[i] = table[s] - 1;
                break;
            }
        }
    }
    if (unique == n)
        goto fail;
    for (uint32_t i = 0; i < m->numIndices; i++)
        indices[i] = remap[m->indices[i]];
    free(table);
    free(remap);
    m->gpuVertices = out;
    m->numGpuVertices = unique;
    m->gpuIndices = indices;
    return 1;
fail:
    free(table);
    free(remap);
    free(out);
    free(indices);
    return 0;
}

static int beanSkinMesh(struct beanSkin *skin)
{
    if (skin->meshTried)
        return skin->mesh != NULL;
    skin->meshTried = 1;
    if (s_nalloc > MAX_ALLOC - 6 || s_nskinMeshes >= MAX_ALLOC)
        return 0;
    GevrStaticMesh *m = calloc(1, sizeof(*m));
    GevrSkinVertex *v = calloc(skin->nverts, sizeof(*v));
    uint32_t *indices = malloc((size_t)skin->nverts * sizeof(*indices));
    GevrStaticMeshRun *runs = calloc(skin->ncommands, sizeof(*runs));
    if (!m || !v || !indices || !runs)
        goto fail;
    for (int i = 0; i < skin->nverts; i++) {
        memcpy(v[i].vtx, &skin->vtx[i], sizeof(Vtx));
        v[i].tint = skin->verts[i].tint;
        memcpy(v[i].p, skin->verts[i].p, sizeof(v[i].p));
        memcpy(v[i].n, skin->verts[i].n, sizeof(v[i].n));
        for (int w = 0; w < 4; w++) {
            if (!skin->verts[i].weight[w])
                continue;
            int bone = skin->verts[i].matrix[w], b;
            for (b = 0; b < skin->nbones && skin->bones[b] != bone; b++) {}
            if (b == skin->nbones) {
                if (b == GEVR_SKIN_BONES)
                    goto fail;
                skin->bones[skin->nbones++] = bone;
            }
            v[i].bone[w] = b;
            v[i].weight[w] = skin->verts[i].weight[w];
        }
    }
    unsigned slots[16] = {0}, loaded = 0, inRun = 0, nindices = 0, nruns = 0;
    for (int c = 0; c < skin->ncommands; c++) {
        const Gfx *g = &skin->gdl[c];
        unsigned op = (unsigned)(g->words.w0 >> 24) & 255;
        if (op == (G_VTX & 255)) {
            unsigned n = (g->words.w0 & 0xffff) / sizeof(Vtx), dest = (g->words.w0 >> 16) & 15;
            uintptr_t offset = g->words.w1 - (uintptr_t)skin->vtx;
            if (offset % sizeof(Vtx) || offset / sizeof(Vtx) + n > (unsigned)skin->nverts || dest + n > 16)
                goto fail;
            if (!inRun) {
                runs[nruns].start = g;
                runs[nruns].firstIndex = nindices;
                loaded = 0;
                inRun = 1;
            }
            for (unsigned k = 0; k < n; k++) {
                slots[dest + k] = offset / sizeof(Vtx) + k;
                loaded |= 1u << (dest + k);
            }
            continue;
        }
        if (op == (G_TRI1 & 255)) {
            if (!inRun || nindices + 3 > (unsigned)skin->nverts)
                goto fail;
            for (int k = 0; k < 3; k++) {
                unsigned slot = (g->words.w1 >> (16 - k * 8) & 255) / 10;
                if (slot >= 16 || !(loaded & (1u << slot)))
                    goto fail;
                indices[nindices++] = slots[slot];
            }
            continue;
        }
        /* Tint is already stored in the VBO. Keep successive vertex loads in
         * one draw; the final clear still ends the run and executes normally. */
        if (op == G_GEVR_VERTEX_TINT && g->words.w1)
            continue;
        if (inRun) {
            runs[nruns].end = g;
            runs[nruns].indexCount = nindices - runs[nruns].firstIndex;
            nruns++;
            inRun = 0;
        }
    }
    if (!nruns || !nindices || inRun)
        goto fail;
    m->vertices = v;
    m->numVertices = skin->nverts;
    m->indices = indices;
    m->numIndices = nindices;
    m->runs = runs;
    m->numRuns = nruns;
    if (beanSkinShare(m)) {
        keepAlloc((void *)m->gpuVertices);
        keepAlloc((void *)m->gpuIndices);
        GEVR_BEAN_LOG("bean skin: %u vertices, %u distinct on the GPU", (unsigned)m->numVertices,
                      (unsigned)m->numGpuVertices);
    }
    keepAlloc(m);
    keepAlloc(v);
    keepAlloc(indices);
    keepAlloc(runs);
    s_skinMeshes[s_nskinMeshes++] = m;
    skin->mesh = m;
    return 1;
fail:
    free(m);
    free(v);
    free(indices);
    free(runs);
    return 0;
}

/* Same quantization as the old CPU pose; called once only if a run cannot use
 * the GPU (first-person cull override or unsupported backend). */
static void *beanSkinPose(GevrSkinDraw *draw)
{
    if (draw->posedVertices)
        return draw->posedVertices;
    Vtx *out = beanFrameAllocate((size_t)draw->mesh->numVertices * sizeof(Vtx));
    if (!out)
        return NULL;
    memcpy(out, draw->bindVertices, (size_t)draw->mesh->numVertices * sizeof(Vtx));
    const GevrSkinVertex *verts = draw->mesh->vertices;
    for (unsigned i = 0; i < draw->mesh->numVertices; i++) {
        float p[3] = {0}, n[3] = {0}, total = 0;
        for (int w = 0; w < 4; w++) {
            unsigned b = verts[i].bone[w];
            if (!verts[i].weight[w] || !(draw->palette.valid[b / 32] & (1u << (b % 32))))
                continue;
            float weight = verts[i].weight[w];
            for (int k = 0; k < 3; k++) {
                const float *r = &draw->palette.rows[b][k * 4];
                p[k] += weight * (r[0] * verts[i].p[w][0] + r[1] * verts[i].p[w][1] + r[2] * verts[i].p[w][2] + r[3]);
                n[k] += weight * (r[0] * verts[i].n[w][0] + r[1] * verts[i].n[w][1] + r[2] * verts[i].n[w][2]);
            }
            total += weight;
        }
        if (total <= 0)
            continue;
        float length = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        for (int k = 0; k < 3; k++) {
            out[i].v.ob[k] = posClamp(p[k] / total);
            out[i].n.n[k] = length > 1e-6f ? (s8)(n[k] * 127.0f / length) : 0;
        }
    }
    draw->posedVertices = out;
    return out;
}

static Gfx *beanSkinGpuDraw(struct beanSkin *skin, Model *model, int anchorIndex,
                           int backIndex, ModelNode *neck, const float inverse[9])
{
    if (!beanSkinMesh(skin))
        return NULL;
    GevrSkinDraw *draw = beanFrameAllocate(sizeof(*draw));
    Gfx *commands = beanFrameAllocate(3 * sizeof(Gfx));
    if (!draw || !commands)
        return NULL;
    memset(draw, 0, sizeof(*draw));
    draw->mesh = skin->mesh;
    draw->authoredColor = 1;
    draw->bindVertices = skin->vtx;
    draw->pose = beanSkinPose;
    const float (*anchor)[4] = model->render_pos[anchorIndex].pos.m;
    for (int b = 0; b < skin->nbones; b++) {
        int matrix = skin->bones[b], back = matrix == -3;
        if (matrix == -2)
            matrix = anchorIndex;
        if (back)
            matrix = backIndex >= 0 ? backIndex : anchorIndex;
        if (matrix < 0 || matrix >= model->obj->numMatrices)
            continue;
        const float (*m)[4] = model->render_pos[matrix].pos.m;
        float origin[3];
        for (int k = 0; k < 3; k++) {
            origin[k] = m[3][k] - anchor[3][k];
            if (back && neck)
                for (int j = 0; j < 3; j++)
                    origin[k] += neck->Data->Group.Origin.f[j] * m[j][k];
        }
        for (int k = 0; k < 3; k++) {
            float *r = &draw->palette.rows[b][k * 4];
            for (int j = 0; j < 3; j++)
                r[j] = inverse[k * 3] * m[j][0] + inverse[k * 3 + 1] * m[j][1] + inverse[k * 3 + 2] * m[j][2];
            r[3] = inverse[k * 3] * origin[0] + inverse[k * 3 + 1] * origin[1] + inverse[k * 3 + 2] * origin[2];
        }
        draw->palette.valid[b / 32] |= 1u << (b % 32);
    }
    draw->numWounds = beanWoundsInAnchor(model, skin, inverse, anchor[3], draw->wounds);
    gSPMatrix(commands, (void *)(uintptr_t)(0x03000000u + anchorIndex * sizeof(Mtx)),
              G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    commands[1].words.w0 = (uintptr_t)G_GEVR_SKIN << 24;
    commands[1].words.w1 = (uintptr_t)draw;
    gSPBranchList(commands + 2, skin->gdl);
    return commands;
}
