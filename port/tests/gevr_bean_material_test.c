/*
 * Bean materials on the player's own XBLA files (test_bean_material.py):
 * - an opaque face marked as a decal has a face of its own plane under it
 *   (the wrist watch, the laser watch, the Spectre, the military truck; the
 *   old bucket test marked 521 of the watch's faces with nothing under them,
 *   and a decal draws only over depth already there, so the watch showed
 *   through),
 * - Nintendo's logo has no translucent face (its blended pass at material
 *   alpha 255 drew two thirds of it as glass),
 * - no Dam face is cleared by its material (the reservoir's alpha 0 made
 *   20,000 faces invisible; DAB never applies it to a level).
 *
 * usage: material <files folder>
 */
#include "gevr_bean_int.h"
int gevrBeanTextureScroll(const void *, float, float [2]);
int gevrBeanTextureTransform(const void *, float, float [6]);
const void *gevrBeanTextureFrame(const void *, float);
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void videoFreeCachedTexture(const void *addr) { (void)addr; }

static struct gevrBeanMesh *load(const char *files, const char *source)
{
	char path[512];
	struct gevrBeanMesh *mesh = NULL;

	snprintf(path, sizeof(path), "%s/new/%s/default.bin", files, source);
	if (!gevrBeanLoad(path, 1, &mesh) || !mesh) {
		fprintf(stderr, "cannot load %s\n", path);
		exit(2);
	}
	return mesh;
}

