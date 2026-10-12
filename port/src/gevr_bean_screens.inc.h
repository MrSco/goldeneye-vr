/*
 * GoldenEye's monitor screens under Bean's prop meshes. Ported from DAB
 * (Dab's Mod, MIT, pinned adef1dc2; port/licenses/dabs-mod.txt):
 * gebean.c struct beanscreen, beanFindScreens(), beanScreenBacking(),
 * beanScreenFace(), beanScreenFitPicture(), beanScreenFit(),
 * beanScreenRecesses() and their part of gebeanBuildRigid(), and xblamesh.c
 * xblaMeshSetScreens(), xblaMeshNodeIsLiveScreen() and xblaMeshScreenQuad().
 * Why, in DAB's CLAUDE-notes/ge-bean.md: "The door consoles' lamp: monitor
 * screens under Bean's meshes", "Dam's modem screen and gate switches", "HD
 * effects: Archives' shafts, Dam's modem screen, ..." and "Programmes on
 * Bean's own screens".
 *
 * GoldenEye draws a monitor's programme on the quad of its part 0 to 3 list
 * (propobj.c process_monitor_animation_microcode(), model->obj->Switches[k]).
 * Bean's mesh hangs on one list and every other list of the prop draws
 * nothing, so with Bean on no monitor showed its programme: the door panel's
 * red and green lamp was Bean's grey card. The renderer hands such a list back
 * to the game while the programme has written it (beanLiveScreen()), and the
 * mesh is built round GoldenEye's screens: the placeholder Bean draws its own
 * programme into, and the panes DAB lists in beanVertexDrops[]
 * (gevr_bean_load.c), are laid behind the screen and coloured black; the back
 * of a recess the screen sits in is laid on its plane; and a screen Bean has
 * somewhere else on its case takes the programme's corners.
 */

/* GoldenEye's own skeletons, which DAB's converter tells apart (below) */
extern ModelSkeleton skeleton_cctv, skeleton_eyelid_door, skeleton_iris_door, skeleton_door,
	skeleton_prop_weapon;

/* DAB gebean.c struct beanscreen */
struct beanScreen {
	float origin[3];
	float axis[3][3]; /* along the quad's first edge, across it, and its normal */
	float lo[2];
	float hi[2];
	float size;
	uint8_t quad;    /* its node's list is the screen's quad alone (beanScreenFace()) */
	uint8_t corners; /* which of its corners a vertex of Bean's lies on (beanScreenRecesses()) */
	int8_t part;     /* the model part the screen is (0 to 3) */
	uint8_t fit;     /* Bean's own screen found on it (beanScreenFit()): fitlo/fithi */
	float fitlo[2];
	float fithi[2];
};

/* What gebeanBuildRigid() keeps of the screens while it lays the mesh out,
 * and its log's counts: a vertex is counted once per draw (its mapped[]). */
struct beanScreenBuild {
	struct beanScreen screens[4];
	int numscreens;
	int numbacking;
	int numrecess;
	uint8_t screenfit;
	uint8_t screenrecess;
	float screenquad[4][4][3];
	uint8_t **counted; /* per draw and buffer vertex: 1 backing, 2 recess */
	int ndraws;
};

#define BEAN_SCREEN_BACKING 1
#define BEAN_SCREEN_RECESS  2

static struct beanScreenBuild *s_screenBuild;

/* What the renderer needs of a model's screens (DAB gebeanmats.screenfit,
 * screenrecess and screenquad), by the model file it was built for. */
struct beanScreenInfo {
	ModelFileHeader *owner;
	void *node; /* the list Bean's mesh hangs on */
	uint8_t fit;
	uint8_t recess;
	float quad[4][4][3];
};
#define MAX_SCREEN_INFO 256
static struct beanScreenInfo s_screenInfo[MAX_SCREEN_INFO];
static int s_nscreenInfo;

/* DAB xblamesh.c screenmodel: while set, the model whose screens the
 * programmes have just written (gevrBeanSetScreens()). */
static Model *s_screenModel;

/* DAB beanScreenPlaceholder is gevr_bean_load.c's draw flag. GoldenEye's parts
 * 0 to 3 are the model's Switches[0..3], where its monitor code finds them. */
