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
 * GoldenEye paints the stock vertex nearest the hit, however far: a shot's
 * point (chr.c, pushed out from the joint and back along the shot) can lie
 * well off Bean's surface, and only 1 hit in 20 came within 60 units of a
 * Bean vertex (user, 2026-10-14). The wound is moved onto the nearest Bean
 * vertex bound to the hit part, in that part's own frame, as GoldenEye's is.
 *
 * A Bean head is a mesh of its own on the body's neck matrix, so a head shot
 * snapped onto the body's collar and the face stayed clean (user, 2026-10-14).
 * As in Dab's (xblaMeshNoteBruise, wd->kinds), a hit on the head's box lands
 * on the head - moved onto the nearest head vertex, at a 40-unit radius, a head
 * being about a third the size of a torso - and on the body; any other hit on
 * the body alone.
 *
 * A wound lasts as long as GoldenEye's own bruise: while the part keeps the
 * vertex copy the stain was painted into (vtxstore takes it back from a corpse,
 * and a reused model starts clean).
 */
#define BEAN_WOUND_MODELS 64
#define BEAN_WOUND_RADIUS 60       /* DAB XBLAMESH_WOUND_RADIUS */
#define BEAN_WOUND_RADIUS_HEAD 40  /* DAB XBLAMESH_WOUND_RADIUS_HEAD */
#define BEAN_WOUND_BODY 1
#define BEAN_WOUND_HEAD 2

/* model.c: declared here, or C takes them for int-returning and halves the pointer */
union ModelRwData *modelGetNodeRwData(Model *model, ModelNode *node);
s32 modelFindNodeMtxIndex(ModelNode *node, s32 arg1);

struct beanWound {
	const ModelNode *stained;    /* the node GoldenEye painted, and the copy it painted */
	const void *vertices;
	int matrix;                  /* the hit part's matrix, and the hit in its frame */
	float local[3];
	uint8_t alpha;               /* the bruise's shade alpha, 20 to 170 */
	uint8_t kinds;               /* BEAN_WOUND_BODY, BEAN_WOUND_HEAD: the meshes it lands on */
};

static struct {
	const Model *model;
	unsigned serial, used;
	struct beanWound ring[GEVR_SKIN_WOUNDS];
} s_wounds[BEAN_WOUND_MODELS];
static unsigned s_woundUse;

/* The Bean head drawn on the model (its skin, the node it draws on), found
 * through the body's head placeholder, which GoldenEye points at the head */
static const struct beanSkin *beanWoundHead(ModelNode *node, int depth, ModelNode **drawn)
{
	for (; node && depth < 64; node = node->Next) {
		const struct beanSwap *s = activeSwap(node);
		const struct beanSkin *found;
		if (s && s->skin && s->skin->head) {
			*drawn = node;
			return s->skin;
		}
		if ((found = beanWoundHead(node->Child, depth + 1, drawn)) != NULL)
			return found;
	}
	return NULL;
}

