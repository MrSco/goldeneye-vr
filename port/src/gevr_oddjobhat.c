/*
 * Mods: Oddjob's hat in place of the throwing knife (docs/oddjob-hat.md).
 *
 * The hat is 007 Nightfire's (GameCube), converted by tools/gevr_oddjob_hat.py
 * into oddjob_hat.bin: two pictures, the hat's triangles (already lit into
 * vertex colours), its brim radius, and where Oddjob's right fist holds the
 * brim. The launcher's Mods page downloads it as the "oddjob-hat" pack
 * (ModManager.java) into texture-packs/oddjob-hat/; files/mods/ takes a
 * hand-made one. It is not in the APK.
 *
 * It replaces the knife at draw time only, as Bean's lists do: the knife's
 * display list nodes draw the hat instead (the first) or nothing (the rest),
 * so gameplay, hits and the network still see a throwing knife. The hand's
 * nodes are left to whoever draws them, so the hat sits in every look's hand:
 *   GthrowknifeZ     the first-person knife. The cartridge's model: the
 *                    blade, guard and handle are three nodes (149, 126, 60
 *                    vertices), the rest Bond's hand, which Bean draws its
 *                    glove on (gevr_bean.c applyMesh). GoldenEye X's: the
 *                    whole file is Perfect Dark's combat knife on the gun's
 *                    matrix (gevr_gexweapon.c KNIFE_DEF), held by its own
 *                    hand model on the gun's matrices; the hat goes on the
 *                    knife's matrix, at that hand's palm, measured once from
 *                    the first frame it is drawn in.
 *   PchrknifeZ       a thrown knife (gevrHatThrown; hunting knives share it)
 *   PchrthrowknifeZ  throwing knives lying in the level
 * A thrown knife spins about its model's Y, so the prop hat's crown is +Y and
 * it flies flat. The lists keep the node's own render mode (types 3 and 4:
 * texel times shade where shade alpha is full), so the hat is fogged and
 * depth-tested with the hand beside it.
 *
 * files/gevr_oddjobhat.txt tunes it live (re-read when it changes):
 *   held <brim radius> <dx> <dy> <dz> <yaw> <pitch> <roll>
 *   gex  <brim radius cm> <dx> <dy> <dz> <yaw> <pitch> <roll>
 *   prop <brim radius> <dx> <dy> <dz> <pitch>
 *   height <times>   (the crown's height, 0.55 by default)
 * in model units and degrees; held and gex turn about the fist (yaw about
 * the knife's axis, pitch across the hand, roll about the forearm).
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
#include <android/log.h>
#endif
#include "gevr_gexmodel.h"
#include "gevr_gexweapon.h"

#ifdef __ANDROID__
#define HAT_DIR "/sdcard/Android/data/com.gevr.port/files/"
#else
#define HAT_DIR ""
#endif
static const char *const s_files[] = {
	HAT_DIR "mods/oddjob_hat.bin",                       /* a hand-made one wins, for testing */
	HAT_DIR "texture-packs/oddjob-hat/oddjob_hat.bin",   /* the launcher's download */
};

/* the game's log is private on a release build; logcat (GEVR-Hat) is not */
#define hatLog(...) do { \
	sysLogPrintf(LOG_NOTE, __VA_ARGS__); \
	HAT_LOGCAT(__VA_ARGS__); \
} while (0)
#ifdef __ANDROID__
#define HAT_LOGCAT(...) __android_log_print(ANDROID_LOG_INFO, "GEVR-Hat", __VA_ARGS__)
#else
#define HAT_LOGCAT(...) ((void)0)
#endif
#define HAT_TUNE HAT_DIR "gevr_oddjobhat.txt"

enum { HAT_HELD, HAT_THROWN, HAT_PICKUP, HAT_KINDS };

#define HAT_MAX_TEX    4
#define HAT_MAX_NODES  64
#define HAT_THROWN_MAX 64
/* a model loaded into several headers is several registrations: the held
 * knife goes into each hand's copy (gun.c copy_of_body_obj_header), and
 * GoldenEye X's weapon panel loads its own */
#define HAT_MAX_REGS   16

/* GthrowknifeZ's fist, measured on the cartridge's model (tools/gevr_model_export.py),
 * in its group's space: the centre of the fist on the knife's axis, the
 * forearm's direction into the fist, and the knife's axis towards its handle */