static int beanFindScreens(const ModelFileHeader *header, struct beanScreen *screens)
{
	int num = 0;

	for (int part = 0; part <= 3; part++) {
		ModelNode *node = header->Switches && part < header->numSwitches ? header->Switches[part] : NULL;
		struct beanScreen *s = &screens[num];
		float c[4][3];
		float e[3];
		float f[3];
		float *n = s->axis[2];
		float len;
		float size;
		Vertex *verts;
		int count, type;
		Gfx *primary;

		dlVerts(node, &verts, &count, &type, &primary);

		if (!verts || count < 4) {
			continue;
		}

		for (int i = 0; i < 4; i++) {
			c[i][0] = verts[i].coord.x;
			c[i][1] = verts[i].coord.y;
			c[i][2] = verts[i].coord.z;
		}

		for (int k = 0; k < 3; k++) {
			e[k] = c[1][k] - c[0][k];
			f[k] = c[2][k] - c[0][k];
		}

		/* the programme is drawn (0 1 2) (0 2 3) with the back culled, so
		 * this is the side it is seen from */
		n[0] = e[1] * f[2] - e[2] * f[1];
		n[1] = e[2] * f[0] - e[0] * f[2];
		n[2] = e[0] * f[1] - e[1] * f[0];
		len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
		size = sqrtf(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);

		if (len < 1.0f || size < 1.0f) {
			continue;
		}

		for (int k = 0; k < 3; k++) {
			n[k] /= len;
			s->axis[0][k] = e[k] / size;
			s->origin[k] = c[0][k];
		}

		s->axis[1][0] = n[1] * s->axis[0][2] - n[2] * s->axis[0][1];
		s->axis[1][1] = n[2] * s->axis[0][0] - n[0] * s->axis[0][2];
		s->axis[1][2] = n[0] * s->axis[0][1] - n[1] * s->axis[0][0];

		for (int a = 0; a < 2; a++) {
			s->lo[a] = s->hi[a] = 0.0f;

			for (int i = 1; i < 4; i++) {
				const float at = (c[i][0] - c[0][0]) * s->axis[a][0] + (c[i][1] - c[0][1]) * s->axis[a][1]
				               + (c[i][2] - c[0][2]) * s->axis[a][2];

				s->lo[a] = at < s->lo[a] ? at : s->lo[a];
				s->hi[a] = at > s->hi[a] ? at : s->hi[a];
			}
		}

		s->size = s->hi[0] - s->lo[0] > s->hi[1] - s->lo[1] ? s->hi[0] - s->lo[0] : s->hi[1] - s->lo[1];
		s->quad = count == 4;
		s->corners = 0;
		s->part = (int8_t)part;
		s->fit = 0;
		num++;
	}

	return num;
}

/* DAB gebean.c beanScreenBacking(): a placeholder vertex (in the model's
 * space) on one of the screens, laid on the plane 2% of the screen's size
 * behind it. */
static int beanScreenBacking(const struct beanScreen *screens, int numscreens, float *pos)
{
	for (int i = 0; i < numscreens; i++) {
		const struct beanScreen *s = &screens[i];
		const float d[3] = { pos[0] - s->origin[0], pos[1] - s->origin[1], pos[2] - s->origin[2] };
		const float u = d[0] * s->axis[0][0] + d[1] * s->axis[0][1] + d[2] * s->axis[0][2];
		const float v = d[0] * s->axis[1][0] + d[1] * s->axis[1][1] + d[2] * s->axis[1][2];
		const float w = d[0] * s->axis[2][0] + d[1] * s->axis[2][1] + d[2] * s->axis[2][2];
		const float margin = s->size * 0.1f;

		if (w >= -s->size * 0.05f && w <= s->size * 0.05f
				&& u >= s->lo[0] - margin && u <= s->hi[0] + margin
				&& v >= s->lo[1] - margin && v <= s->hi[1] + margin) {
			const float back = w + s->size * 0.02f;

			for (int k = 0; k < 3; k++) {
				pos[k] -= s->axis[2][k] * back;
			}

			return 1;
		}
	}

	return 0;
}

/* DAB gebean.c beanScreenFace(): a vertex of Bean's own model within 1% of a
 * screen's size of its plane and edges is the back of the recess the screen
 * sits in, laid on the plane (the programme is drawn over it in the decal z
 * mode, gevrBeanScreenQuad()). Only on a screen whose list is a quad alone
 * with a recess on all four corners. Returns the screen's index + 1. */
static int beanScreenFace(const struct beanScreen *screens, int numscreens, float *pos)
{
	for (int i = 0; i < numscreens; i++) {
		const struct beanScreen *s = &screens[i];
		const float d[3] = { pos[0] - s->origin[0], pos[1] - s->origin[1], pos[2] - s->origin[2] };
		const float u = d[0] * s->axis[0][0] + d[1] * s->axis[0][1] + d[2] * s->axis[0][2];
		const float v = d[0] * s->axis[1][0] + d[1] * s->axis[1][1] + d[2] * s->axis[1][2];
		const float w = d[0] * s->axis[2][0] + d[1] * s->axis[2][1] + d[2] * s->axis[2][2];
		const float margin = s->size * 0.01f;

		if (s->quad && s->corners == 0xf && w >= -margin && w <= margin
				&& u >= s->lo[0] - margin && u <= s->hi[0] + margin
				&& v >= s->lo[1] - margin && v <= s->hi[1] + margin) {
			for (int k = 0; k < 3; k++) {
				pos[k] -= s->axis[2][k] * w;
			}

			return i + 1;
		}
	}

	return 0;
}

