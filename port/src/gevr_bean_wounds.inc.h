/*
 * Wounds (DAB xblamesh.c "Wounds", port/licenses/dabs-mod.txt): a bullet's
 * bruise on a Bean body. GoldenEye paints a hit into the nearest stock vertex's
 * shade alpha (chr.c chrCreateBloodStain), and the chr combiner lerps that part
 * towards the blood tint; Bean's body draws its own vertices, so shot guards
 * stayed clean (user, 2026-10-14). The hit is noted here in its body part's own
 * frame, with the bruise's alpha; each draw carries it into the body's anchor
 * space, and the skin shader lowers the shade alpha of the Bean vertices within
 * 60 units, falling off to nothing at the edge (gfx_opengl.cpp uWound).
 *
 * A wound lasts as long as GoldenEye's own bruise: while the part keeps the
 * vertex copy the stain was painted into (vtxstore takes it back from a corpse,
 * and a reused model starts clean).
 */
#define BEAN_WOUND_MODELS 64

/* model.c: declared here, or C takes them for int-returning and halves the pointer */
union ModelRwData *modelGetNodeRwData(Model *model, ModelNode *node);
s32 modelFindNodeMtxIndex(ModelNode *node, s32 arg1);

struct beanWound {
	const ModelNode *stained;    /* the node GoldenEye painted, and the copy it painted */
	const void *vertices;
	int matrix;                  /* the hit part's matrix, and the hit in its frame */
	float local[3];
	uint8_t alpha;               /* the bruise's shade alpha, 20 to 170 */
};

static struct {
	const Model *model;
	unsigned serial, used;
	struct beanWound ring[GEVR_SKIN_WOUNDS];
} s_wounds[BEAN_WOUND_MODELS];
static unsigned s_woundUse;

void gevrBeanNoteWound(Model *model, ModelNode *hitnode, ModelNode *stained, const void *vertices,
                       const coord3d *local, s32 alpha)
{
	int pick = -1;
	if (!model || !hitnode || !stained || !vertices || !local)
		return;
	const int matrix = modelFindNodeMtxIndex(hitnode, 0);
	if (matrix < 0)
		return;
	for (int i = 0; i < BEAN_WOUND_MODELS && pick < 0; i++)
		if (s_wounds[i].model == model)
			pick = i;
	if (pick < 0) {
		pick = 0;
		for (int i = 0; i < BEAN_WOUND_MODELS; i++) {
			if (!s_wounds[i].model) {
				pick = i;
				break;
			}
			if (s_wounds[i].used < s_wounds[pick].used)
				pick = i;
		}
		memset(&s_wounds[pick], 0, sizeof(s_wounds[pick]));
		s_wounds[pick].model = model;
	}
	struct beanWound *w = &s_wounds[pick].ring[s_wounds[pick].serial++ % GEVR_SKIN_WOUNDS];
	w->stained = stained;
	w->vertices = vertices;
	w->matrix = matrix;
	w->local[0] = local->x;
	w->local[1] = local->y;
	w->local[2] = local->z;
	w->alpha = (uint8_t)(alpha < 0 ? 0 : alpha > 255 ? 255 : alpha);
	s_wounds[pick].used = ++s_woundUse;
}

/* The model's live wounds in its anchor space (as beanSkinGpuDraw's palette:
 * inverse * (world - anchor)), xyz then 255 less the alpha; how many. */
static int beanWoundsInAnchor(Model *model, const float inverse[9], const float anchor[4],
                              float out[GEVR_SKIN_WOUNDS][4])
{
	int n = 0, live = 0, slot = -1;
	if (!model || !model->obj || !model->render_pos)
		return 0;
	for (int i = 0; i < BEAN_WOUND_MODELS && slot < 0; i++)
		if (s_wounds[i].model == model)
			slot = i;
	if (slot < 0)
		return 0;
	const unsigned serial = s_wounds[slot].serial;
	const unsigned from = serial > GEVR_SKIN_WOUNDS ? serial - GEVR_SKIN_WOUNDS : 0;
	for (unsigned s = from; s < serial; s++) {
		const struct beanWound *w = &s_wounds[slot].ring[s % GEVR_SKIN_WOUNDS];
		const union ModelRwData *rw = modelGetNodeRwData(model, (ModelNode *)w->stained);
		if (!rw || rw->DisplayListCollisions.Vertices != w->vertices)
			continue;   /* the copy is gone: so is the bruise */
		live++;
		if (w->matrix >= model->obj->numMatrices)
			continue;
		const float (*m)[4] = model->render_pos[w->matrix].pos.m;
		float world[3];
		for (int c = 0; c < 3; c++)
			world[c] = w->local[0] * m[0][c] + w->local[1] * m[1][c] + w->local[2] * m[2][c] + m[3][c] - anchor[c];
		for (int k = 0; k < 3; k++)
			out[n][k] = inverse[k * 3] * world[0] + inverse[k * 3 + 1] * world[1] + inverse[k * 3 + 2] * world[2];
		out[n][3] = 255.0f - w->alpha;
		n++;
	}
	if (!live)
		memset(&s_wounds[slot], 0, sizeof(s_wounds[slot]));
	return n;
}