static const float s_fist[3] = { 0.49f, 12.54f, 42.72f };
static const float s_forearm[3] = { 0.016f, 0.205f, 0.979f };
static const float s_knife[3] = { -0.003f, 0.933f, 0.360f };

/* GoldenEye X's hand model: the right wrist's matrix, its palm along it (gun.c) */
extern f32 D_800364CC;                               /* the level scale: view units a centimetre */
extern f32 gevrGunSizeNow(s32 *setting, s32 *online);   /* bondview2.c: the guns' size */

#define GEX_WRIST  2
#define GEX_PALM_Z 50.0f

struct hatCorner {
	float p[3], st[2];
	u8 rgba[4];
	u8 tex;
};

static struct {
	int ok;
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
	int kind;
	int drawn;         /* logged its first draw */
	unsigned used;     /* when last drawn, to reuse the oldest */
	ModelNode *nodes[HAT_MAX_NODES];
	u8 knife[HAT_MAX_NODES];
	int nnodes;
	ModelNode *hatNode;
	int matrix;
	int gex;           /* GoldenEye X's knife: the fist is measured at its first draw */
	int placed;        /* the fist below is known */
	float fist[3], forearm[3], handle[3];   /* in the hat node's matrix space */
	float camera;      /* GoldenEye X: view units a knife-matrix unit (its fit sizes the knife) */
	float origin[3];   /* the model's origin in that space (props) */
	Vtx *vtx;
	Gfx *gdl;
	int built;
};

static struct hatModel s_models[HAT_MAX_REGS];
static unsigned s_clock;
static struct Model *s_thrown[HAT_THROWN_MAX];
static int s_nthrown;

static struct {
	float held[7];
	float gex[7];
	float prop[5];
	float height;      /* the crown's height, times: the capture gives no true height (X is fixed by the round band, Y is not); 0.55 undoes the capture's vertical stretch, by eye, until the converter takes it */
	long stamp;
	int frames;
} s_tune = { { 133.0f, 0, 0, 0, 0, 0, 0 }, { 15.0f, 0, 0, 0, 0, 0, 0 }, { 200.0f, 0, 0, 0, 0 }, 0.55f, -1, 0 };

static u32 rd32(const u8 *p) { u32 v; memcpy(&v, p, 4); return v; }
static float rdf(const u8 *p) { float v; memcpy(&v, p, 4); return v; }

static const char *hatFile(void)
{
	struct stat st;
	for (unsigned i = 0; i < sizeof(s_files) / sizeof(s_files[0]); i++)
		if (stat(s_files[i], &st) == 0 && st.st_size > 64) return s_files[i];
	return NULL;
}

static void hatLoad(void)
{
	static const char *s_failed;
	const char *path;
	FILE *f;
	long size;
	u8 *data, *p, *end;
	if (s_hat.ok) return;
	path = hatFile();
	if (!path || path == s_failed) return;
	f = fopen(path, "rb");
	if (!f) return;
	s_failed = path;   /* until it loads: a broken file is reported once */
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
	hatLog("oddjob hat: %s: %d triangles, %d pictures", path, s_hat.ntri, s_hat.ntex);
	return;
bad:
	for (int t = 0; t < HAT_MAX_TEX; t++) {
		free(s_hat.rgba[t]);
		s_hat.rgba[t] = NULL;
	}
	free(data);
	sysLogPrintf(LOG_WARNING, "oddjob hat: %s is not a hat file from tools/gevr_oddjob_hat.py", path);
}