/* DAB gebean.c beanScreenFitPicture(): the dark patch of the picture under the
 * quad's middle, flooded out from the texel there, taken back through the
 * triangle's own mapping to the quad's plane. The picture is the draw's
 * decoded stand-in, bottom row first as DAB's beanDecodeTexture() gives it. */
static int beanScreenFitPicture(const struct gevrBeanMesh *mesh, const struct gevrBeanDrawSrc *d,
                                const uint32_t *tri, float (*uvw)[3], float cu, float cv, float *lo, float *hi)
{
	float p[3][2];
	float t[3][2];
	float det;
	float m[2][2];  /* plane to picture */
	float mi[2][2]; /* picture to plane */
	float cs;
	float ct;
	int w;
	int h;
	const uint8_t *rgba;
	uint8_t *seen;
	int *queue;
	int head = 0;
	int tail = 0;
	int x0;
	int y0;
	int minx;
	int maxx;
	int miny;
	int maxy;
	int limit;
	int start;
	int ok = 1;

	for (int k = 0; k < 3; k++) {
		if (tri[k] >= d->nverts || !(d->state[tri[k]] & GEVR_BEAN_V_READ)) {
			return 0;
		}

		p[k][0] = uvw[tri[k]][0];
		p[k][1] = uvw[tri[k]][1];
		t[k][0] = d->uv[tri[k]][0];
		t[k][1] = d->uv[tri[k]][1];
	}

	det = (p[1][0] - p[0][0]) * (p[2][1] - p[0][1]) - (p[2][0] - p[0][0]) * (p[1][1] - p[0][1]);

	if (fabsf(det) < 1e-6f) {
		return 0;
	}

	/* m = [dT1 dT2] * inverse([dP1 dP2]) */
	{
		const float a = (p[2][1] - p[0][1]) / det;
		const float b = -(p[2][0] - p[0][0]) / det;
		const float c = -(p[1][1] - p[0][1]) / det;
		const float e = (p[1][0] - p[0][0]) / det;

		for (int r = 0; r < 2; r++) {
			const float d1 = t[1][r] - t[0][r];
			const float d2 = t[2][r] - t[0][r];

			m[r][0] = d1 * a + d2 * c;
			m[r][1] = d1 * b + d2 * e;
		}
	}

	det = m[0][0] * m[1][1] - m[0][1] * m[1][0];

	if (fabsf(det) < 1e-12f) {
		return 0;
	}

	mi[0][0] = m[1][1] / det;
	mi[0][1] = -m[0][1] / det;
	mi[1][0] = -m[1][0] / det;
	mi[1][1] = m[0][0] / det;

	cs = t[0][0] + m[0][0] * (cu - p[0][0]) + m[0][1] * (cv - p[0][1]);
	ct = t[0][1] + m[1][0] * (cu - p[0][0]) + m[1][1] * (cv - p[0][1]);

	if (d->tex < 0 || d->tex >= mesh->ntex || !gevrBeanTexture(mesh->standin[d->tex], &rgba, &w, &h)
			|| w <= 0 || h <= 0) {
		return 0;
	}

	seen = calloc((size_t)w * h, 1);
	queue = malloc((size_t)w * h * 2 * sizeof(int));

	if (!seen || !queue) {
		free(seen);
		free(queue);
		return 0;
	}

	/* the picture's rows are its bottom first */
	x0 = (int)floorf(cs * w);
	y0 = (int)floorf((1.0f - ct) * h);
	minx = maxx = x0;
	miny = maxy = y0;

	{
		const int wx = ((x0 % w) + w) % w;
		const int wy = ((y0 % h) + h) % h;
		const uint8_t *px = &rgba[(wy * w + wx) * 4];
		int dark = px[0] > px[1] ? px[0] : px[1];

		dark = px[2] > dark ? px[2] : dark;
		start = dark;
	}

	/* a screen is dark; the case round it is not */
	if (start > 96) {
		ok = 0;
	}

	/* Close to the texel's own shade: Silo's screens are 29 with a glass's
	 * glow on them, their bezel grey flecked down to 44 and edged in black,
	 * and a flood to 77 ran out through the flecks round the whole picture */
	limit = start + 16;
	queue[tail++] = x0;
	queue[tail++] = y0;
	seen[(((y0 % h) + h) % h) * w + ((x0 % w) + w) % w] = 1;

	while (ok && head < tail) {
		const int x = queue[head++];
		const int y = queue[head++];
		static const int step[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };

		minx = x < minx ? x : minx;
		maxx = x > maxx ? x : maxx;
		miny = y < miny ? y : miny;
		maxy = y > maxy ? y : maxy;

		/* a patch the width or height of the picture is not a screen on it */
		if (maxx - minx >= w - 1 || maxy - miny >= h - 1) {
			ok = 0;
			break;
		}

		for (int k = 0; k < 4; k++) {
			const int nx = x + step[k][0];
			const int ny = y + step[k][1];
			const int wx = ((nx % w) + w) % w;
			const int wy = ((ny % h) + h) % h;
			const uint8_t *px = &rgba[(wy * w + wx) * 4];
			int lum = px[0] > px[1] ? px[0] : px[1];

			lum = px[2] > lum ? px[2] : lum;

			if (!seen[wy * w + wx] && lum <= limit && tail + 2 <= w * h * 2) {
				seen[wy * w + wx] = 1;
				queue[tail++] = nx;
				queue[tail++] = ny;
			}
		}
	}

	free(seen);
	free(queue);

	if (!ok) {
		return 0;
	}

	lo[0] = lo[1] = 1e30f;
	hi[0] = hi[1] = -1e30f;

	/* the patch's corners, texel edges included, back on the plane */
	for (int c = 0; c < 4; c++) {
		const float s = (float)((c & 1) ? maxx + 1 : minx) / w - t[0][0];
		const float tt = (1.0f - (float)((c & 2) ? maxy + 1 : miny) / h) - t[0][1];

		for (int a = 0; a < 2; a++) {
			const float at = p[0][a] + mi[a][0] * s + mi[a][1] * tt;

			lo[a] = at < lo[a] ? at : lo[a];
			hi[a] = at > hi[a] ? at : hi[a];
		}
	}

	return 1;
}

