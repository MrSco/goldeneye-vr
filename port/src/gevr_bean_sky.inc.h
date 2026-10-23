/* DAB gebeansky.c skyCapFade/skyBuild (aae28c8, port/licenses/dabs-mod.txt).
 * CE cloud photographs lack the retail cap's alpha and otherwise draw a
 * hard circle across the dome. Keep authored alpha; fade only opaque caps. */
static void beanSkyMaterials(struct gevrBeanMesh *mesh)
{
	for (int tex = 0; tex < mesh->ntex; tex++) {
		mesh->texClamp[tex] = tex ? 3 : 2; /* dome U wraps round the horizon */
		mesh->texClampBase[tex][0] = mesh->texClampBase[tex][1] = 0;
	}
	for (int i = 0; i < mesh->ntris; i++) {
		struct gevrBeanTri *t = &mesh->tris[i];
		int cap = t->tex > 0 && t->tex < mesh->ntex;
		t->alpha = cap ? 254 : 255;
		t->alphatest = 0;
		for (int c = 0; c < 3; c++) {
			uint8_t a = 255;
			if (cap && !mesh->texCut[t->tex]) {
				float u = t->uv[c * 2] - 0.5f, v = t->uv[c * 2 + 1] - 0.5f;
				float r = sqrtf(u * u + v * v);
				float f = (0.475f - r) / (0.475f - 0.33f);
				if (f < 0) f = 0;
				if (f > 1) f = 1;
				a = (uint8_t)(255 * f * f * (3 - 2 * f));
			}
			t->rgba[c * 4] = t->rgba[c * 4 + 1] = t->rgba[c * 4 + 2] = 255;
			t->rgba[c * 4 + 3] = a;
		}
	}
}
