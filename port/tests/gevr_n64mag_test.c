/* GoldenEye's own guns' magazines (port/src/gevr_n64mag.c) on the ROM's models, through the
 * production converter (gevr_bean_native_model.c). test_n64_magazine.py runs it per gun:
 *   gevr_n64mag_test FILE NAME SWITCHES TEXTURES MATRICES
 * and reads the MAGAZINE line. Nothing of the ROM is kept. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include <PR/gbi.h>
#include <gbi_extension.h>   /* Rare's G_TRI4 */
#include <bondtypes.h>
#include <bondconstants.h>
#include "gevr_bean.h"
#include "gevr_n64mag.h"

extern void *beanNativeModel(const char *, const char *, int, int, int, ModelFileHeader *);

static int s_state = GEVR_BEAN_MAG_IN;
int gevrBeanMagazineState(void)
{
	return s_state;
}

/* the triangles a list draws: a struck one is an empty command, or three zero corners of Rare's four */
static int drawn(const Gfx *list)
{
	int n = 0;
	for (;; list++) {
		const u32 w0 = (u32)list->words.w0, w1 = (u32)list->words.w1;
		const u8 op = (u8)(w0 >> 24);
		if (op == (u8)G_ENDDL)
			return n;
		if (op == (u8)G_TRI1)
			n++;
		else if (op == (u8)G_TRI4)
			for (int t = 0; t < 4; t++)
				n += ((w1 >> (8 * t)) & 0xff) || ((w0 >> (4 * t)) & 0xf);
	}
}

static const Gfx *listOf(const ModelRoData_DisplayListRecord *dl, const Gfx *list)
{
	const uintptr_t v = (uintptr_t)list;
	return list && (v >> 32) == 0 && ((v >> 24) & 0xf) == 5 ? (const Gfx *)((const u8 *)dl->BaseAddr + (v & 0x00fffffe)) : list;
}

static ModelNode *next(ModelNode *node)
{
	if (node->Child)
		return node->Child;
	while (node && !node->Next)
		node = node->Parent;
	return node ? node->Next : NULL;
}

int main(int argc, char **argv)
{
	ModelFileHeader header;
	struct gevrBeanMagInfo info;
	const GevrN64MagDef *def;
	int triangles = 0, lists = 0, whole = 0, open = 0, alone, opened = 0;

	assert(argc == 6);
	void *native = beanNativeModel(argv[1], argv[2], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), &header);
	assert(native);
	def = gevrN64MagFor(argv[2]);
	assert(def);
	/* a hack's copy of the file is served too, another file is not */
	{
		char copy[64];
		snprintf(copy, sizeof(copy), "%.*s1Z", (int)strlen(def->name) - 1, def->name);
		assert(gevrN64MagFor(copy) == def && !gevrN64MagFor("GknifeZ") && !gevrN64MagFor(NULL));
	}
	assert(!gevrN64GunMagazine(&header, NULL));
	gevrN64MagApply(&header, argv[2]);
	if (!gevrN64GunMagazine(&header, &info)) {
		printf("NOMAGAZINE %s\n", argv[2]);
		free(native);
		return 0;
	}
	assert(gevrN64MagStats(&header, &triangles, &lists));
	assert(info.node && info.matrix >= 0 && info.matrix < atoi(argv[5]));
	/* a made round or rocket is never drawn in the gun: only a made magazine is */
	assert(info.made == (def->made && !def->single && !def->consumed) && info.single == def->single && info.pullUp == def->pullUp);
	assert(info.noPull == def->noPull && info.consumed == def->consumed);
	/* in the gun: every list as the model has it */
	for (ModelNode *node = header.RootNode; node; node = next(node)) {
		const ModelRoData_DisplayListRecord *dl;
		if ((node->Opcode & 0xff) != MODELNODE_OPCODE_DL || !node->Data)
			continue;
		dl = &node->Data->DisplayList;
		for (int which = 0; which < 2; which++) {
			Gfx *list = which ? dl->Secondary : dl->Primary;
			const Gfx *shown;
			if (!list)
				continue;
			s_state = GEVR_BEAN_MAG_IN;
			assert(gevrN64MagGdl(node, list, which) == list);
			s_state = GEVR_BEAN_MAG_OUT;
			shown = gevrN64MagGdl(node, list, which);
			whole += drawn(listOf(dl, list));
			open += drawn(listOf(dl, shown));
			opened += shown != list;
			/* its own draw: the magazine on its host's primary, nothing of any other state's */
			s_state = GEVR_BEAN_MAG_ITSELF;
			assert(node == info.node || gevrN64MagGdl(node, list, which) == list);
		}
	}
	s_state = GEVR_BEAN_MAG_ITSELF;
	{
		ModelNode *host = info.node;
		Gfx *list = host->Data->DisplayList.Primary;
		const Gfx *mag = gevrN64MagGdl(host, list, 0);
		assert(mag && mag != list);
		alone = drawn(mag);
		s_state = GEVR_BEAN_MAG_GHOST;
		assert(gevrN64MagGdl(host, list, 0) == mag);
		s_state = GEVR_BEAN_MAG_GHOST_READY;
		assert(gevrN64MagGdl(host, list, 0) == mag && !gevrN64MagGdl(host, list, 1));
	}
	s_state = GEVR_BEAN_MAG_IN;
	if (def->made) {
		/* nothing of the gun is struck out; the box is its twelve triangles, wound both ways */
		assert(open == whole && !opened && !lists && alone == 24 && triangles == 12);
	} else if (def->single) {
		/* the gun keeps its shell: the round is a copy */
		assert(open == whole && !opened && alone == triangles && triangles > 0);
	} else {
		assert(open == whole - triangles && opened >= 1 && alone == triangles && triangles > 0);
	}
	/* the mouth is on the magazine's way in, short of its top */
	{
		float along = 0;
		for (int k = 0; k < 3; k++)
			along += (info.top[k] - info.mouth[k]) * info.axis[k];
		assert(def->single || (along >= 0.0f && fabsf(along - def->inside) < 0.2f));
	}
	printf("MAGAZINE %s triangles %d lists %d opened %d whole %d top %.1f %.1f %.1f mouth %.1f %.1f %.1f foot %.1f %.1f %.1f "
	       "hold %.1f %.1f %.1f\n",
	       argv[2], triangles, lists, opened, whole, info.top[0], info.top[1], info.top[2], info.mouth[0], info.mouth[1],
	       info.mouth[2], info.foot[0], info.foot[1], info.foot[2], info.hold[0], info.hold[1], info.hold[2]);
	/* loaded again, or another model in its place: nothing stale */
	gevrN64MagApply(&header, argv[2]);
	assert(gevrN64GunMagazine(&header, NULL));
	gevrN64MagApply(&header, "GknifeZ");
	assert(!gevrN64GunMagazine(&header, NULL));
	gevrN64MagApply(&header, argv[2]);
	gevrN64MagForgetStorage(((ModelNode *)info.node)->Data->DisplayList.BaseAddr, 16);
	assert(!gevrN64GunMagazine(&header, NULL));
	free(native);
	return 0;
}