int gevrHatAvailable(void)
{
	return s_hat.ok || hatFile() != NULL;
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

/* every display list node, through the children and what switches, LODs and
 * BSPs point at (as gevr_bean.c applyMesh walks) */
static void collect(struct hatModel *m, ModelNode *n, int depth)
{
	for (; n && depth < 64; n = n->Next) {
		const int op = nodeOp(n);
		if (op == MODELNODE_OPCODE_DL && n->Data) {
			int seen = 0;
			for (int i = 0; i < m->nnodes; i++) seen |= m->nodes[i] == n;
			if (seen) continue;
			if (m->nnodes < HAT_MAX_NODES) m->nodes[m->nnodes++] = n;
		}
		if (n->Data) {
			ModelNode *a = op == MODELNODE_OPCODE_SWITCH ? n->Data->Switch.Controls
			             : op == MODELNODE_OPCODE_LOD    ? n->Data->LOD.Affects
			             : op == MODELNODE_OPCODE_BSP    ? n->Data->BSP.leftChild : NULL;
			ModelNode *b = op == MODELNODE_OPCODE_BSP ? n->Data->BSP.rightChild : NULL;
			if (a && a != n->Child) collect(m, a, depth + 1);
			if (b && b != n->Child) collect(m, b, depth + 1);
		}
		if (n->Child) collect(m, n->Child, depth + 1);
	}
}

void gevrHatModelLoaded(struct ModelFileHeader *header, const char *name)
{
	int kind, found = 0, want, counts = 0;
	char list[160] = "";
	struct hatModel *m = NULL;
	const GexWeaponDef *gex = NULL;
	if (!header || !name) return;
	for (int r = 0; r < HAT_MAX_REGS; r++)
		if (s_models[r].header == header) {
			m = &s_models[r];
			m->header = NULL;   /* the header holds another load now */
		}
	if (strcmp(name, "GthrowknifeZ") == 0) kind = HAT_HELD;
	else if (strcmp(name, "PchrknifeZ") == 0) kind = HAT_THROWN;
	else if (strcmp(name, "PchrthrowknifeZ") == 0) kind = HAT_PICKUP;
	else return;
	if (!m) {
		/* a free registration, else the one drawn longest ago */
		for (int r = 0; r < HAT_MAX_REGS && !m; r++)
			if (!s_models[r].header) m = &s_models[r];
		for (int r = 0; r < HAT_MAX_REGS; r++)
			if (!m || s_models[r].used < m->used) m = &s_models[r];
	}
	memset(m->nodes, 0, sizeof(m->nodes));
	memset(m->knife, 0, sizeof(m->knife));
	m->kind = kind;
	m->nnodes = 0;
	m->hatNode = NULL;
	m->drawn = 0;
	m->used = s_clock;
	m->gex = kind == HAT_HELD && gevrGexPendingFile != NULL;
	m->placed = 0;
	if (m->gex) gex = gevrGexWeaponGet(ITEM_THROWKNIFE);
	if (m->gex && !gex) return;
	collect(m, header->RootNode, 0);
	/* the knife's nodes: GoldenEye X's file is the knife alone (one list,
	 * 347 vertices, loading the blade's matrix itself; its hand is another
	 * model); the cartridge's held knife's blade, guard and handle by their
	 * vertex counts; a prop is the knife alone. Another ROM's model is left be. */
	for (int i = 0; i < m->nnodes; i++) {
		int n = m->nodes[i]->Data->DisplayList.numVertices;
		float o[3];
		int mtx = nodeMatrix(m->nodes[i], o);
		if (m->gex) m->knife[i] = 1;
		else if (kind == HAT_HELD) m->knife[i] = n == 149 || n == 126 || n == 60;
		else m->knife[i] = n == 39 && m->nnodes == 1;
		if (m->knife[i] && !m->hatNode) m->hatNode = m->nodes[i];
		found += m->knife[i];
		if (counts < (int)sizeof(list) - 16)
			counts += snprintf(list + counts, sizeof(list) - counts, " %d@%d%s", n, mtx, m->knife[i] ? "*" : "");
	}
	want = m->gex ? found : kind == HAT_HELD ? 3 : 1;
	if (!found || found != want) {
		hatLog("oddjob hat: %s%s is not a model the hat was fitted to (lists:%s)", name, m->gex ? " (GoldenEye X)" : "", list);
		return;
	}
	m->origin[0] = m->origin[1] = m->origin[2] = 0;
	/* GoldenEye X's hat hangs on the blade's matrix, not its list's group's */
	m->matrix = m->gex ? gex->gunMatrix : nodeMatrix(m->hatNode, m->origin);
	if (m->matrix < 0) return;
	if (kind == HAT_HELD && !m->gex) {
		for (int k = 0; k < 3; k++) {
			m->fist[k] = s_fist[k];
			m->forearm[k] = s_forearm[k];
			m->handle[k] = s_knife[k];
		}
	}
	m->placed = !m->gex;   /* GoldenEye X's fist is measured at its first draw */
	m->header = header;
	m->built = 0;
	if (kind == HAT_THROWN) s_nthrown = 0;   /* a new level's instances */
	hatLog("oddjob hat: %s%s in header %p: lists (vertices@matrix, * the knife's):%s; hat on matrix %d", name,
	       m->gex ? " (GoldenEye X)" : "", (void *)header, list, m->matrix);
	if (VrOddjobHat) hatLoad();   /* before the first draw, so Bean stands aside from the start */
}

void gevrHatThrown(struct Model *model)
{
	if (!model) return;
	s_thrown[s_nthrown % HAT_THROWN_MAX] = model;
	s_nthrown++;
	hatLog("oddjob hat: thrown knife %d, model %p, header %p", s_nthrown, (void *)model, (void *)model->obj);
}

static int isThrown(struct Model *model)
{
	int n = s_nthrown < HAT_THROWN_MAX ? s_nthrown : HAT_THROWN_MAX;
	for (int i = 0; i < n; i++)
		if (s_thrown[i] == model) return 1;
	return 0;
}

/* GoldenEye X: the hand's palm and forearm, and the blade (the knife
 * matrix's x, the end above the hand as it is first drawn), in the knife's
 * matrix space. The matrices are camera space, rows as GoldenEye's. */
static int gexMeasure(struct hatModel *m, struct Model *model)
{
	const float (*w)[4], (*k)[4];
	float l[9], inv[9], d, palm[3], rel[3], arm[3];
	if (!model->render_pos || model->obj->numMatrices <= m->matrix || model->obj->numMatrices <= GEX_WRIST) return 0;
	w = model->render_pos[GEX_WRIST].pos.m;
	k = model->render_pos[m->matrix].pos.m;
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++) l[i * 3 + j] = k[i][j];
	d = l[0] * (l[4] * l[8] - l[5] * l[7]) - l[1] * (l[3] * l[8] - l[5] * l[6]) + l[2] * (l[3] * l[7] - l[4] * l[6]);
	if (!isfinite(d) || fabsf(d) < 1e-12f) return 0;
	inv[0] = (l[4] * l[8] - l[5] * l[7]) / d;
	inv[1] = (l[2] * l[7] - l[1] * l[8]) / d;
	inv[2] = (l[1] * l[5] - l[2] * l[4]) / d;
	inv[3] = (l[5] * l[6] - l[3] * l[8]) / d;
	inv[4] = (l[0] * l[8] - l[2] * l[6]) / d;
	inv[5] = (l[2] * l[3] - l[0] * l[5]) / d;
	inv[6] = (l[3] * l[7] - l[4] * l[6]) / d;
	inv[7] = (l[1] * l[6] - l[0] * l[7]) / d;
	inv[8] = (l[0] * l[4] - l[1] * l[3]) / d;
	/* a row vector p maps to p * L + t: back is (q - t) * inv(L) */
	for (int j = 0; j < 3; j++) {
		palm[j] = GEX_PALM_Z * w[2][j] + w[3][j];
		rel[j] = palm[j] - k[3][j];
		arm[j] = w[2][j];
	}
	for (int j = 0; j < 3; j++) {
		m->fist[j] = rel[0] * inv[j] + rel[1] * inv[3 + j] + rel[2] * inv[6 + j];
		m->forearm[j] = arm[0] * inv[j] + arm[1] * inv[3 + j] + arm[2] * inv[6 + j];
	}
	m->handle[0] = k[0][1] >= 0 ? 1.0f : -1.0f;   /* camera y is up */
	m->handle[1] = m->handle[2] = 0;
	/* sized in centimetres: the knife matrix's view units a unit */
	m->camera = sqrtf(k[0][0] * k[0][0] + k[0][1] * k[0][1] + k[0][2] * k[0][2]);
	if (!(m->camera > 1e-9f)) return 0;
	m->placed = 1;
	hatLog("oddjob hat: GoldenEye X fist %.1f %.1f %.1f, forearm %.2f %.2f %.2f, blade %+.0f x, %.4f view units a unit",
	             m->fist[0], m->fist[1], m->fist[2], m->forearm[0], m->forearm[1], m->forearm[2], m->handle[0], m->camera);
	return 1;
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

/* the hat's corners in the hat node's matrix space */
static void place(struct hatModel *m, Vtx *vtx)
{
	const int kind = m->kind;
	const float rad = (float)M_PI / 180.0f;
	const float *tune = kind != HAT_HELD ? s_tune.prop : m->gex ? s_tune.gex : s_tune.held;
	float ge[3][3], nf[3][3], turn[3][3];
	/* GoldenEye X's brim radius is in centimetres at the guns' size (bondview2.c:
	 * view units a centimetre are the level scale); the others in model units */
	float scale = tune[0] / s_hat.radius;
	if (kind == HAT_HELD && m->gex) {
		s32 setting, online;
		scale = tune[0] * D_800364CC * gevrGunSizeNow(&setting, &online) / m->camera / s_hat.radius;
	}
	if (kind == HAT_HELD) {
		frame(ge, m->forearm, m->handle);
		frame(nf, s_hat.forearm, s_hat.up);
		rotate(turn, tune[4] * rad, tune[5] * rad, tune[6] * rad);
	} else {
		rotate(turn, 0, 0, tune[4] * rad);
	}
	for (int i = 0; i < s_hat.ntri * 3; i++) {
		const struct hatCorner *c = &s_hat.corners[i];
		/* the crown's height: hat-local y, the fist's too */
		const float p[3] = { c->p[0], c->p[1] * s_tune.height, c->p[2] };
		const float fist[3] = { s_hat.fist[0], s_hat.fist[1] * s_tune.height, s_hat.fist[2] };
		float q[3], r[3], out[3];
		if (kind == HAT_HELD) {
			/* Oddjob's fist frame -> turn -> the knife hand's fist frame */
			for (int a = 0; a < 3; a++) {
				q[a] = 0;
				for (int k = 0; k < 3; k++) q[a] += nf[a][k] * (p[k] - fist[k]) * scale;
			}
			for (int a = 0; a < 3; a++) r[a] = turn[a][0] * q[0] + turn[a][1] * q[1] + turn[a][2] * q[2];
			for (int k = 0; k < 3; k++)
				out[k] = m->fist[k] + tune[1 + k] + ge[0][k] * r[0] + ge[1][k] * r[1] + ge[2][k] * r[2];
		} else {
			for (int k = 0; k < 3; k++) q[k] = p[k] * scale;
			for (int k = 0; k < 3; k++)
				out[k] = m->origin[k] + tune[1 + k] + turn[k][0] * q[0] + turn[k][1] * q[1] + turn[k][2] * q[2];
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

static void build(struct hatModel *m)
{
	const int nv = s_hat.ntri * 3;
	Gfx *g;
	if (!m->vtx) {
		m->vtx = calloc((size_t)nv, sizeof(Vtx));
		/* per picture ~14 commands; a triangle at most two (a batch split by another picture's) */
		m->gdl = calloc((size_t)(s_hat.ntex * 16 + s_hat.ntri * 2 + 16), sizeof(Gfx));
		if (!m->vtx || !m->gdl) return;
	}
	place(m, m->vtx);
	g = m->gdl;
	gSPMatrix(g++, (void *)(uintptr_t)(0x03000000u + m->matrix * sizeof(Mtx)), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
	gDPPipeSync(g++);
	gDPSetTexturePersp(g++, G_TP_PERSP);
	gDPSetTextureLUT(g++, G_TT_NONE);
	gSPClearGeometryMode(g++, G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR | G_LIGHTING | G_CULL_BOTH);
	gSPSetGeometryMode(g++, G_SHADE | G_SHADING_SMOOTH);
	for (int t = 0; t < s_hat.ntex; t++) {
		const int w = s_hat.w[t], h = s_hat.h[t], ms = texMask(w), mt = texMask(h);
		int batch[5], nb = 0;
		gSPTexture(g++, 0xffff, 0xffff, 0, G_TX_RENDERTILE, G_ON);
		gDPLoadTextureBlock(g++, s_standin[t], G_IM_FMT_RGBA, G_IM_SIZ_16b, 16, 16, 0,
		                    G_TX_WRAP, G_TX_WRAP, 4, 4, G_TX_NOLOD, G_TX_NOLOD);
		for (int tile = G_TX_RENDERTILE; tile <= G_TX_RENDERTILE + 1; tile++) {
			gDPSetTile(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b, ((w * 2) + 7) >> 3, 0, tile, 0,
			           G_TX_WRAP, mt, G_TX_NOLOD, G_TX_WRAP, ms, G_TX_NOLOD);
			gDPSetTileSize(g++, tile, 0, 0, (w - 1) << G_TEXTURE_IMAGE_FRAC, (h - 1) << G_TEXTURE_IMAGE_FRAC);
		}
		for (int tri = 0; tri <= s_hat.ntri; tri++) {
			/* five triangles a load when they are consecutive in the vertex
			 * array; a run broken by another picture's goes one by one */
			int flush = tri == s_hat.ntri;
			if (!flush) {
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
				if (nb && batch[nb - 1] != tri - 1) flush = 2;
			}
			if (flush && nb) {
				for (int b = 0; b < nb;) {
					int run = 1;
					while (b + run < nb && batch[b + run] == batch[b] + run) run++;
					gSPVertex(g++, osVirtualToPhysical(&m->vtx[batch[b] * 3]), run * 3, 0);
					for (int r = 0; r < run; r++) gSP1Triangle(g++, r * 3, r * 3 + 1, r * 3 + 2, 0);
					b += run;
				}
				nb = 0;
			}
			if (tri < s_hat.ntri && s_hat.corners[tri * 3].tex == t) {
				batch[nb++] = tri;
				if (nb == 5) {
					gSPVertex(g++, osVirtualToPhysical(&m->vtx[batch[0] * 3]), 15, 0);
					for (int r = 0; r < 5; r++) gSP1Triangle(g++, r * 3, r * 3 + 1, r * 3 + 2, 0);
					nb = 0;
				}
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
		float *h = s_tune.held, *x = s_tune.gex, *p = s_tune.prop;
		if (sscanf(line, "held %f %f %f %f %f %f %f", &h[0], &h[1], &h[2], &h[3], &h[4], &h[5], &h[6]) >= 1) continue;
		if (sscanf(line, "gex %f %f %f %f %f %f %f", &x[0], &x[1], &x[2], &x[3], &x[4], &x[5], &x[6]) >= 1) continue;
		if (sscanf(line, "height %f", &s_tune.height) == 1) continue;
		sscanf(line, "prop %f %f %f %f %f", &p[0], &p[1], &p[2], &p[3], &p[4]);
	}
	if (f) fclose(f);
	hatLog("oddjob hat: height %.2f held %.1f %.1f %.1f %.1f %.1f %.1f %.1f gex %.1f %.1f %.1f %.1f %.1f %.1f %.1f"
	             " prop %.1f %.1f %.1f %.1f %.1f",
	             s_tune.height, s_tune.held[0], s_tune.held[1], s_tune.held[2], s_tune.held[3], s_tune.held[4], s_tune.held[5], s_tune.held[6],
	             s_tune.gex[0], s_tune.gex[1], s_tune.gex[2], s_tune.gex[3], s_tune.gex[4], s_tune.gex[5], s_tune.gex[6],
	             s_tune.prop[0], s_tune.prop[1], s_tune.prop[2], s_tune.prop[3], s_tune.prop[4]);
	for (int r = 0; r < HAT_MAX_REGS; r++)
		if (s_models[r].built) place(&s_models[r], s_models[r].vtx);
}

static struct hatModel *ownerOf(struct Model *model)
{
	if (!VrOddjobHat || !model || !model->obj) return NULL;
	for (int r = 0; r < HAT_MAX_REGS; r++)
		if (s_models[r].header && s_models[r].header == model->obj)
			return s_models[r].kind == HAT_THROWN && !isThrown(model) ? NULL : &s_models[r];
	return NULL;
}

int gevrHatNodeGdl(struct ModelNode *node, struct Model *model, void **gdl)
{
	struct hatModel *m = ownerOf(model);
	int i;
	if (!m) return 0;
	for (i = 0; i < m->nnodes && m->nodes[i] != node; i++) {}
	/* the hand's lists are whoever draws them: the cartridge, Bean's glove */
	if (i == m->nnodes || !m->knife[i]) return 0;
	hatLoad();
	if (!s_hat.ok) return 0;
	tuneCheck();
	if (!m->placed && !(m->gex && gexMeasure(m, model))) return 0;
	if (!m->built) build(m);
	if (!m->built) return 0;
	m->used = ++s_clock;
	if (!m->drawn) {
		m->drawn = 1;
		hatLog("oddjob hat: drawn for model %p (header %p, kind %d%s)", (void *)model, (void *)model->obj, m->kind,
		       m->gex ? ", GoldenEye X" : "");
	}
	*gdl = node == m->hatNode ? (void *)m->gdl : NULL;
	return 1;
}

int gevrHatOwnsNode(const void *node)
{
	if (!VrOddjobHat || !s_hat.ok || !node) return 0;
	/* not the thrown kind: a hunting knife shares its lists and keeps Bean's */
	for (int r = 0; r < HAT_MAX_REGS; r++) {
		const struct hatModel *m = &s_models[r];
		if (m->kind == HAT_THROWN || !m->header) continue;
		for (int i = 0; i < m->nnodes; i++)
			if (m->nodes[i] == node && m->knife[i]) return 1;
	}
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
