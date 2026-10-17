/*
 * Mods: Oddjob's hat in place of the throwing knife (docs/oddjob-hat.md).
 *
 * The hat comes from the player's own 007 Nightfire capture, converted by
 * tools/gevr_oddjob_hat.py into files/mods/oddjob_hat.bin: two pictures, the
 * hat's triangles (already lit into vertex colours), its brim radius, and
 * where Oddjob's right fist holds the brim. Nothing of Nightfire ships.
 *
 * It replaces the knife at draw time only, as Bean's lists do: the knife's
 * display list nodes draw the hat instead (the first) or nothing (the rest),
 * so gameplay, hits and the network still see a throwing knife. Three models:
 *   GthrowknifeZ     the first-person knife: Bond's hand stays, the hat sits
 *                    in his fist the way Oddjob's fist holds the brim
 *   PchrknifeZ       a thrown knife (gevrHatThrown; hunting knives share it)
 *   PchrthrowknifeZ  throwing knives lying in the level
 * A thrown knife spins about its model's Y, so the prop hat's crown is +Y and
 * it flies flat. The lists keep the node's own render mode (types 3 and 4:
 * texel times shade where shade alpha is full), so the hat is fogged and
 * depth-tested with the hand beside it.
 *
 * files/gevr_oddjobhat.txt tunes it live (re-read when it changes):
 *   held <brim radius> <dx> <dy> <dz> <yaw> <pitch> <roll>
 *   prop <brim radius> <dx> <dy> <dz> <pitch>
 * in model units and degrees; held turns about the fist (yaw about the
 * knife's axis, pitch across the hand, roll about the forearm).
 */

#include <ultra64.h>
#include <PR/gbi.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <bondtypes.h>
#include <bondconstants.h>

#include "system.h"
#include "gevr_oddjobhat.h"

#ifdef __ANDROID__
#define HAT_DIR "/sdcard/Android/data/com.gevr.port/files/"
#else
#define HAT_DIR ""
#endif
#define HAT_FILE HAT_DIR "mods/oddjob_hat.bin"
#define HAT_TUNE HAT_DIR "gevr_oddjobhat.txt"

enum { HAT_HELD, HAT_THROWN, HAT_PICKUP, HAT_KINDS };

#define HAT_MAX_TEX   4
#define HAT_MAX_NODES 32
#define HAT_THROWN_MAX 64

/* GthrowknifeZ's fist, measured on the cartridge's model (tools/gevr_model_export.py):
 * the centre of the fist on the knife's axis, the forearm's direction into
 * the fist, and the knife's axis towards its handle */
static const float s_fist[3] = { 0.49f, -53.03f, -43.48f };
static const float s_forearm[3] = { 0.016f, 0.205f, 0.979f };
static const float s_knife[3] = { -0.003f, 0.933f, 0.360f };

struct hatCorner {
	float p[3], st[2];
	u8 rgba[4];
	u8 tex;
};

static struct {
	int tried, ok;
	float radius;
	float fist[3], forearm[3], up[3];
	int ntex;
	u8 *rgba[HAT_MAX_TEX];
	int w[HAT_MAX_TEX], h[HAT_MAX_TEX];
	int ntri;
	struct hatCorner *corners;
} s_hat;

static u16 s_standin[HAT_MAX_TEX][16 * 16] __attribute__((aligned(8)));

struct hatModel {
	struct ModelFileHeader *header;
	ModelNode *nodes[HAT_MAX_NODES];
	u8 knife[HAT_MAX_NODES];
	int nnodes;
	ModelNode *hatNode;
	int matrix;
	float origin[3];   /* the model's origin in the hat node's matrix space */
	Vtx *vtx;
	Gfx *gdl;
	int built;
};

static struct hatModel s_models[HAT_KINDS];
static struct Model *s_thrown[HAT_THROWN_MAX];
static int s_nthrown;

