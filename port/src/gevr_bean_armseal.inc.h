/* Bean's off-hand arm (new/char/suitlfhand), sealed (user, 2026-11-18: "patch
 * the left offhand for the bean model ... the arm opening is hollow, so that
 * should be sealed"). The release's watch arm was made to be seen from one
 * side, as the cartridge's: its hand is whole, but the bare forearm ends in an
 * open rim, every outfit's sleeve is open at the elbow, and two sleeves (the
 * dinner jacket's cuff and one more) are open along their length. The arm is
 * drawn with its backs culled, so through each opening the far wall is not
 * there either: hollow.
 *
 * Nothing is authored here. Each picture of the arm (the hand, each outfit's
 * sleeve) is taken on its own, its open rims found, and:
 *  - a rim that rings the arm at the piece's far end (the elbow's, or the
 *    forearm's) is closed with a fan to its middle, a little darker;
 *  - a rim that runs along the arm (a sleeve open down its length) is laced
 *    shut from side to side, from a little way off the wrist to its far end;
 *  - a rim at the wrist end, where the hand comes through, and the small rims
 *    of buttons and the watch are left.
 * A new triangle's corners are the rim's own (place, picture, shade, bones),
 * and it takes each rim edge the other way round from the triangle that owns
 * it, so it faces as the surface it continues does.
 */
#define ARMSEAL_SMALL 70.0f      /* a rim no bigger than this across is a button's */
#define ARMSEAL_LONG 0.40f       /* a rim over this much of its piece's length runs along the arm */
#define ARMSEAL_WRIST 0.10f      /* lacing starts this far up a long rim from its wrist end */
#define ARMSEAL_MAX_RIM 512
#define ARMSEAL_FIRST_TEX 3
#define ARMSEAL_LAST_TEX 8

struct armSealCorner { int key[3], id; };
struct armSealEdge { int lo, hi, from, to, corner; };

static int armSealCornerCmp(const void *a, const void *b)
{
	const struct armSealCorner *p = a, *q = b;
	for (int k = 0; k < 3; k++)
		if (p->key[k] != q->key[k])
			return p->key[k] < q->key[k] ? -1 : 1;
	return p->id - q->id;
}

static int armSealEdgeCmp(const void *a, const void *b)
{
	const struct armSealEdge *p = a, *q = b;
	return p->lo != q->lo ? (p->lo < q->lo ? -1 : 1) : p->hi != q->hi ? (p->hi < q->hi ? -1 : 1) : 0;
}

/* corner `to` of out takes corner (id % 3) of triangle (id / 3) */
static void armSealCopy(const struct gevrBeanMesh *mesh, int id, struct gevrBeanTri *out, int to)
{
	const struct gevrBeanTri *t = &mesh->tris[id / 3];
	const int c = id % 3;
	memcpy(&out->p[to * 3], &t->p[c * 3], 3 * sizeof(float));
	memcpy(&out->n[to * 3], &t->n[c * 3], 3 * sizeof(float));
	memcpy(&out->uv[to * 2], &t->uv[c * 2], 2 * sizeof(float));
	memcpy(&out->rgba[to * 4], &t->rgba[c * 4], 4);
	memcpy(&out->bone[to * 4], &t->bone[c * 4], 4);
	memcpy(&out->weight[to * 4], &t->weight[c * 4], 4);
}

static int armSealPush(struct gevrBeanMesh *mesh, int a, int b, int c)
{
	struct gevrBeanTri t;
	if (mesh->ntris >= mesh->tricaps) {
		const int caps = mesh->tricaps + 1024;
		struct gevrBeanTri *grown = realloc(mesh->tris, (size_t)caps * sizeof(*grown));
		if (!grown)
			return 0;
		mesh->tris = grown;
		mesh->tricaps = caps;
	}
	t = mesh->tris[a / 3];
	armSealCopy(mesh, a, &t, 0);
	armSealCopy(mesh, b, &t, 1);
	armSealCopy(mesh, c, &t, 2);
	mesh->tris[mesh->ntris++] = t;
	return 1;
}