/* DAB gebean.c beanScreenFit(): Bean's own screen on a monitor whose HD case is
 * not GoldenEye's shape (Facility's wall console, Silo's banks of four). The
 * pane is the triangles of one draw, joined by the vertices they share, that
 * lie in the quad's plane with one of them over the quad's middle; the
 * triangle over the middle alone when those are the whole front of the case;
 * and the dark patch of its picture when that triangle is the front too. Only
 * when it is a screen's size, and not where Bean has the placeholder or a
 * recess. */
static void beanScreenFit(const struct gevrBeanMesh *mesh, const struct beanFit *fit, const char *source,
                          struct beanScreen *screens, int numscreens)
{
	for (int i = 0; i < numscreens; i++) {
		struct beanScreen *sc = &screens[i];
		const float cu = (sc->lo[0] + sc->hi[0]) * 0.5f;
		const float cv = (sc->lo[1] + sc->hi[1]) * 0.5f;
		const float area = (sc->hi[0] - sc->lo[0]) * (sc->hi[1] - sc->lo[1]);
		const float margin = sc->size * 0.01f;
		int placeholder = 0;
		int settled = 0;

		if (!sc->quad || sc->corners == 0xf || area <= 0.0f) {
			continue;
		}

		/* Every draw twice: the placeholder's first, wherever the model has
		 * them, then the rest for the pane */
		for (int k = 0; k < 2 * mesh->ndraws && !settled && !placeholder; k++) {
			const int di = k % mesh->ndraws;
			const struct gevrBeanDrawSrc *d = &mesh->draws[di];
			const uint32_t *tris = d->idx;
			const int numtris = d->ntris;
			float (*uvw)[3];
			uint8_t *state;
			int seed = -1;

			if (d->placeholder != (k < mesh->ndraws)) {
				continue;
			}

			if (!d->state || d->nverts == 0) {
				continue;
			}

			if (numtris <= 0) {
				continue;
			}

			uvw = malloc(d->nverts * sizeof(*uvw));
			state = calloc(d->nverts, 1); /* 1 read, 2 dropped or unreadable, 4 in the pane */

			if (!uvw || !state) {
				free(uvw);
				free(state);
				continue;
			}

			for (uint32_t vi = 0; vi < d->nverts; vi++) {
				float pos[3];

				if ((d->state[vi] & GEVR_BEAN_V_DROPPED) || !(d->state[vi] & GEVR_BEAN_V_READ)) {
					state[vi] = 2;
					continue;
				}

				fitPoint(fit, d->pos[vi], pos);

				for (int a = 0; a < 3; a++) {
					uvw[vi][a] = (pos[0] - sc->origin[0]) * sc->axis[a][0] + (pos[1] - sc->origin[1]) * sc->axis[a][1]
					           + (pos[2] - sc->origin[2]) * sc->axis[a][2];
				}

				state[vi] = 1;
			}

			/* a placeholder anywhere on the screen: the release's own screen,
			 * which beanScreenBacking() sees to */
			if (d->placeholder) {
				for (uint32_t vi = 0; vi < d->nverts && !placeholder; vi++) {
					if (state[vi] == 1 && uvw[vi][2] >= -sc->size * 0.05f && uvw[vi][2] <= sc->size * 0.05f
							&& uvw[vi][0] >= sc->lo[0] - sc->size * 0.1f && uvw[vi][0] <= sc->hi[0] + sc->size * 0.1f
							&& uvw[vi][1] >= sc->lo[1] - sc->size * 0.1f && uvw[vi][1] <= sc->hi[1] + sc->size * 0.1f) {
						placeholder = 1;
					}
				}

				free(uvw);
				free(state);
				continue;
			}

			/* the flat triangle over the quad's middle */
			for (int t = 0; t < numtris && seed < 0; t++) {
				const uint32_t *tv = &tris[t * 3];
				int flat = 1;
				float e0;
				float e1;
				float e2;

				for (int k2 = 0; k2 < 3; k2++) {
					if (tv[k2] >= d->nverts || state[tv[k2]] != 1 || uvw[tv[k2]][2] < -margin || uvw[tv[k2]][2] > margin) {
						flat = 0;
					}
				}

				if (!flat) {
					continue;
				}

				e0 = (uvw[tv[1]][0] - uvw[tv[0]][0]) * (cv - uvw[tv[0]][1]) - (uvw[tv[1]][1] - uvw[tv[0]][1]) * (cu - uvw[tv[0]][0]);
				e1 = (uvw[tv[2]][0] - uvw[tv[1]][0]) * (cv - uvw[tv[1]][1]) - (uvw[tv[2]][1] - uvw[tv[1]][1]) * (cu - uvw[tv[1]][0]);
				e2 = (uvw[tv[0]][0] - uvw[tv[2]][0]) * (cv - uvw[tv[2]][1]) - (uvw[tv[0]][1] - uvw[tv[2]][1]) * (cu - uvw[tv[2]][0]);

				if ((e0 >= 0.0f && e1 >= 0.0f && e2 >= 0.0f) || (e0 <= 0.0f && e1 <= 0.0f && e2 <= 0.0f)) {
					seed = t;
				}
			}

			if (seed >= 0) {
				const int seedtri = seed;
				int grew = 1;
				float lo[2];
				float hi[2];
				float fitarea;

				for (int k2 = 0; k2 < 3; k2++) {
					state[tris[seed * 3 + k2]] |= 4;
				}

				/* every flat triangle sharing a vertex with the pane is the pane */
				while (grew) {
					grew = 0;

					for (int t = 0; t < numtris; t++) {
						const uint32_t *tv = &tris[t * 3];
						int flat = 1;
						int touches = 0;
						int all = 1;

						for (int k2 = 0; k2 < 3; k2++) {
							if (tv[k2] >= d->nverts || !(state[tv[k2]] & 1) || uvw[tv[k2]][2] < -margin || uvw[tv[k2]][2] > margin) {
								flat = 0;
								break;
							}

							touches |= (state[tv[k2]] & 4) != 0;
							all &= (state[tv[k2]] & 4) != 0;
						}

						if (flat && touches && !all) {
							for (int k2 = 0; k2 < 3; k2++) {
								state[tv[k2]] |= 4;
							}

							grew = 1;
						}
					}
				}

				lo[0] = lo[1] = 1e30f;
				hi[0] = hi[1] = -1e30f;

				for (uint32_t vi = 0; vi < d->nverts; vi++) {
					if (state[vi] & 4) {
						for (int a = 0; a < 2; a++) {
							lo[a] = uvw[vi][a] < lo[a] ? uvw[vi][a] : lo[a];
							hi[a] = uvw[vi][a] > hi[a] ? uvw[vi][a] : hi[a];
						}
					}
				}

				fitarea = (hi[0] - lo[0]) * (hi[1] - lo[1]);

				/* A pane joined to its bezel by the vertices they share is the
				 * whole front of the case (Silo's banks of four, 490 square
				 * round a screen 378 by 369): the triangle over the middle is
				 * half of the pane itself, and has the pane's extent */
				if (fitarea > area * 1.5f) {
					float slo[2] = { 1e30f, 1e30f };
					float shi[2] = { -1e30f, -1e30f };

					for (int k2 = 0; k2 < 3; k2++) {
						const uint32_t vi = tris[seedtri * 3 + k2];

						for (int a = 0; a < 2; a++) {
							slo[a] = uvw[vi][a] < slo[a] ? uvw[vi][a] : slo[a];
							shi[a] = uvw[vi][a] > shi[a] ? uvw[vi][a] : shi[a];
						}
					}

					lo[0] = slo[0];
					lo[1] = slo[1];
					hi[0] = shi[0];
					hi[1] = shi[1];
					fitarea = (hi[0] - lo[0]) * (hi[1] - lo[1]);
				}

				/* and a triangle wider than the screen is the case's front,
				 * with the screen painted on it */
				if (fitarea > area * 1.5f
						&& beanScreenFitPicture(mesh, d, &tris[seedtri * 3], uvw, cu, cv, lo, hi)) {
					fitarea = (hi[0] - lo[0]) * (hi[1] - lo[1]);
				}

				if (fitarea >= area * 0.5f && fitarea <= area * 1.5f
						&& fabsf((lo[0] + hi[0]) * 0.5f - cu) <= sc->size * 0.1f
						&& fabsf((lo[1] + hi[1]) * 0.5f - cv) <= sc->size * 0.1f) {
					sc->fit = 1;
					sc->fitlo[0] = lo[0];
					sc->fitlo[1] = lo[1];
					sc->fithi[0] = hi[0];
					sc->fithi[1] = hi[1];
				}

				GEVR_BEAN_LOG("gevr bean: %s screen %d: Bean's pane in draw %d is %.0f..%.0f x %.0f..%.0f, GoldenEye's quad %.0f..%.0f x %.0f..%.0f%s\n",
				              source, sc->part, di, lo[0], hi[0], lo[1], hi[1], sc->lo[0], sc->hi[0], sc->lo[1], sc->hi[1],
				              sc->fit ? ", programme fitted to the pane" : ", not a screen's size: left alone");

				/* the draw with the flat triangle over the middle settles it */
				seed = numtris;
			}

			free(uvw);
			free(state);

			settled = seed == numtris;
		}
	}
}