static struct {
	float held[7];
	float prop[5];
	long stamp;
	int frames;
} s_tune = { { 133.0f, 0, 0, 0, 0, 0, 0 }, { 200.0f, 0, 0, 0, 0 }, -1, 0 };

static u32 rd32(const u8 *p) { u32 v; memcpy(&v, p, 4); return v; }
static float rdf(const u8 *p) { float v; memcpy(&v, p, 4); return v; }

static void hatLoad(void)
{
	FILE *f;
	long size;
	u8 *data, *p, *end;
	if (s_hat.tried) return;
	s_hat.tried = 1;
	f = fopen(HAT_FILE, "rb");
	if (!f) return;
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);
	data = size > 64 ? malloc((size_t)size) : NULL;
	if (!data || fread(data, 1, (size_t)size, f) != (size_t)size) {
		free(data);
		fclose(f);
		return;
	}
	fclose(f);
	p = data;
	end = data + size;
	if (memcmp(p, "GEVRHAT1", 8) != 0) goto bad;
	p += 8;
	s_hat.radius = rdf(p);
	for (int k = 0; k < 3; k++) {
		s_hat.fist[k] = rdf(p + 4 + k * 4);
		s_hat.forearm[k] = rdf(p + 16 + k * 4);
		s_hat.up[k] = rdf(p + 28 + k * 4);
	}
	p += 40;
	s_hat.ntex = (int)rd32(p);
	p += 4;
	if (s_hat.ntex < 1 || s_hat.ntex > HAT_MAX_TEX || !(s_hat.radius > 0)) goto bad;
	for (int t = 0; t < s_hat.ntex; t++) {
		int w, h;
		if (p + 4 > end) goto bad;
		w = p[0] | p[1] << 8;
		h = p[2] | p[3] << 8;
		p += 4;
		if (w < 1 || h < 1 || w > 512 || h > 512 || p + w * h * 4 > end) goto bad;
		s_hat.rgba[t] = malloc((size_t)w * h * 4);
		memcpy(s_hat.rgba[t], p, (size_t)w * h * 4);
		s_hat.w[t] = w;
		s_hat.h[t] = h;
		p += w * h * 4;
	}
	if (p + 4 > end) goto bad;
	s_hat.ntri = (int)rd32(p);
	p += 4;
	if (s_hat.ntri < 1 || s_hat.ntri > 20000 || p + (size_t)s_hat.ntri * 3 * 28 > end) goto bad;
	s_hat.corners = malloc((size_t)s_hat.ntri * 3 * sizeof(struct hatCorner));
	for (int i = 0; i < s_hat.ntri * 3; i++, p += 28) {
		struct hatCorner *c = &s_hat.corners[i];
		for (int k = 0; k < 3; k++) c->p[k] = rdf(p + k * 4);
		c->st[0] = rdf(p + 12);
		c->st[1] = rdf(p + 16);
		memcpy(c->rgba, p + 20, 4);
		c->tex = p[24] < s_hat.ntex ? p[24] : 0;
	}
	free(data);
	s_hat.ok = 1;
	sysLogPrintf(LOG_NOTE, "oddjob hat: %d triangles, %d pictures", s_hat.ntri, s_hat.ntex);
	return;
bad:
	free(data);
	sysLogPrintf(LOG_WARNING, "oddjob hat: %s is not a hat file from tools/gevr_oddjob_hat.py", HAT_FILE);
}

int gevrHatAvailable(void)
{
	static int known = -1;
	if (s_hat.ok) return 1;
	if (known < 0) {
		struct stat st;
		known = stat(HAT_FILE, &st) == 0 && st.st_size > 64;
	}
	return known;
}

static int nodeOp(const ModelNode *n)
{
	return n ? (n->Opcode & 0xff) : 0;
}

