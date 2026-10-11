/* DAB preserves the ending pilot: Bean's aircraft omits these vertices.
 * Index ranges were matched against original/prop/helicopter in the local
 * USA ROM (2-unit tolerance); no cartridge vertices are embedded here. */
static int helicopterPilotVertex(unsigned index)
{
	return index <= 23 || (index >= 34 && index <= 460) || (index >= 551 && index <= 1095) ||
	       (index >= 1487 && index <= 1489) || (index >= 1616 && index <= 1619);
}
static Gfx *helicopterPilotList(ModelNode *node)
{
	Vertex *vertices;
	int count, type;
	Gfx *original;
	dlVerts(node, &vertices, &count, &type, &original);
	if (count != 1624 || !original)
		return NULL;
	int commands = 0;
	while (commands < 8192 && (original[commands].words.w0 >> 24) != (G_ENDDL & 255)) {
		if ((original[commands].words.w0 >> 24) == (G_DL & 255))
			return NULL;
		commands++;
	}
	if (commands == 8192)
		return NULL;
	Gfx *result = malloc((commands + 7) * sizeof(Gfx));
	if (!result)
		return NULL;
	Gfx *out = result;
	gDPPipeSync(out++);
	gDPSetCycleType(out++, G_CYC_1CYCLE);
	gDPSetRenderMode(out++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
	gDPSetCombineMode(out++, G_CC_MODULATEIA, G_CC_MODULATEIA);
	int keep[16] = {0}, triangles = 0;
	for (int i = 0; i < commands; i++) {
		Gfx command = original[i];
		unsigned w0 = command.words.w0, op = w0 >> 24;
		uintptr_t w1 = command.words.w1;
		if (op == (G_VTX & 255)) {
			unsigned first = (w0 >> 16) & 15, n = (w0 & 0xffff) / sizeof(Vertex);
			unsigned offset = w1 & 0xffffff, index = offset / sizeof(Vertex);
			if (first + n > 16) {
				free(result);
				return NULL;
			}
			for (unsigned v = 0; v < n; v++)
				keep[first + v] = (w1 >> 24) == SPSEGMENT_MODEL_VTX && index + v < (unsigned)count &&
				                  helicopterPilotVertex(index + v);
		} else if (op == (G_TRI1 & 255)) {
			unsigned a = (w1 >> 16 & 255) / 10, b = (w1 >> 8 & 255) / 10, c = (w1 & 255) / 10;
			if (a >= 16 || b >= 16 || c >= 16 || !keep[a] || !keep[b] || !keep[c])
				continue;
			triangles++;
		} else if (op == 0xb1) {
			unsigned filtered0 = w0 & 0xffff0000, filtered1 = 0;
			for (int t = 0; t < 4; t++) {
				unsigned a = w1 >> (t * 8) & 15, b = w1 >> (t * 8 + 4) & 15, c = w0 >> (t * 4) & 15;
				if (!(a || b || c) || !keep[a] || !keep[b] || !keep[c])
					continue;
				filtered0 |= c << (t * 4);
				filtered1 |= (a | b << 4) << (t * 8);
				triangles++;
			}
			if (!filtered1 && !(filtered0 & 0xffff))
				continue;
			command.words.w0 = filtered0;
			command.words.w1 = filtered1;
		}
		*out++ = command;
	}
	gSPEndDisplayList(out);
	if (!triangles || !keepAlloc(result)) {
		free(result);
		return NULL;
	}
	return result;
}
static Gfx *helicopterWithPilot(ModelNode *node, Gfx *aircraft)
{
	Gfx *pilot = helicopterPilotList(node);
	if (!pilot)
		return aircraft;
	Gfx *combined = malloc(3 * sizeof(Gfx));
	if (!combined)
		return aircraft;
	gSPDisplayList(combined, aircraft);
	gSPDisplayList(combined + 1, pilot);
	gSPEndDisplayList(combined + 2);
	if (!keepAlloc(combined)) {
		free(combined);
		return aircraft;
	}
	return combined;
}
