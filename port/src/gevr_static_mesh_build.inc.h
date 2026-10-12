GevrStaticMesh *gevrStaticMeshBuild(const void *gdl)
{
	const Gfx *g;
	unsigned ntri = 0, nruns = 0, inRun = 0, runTris = 0;
	if (!gdl)
		return NULL;
	for (g = gdl;; g++) {
		unsigned op = (unsigned)(g->words.w0 >> 24) & 0xff;
		if (op == (G_ENDDL & 0xff))
			break;
		if (op == (G_VTX & 0xff)) {
			if (!inRun || runTris >= 512) { nruns++; runTris = 0; }
			inRun = 1;
		} else if (op == (G_TRI1 & 0xff)) {
			if (!inRun)
				return NULL;   /* drawn from vertices an earlier run loaded */
			ntri++;
			runTris++;
		} else if (op == 0xb1) {
			return NULL;   /* G_TRI4: not a list gevrBeanEmitList makes */
		} else {
			inRun = 0;
		}
	}
	if (!ntri)
		return NULL;
	unsigned cap = 1;
	while (cap < ntri * 6)
		cap <<= 1;
	GevrStaticMesh *m = calloc(1, sizeof(*m));
	Vtx *verts = malloc((size_t)ntri * 3 * sizeof(Vtx));
	uint32_t *indices = malloc((size_t)ntri * 3 * sizeof(uint32_t));
	GevrStaticMeshRun *runs = calloc(nruns, sizeof(*runs));
	uint32_t *table = calloc(cap, sizeof(uint32_t));   /* vertex + 1, 0 empty */
	if (!m || !verts || !indices || !runs || !table) {
		free(m);
		free(verts);
		free(indices);
		free(runs);
		free(table);
		return NULL;
	}
	const Vtx *slot[16] = {0};
	uint32_t nverts = 0, nindices = 0, run = 0;
	unsigned loaded = 0;
	inRun = 0;
	for (g = gdl;; g++) {
		unsigned op = (unsigned)(g->words.w0 >> 24) & 0xff;
		if (op == (G_VTX & 0xff)) {
			unsigned n = (unsigned)(g->words.w0 & 0xffff) / sizeof(Vtx), dest = (unsigned)(g->words.w0 >> 16) & 15;
            if (inRun && nindices - runs[run].firstIndex >= 512 * 3) {
                runs[run].end = g;
                runs[run].indexCount = nindices - runs[run].firstIndex;
                run++;
                inRun = 0;
            }
			if (!inRun) {
				runs[run].start = g;
				runs[run].firstIndex = nindices;
				loaded = 0;   /* a run may use only what it loads */
				inRun = 1;
			}
			for (unsigned k = 0; k < n && dest + k < 16; k++) {
				slot[dest + k] = (const Vtx *)g->words.w1 + k;
				loaded |= 1u << (dest + k);
			}
			continue;
		}
		if (op == (G_TRI1 & 0xff)) {
			const unsigned corner[3] = {(unsigned)(g->words.w1 >> 16 & 255) / 10, (unsigned)(g->words.w1 >> 8 & 255) / 10,
			                           (unsigned)(g->words.w1 & 255) / 10};
			for (int k = 0; k < 3; k++) {
				if (corner[k] >= 16 || !(loaded & (1u << corner[k]))) {
					m->vertices = verts;
					m->indices = indices;
					m->runs = runs;
					free(table);
					gevrStaticMeshFree(m);
					return NULL;
				}
				const Vtx *v = slot[corner[k]];
				uint32_t h = 2166136261u;
				for (size_t b = 0; b < sizeof(Vtx); b++)
					h = (h ^ ((const uint8_t *)v)[b]) * 16777619u;
				for (h &= cap - 1; table[h] && memcmp(&verts[table[h] - 1], v, sizeof(Vtx)) != 0; h = (h + 1) & (cap - 1))
					;
				if (!table[h]) {
					verts[nverts] = *v;
					table[h] = ++nverts;
				}
				indices[nindices++] = table[h] - 1;
			}
			continue;
		}
		if (inRun) {
			runs[run].end = g;
			runs[run].indexCount = nindices - runs[run].firstIndex;
			run++;
			inRun = 0;
		}
		if (op == (G_ENDDL & 0xff))
			break;
	}
    for (uint32_t r = 0; r < run; r++) {
        const GevrStaticMeshRun *chunk = &runs[r];
        for (uint32_t j = 0; j < chunk->indexCount; j++) {
            const Vtx *v = &verts[indices[chunk->firstIndex + j]];
            for (int k = 0; k < 3; k++) {
                if (!j || v->v.ob[k] < runs[r].lo[k]) runs[r].lo[k] = v->v.ob[k];
                if (!j || v->v.ob[k] > runs[r].hi[k]) runs[r].hi[k] = v->v.ob[k];
            }
        }
        runs[r].hasBounds = chunk->indexCount != 0;
    }
	free(table);
	m->vertices = verts;
	m->numVertices = nverts;
	m->indices = indices;
	m->numIndices = nindices;
	m->runs = runs;
	m->numRuns = run;
	return m;
}

void gevrStaticMeshFree(GevrStaticMesh *m)
{
	if (!m)
		return;
	gfx_static_mesh_release(m);
	free((void *)m->vertices);
	free((void *)m->indices);
	free((void *)m->runs);
	free(m);
}