/* as gevr_bean.c's nodeMatrix: the matrix of the group a node hangs from */
static int nodeMatrix(ModelNode *n, float origin[3])
{
	for (; n; n = n->Parent) {
		if (!n->Data) continue;
		switch (nodeOp(n)) {
		case MODELNODE_OPCODE_HEADER:
			return n->Data->Header.MatrixIndex;
		case MODELNODE_OPCODE_GROUP:
			for (int k = 0; k < 3; k++) origin[k] = -n->Data->Group.Origin.f[k];
			return n->Data->Group.MatrixIDs[0];
		case MODELNODE_OPCODE_GROUPSIMPLE:
			for (int k = 0; k < 3; k++) origin[k] = -n->Data->GroupSimple.Origin.f[k];
			return n->Data->GroupSimple.Group1;
		}
	}
	return -1;
}

static void collect(struct hatModel *m, ModelNode *n)
{
	for (; n; n = n->Next) {
		if (nodeOp(n) == MODELNODE_OPCODE_DL && n->Data && m->nnodes < HAT_MAX_NODES)
			m->nodes[m->nnodes++] = n;
		if (n->Child) collect(m, n->Child);
	}
}

void gevrHatModelLoaded(struct ModelFileHeader *header, const char *name)
{
	int kind;
	struct hatModel *m;
	if (!header || !name) return;
	if (strcmp(name, "GthrowknifeZ") == 0) kind = HAT_HELD;
	else if (strcmp(name, "PchrknifeZ") == 0) kind = HAT_THROWN;
	else if (strcmp(name, "PchrthrowknifeZ") == 0) kind = HAT_PICKUP;
	else {
		for (int k = 0; k < HAT_KINDS; k++)
			if (s_models[k].header == header) s_models[k].header = NULL;   /* the slot holds another model now */
		return;
	}
	m = &s_models[kind];
	memset(m->nodes, 0, sizeof(m->nodes));
	memset(m->knife, 0, sizeof(m->knife));
	m->header = NULL;
	m->nnodes = 0;
	m->hatNode = NULL;
	collect(m, header->RootNode);
	/* the knife's nodes by their vertex counts: the held knife's blade, guard
	 * and handle; a prop is the knife alone. Another ROM's model is left be. */
	for (int i = 0; i < m->nnodes; i++) {
		int n = m->nodes[i]->Data->DisplayList.numVertices;
		m->knife[i] = kind == HAT_HELD ? (n == 149 || n == 126 || n == 60) : (n == 39 && m->nnodes == 1);
		if (m->knife[i] && !m->hatNode) m->hatNode = m->nodes[i];
	}
	{
		int found = 0;
		for (int i = 0; i < m->nnodes; i++) found += m->knife[i];
		if (found != (kind == HAT_HELD ? 3 : 1)) {
			sysLogPrintf(LOG_WARNING, "oddjob hat: %s is not the model the hat was fitted to", name);
			return;
		}
	}
	m->origin[0] = m->origin[1] = m->origin[2] = 0;
	m->matrix = nodeMatrix(m->hatNode, m->origin);
	if (m->matrix < 0) return;
	m->header = header;
	if (kind == HAT_THROWN) s_nthrown = 0;   /* a new level's instances */
	if (VrOddjobHat) hatLoad();   /* before the first draw, so Bean stands aside from the start */
}

void gevrHatThrown(struct Model *model)
{
	if (!model) return;
	s_thrown[s_nthrown % HAT_THROWN_MAX] = model;
	s_nthrown++;
}

static int isThrown(struct Model *model)
{
	int n = s_nthrown < HAT_THROWN_MAX ? s_nthrown : HAT_THROWN_MAX;
	for (int i = 0; i < n; i++)
		if (s_thrown[i] == model) return 1;
	return 0;
}