/* DAB gebean.c beanScreenRecesses(): which of the screens have a recess of
 * Bean's behind them for beanScreenFace(): a vertex of the model on each of
 * the screen's four corners, in its plane. */
static void beanScreenRecesses(const struct gevrBeanMesh *mesh, const struct beanFit *fit,
                               struct beanScreen *screens, int numscreens)
{
	for (int di = 0; di < mesh->ndraws && numscreens > 0; di++) {
		const struct gevrBeanDrawSrc *d = &mesh->draws[di];

		if (!d->state || d->placeholder) {
			continue;
		}

		for (uint32_t vi = 0; vi < d->nverts; vi++) {
			float pos[3];

			if ((d->state[vi] & GEVR_BEAN_V_DROPPED) || !(d->state[vi] & GEVR_BEAN_V_READ)) {
				continue;
			}

			fitPoint(fit, d->pos[vi], pos);

			for (int i = 0; i < numscreens; i++) {
				struct beanScreen *sc = &screens[i];
				const float dd[3] = { pos[0] - sc->origin[0], pos[1] - sc->origin[1], pos[2] - sc->origin[2] };
				const float u = dd[0] * sc->axis[0][0] + dd[1] * sc->axis[0][1] + dd[2] * sc->axis[0][2];
				const float w2 = dd[0] * sc->axis[1][0] + dd[1] * sc->axis[1][1] + dd[2] * sc->axis[1][2];
				const float w = dd[0] * sc->axis[2][0] + dd[1] * sc->axis[2][1] + dd[2] * sc->axis[2][2];
				const float margin = sc->size * 0.01f;

				if (w < -margin || w > margin) {
					continue;
				}

				for (int c = 0; c < 4; c++) {
					const float cu = (c == 1 || c == 2) ? sc->hi[0] : sc->lo[0];
					const float cv = c >= 2 ? sc->hi[1] : sc->lo[1];

					if (fabsf(u - cu) <= margin && fabsf(w2 - cv) <= margin) {
						sc->corners |= 1 << c;
					}
				}
			}
		}
	}
}