/* A rim ringing the arm: a fan from its middle, each edge the other way round. */
static int armSealFan(struct gevrBeanMesh *mesh, const int *rim, int n)
{
	float p[3] = { 0, 0, 0 }, nrm[3] = { 0, 0, 0 }, uv[2] = { 0, 0 }, shade[3] = { 0, 0, 0 };
	int made = 0;
	for (int i = 0; i < n; i++) {
		const struct gevrBeanTri *t = &mesh->tris[rim[i] / 3];
		const int c = rim[i] % 3;
		for (int k = 0; k < 3; k++) {
			p[k] += t->p[c * 3 + k] / n;
			nrm[k] += t->n[c * 3 + k] / n;
			shade[k] += t->rgba[c * 4 + k] / (float)n;
		}
		uv[0] += t->uv[c * 2] / n;
		uv[1] += t->uv[c * 2 + 1] / n;
	}
	for (int i = 0; i < n; i++) {
		struct gevrBeanTri *t;
		if (!armSealPush(mesh, rim[(i + 1) % n], rim[i], rim[i]))
			return made;
		/* its third corner is the middle: inside the arm, so a little darker */
		t = &mesh->tris[mesh->ntris - 1];
		memcpy(&t->p[6], p, sizeof(p));
		memcpy(&t->n[6], nrm, sizeof(nrm));
		memcpy(&t->uv[4], uv, sizeof(uv));
		for (int k = 0; k < 3; k++)
			t->rgba[8 + k] = (uint8_t)(shade[k] * 0.7f);
		made++;
	}
	return made;
}

/* A rim running along the arm (axis y): its two sides laced together from a
 * little off its wrist end to its far end, each edge the other way round. */
static int armSealLace(struct gevrBeanMesh *mesh, const int *rim, int n, const float *y)
{
	int low = 0, high = 0, na = 0, nb = 0, i = 0, j = 0, made = 0;
	int a[ARMSEAL_MAX_RIM], b[ARMSEAL_MAX_RIM];
	float from;
	for (int k = 1; k < n; k++) {
		if (y[k] < y[low]) low = k;
		if (y[k] > y[high]) high = k;
	}
	from = y[low] + (y[high] - y[low]) * ARMSEAL_WRIST;
	/* up one side as the rim runs, and up the other against it */
	for (int k = low; ; k = (k + 1) % n) {
		if (y[k] >= from || k == high) a[na++] = k;
		if (k == high) break;
	}
	for (int k = low; ; k = (k + n - 1) % n) {
		if (y[k] >= from || k == high) b[nb++] = k;
		if (k == high) break;
	}
	while (i < na - 1 || j < nb - 1) {
		const int stepA = i < na - 1 && (j == nb - 1 || y[a[i + 1]] <= y[b[j + 1]]);
		if (stepA) {
			if (!armSealPush(mesh, rim[a[i + 1]], rim[a[i]], rim[b[j]]))
				return made;
			i++;
		} else {
			if (!armSealPush(mesh, rim[b[j]], rim[b[j + 1]], rim[a[i]]))
				return made;
			j++;
		}
		made++;
	}
	return made;
}

/*
 * The off-hand arm a little smaller, its watch as it was (user, 2026-11-18:
 * "keep the watch the same size and just shrink the arm wrist and hand ...
 * that'll also make the watch band fit over the wrist better without clipping
 * through it"). The hand's and the sleeves' corners move towards the middle of
 * the watch (its pictures: every one that is not the arm's), so the wrist draws
 * in under the band and the watch stays on it.
 */
#define ARMSEAL_ARM_SIZE 0.92f

static void beanShrinkWatchArm(struct gevrBeanMesh *mesh)
{
	double middle[3] = { 0, 0, 0 };
	int corners = 0;
	for (int i = 0; i < mesh->ntris; i++) {
		const struct gevrBeanTri *t = &mesh->tris[i];
		if (t->tex >= ARMSEAL_FIRST_TEX && t->tex <= ARMSEAL_LAST_TEX)
			continue;
		for (int c = 0; c < 9; c++)
			middle[c % 3] += t->p[c];
		corners += 3;
	}
	if (!corners)
		return;
	for (int k = 0; k < 3; k++)
		middle[k] /= corners;
	for (int i = 0; i < mesh->ntris; i++) {
		struct gevrBeanTri *t = &mesh->tris[i];
		if (t->tex < ARMSEAL_FIRST_TEX || t->tex > ARMSEAL_LAST_TEX)
			continue;
		for (int c = 0; c < 9; c++)
			t->p[c] = (float)(middle[c % 3] + (t->p[c] - middle[c % 3]) * ARMSEAL_ARM_SIZE);
	}
}