static void rotate(float out[3][3], float yaw, float pitch, float roll)
{
	/* in the fist's frame (x forearm, y knife, z across): roll about x, yaw
	 * about y, pitch about z, applied roll, then pitch, then yaw */
	float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch), cr = cosf(roll), sr = sinf(roll);
	float ry[3][3] = { { cy, 0, sy }, { 0, 1, 0 }, { -sy, 0, cy } };
	float rp[3][3] = { { cp, -sp, 0 }, { sp, cp, 0 }, { 0, 0, 1 } };
	float rr[3][3] = { { 1, 0, 0 }, { 0, cr, -sr }, { 0, sr, cr } };
	float t[3][3];
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++) {
			t[i][j] = 0;
			for (int k = 0; k < 3; k++) t[i][j] += rp[i][k] * rr[k][j];
		}
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++) {
			out[i][j] = 0;
			for (int k = 0; k < 3; k++) out[i][j] += ry[i][k] * t[k][j];
		}
}

static void frame(float rows[3][3], const float along[3], const float up[3])
{
	float d = 0, len = 0;
	for (int k = 0; k < 3; k++) len += along[k] * along[k];
	len = sqrtf(len);
	for (int k = 0; k < 3; k++) rows[0][k] = along[k] / len;
	for (int k = 0; k < 3; k++) d += up[k] * rows[0][k];
	len = 0;
	for (int k = 0; k < 3; k++) {
		rows[1][k] = up[k] - d * rows[0][k];
		len += rows[1][k] * rows[1][k];
	}
	len = sqrtf(len);
	for (int k = 0; k < 3; k++) rows[1][k] /= len;
	rows[2][0] = rows[0][1] * rows[1][2] - rows[0][2] * rows[1][1];
	rows[2][1] = rows[0][2] * rows[1][0] - rows[0][0] * rows[1][2];
	rows[2][2] = rows[0][0] * rows[1][1] - rows[0][1] * rows[1][0];
}

static s16 clampS16(float v)
{
	return v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : (s16)lrintf(v);
}

/* the hat's corners in the model's own space */
static void place(int kind, Vtx *vtx)
{
	struct hatModel *m = &s_models[kind];
	const float rad = (float)M_PI / 180.0f;
	float ge[3][3], nf[3][3], turn[3][3];
	float scale;
	if (kind == HAT_HELD) {
		frame(ge, s_forearm, s_knife);
		frame(nf, s_hat.forearm, s_hat.up);
		rotate(turn, s_tune.held[4] * rad, s_tune.held[5] * rad, s_tune.held[6] * rad);
		scale = s_tune.held[0] / s_hat.radius;
	} else {
		rotate(turn, 0, 0, s_tune.prop[4] * rad);
		scale = s_tune.prop[0] / s_hat.radius;
	}
	for (int i = 0; i < s_hat.ntri * 3; i++) {
		const struct hatCorner *c = &s_hat.corners[i];
		float q[3], r[3], out[3];
		if (kind == HAT_HELD) {
			/* Oddjob's fist frame -> turn -> Bond's fist frame */
			for (int a = 0; a < 3; a++) {
				q[a] = 0;
				for (int k = 0; k < 3; k++) q[a] += nf[a][k] * (c->p[k] - s_hat.fist[k]) * scale;
			}
			for (int a = 0; a < 3; a++) r[a] = turn[a][0] * q[0] + turn[a][1] * q[1] + turn[a][2] * q[2];
			for (int k = 0; k < 3; k++)
				out[k] = s_fist[k] + s_tune.held[1 + k] + ge[0][k] * r[0] + ge[1][k] * r[1] + ge[2][k] * r[2];
		} else {
			for (int k = 0; k < 3; k++) q[k] = c->p[k] * scale;
			for (int k = 0; k < 3; k++)
				out[k] = m->origin[k] + s_tune.prop[1 + k] + turn[k][0] * q[0] + turn[k][1] * q[1] + turn[k][2] * q[2];
		}
		for (int k = 0; k < 3; k++) vtx[i].v.ob[k] = clampS16(out[k]);
	}
}