/* DAB's converter (tools/geconvert/gemodelconv.py prop_skel()) puts a prop on
 * Perfect Dark's basic skeleton unless it is a CCTV or a windowed door with
 * four switches, an eyelid or iris door, or a held weapon with three; DAB's
 * screens ask for that one (gebeanBuildRigid(): "a monitor is converted on the
 * basic skeleton; a windowed door's glass or a CCTV's lens is a part of a
 * skeleton of its own"). */
static int beanSkelBasic(const ModelFileHeader *header)
{
	const ModelSkeleton *skel = header->Skeleton;

	if ((skel == &skeleton_cctv || skel == &skeleton_door) && header->numSwitches >= 4) {
		return 0;
	}

	if (skel == &skeleton_eyelid_door || skel == &skeleton_iris_door) {
		return 0;
	}

	return !(skel == &skeleton_prop_weapon && header->numSwitches >= 3);
}

/* DAB gebeanBuildRigid() before its draw loop: the screens, their recesses
 * and Bean's own screens, and where the programme's corners go. A prop's
 * mesh and GoldenEye's quads are both in the model's own space on its first
 * matrix (fitPoint()). */
static void beanScreensBuild(struct beanScreenBuild *sb, const ModelFileHeader *header,
                             const struct gevrBeanMesh *mesh, const struct beanFit *fit,
                             const struct beanRigidBinding *binding, ModelNode *primary)
{
	int numparts = 0;

	memset(sb, 0, sizeof(*sb));

	if (!fit) {
		return;
	}

	for (int b = 0; b < mesh->nbones && b < 64; b++) {
		numparts += binding->group[b] != binding->base;
	}

	sb->numscreens = beanFindScreens(header, sb->screens);

	/* a monitor is converted on the basic skeleton; a windowed door's glass
	 * or a CCTV's lens is a part of a skeleton of its own */
	if (mesh->draws && beanSkelBasic(header)) {
		beanScreenRecesses(mesh, fit, sb->screens, sb->numscreens);

		/* a rigid prop's screens and Bean's vertices are both in the model's
		 * own space on its first matrix, which the programme's quad is
		 * drawn in */
		if (numparts == 0 && nodeMatrix(primary) == 0) {
			beanScreenFit(mesh, fit, fit->source, sb->screens, sb->numscreens);
		}
	}

	for (int i = 0; i < sb->numscreens; i++) {
		const struct beanScreen *sc = &sb->screens[i];
		Vertex *verts;
		int count, type;
		Gfx *list;

		if (!sc->fit) {
			continue;
		}

		dlVerts(header->Switches[sc->part], &verts, &count, &type, &list);

		/* GoldenEye's corners, each taken to the same place on Bean's pane */
		for (int c = 0; c < 4; c++) {
			const float d[3] = { verts[c].coord.x - sc->origin[0], verts[c].coord.y - sc->origin[1],
			                     verts[c].coord.z - sc->origin[2] };
			float at[2];

			for (int a = 0; a < 2; a++) {
				const float on = d[0] * sc->axis[a][0] + d[1] * sc->axis[a][1] + d[2] * sc->axis[a][2];
				const float frac = sc->hi[a] > sc->lo[a] ? (on - sc->lo[a]) / (sc->hi[a] - sc->lo[a]) : 0.0f;

				at[a] = sc->fitlo[a] + frac * (sc->fithi[a] - sc->fitlo[a]);
			}

			/* a hundredth of the size in front of Bean's screen, which is
			 * on the quad's plane: the depth test is the programme's */
			for (int k = 0; k < 3; k++) {
				sb->screenquad[sc->part][c][k] = sc->origin[k] + at[0] * sc->axis[0][k] + at[1] * sc->axis[1][k]
				                               + sc->size * 0.01f * sc->axis[2][k];
			}
		}

		sb->screenfit |= 1 << sc->part;
	}

	sb->ndraws = mesh->ndraws;
	sb->counted = mesh->ndraws ? calloc((size_t)mesh->ndraws, sizeof(*sb->counted)) : NULL;
}

