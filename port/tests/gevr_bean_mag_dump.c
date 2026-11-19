/* tools/gevr_bean_mag_preview.py and port/tests/test_bean_magazine.py: a Bean
 * gun's magazine as gevr_bean.c cuts it, from the production loader and
 * gevr_beanmag.c. Text to stdout: one triangle a line, "<set> <kind> x y z x3",
 * set g the whole gun, o the gun with the magazine out, m the magazine; kind
 * 0 the gun's, 1 its hand, 2 the foot, 3 made here. No asset bytes are written. */
#include "../src/gevr_bean_load.c"
#include "gevr_beanmag.h"

void videoFreeCachedTexture(const void *addr) { (void)addr; }

static int glove(const struct gevrBeanMesh *m, const struct gevrBeanTri *t)
{
	return t->tex >= 0 && t->tex < m->ntex && m->texW[t->tex] == 512 && m->texH[t->tex] == 511;
}

static void line(char set, int kind, const struct gevrBeanTri *t)
{
	printf("%c %d", set, kind);
	for (int k = 0; k < 9; k++)
		printf(" %.3f", t->p[k]);
	printf(" %.3f %.3f %.3f\n", (t->n[0] + t->n[3] + t->n[6]) / 3, (t->n[1] + t->n[4] + t->n[7]) / 3,
	       (t->n[2] + t->n[5] + t->n[8]) / 3);
}

int main(int argc, char **argv)
{
	struct gevrBeanMesh *m = NULL;
	const GevrBeanMagDef *def = argc > 2 ? gevrBeanMagFor(argv[2]) : NULL;
	GevrBeanMagBuilt built;
	unsigned char *eligible;
	if (argc == 2 && !strcmp(argv[1], "--guns")) {
		/* the table: a gun a line, and whether its magazine is a piece of its own */
		for (int n = 0; gevrBeanMagAt(n); n++)
			printf("%s %d %d\n", gevrBeanMagAt(n)->source, gevrBeanMagAt(n)->island, gevrBeanMagAt(n)->pullUp);
		return 0;
	}
	if (argc < 3 || !def) {
		fprintf(stderr, "usage: %s <default.bin> <source, as gun/ppk>\n", argv[0]);
		return 2;
	}
	if (!gevrBeanLoad(argv[1], 1, &m) || !m || m->ntris <= 0) {
		fprintf(stderr, "cannot load %s\n", argv[1]);
		return 1;
	}
	eligible = calloc((size_t)m->ntris, 1);
	for (int i = 0; i < m->ntris; i++)
		eligible[i] = !(m->tris[i].flags & (GEVR_BEAN_F_FLASH | GEVR_BEAN_F_SLIDE)) && !glove(m, &m->tris[i]);
	if (!gevrBeanMagBuild(def, m->tris, m->ntris, eligible, &built)) {
		fprintf(stderr, "nothing to cut in %s\n", argv[1]);
		return 1;
	}
	printf("# tris %d open %d mag %d foot %d\n", m->ntris, built.nopen, built.nmag, built.nfoot);
	printf("# axis %.5f %.5f %.5f\n", built.axis[0], built.axis[1], built.axis[2]);
	printf("# mouth %.3f %.3f %.3f\n", built.mouth[0], built.mouth[1], built.mouth[2]);
	printf("# top %.3f %.3f %.3f\n", built.top[0], built.top[1], built.top[2]);
	printf("# foot %.3f %.3f %.3f\n", built.foot[0], built.foot[1], built.foot[2]);
	printf("# hold %.3f %.3f %.3f\n", built.hold[0], built.hold[1], built.hold[2]);
	printf("# plane %.5f %.5f %.5f %.3f\n", def->plane[0], def->plane[1], def->plane[2], def->plane[3]);
	printf("# island %d\n", def->island);
	for (int i = 0; i < m->ntris; i++)
		if (!(m->tris[i].flags & GEVR_BEAN_F_FLASH))
			line('g', glove(m, &m->tris[i]), &m->tris[i]);
	for (int i = 0; i < built.nopen; i++) {
		const int src = built.openSrc[i];
		if (src >= 0 && (m->tris[src].flags & GEVR_BEAN_F_FLASH))
			continue;
		line('o', src < 0 ? 3 : glove(m, &m->tris[src]), &built.open[i]);
	}
	for (int i = 0; i < built.nmag; i++)
		line('m', i < built.nfoot ? 2 : 3, &built.mag[i]);
	gevrBeanMagFree(&built);
	return 0;
}