static int texMask(int n)
{
	int m = 0;
	while ((1 << m) < n) m++;
	return m;
}

static void build(int kind)
{
	struct hatModel *m = &s_models[kind];
	const int nv = s_hat.ntri * 3;
	Gfx *g;
	if (!m->vtx) {
		m->vtx = calloc((size_t)nv, sizeof(Vtx));
		/* per picture ~14 commands; a triangle at most two (a batch split by another picture's) */
		m->gdl = calloc((size_t)(s_hat.ntex * 16 + s_hat.ntri * 2 + 16), sizeof(Gfx));
		if (!m->vtx || !m->gdl) return;
	}
	place(kind, m->vtx);
	if (m->built) return;
	g = m->gdl;
	gSPMatrix(g++, (void *)(uintptr_t)(0x03000000u + m->matrix * sizeof(Mtx)), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
	gDPPipeSync(g++);
	gDPSetTexturePersp(g++, G_TP_PERSP);
	gDPSetTextureLUT(g++, G_TT_NONE);
	gSPClearGeometryMode(g++, G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR | G_LIGHTING | G_CULL_BOTH);
	gSPSetGeometryMode(g++, G_SHADE | G_SHADING_SMOOTH);
	for (int t = 0; t < s_hat.ntex; t++) {
		const int w = s_hat.w[t], h = s_hat.h[t], ms = texMask(w), mt = texMask(h);
		int tri = 0, batch[15], nb = 0;
		gSPTexture(g++, 0xffff, 0xffff, 0, G_TX_RENDERTILE, G_ON);
		gDPLoadTextureBlock(g++, s_standin[t], G_IM_FMT_RGBA, G_IM_SIZ_16b, 16, 16, 0,
		                    G_TX_WRAP, G_TX_WRAP, 4, 4, G_TX_NOLOD, G_TX_NOLOD);
		for (int tile = G_TX_RENDERTILE; tile <= G_TX_RENDERTILE + 1; tile++) {
			gDPSetTile(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b, ((w * 2) + 7) >> 3, 0, tile, 0,
			           G_TX_WRAP, mt, G_TX_NOLOD, G_TX_WRAP, ms, G_TX_NOLOD);
			gDPSetTileSize(g++, tile, 0, 0, (w - 1) << G_TEXTURE_IMAGE_FRAC, (h - 1) << G_TEXTURE_IMAGE_FRAC);
		}
		for (tri = 0; tri < s_hat.ntri; tri++) {
			const struct hatCorner *c = &s_hat.corners[tri * 3];
			/* rebase the whole triangle together, as Bean's lists do */
			const float bs = floorf(c[0].st[0]), bt = floorf(c[0].st[1]);
			if (c->tex != t) continue;
			for (int k = 0; k < 3; k++) {
				Vtx *v = &m->vtx[tri * 3 + k];
				v->v.flag = 0;
				v->v.tc[0] = clampS16((c[k].st[0] - bs) * w * 32.0f);
				v->v.tc[1] = clampS16((c[k].st[1] - bt) * h * 32.0f);
				memcpy(v->v.cn, c[k].rgba, 4);
			}
			batch[nb++] = tri;
			if (nb == 5 || tri == s_hat.ntri - 1) {
				/* a batch's triangles are consecutive only when no other picture's fall between */
				int run = 1;
				for (int b = 1; b < nb; b++) run &= batch[b] == batch[0] + b;
				if (run) {
					gSPVertex(g++, osVirtualToPhysical(&m->vtx[batch[0] * 3]), nb * 3, 0);
					for (int b = 0; b < nb; b++) gSP1Triangle(g++, b * 3, b * 3 + 1, b * 3 + 2, 0);
				} else {
					for (int b = 0; b < nb; b++) {
						gSPVertex(g++, osVirtualToPhysical(&m->vtx[batch[b] * 3]), 3, 0);
						gSP1Triangle(g++, 0, 1, 2, 0);
					}
				}
				nb = 0;
			}
		}
		if (nb) {
			for (int b = 0; b < nb; b++) {
				gSPVertex(g++, osVirtualToPhysical(&m->vtx[batch[b] * 3]), 3, 0);
				gSP1Triangle(g++, 0, 1, 2, 0);
			}
		}
	}
	gSPEndDisplayList(g++);
	m->built = 1;
}

static void tuneCheck(void)
{
	struct stat st;
	long stamp;
	FILE *f;
	char line[160];
	if (s_tune.frames++ % 60) return;
	stamp = stat(HAT_TUNE, &st) == 0 ? (long)st.st_mtime : 0;
	if (stamp == s_tune.stamp) return;
	s_tune.stamp = stamp;
	f = stamp ? fopen(HAT_TUNE, "r") : NULL;
	while (f && fgets(line, sizeof(line), f)) {
		float *h = s_tune.held, *p = s_tune.prop;
		if (sscanf(line, "held %f %f %f %f %f %f %f", &h[0], &h[1], &h[2], &h[3], &h[4], &h[5], &h[6]) >= 1) continue;
		sscanf(line, "prop %f %f %f %f %f", &p[0], &p[1], &p[2], &p[3], &p[4]);
	}
	if (f) fclose(f);
	sysLogPrintf(LOG_NOTE, "oddjob hat: held %.1f %.1f %.1f %.1f %.1f %.1f %.1f prop %.1f %.1f %.1f %.1f %.1f",
	             s_tune.held[0], s_tune.held[1], s_tune.held[2], s_tune.held[3], s_tune.held[4], s_tune.held[5],
	             s_tune.held[6], s_tune.prop[0], s_tune.prop[1], s_tune.prop[2], s_tune.prop[3], s_tune.prop[4]);
	for (int k = 0; k < HAT_KINDS; k++)
		if (s_models[k].vtx) place(k, s_models[k].vtx);
}

static struct hatModel *ownerOf(struct Model *model)
{
	if (!VrOddjobHat || !model || !model->obj) return NULL;
	for (int k = 0; k < HAT_KINDS; k++)
		if (s_models[k].header && s_models[k].header == model->obj)
			return k == HAT_THROWN && !isThrown(model) ? NULL : &s_models[k];
	return NULL;
}

int gevrHatNodeGdl(struct ModelNode *node, struct Model *model, void **gdl)
{
	struct hatModel *m = ownerOf(model);
	if (!m) return 0;
	hatLoad();
	if (!s_hat.ok) return 0;
	tuneCheck();
	if (!m->built) build((int)(m - s_models));
	if (!m->built) return 0;
	for (int i = 0; i < m->nnodes; i++) {
		if (m->nodes[i] != node) continue;
		*gdl = !m->knife[i] ? (void *)node->Data->DisplayList.Primary : node == m->hatNode ? (void *)m->gdl : NULL;
		return 1;
	}
	return 0;
}

int gevrHatOwnsNode(const void *node)
{
	const ModelNode *n = node, *root;
	if (!VrOddjobHat || !s_hat.ok || !n) return 0;
	for (root = n; root->Parent; root = root->Parent) {}
	for (int k = 0; k < HAT_KINDS; k++)
		if (k != HAT_THROWN && s_models[k].header && s_models[k].header->RootNode == root) return 1;
	return 0;
}

int gevrHatTexture(const void *addr, const u8 **rgba, int *w, int *h)
{
	if (!s_hat.ok || (const u8 *)addr < (const u8 *)s_standin || (const u8 *)addr >= (const u8 *)s_standin[s_hat.ntex])
		return 0;
	for (int t = 0; t < s_hat.ntex; t++)
		if (addr == s_standin[t]) {
			*rgba = s_hat.rgba[t];
			*w = s_hat.w[t];
			*h = s_hat.h[t];
			return 1;
		}
	return 0;
}