/* A vertex counted once per draw, as DAB's mapped[] has it */
static void beanScreenCount(struct beanScreenBuild *sb, const struct gevrBeanMesh *mesh,
                            const struct gevrBeanTriSrc *src, int corner, int what)
{
	const int draw = src->draw;
	const uint32_t vi = src->vi[corner];

	if (!sb->counted || draw < 0 || draw >= sb->ndraws || vi >= mesh->draws[draw].nverts) {
		return;
	}

	if (!sb->counted[draw]) {
		sb->counted[draw] = calloc(mesh->draws[draw].nverts ? mesh->draws[draw].nverts : 1, 1);
	}

	if (sb->counted[draw] && !(sb->counted[draw][vi] & what)) {
		sb->counted[draw][vi] |= (uint8_t)what;
		*(what == BEAN_SCREEN_BACKING ? &sb->numbacking : &sb->numrecess) += 1;
	}
}

/* DAB gebeanBuildRigid()'s draw loop, for one corner of triangle `tri` at pos
 * (fitted): -1 leaves the triangle out, BEAN_SCREEN_BACKING laid behind a
 * screen and drawn black, BEAN_SCREEN_RECESS laid on a screen's plane. */
static int beanScreenVertex(struct beanScreenBuild *sb, const struct gevrBeanMesh *mesh, int tri, int corner,
                            float *pos)
{
	const struct gevrBeanTriSrc *src = mesh->triSrc ? &mesh->triSrc[tri] : NULL;
	const struct gevrBeanDrawSrc *d = src && src->draw >= 0 && src->draw < mesh->ndraws ? &mesh->draws[src->draw] : NULL;
	/* A pane the release draws untextured over a screen it shows a
	 * programme on (the modem's, the door panel's): the screen's backing, as
	 * the consoles' placeholder is, so the release's draw is there and the
	 * programme in front of it, not a grey card */
	const int pane = src && ((src->pane >> corner) & 1);
	const int placeholder = sb->numscreens > 0 && d && d->placeholder;

	if (!src) {
		return 0;
	}

	if (pane && sb->numscreens == 0) {
		return -1;
	}

	if ((placeholder || pane) && beanScreenBacking(sb->screens, sb->numscreens, pos)) {
		beanScreenCount(sb, mesh, src, corner, BEAN_SCREEN_BACKING);
		return BEAN_SCREEN_BACKING;
	}

	if (pane) {
		return -1;
	}

	if (!placeholder) {
		const int face = beanScreenFace(sb->screens, sb->numscreens, pos);

		if (face) {
			beanScreenCount(sb, mesh, src, corner, BEAN_SCREEN_RECESS);
			sb->screenrecess |= 1 << sb->screens[face - 1].part;
			return BEAN_SCREEN_RECESS;
		}
	}

	return 0;
}

/* The last prop's counts, which its log line gives */
static struct {
	int numbacking;
	int numrecess;
	uint8_t screenfit;
	uint8_t screenrecess;
} s_screenCounts;

