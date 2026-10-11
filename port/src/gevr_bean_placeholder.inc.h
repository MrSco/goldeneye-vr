/* Adapted from DAB beanTexIsPlaceholder/beanPlaceholderStandIn/
 * beanPlaceholderColour, pinned adef1dc2; port/licenses/dabs-mod.txt.
 * Preserve the stream's shader boundaries when borrowing neighbouring art. */
static int placeholderNeighbour(const struct gevrBeanMesh *mesh, const struct beanDraw *draws, int count,
								int tex, const uint8_t *placeholder, int borrow)
{
	int tested = 1, blended = 1, best = -1, distance = 0x7fffffff;
	for (int i = 0; i < count; i++)
		if (draws[i].tex == (uint32_t)tex) {
			tested &= draws[i].alphatest != 0;
			blended &= draws[i].blend != 0;
		}
	if (!tested && blended && borrow) {
		for (int i = 0; i < count; i++) {
			if (draws[i].tex != (uint32_t)tex)
				continue;
			for (int j = 0; j < count; j++) {
				int other = (int)draws[j].tex, d = abs(j - i);
				if (other == tex || other < 0 || other >= mesh->ntex || d >= distance ||
					!draws[j].alphatest || !placeholder[other])
					continue;
				int replacement = placeholderNeighbour(mesh, draws, count, other, placeholder, 0);
				if (replacement >= 0) {
					best = replacement;
					distance = d;
				}
			}
		}
		return best;
	}
	if (!tested)
		return -1;
	for (int i = 0; i < count; i++) {
		if (draws[i].tex != (uint32_t)tex)
			continue;
		for (int dir = -1; dir <= 1; dir += 2)
			for (int j = i + dir; j >= 0 && j < count && draws[j].vs == draws[i].vs; j += dir) {
				int other = (int)draws[j].tex, d = abs(j - i), w, h, clear = 0;
				const uint8_t *rgba;
				if (other == tex || other < 0 || other >= mesh->ntex || !draws[j].alphatest ||
					d >= distance || placeholder[other])
					continue;
				if (gevrBeanTexture(mesh->standin[other], &rgba, &w, &h))
					for (int k = 0; k < w * h && !clear; k++)
						clear = rgba[k * 4 + 3] < 128;
				if (clear) {
					best = other;
					distance = d;
					break;
				}
			}
	}
	return best;
}

static uint32_t placeholderColour(const struct gevrBeanMesh *mesh, const struct beanDraw *draws, int count,
								  int tex, const uint8_t *placeholder)
{
	uint64_t sum[3] = {0}, pixels = 0;
	uint8_t used[MAX_TEX] = {0};
	for (int i = 0; i < count; i++) {
		if (draws[i].tex != (uint32_t)tex)
			continue;
		for (int dir = -1; dir <= 1; dir += 2)
			for (int j = i + dir; j >= 0 && j < count && draws[j].vs == draws[i].vs; j += dir) {
				int other = (int)draws[j].tex, w, h;
				const uint8_t *rgba;
				if (other == tex || other < 0 || other >= mesh->ntex || used[other])
					continue;
				used[other] = 1;
				if (placeholder[other] || !gevrBeanTexture(mesh->standin[other], &rgba, &w, &h))
					continue;
				for (int k = 0; k < w * h; k++)
					if (rgba[k * 4 + 3] >= 128) {
						for (int c = 0; c < 3; c++)
							sum[c] += rgba[k * 4 + c];
						pixels++;
					}
				break;
			}
	}
	return pixels ? (uint32_t)(sum[0] / pixels) << 16 | (uint32_t)(sum[1] / pixels) << 8 |
						(uint32_t)(sum[2] / pixels)
				  : 0x808080;
}

static void fixPlaceholders(struct gevrBeanMesh *mesh, const struct beanDraw *draws, int count,
							const char *source)
{
	uint8_t placeholder[MAX_TEX] = {0};
	int neighbour[MAX_TEX];
	uint32_t colour[MAX_TEX];
	for (int t = 0; t < mesh->ntex; t++)
		placeholder[t] = gevrBeanTexIsPlaceholder(mesh->standin[t]);
	/* Resolve from the original pictures before painting or substituting any. */
	for (int t = 0; t < mesh->ntex; t++)
		if (placeholder[t]) {
			neighbour[t] = placeholderNeighbour(mesh, draws, count, t, placeholder, 1);
			colour[t] = placeholderColour(mesh, draws, count, t, placeholder);
		}
	for (int t = 0; t < mesh->ntex; t++)
		if (placeholder[t]) {
			int other = neighbour[t];
			if (other >= 0) {
				mesh->standin[t] = mesh->standin[other];
				mesh->texW[t] = mesh->texW[other];
				mesh->texH[t] = mesh->texH[other];
				mesh->texCut[t] = mesh->texCut[other];
				GEVR_BEAN_LOG("gevr bean: %s: placeholder %d uses neighbouring texture %d", source, t, other);
			} else {
				gevrBeanTexPaint(mesh->standin[t], colour[t]);
				GEVR_BEAN_LOG("gevr bean: %s: placeholder %d painted %06x", source, t, colour[t]);
			}
		}
}