void gevrBeanNoteWound(Model *model, s32 hitpart, ModelNode *hitnode, ModelNode *stained, const void *vertices,
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
	float at[3] = { local->x, local->y, local->z };
	uint8_t kinds = BEAN_WOUND_BODY;
	int snapped = 0;
	if (hitpart == 8 && model->obj) {   /* HIT_HEAD (bondaicommands.h) */
		/* the Bean head's nearest vertex on its anchor, when that is the hit's matrix */
		ModelNode *drawn = NULL;
		const struct beanSkin *head = beanWoundHead(model->obj->RootNode, 0, &drawn);
		const int anchor = head ? (head->anchor >= 0 ? head->anchor : modelFindNodeMtxIndex(drawn, 0)) : -1;
		kinds = BEAN_WOUND_BODY | BEAN_WOUND_HEAD;
		float best = 1e30f;
		for (int v = 0; head && anchor == matrix && head->verts && v < head->nverts; v++) {
			const struct beanSkinVert *sv = &head->verts[v];
			for (int k = 0; k < 4; k++) {
				if (!sv->weight[k] || sv->matrix[k] != -2)
					continue;
				const float dx = sv->p[k][0] - local->x, dy = sv->p[k][1] - local->y, dz = sv->p[k][2] - local->z;
				const float d = dx * dx + dy * dy + dz * dz;
				if (d < best) {
					best = d;
					at[0] = sv->p[k][0];
					at[1] = sv->p[k][1];
					at[2] = sv->p[k][2];
					snapped = 1;
				}
			}
		}
		if (!snapped) {
			static int told;
			if (told++ < 4)
				GEVR_BEAN_LOG("gevr bean wounds: head shot not on a Bean head (head %d, anchor %d, hit matrix %d)",
				              head != NULL, anchor, matrix);
		}
	}
	if (!snapped) {
		/* the Bean body's nearest vertex bound to the hit part (struct beanSkinVert) */
		const struct beanSwap *body = model->obj ? activeSwap(model->obj->RootNode) : NULL;
		const struct beanSkin *skin = body ? body->skin : NULL;
		float best = 1e30f;
		for (int v = 0; skin && skin->verts && v < skin->nverts; v++) {
			const struct beanSkinVert *sv = &skin->verts[v];
			for (int k = 0; k < 4; k++) {
				const int m = sv->matrix[k] == -2 ? skin->anchor : sv->matrix[k];
				if (!sv->weight[k] || m != matrix)
					continue;
				const float dx = sv->p[k][0] - local->x, dy = sv->p[k][1] - local->y, dz = sv->p[k][2] - local->z;
				const float d = dx * dx + dy * dy + dz * dz;
				if (d < best) {
					best = d;
					at[0] = sv->p[k][0];
					at[1] = sv->p[k][1];
					at[2] = sv->p[k][2];
				}
			}
		}
	}
	struct beanWound *w = &s_wounds[pick].ring[s_wounds[pick].serial++ % GEVR_SKIN_WOUNDS];
	w->stained = stained;
	w->vertices = vertices;
	w->matrix = matrix;
	w->local[0] = at[0];
	w->local[1] = at[1];
	w->local[2] = at[2];
	w->alpha = (uint8_t)(alpha < 0 ? 0 : alpha > 255 ? 255 : alpha);
	w->kinds = kinds;
	s_wounds[pick].used = ++s_woundUse;
}

/* The model's live wounds on this skin (its body, or a Bean head) in its
 * anchor space (as beanSkinGpuDraw's palette: inverse * (world - anchor)):
 * xyz, then 255 less the alpha plus 256 times the radius; how many. */
static int beanWoundsInAnchor(Model *model, const struct beanSkin *skin, const float inverse[9],
                              const float anchor[4], float out[GEVR_SKIN_WOUNDS][4])
{
	const int head = skin && skin->head;
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
		if (w->matrix >= model->obj->numMatrices || !(w->kinds & (head ? BEAN_WOUND_HEAD : BEAN_WOUND_BODY)))
			continue;
		const float (*m)[4] = model->render_pos[w->matrix].pos.m;
		float world[3];
		for (int c = 0; c < 3; c++)
			world[c] = w->local[0] * m[0][c] + w->local[1] * m[1][c] + w->local[2] * m[2][c] + m[3][c] - anchor[c];
		for (int k = 0; k < 3; k++)
			out[n][k] = inverse[k * 3] * world[0] + inverse[k * 3 + 1] * world[1] + inverse[k * 3 + 2] * world[2];
		out[n][3] = 255.0f - w->alpha + 256.0f * (head ? BEAN_WOUND_RADIUS_HEAD : BEAN_WOUND_RADIUS);
		n++;
	}
	if (!live)
		memset(&s_wounds[slot], 0, sizeof(s_wounds[slot]));
	return n;
}