/* After the draw loop: the log's counts, and what the renderer keeps
 * (DAB mats->screenfit, screenrecess, screenquad) when Bean's mesh is drawn. */
static void beanScreensFinish(struct beanScreenBuild *sb, ModelFileHeader *header, void *primary,
                              const struct beanFit *fit, int emitted)
{
	s_screenCounts.numbacking = sb->numbacking;
	s_screenCounts.numrecess = sb->numrecess;
	s_screenCounts.screenfit = sb->screenfit;
	s_screenCounts.screenrecess = sb->screenrecess;

	if (fit && (sb->numbacking || sb->numrecess || sb->screenfit)) {
		GEVR_BEAN_LOG("gevr bean: %s: %d screen backing, %d screen recess, screens fitted 0x%x\n", fit->source,
		              sb->numbacking, sb->numrecess, sb->screenfit);
	}

	if (emitted && (sb->screenfit || sb->screenrecess) && s_nscreenInfo < MAX_SCREEN_INFO) {
		struct beanScreenInfo *info = &s_screenInfo[s_nscreenInfo++];

		info->owner = header;
		info->node = primary;
		info->fit = sb->screenfit;
		info->recess = sb->screenrecess;
		memcpy(info->quad, sb->screenquad, sizeof(info->quad));
	}

	for (int i = 0; sb->counted && i < sb->ndraws; i++) {
		free(sb->counted[i]);
	}

	free(sb->counted);
	sb->counted = NULL;
}

static void beanScreenInfoForget(const ModelFileHeader *header, uintptr_t start, u32 size)
{
	int kept = 0;

	for (int i = 0; i < s_nscreenInfo; i++) {
		const uintptr_t node = (uintptr_t)s_screenInfo[i].node;

		if (s_screenInfo[i].owner != header && (size == 0 || node < start || node - start >= size)) {
			s_screenInfo[kept++] = s_screenInfo[i];
		}
	}

	s_nscreenInfo = kept;
}

/* DAB xblamesh.c xblaMeshNodeIsLiveScreen(): one of a monitor's screens -
 * parts 0 to 3 - with the list the programme wrote for it this frame in
 * place of its own (model.c modelGetNodeRwData() for a list outside a head). */
static int beanLiveScreen(void *node)
{
	Model *model = s_screenModel;
	ModelFileHeader *header = model ? model->obj : NULL;

	if (!header || !header->Switches || !model->datas || !node) {
		return 0;
	}

	for (int part = 0; part <= 3 && part < header->numSwitches; part++) {
		if (header->Switches[part] == node) {
			ModelNode *n = node;
			const ModelRwData_DisplayList_CollisionRecord *rw;

			if (nodeOp(n) != MODELNODE_OPCODE_DLCOLLISION || !n->Data) {
				return 0;
			}

			rw = (const ModelRwData_DisplayList_CollisionRecord *)&model->datas[n->Data->DisplayListCollisions.RwDataIndex];

			return rw->gdl && rw->gdl != n->Data->DisplayListCollisions.Primary;
		}
	}

	return 0;
}

/* DAB xblamesh.c xblaMeshSetScreens() */
void gevrBeanSetScreens(struct Model *model)
{
	s_screenModel = model;
}

/* DAB xblamesh.c xblaMeshScreenQuad(): moves the corners of a monitor's screen
 * the programme is writing onto the screen of the model it will be drawn as,
 * when that is Bean's mesh and its screen is not where GoldenEye's quad is.
 * The texture coordinates are the programme's and stay. 1 when the screen is
 * on the back of Bean's recess, which lies in the same plane (beanScreenFace()). */
int gevrBeanScreenQuad(struct Model *model, void *node, struct Vertex *vertices)
{
	ModelFileHeader *header = model ? model->obj : NULL;
	const struct beanScreenInfo *info = NULL;
	struct beanSwap *drawn;
	int part = -1;

	if (!header || !header->Switches || !node || !vertices) {
		return 0;
	}

	for (int p = 0; p <= 3 && p < header->numSwitches; p++) {
		if (header->Switches[p] == node) {
			part = p;
			break;
		}
	}

	if (part < 0) {
		return 0;
	}

	for (int i = 0; i < s_nscreenInfo && !info; i++) {
		if (s_screenInfo[i].owner == header) {
			info = &s_screenInfo[i];
		}
	}

	/* the test the renderer draws Bean's model by: the list the mesh hangs on */
	drawn = info ? activeSwap(info->node) : NULL;

	if (!drawn || drawn->owner != header || drawn->ours == s_blank) {
		return 0;
	}

	if (info->fit & (1 << part)) {
		for (int c = 0; c < 4; c++) {
			vertices[c].coord.x = (s16)floorf(info->quad[part][c][0] + 0.5f);
			vertices[c].coord.y = (s16)floorf(info->quad[part][c][1] + 0.5f);
			vertices[c].coord.z = (s16)floorf(info->quad[part][c][2] + 0.5f);
		}
	}

	return (info->recess & (1 << part)) != 0;
}