static void sub(const float *a, const float *b, float *o) { for (int k = 0; k < 3; k++) o[k] = a[k] - b[k]; }
static float dot(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void cross(const float *a, const float *b, float *o)
{
	o[0] = a[1] * b[2] - a[2] * b[1];
	o[1] = a[2] * b[0] - a[0] * b[2];
	o[2] = a[0] * b[1] - a[1] * b[0];
}

/* unit normal and area */
static float normal(const float *p, float *n)
{
	float e1[3], e2[3];
	sub(p + 3, p, e1);
	sub(p + 6, p, e2);
	cross(e1, e2, n);
	float len = sqrtf(dot(n, n));
	if (len <= 0.0f)
		return 0.0f;
	for (int k = 0; k < 3; k++)
		n[k] /= len;
	return len * 0.5f;
}

/* q (on the plane) inside triangle p, with a little slack */
static int inside(const float *p, const float *n, const float *q)
{
	for (int e = 0; e < 3; e++) {
		float edge[3], to[3], c[3];
		sub(p + ((e + 1) % 3) * 3, p + e * 3, edge);
		sub(q, p + e * 3, to);
		cross(edge, to, c);
		if (dot(c, n) < -1e-3f * dot(edge, edge))
			return 0;
	}
	return 1;
}

/* opaque decal faces with no face of their plane under them */
static int baseless(const struct gevrBeanMesh *m)
{
	int count = 0;
	for (int i = 0; i < m->ntris; i++) {
		const struct gevrBeanTri *t = &m->tris[i];
		float ni[3], c[3];
		if (!(t->flags & GEVR_BEAN_F_DECAL) || t->alpha != 255 || normal(t->p, ni) <= 0.0f)
			continue;
		for (int k = 0; k < 3; k++)
			c[k] = (t->p[k] + t->p[3 + k] + t->p[6 + k]) / 3.0f;
		int found = 0;
		for (int j = 0; j < m->ntris && !found; j++) {
			const struct gevrBeanTri *u = &m->tris[j];
			float nj[3], area = normal(u->p, nj);
			if (j == i || (u->flags & GEVR_BEAN_F_DECAL) || area <= 0.0f || fabsf(dot(ni, nj)) < 0.9f)
				continue;
			float tol = 0.01f * sqrtf(area) + 1e-3f, ok = 1;
			for (int v = 0; v < 3 && ok; v++) {
				float d[3];
				sub(t->p + v * 3, u->p, d);
				ok = fabsf(dot(d, nj)) <= tol;
			}
			if (!ok)
				continue;
			/* its middle, or a corner drawn a tenth toward the middle, over the base */
			for (int v = -1; v < 3 && !found; v++) {
				float q[3], d[3];
				for (int k = 0; k < 3; k++)
					q[k] = v < 0 ? c[k] : t->p[v * 3 + k] * 0.9f + c[k] * 0.1f;
				sub(q, u->p, d);
				float h = dot(d, nj);
				for (int k = 0; k < 3; k++)
					q[k] -= h * nj[k];
				found = inside(u->p, nj, q);
			}
		}
		count += !found;
	}
	return count;
}

int main(int argc, char **argv)
{
	static const char *const rigid[] = { "char/suitlfhand", "gun/watchlaser", "gun/spectre", "prop/miltruck" };
	int bad = 0;

	if (argc < 2) {
		return 2;
	}
	for (unsigned r = 0; r < sizeof(rigid) / sizeof(rigid[0]); r++) {
		struct gevrBeanMesh *m = load(argv[1], rigid[r]);
		int decals = 0, n = baseless(m);
		for (int i = 0; i < m->ntris; i++)
			decals += (m->tris[i].flags & GEVR_BEAN_F_DECAL) != 0;
		printf("%s: %d triangles, %d decal, %d opaque decal with nothing under it\n", rigid[r], m->ntris, decals, n);
        bad |= n != 0;
        if (r == 0) {
            int glass = 0, grey = 0, light = 0;
            for (int i = 0; i < m->ntris; i++) {
                grey += m->tris[i].tex == 10; light += m->tris[i].tex == 9;
                if (m->tris[i].tex == 11) { glass++; bad |= m->tris[i].alpha == 255; }
            }
            const uint8_t *pic = NULL; int w = 0, h = 0;
            bad |= !gevrBeanTexture(m->standin[11], &pic, &w, &h);
            if (w*h > 0) for (int i=0; i<w*h; i++) bad |= pic[i*4] || pic[i*4+1] || pic[i*4+2] || pic[i*4+3]>=128;
            bad |= grey || glass != 135 || light != 2;
            printf("Watch: %d translucent black faces, %d grey shader-input faces, %d light faces\n", glass, grey, light);
        }
		gevrBeanMeshFree(m);
	}
	{
		struct gevrBeanMesh *m = load(argv[1], "prop/nintendologo");
		int xlu = 0;
		for (int i = 0; i < m->ntris; i++)
			xlu += m->tris[i].alpha < 255;
		printf("prop/nintendologo: %d triangles, %d translucent\n", m->ntris, xlu);
		bad |= xlu != 0;
		gevrBeanMeshFree(m);
	}
	{
		struct gevrBeanMesh *m = load(argv[1], "background/dam");
		int cleared = 0;
		for (int i = 0; i < m->ntris; i++) {
			const struct gevrBeanTri *t = &m->tris[i];
			int seen = t->rgba[3] | t->rgba[7] | t->rgba[11];
			cleared += t->alpha == 0 && seen;
		}
		printf("background/dam: %d triangles, %d cleared by their material\n", m->ntris, cleared);
        bad |= cleared != 0;
        float scroll[2];
        bad |= !gevrBeanTextureScroll(m->standin[71], 1.0f, scroll);
        bad |= fabsf(scroll[0] - .5f)>1e-6f || fabsf(scroll[1]+.5f)>1e-6f;
        gevrBeanTextureScroll(m->standin[71], 2.0f, scroll);
        bad |= fabsf(scroll[0])>1e-6f || fabsf(scroll[1])>1e-6f;
        bad |= gevrBeanTextureScroll(m->standin[0], 1.0f, scroll);
        int paint=0;
        for(int i=0;i<m->ntris;i++) if(m->tris[i].tex==72) {
            paint++; bad |= !(m->tris[i].flags & GEVR_BEAN_F_DECAL) || m->tris[i].alpha==255;
        }
        bad |= paint!=4;
        int dashes=0;
        for(int i=0;i<m->ntris;i++)if(m->tris[i].tex==58 || m->tris[i].tex==61) {
            dashes++;bad |= !(m->tris[i].flags & GEVR_BEAN_F_DECAL);
        }
        bad |= dashes!=46;
        printf("Dam: %d straight/curved dashed-road faces retain decal modes\n",dashes);
        printf("Dam: reservoir scrolls at 0.5 repeats/s on game time; %d vertex-alpha road-paint faces retain decals\n",paint);
		gevrBeanMeshFree(m);
	}
    {
        struct gevrBeanMesh *m = load(argv[1], "background/complex");
        int reflection = 0, water = 0;
        for (int i = 0; i < m->ntris; i++) {
            reflection += m->tris[i].tex == 22;
            if (m->tris[i].tex == 23) { water++; bad |= m->tris[i].alpha == 255; }
        }
        bad |= reflection || water != 4;
        const void *f0=gevrBeanTextureFrame(m->standin[24],0);
        const void *f1=gevrBeanTextureFrame(m->standin[24],.5f);
        bad |= f0==f1 || gevrBeanTextureFrame(m->standin[24],1)!=f0;
        const uint8_t *p0=NULL,*p1=NULL; int w0,h0,w1,h1;
        bad |= !gevrBeanTexture(f0,&p0,&w0,&h0) || !gevrBeanTexture(f1,&p1,&w1,&h1);
        if(p0 && p1) bad |= w0!=w1 || h0!=h1 || !memcmp(p0,p1,(size_t)w0*h0*4);
        printf("Complex beacon: two distinct cached frames alternate every 0.5 seconds\n");
        printf("Complex: %d translucent water faces on surface 23, %d faces incorrectly using reflection 22\n", water, reflection);
        gevrBeanMeshFree(m);
    }
    {
        struct gevrBeanMesh *m = load(argv[1], "prop/helicopter");
        float offset[2]; int scrolling = 0;
        for (int t = 0; t < m->ntex; t++) if (gevrBeanTextureScroll(m->standin[t], 1, offset)) {
            scrolling++;
            bad |= fabsf(offset[0] - .9f) > 1e-6f || fabsf(offset[1]) > 1e-6f;
        }
        bad |= scrolling != 1;
        float uv[6]; bad |= !gevrBeanTextureTransform(m->standin[0],3.6f,uv);
        bad |= fabsf(uv[0])>1e-5f || fabsf(uv[1]+1)>1e-5f; /* 25 degrees/s, inverse sample. */
        printf("Helicopter: %d texture scrolls at the authored 0.9 repeats/s\n", scrolling);
        gevrBeanMeshFree(m);
    }
    {
        struct gevrBeanMesh *m=load(argv[1],"prop/bridgeconsole1a");
        int rotating=0; float uv[6];
        for(int t=0;t<m->ntex;t++) if(gevrBeanTextureTransform(m->standin[t],1.5f,uv)) {
            rotating++; bad |= fabsf(uv[0])>1e-5f || fabsf(uv[1]-1)>1e-5f;
        }
        bad |= rotating!=1;
        printf("Frigate radar: clockwise 60 degrees/s around its centre\n");
        gevrBeanMeshFree(m);
    }
    {
        struct gevrBeanMesh *m=load(argv[1],"prop/hathelmet");
        int mapped=0;
        for(int i=0;i<m->ntris;i++) {
            const struct gevrBeanTri *t=m->tris+i;
            mapped+=t->reflection!=NULL;
            bad |= t->reflectionDiffuse!=2 || t->reflectionColour!=0x43434343u || t->reflectionAlphaBase!=255;
        }
        const uint8_t *p=NULL;int w=0,h=0;
        bad |= mapped!=744 || !gevrBeanTexture(m->reflection[0],&p,&w,&h);
        if(p)for(int i=0;i<w*h;i++)for(int k=0;k<4;k++)bad |= p[i*4+k]>89;
        printf("Helmet: %d coloured normal-reflection faces; RGB and alpha retain c12/c13/c14\n",mapped);
        gevrBeanMeshFree(m);
    }
    {
        struct gevrBeanMesh *m=load(argv[1],"prop/gastank");
        int mapped=0,white=0;
        for(int t=0;t<m->ntex;t++) if(m->reflection[t]) {
            mapped++;
            const uint8_t *p=NULL;int w=0,h=0;
            bad |= !gevrBeanTexture(m->reflection[t],&p,&w,&h);
            if(p) {
                int lo=255,hi=0;
                for(int i=0;i<w*h;i++) for(int c=0;c<3;c++) {
                    if(p[i*4+c]<lo)lo=p[i*4+c];if(p[i*4+c]>hi)hi=p[i*4+c];
                    bad |= p[i*4+c]>143;
                }
                bad |= hi-lo<20;
            }
            for(int i=0;i<m->ntris;i++) if(m->tris[i].tex==t)
                for(int k=0;k<3;k++) white |= m->tris[i].rgba[k*4]==255;
        }
        bad |= mapped==0 || !white;
        printf("Gas tank: %d normal-dependent spot/land maps; diffuse retains c13 white\n",mapped);
        gevrBeanMeshFree(m);
    }
    {
        const char *families[]={"prop/window","prop/glassware1","head/headbike"};
        for(unsigned f=0;f<sizeof(families)/sizeof(families[0]);f++) {
            struct gevrBeanMesh *m=load(argv[1],families[f]);int reflected=0,soft=0;
            for(int i=0;i<m->ntris;i++)if(m->tris[i].reflection) {
                reflected++;const uint8_t *pixels=NULL;int w=0,h=0;
                gevrBeanTexture(m->standin[m->tris[i].tex],&pixels,&w,&h);
                soft+=m->tris[i].alpha<255 || (pixels && pixels[3]<255);
                /* the tower window is glass at c12's 0.56, not c13's 1 */
                if(f==0)bad |= m->tris[i].alpha!=142;
                const uint8_t *map=NULL;int mw=0,mh=0;
                bad |= !gevrBeanTexture(m->tris[i].reflection,&map,&mw,&mh);
                if(map)for(int v=0;v<mw*mh;v++)
                    bad |= map[v*4+3]!=(m->tris[i].reflectionDiffuse==2 ? 89 : 255);
            }
            bad |= reflected==0;
            if(f==0) {
                const uint8_t *raw=NULL,*map=NULL;int w=0,h=0,mw=0,mh=0;
                bad |= !gevrBeanTexture(m->standin[0],&raw,&w,&h);
                bad |= !gevrBeanTexture(m->reflection[1],&map,&mw,&mh);
                bad |= w!=mw || h!=mh;
                if(raw && map && w==mw && h==mh)for(int v=0;v<w*h;v++)for(int c=0;c<3;c++)
                    bad |= abs((int)map[v*4+c]-(int)(raw[v*4+c]*142.0f/255+.5f))>1;
            }
            printf("%s: %d normal-map reflection faces, %d soft-alpha faces\n",families[f],reflected,soft);
            gevrBeanMeshFree(m);
        }
    }
    {
        struct gevrBeanMesh *m=load(argv[1],"prop/cctv");int coupled=0;
        for(int i=0;i<m->ntris;i++)if(m->tris[i].reflectionDiffuse && m->tris[i].reflection) {
            coupled++;bad |= m->tris[i].alpha!=255;
        }
        bad |= coupled==0;
        printf("CCTV: %d diffuse-tinted normal-reflection faces, opaque c13 alpha\n",coupled);
        gevrBeanMeshFree(m);
    }
    {
        /* GoldenEye's monitor screens (gevr_bean_screens.inc.h): the props'
         * draws as DAB reads them, DAB's beanVertexDrops[] by .gpu offset and
         * range, and its placeholder _0x008C4635 - on the door consoles,
         * Frigate's bridge consoles 1a/2a/3a/3b, Silo's tuning console and its
         * console2/3 (CLAUDE-notes/ge-bean.md), nowhere else of these. */
        static const struct { const char *source; int placeholders; unsigned vboff, first, last; } screens[] = {
            {"prop/doorpanel", 0, 1160, 0, 7}, {"prop/modembox", 0, 1264, 0, 3},
            {"prop/console2", 1, 0, 118, 133}, {"prop/console3", 1, 0, 118, 133},
            {"prop/console1", 0, 0, 1, 0}, {"prop/doorconsole", 1, 0, 1, 0},
            {"prop/bridgeconsole1a", 1, 0, 1, 0}, {"prop/bridgeconsole2a", 1, 0, 1, 0},
            {"prop/bridgeconsole3a", 1, 0, 1, 0}, {"prop/bridgeconsole3b", 1, 0, 1, 0},
            {"prop/tuningconsole1", 1, 0, 1, 0}, {"prop/consolesev2b", 0, 0, 1, 0}, {"prop/tv1", 0, 0, 1, 0},
        };
        for (unsigned s = 0; s < sizeof(screens) / sizeof(screens[0]); s++) {
            struct gevrBeanMesh *m = load(argv[1], screens[s].source);
            int placeholders = 0, dropped = 0, wrong = 0, panes = 0;
            bad |= !m->draws || !m->triSrc;
            for (int d = 0; m->draws && d < m->ndraws; d++) {
                const struct gevrBeanDrawSrc *ds = &m->draws[d];
                int shared = 0;
                placeholders += ds->placeholder;
                for (int e = 0; e < d; e++)
                    shared |= m->draws[e].state && m->draws[e].vboff == ds->vboff;
                for (unsigned v = 0; !shared && ds->state && v < ds->nverts; v++) {
                    const int on = ds->vboff == screens[s].vboff && v >= screens[s].first && v <= screens[s].last;
                    dropped += (ds->state[v] & GEVR_BEAN_V_DROPPED) != 0;
                    wrong += !(ds->state[v] & GEVR_BEAN_V_DROPPED) != !on;
                }
            }
            /* every triangle is its draw's own vertices, pane corners marked */
            for (int t = 0; m->triSrc && t < m->ntris; t++) {
                const struct gevrBeanTriSrc *src = &m->triSrc[t];
                const struct gevrBeanDrawSrc *ds = &m->draws[src->draw];
                panes += src->pane != 0;
                for (int c = 0; c < 3; c++) {
                    wrong += src->vi[c] >= ds->nverts || !(ds->state[src->vi[c]] & GEVR_BEAN_V_READ) ||
                             !((ds->state[src->vi[c]] & GEVR_BEAN_V_DROPPED) != 0) != !((src->pane >> c) & 1);
                    for (int k = 0; src->vi[c] < ds->nverts && k < 3; k++)
                        wrong += ds->pos[src->vi[c]][k] != m->tris[t].p[c * 3 + k];
                }
            }
            bad |= placeholders != screens[s].placeholders || wrong ||
                   dropped != (int)(screens[s].last + 1 - screens[s].first) || (dropped && !panes);
            printf("%s: %d draws, %d placeholder, %d pane vertices, %d pane triangles\n", screens[s].source,
                   m->ndraws, placeholders, dropped, panes);
            gevrBeanMeshFree(m);
        }
        /* only a prop keeps them */
        static const char *const others[] = {"gun/spectre", "char/suitlfhand"};
        for (unsigned o = 0; o < sizeof(others) / sizeof(others[0]); o++) {
            struct gevrBeanMesh *m = load(argv[1], others[o]);
            bad |= m->draws || m->triSrc || m->ndraws;
            gevrBeanMeshFree(m);
        }
    }
    puts(bad ? "FAIL" : "Bean material checks passed");
	return bad;
}