/* The triangles added, for the pictures firstTex to lastTex. */
static int armSealPieces(struct gevrBeanMesh *mesh, int firstTex, int lastTex)
{
	const int ntris = mesh->ntris;
	struct armSealCorner *corners = malloc((size_t)ntris * 3 * sizeof(*corners));
	struct armSealEdge *edges = malloc((size_t)ntris * 3 * sizeof(*edges));
	int *weld = malloc((size_t)ntris * 3 * sizeof(int));
	int *next = malloc((size_t)ntris * 3 * sizeof(int));
	int *source = malloc((size_t)ntris * 3 * sizeof(int));
	int added = 0;
	if (!corners || !edges || !weld || !next || !source)
		goto done;
	for (int tex = firstTex; tex <= lastTex && tex < mesh->ntex; tex++) {
		float lowY = 1e30f, highY = -1e30f;
		int n = 0, ne = 0;
		for (int i = 0; i < ntris; i++) {
			if (mesh->tris[i].tex != tex)
				continue;
			for (int c = 0; c < 3; c++, n++) {
				const float *p = &mesh->tris[i].p[c * 3];
				for (int k = 0; k < 3; k++)
					corners[n].key[k] = (int)lroundf(p[k] * 64.0f);
				corners[n].id = i * 3 + c;
				if (p[1] < lowY) lowY = p[1];
				if (p[1] > highY) highY = p[1];
			}
		}
		if (n < 9 || highY - lowY < 1.0f)
			continue;
		/* corners at one place are one point: the first of them */
		qsort(corners, (size_t)n, sizeof(*corners), armSealCornerCmp);
		for (int i = 0, first = 0; i < n; i++) {
			if (memcmp(corners[i].key, corners[first].key, sizeof(corners[i].key)))
				first = i;
			weld[corners[i].id] = corners[first].id;
		}
		for (int i = 0; i < ntris; i++) {
			if (mesh->tris[i].tex != tex)
				continue;
			for (int c = 0; c < 3; c++) {
				const int from = weld[i * 3 + c], to = weld[i * 3 + (c + 1) % 3];
				if (from == to)
					continue;
				edges[ne].lo = from < to ? from : to;
				edges[ne].hi = from < to ? to : from;
				edges[ne].from = from;
				edges[ne].to = to;
				edges[ne].corner = i * 3 + c;
				ne++;
			}
		}
		qsort(edges, (size_t)ne, sizeof(*edges), armSealEdgeCmp);
		for (int i = 0; i < n; i++)
			next[corners[i].id] = -1;
		/* an edge one triangle has is a rim's; a point two rims pass through (-2) ends both */
		for (int i = 0; i < ne; ) {
			int same = i + 1;
			while (same < ne && !armSealEdgeCmp(&edges[i], &edges[same]))
				same++;
			if (same == i + 1) {
				next[edges[i].from] = next[edges[i].from] == -1 ? edges[i].to : -2;
				source[edges[i].from] = edges[i].corner;
			}
			i = same;
		}
		for (int i = 0; i < n; i++) {
			int rim[ARMSEAL_MAX_RIM], count = 0, at = corners[i].id, closed = 0;
			float y[ARMSEAL_MAX_RIM], lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f }, across;
			if (weld[at] != at || next[at] < 0)
				continue;
			while (count < ARMSEAL_MAX_RIM && next[at] >= 0) {
				const int to = next[at];
				const float *p = &mesh->tris[source[at] / 3].p[(source[at] % 3) * 3];
				rim[count] = source[at];
				y[count++] = p[1];
				for (int k = 0; k < 3; k++) {
					if (p[k] < lo[k]) lo[k] = p[k];
					if (p[k] > hi[k]) hi[k] = p[k];
				}
				next[at] = -3;   /* walked */
				at = to;
				if (at == corners[i].id) {
					closed = 1;
					break;
				}
			}
			if (!closed || count < 3)
				continue;
			across = hi[0] - lo[0] > hi[2] - lo[2] ? hi[0] - lo[0] : hi[2] - lo[2];
			if (across < ARMSEAL_SMALL && hi[1] - lo[1] < ARMSEAL_SMALL)
				continue;   /* a button's, the watch's */
			if (hi[1] - lo[1] > (highY - lowY) * ARMSEAL_LONG)
				added += armSealLace(mesh, rim, count, y);
			else if ((lo[1] + hi[1]) * 0.5f > (lowY + highY) * 0.5f)
				added += armSealFan(mesh, rim, count);   /* the far end's; the wrist's stays open */
		}
	}
done:
	free(corners);
	free(edges);
	free(weld);
	free(next);
	free(source);
	return added;
}

/* the arm's own pictures: its hand (3) and each outfit's sleeve (4 to 8, DAB's
 * char/suitlfhand order, watchTriangleWanted()); not the watch's */
static int beanSealWatchArm(struct gevrBeanMesh *mesh)
{
	beanShrinkWatchArm(mesh);
	return armSealPieces(mesh, ARMSEAL_FIRST_TEX, ARMSEAL_LAST_TEX);
}
