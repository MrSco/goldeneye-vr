/* Display-list primaries stay segmented, exactly as modelPromoteNodeOffsetsToPointers
 * leaves them in game. Only CPU-side node and vertex pointers are promoted.
 * Exercise the production cartridge converter without loading game assets
 * into the repository. The caller supplies an ignored, decompressed file. */
#include "../src/gevr_model.c"
#include <stdarg.h>
#include <stdio.h>

void sysLogPrintf(s32 level, const char *fmt, ...)
{
	(void)level;
	(void)fmt;
}
void sysFatalError(const char *fmt, ...)
{
	(void)fmt;
	abort();
}
int gevrHandPatchWants(const char *name)
{
	(void)name;
	return 0;
}
void gevrHandPatchNoteNode(u32 cart, u32 host)
{
	(void)cart;
	(void)host;
}

static void *promoted(void *value, u8 *base)
{
	uintptr_t v = (uintptr_t)value;
	return v >> 24 == 5 ? base + (v & 0xffffff) : value;
}
#define PROMOTE(field) field = promoted(field, out)

void *beanNativeModel(const char *path, const char *name, int switches, int textures, int matrices,
                      ModelFileHeader *header)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	rewind(f);
	u8 *raw = malloc(len);
	if (!raw) {
		fclose(f);
		return NULL;
	}
	if (fread(raw, 1, len, f) != (size_t)len) {
		fclose(f);
		free(raw);
		return NULL;
	}
	fclose(f);
	struct conv c = {0};
	c.src = raw;
	c.srcLen = len;
	c.name = name;
	c.numSwitches = switches;
	c.numTextures = textures;
	c.maxBlocks = len / 4 + 64;
	c.blocks = malloc(c.maxBlocks * sizeof(*c.blocks));
	discover(&c);
	u32 size;
	do {
		size = layout(&c);
	} while (discoverGdlRefs(&c) > 0);
	u8 *out = calloc(1, size);
	convert(&c, out);
	memset(header, 0, sizeof(*header));
	header->numMatrices = matrices;
	header->numSwitches = switches;
	header->Switches = (void *)out;
	for (int i = 0; i < switches; i++)
		PROMOTE(header->Switches[i]);
	header->RootNode =
	    promoted((void *)(uintptr_t)resolve(&c, 0x05000000 + switches * 4 + textures * 12), out);
	for (int i = 0; i < c.numBlocks; i++) {
		struct block *b = &c.blocks[i];
		if (b->kind == BK_NODE) {
			ModelNode *n = (void *)(out + b->dst);
			PROMOTE(n->Data);
			PROMOTE(n->Parent);
			PROMOTE(n->Child);
			PROMOTE(n->Next);
			PROMOTE(n->Prev);
		} else if (b->kind == BK_RODATA) {
			union ModelRoData *r = (void *)(out + b->dst);
			switch (b->opcode) {
			case 1:
				PROMOTE(r->Header.FirstGroupNode);
				break;
			case 2:
			case 3:
				PROMOTE(r->Group.ChildGroupNode);
				break;
			case 4:
				r->DisplayList.BaseAddr = out;
				PROMOTE(r->DisplayList.Vertices);
				break;
			case 8:
				PROMOTE(r->LOD.Affects);
				break;
			case 9:
				PROMOTE(r->BSP.leftChild);
				PROMOTE(r->BSP.rightChild);
				break;
			case 18:
				PROMOTE(r->Switch.Controls);
				break;
			case 22:
				PROMOTE(r->DisplayListPrimary.Vertices);
				PROMOTE(r->DisplayListPrimary.Primary);
				break;
			case 24:
				r->DisplayListCollisions.BaseAddr = out;
				PROMOTE(r->DisplayListCollisions.Vertices);
				break;
			}
		}
	}
	free(raw);
	free(c.blocks);
	return out;
}
