/* Exercise the production vertex reader and joint selection without game data. */
#include "../src/gevr_bean_load.c"
#include "../src/gevr_bean_tex.c"
#include <assert.h>
void videoFreeCachedTexture(const void *addr) { (void)addr; }

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

int main(void)
{
    /* A header without its section table must be rejected, even when the
     * counts and section-data fields happen to fit in the file. */
    uint8_t bad[0x68] = {0};
    memcpy(bad, "CAFF", 4); bad[0x48] = 1;
    struct gevrCaff caff;
    assert(!gevrCaffOpen(&caff, bad, sizeof(bad)));
    assert(!gevrCaffOpen(&caff, NULL, 0));
    uint8_t bytes[36] = {0};
    struct beanVb vb = {.stride = 36, .count = 1};
    struct beanVtx v;
    /* Rigid FLOAT3 starts after the first UV set. A UV can look like NaN. */
    put32(bytes, 0xffffffff);
    put32(bytes + 4, 0x3f800000); put32(bytes + 8, 0x40000000); put32(bytes + 12, 0x40400000);
    put32(bytes + 20, 0x00800040); put32(bytes + 32, 0xff123456);
    assert(readVtx(bytes, &vb, 0, 128, 0, &v));
    assert(v.pos[0] == 1 && v.pos[1] == 2 && v.pos[2] == 3);
    assert(v.uv[0] == 1 && v.uv[1] == 0.5f && v.argb == 0xff563412);
    assert(!readVtx(bytes, &vb, 1, 128, 0, &v));
    /* A rigid room vertex keeps its second UV apart from the lit colour. */
    memset(bytes, 0, sizeof(bytes)); vb.stride = 32;
    put32(bytes + 16, 0x00800040); /* diffuse UV */
    put32(bytes + 20, 0x00000000); /* second UV, black if misread */
    put32(bytes + 24, 0x11223344); /* blend word */
    put32(bytes + 28, 0xff123456); /* ABGR colour */
    assert(readVtx(bytes, &vb, 0, 128, 0, &v));
    assert(v.uv[0] == 1 && v.uv[1] == 0.5f && v.argb == 0xff563412);
    /* Palette slot 20 is not a weight-array index. The winning influence
     * here is weight[2], mapped through slot[2] to palette[20]. */
    struct gevrBeanMesh mesh = {0};
    uint8_t pal[21] = {0}, muzzle[MAX_BONES] = {0}, slide[MAX_BONES] = {0};
    int boneSk[MAX_BONES] = {0};
    for (int s = 0; s < 4; s++) v.slot[s] = -1;
    v.slot[2] = 20; v.weight[2] = 200; pal[20] = 5; boneSk[5] = 7;
    assert(pushTri(&mesh, &v, &v, &v, -1, 255, 0, 0, pal, 21, boneSk, muzzle, slide));
    assert(mesh.tris[0].sk == 7);
	assert(mesh.tris[0].bone[2] == 5 && mesh.tris[0].weight[2] == 200);
    struct beanVtx boundary[3] = {0};
    for (int c = 0; c < 3; c++) for (int w = 0; w < 4; w++) boundary[c].slot[w] = -1;
    pal[0] = 0; pal[1] = 1; boneSk[0] = 5; boneSk[1] = 11;
    boundary[0].slot[0] = 0; boundary[0].weight[0] = 255;
    boundary[1].slot[0] = boundary[2].slot[0] = 1;
    boundary[1].weight[0] = boundary[2].weight[0] = 240;
    assert(pushTri(&mesh, boundary, boundary+1, boundary+2, -1, 255, 0, 0, pal, 2, boneSk, muzzle, slide));
    assert(mesh.tris[1].sk == 11); /* 480 total wins over one corner's 255. */
	assert(boneIs("SKEL_LF_WRIST_WPRE", "SKEL_LF_WRIST"));
	assert(!boneIs("SKEL_LF_WRIST_OTHER", "SKEL_LF_WRIST"));
	/* Animated type-4 textures put dimensions in frame tables, not +0x24. */
	uint8_t texture[0x1080] = {0};
	memcpy(texture, "texture", 7);
	texture[0x1b] = 6;
	put32(texture + 0x1c, 4);
	put32(texture + 0x44, 0x50);
	put32(texture + 0x48, 0x54);
	put32(texture + 0x4c, 0x58);
	put32(texture + 0x50, 4);
	put32(texture + 0x54, 8);
	put32(texture + 0x58, 1);
	memset(texture + 0x80, 255, 4096);
	/* An asymmetric tiled picture detects vertical reversal and channel swaps. */
	for (unsigned y = 0; y < 8; y++)
		for (unsigned x = 0; x < 4; x++) {
			uint8_t *p = texture + 0x80 + tiledOffset(x, y, 32, 2) * 4;
			p[0] = x * 30; p[1] = y * 20; p[2] = 10; p[3] = 255;
		}
	struct gevrCaffFile files[2] = {{1, 0, 0x60, 1}, {1, 0, 4096, 2}};
	struct gevrCaff image = {
	    .d = texture, .len = sizeof(texture), .numfiles = 2, .numsects = 2, .files = files};
	image.sects[0].size = 0x60;
	strcpy(image.sects[0].name, ".data");
	image.sects[1].size = 4096;
	image.sects[1].offset = 0x80;
	strcpy(image.sects[1].name, ".gpu");
	int width = 0, height = 0, alpha = 0;
	assert(gevrBeanTexDecode(&image, 0, "authored:type4", &width, &height, &alpha));
	assert(width == 4 && height == 8 && alpha == 0);
	const uint8_t *decoded;
	const void *tile = gevrBeanTexDecode(&image, 0, "authored:type4", &width, &height, &alpha);
	assert(gevrBeanTexture(tile, &decoded, &width, &height));
	assert(decoded[0] == 0 && decoded[1] == 140 && decoded[2] == 10);
	assert(decoded[(7 * 4 + 3) * 4] == 90 && decoded[(7 * 4 + 3) * 4 + 1] == 0);
	/* A real coloured 4x8 picture must never be treated as missing art. */
	assert(!gevrBeanTexIsPlaceholder(tile));
	gevrBeanTexPaint(tile, 0xff00ff);
	assert(gevrBeanTexIsPlaceholder(tile));
	gevrBeanTexPaint(tile, 0x808080);
	assert(!gevrBeanTexIsPlaceholder(tile) && decoded[0] == 128 && decoded[3] == 255);
	/* DAB borrows cutout art only across draws sharing a vertex shader. */
	gevrBeanTexPaint(tile, 0xff00ff);
	for (unsigned y = 0; y < 8; y++)
		for (unsigned x = 0; x < 4; x++) {
			uint8_t *p = texture + 0x80 + tiledOffset(x, y, 32, 2) * 4;
			p[0] = 10; p[1] = 180; p[2] = 30; p[3] = x == 0 ? 0 : 255;
		}
	const void *leaves = gevrBeanTexDecode(&image, 0, "authored:leaves", &width, &height, &alpha);
	assert(leaves && alpha == 1);
	struct gevrBeanMesh foliage = {.ntex = 2};
	foliage.standin[0] = tile; foliage.standin[1] = leaves;
	foliage.texW[1] = 4; foliage.texH[1] = 8; foliage.texCut[1] = 1;
	struct beanDraw neighbours[2] = {{.tex = 0, .alphatest = 1, .vs = 7},
	                                  {.tex = 1, .alphatest = 1, .vs = 7}};
	fixPlaceholders(&foliage, neighbours, 2, "authored:matching-shader");
	assert(foliage.standin[0] == leaves && foliage.texCut[0] == 1);
	foliage.standin[0] = tile; neighbours[1].vs = 8;
	fixPlaceholders(&foliage, neighbours, 2, "authored:shader-boundary");
	assert(foliage.standin[0] == tile && !gevrBeanTexIsPlaceholder(tile));
	assert(decoded[0] == 128 && decoded[1] == 128 && decoded[2] == 128);


    struct gevrBeanMesh terrain = {.ntex=2};
    struct beanDraw draw = {.tex=1, .masktex=0, .maskslot=0};
    struct beanVb ground = {.stride=32, .count=3};
    uint8_t gpu[96] = {0}; uint32_t indices[3] = {0,1,2};
    gpu[24]=0; gpu[56]=128; gpu[88]=255;
    struct beanLayers layers=levelLayers(&terrain,&draw,&ground,gpu,indices,1,1);
    assert(layers.base==1 && layers.other==0 && layers.blend && !layers.decal && !layers.mask);
    gpu[24]=gpu[56]=gpu[88]=128;
    layers=levelLayers(&terrain,&draw,&ground,gpu,indices,1,1);
    assert(!layers.blend); /* An unrelated lightmap is not terrain. */
    draw.terrainBlend=1;
    layers=levelLayers(&terrain,&draw,&ground,gpu,indices,1,1);
    assert(layers.blend); /* A matching shader keeps its partial snow blend. */
    draw.terrainBlend=0;
    /* A cut-out decal cannot replace the opaque wall underneath it. */
    terrain.texCut[1]=1; draw.maskslot=1;
    layers=levelLayers(&terrain,&draw,&ground,gpu,indices,1,1);
    assert(layers.decal && !layers.blend);
    /* Alpha testing selects the mask's own second UV set. */
    draw.tex=0; draw.masktex=1; draw.alphatest=1;
    layers=levelLayers(&terrain,&draw,&ground,gpu,indices,1,1);
    assert(layers.mask && layers.base==1 && !layers.decal);
    draw.alphatest=0; ground.stride=12;
    layers=levelLayers(&terrain,&draw,&ground,gpu,indices,1,1);
    assert(layers.base==-1 && !layers.mask);
	put32(texture + 0x44, 0xffffffff);
	assert(!gevrBeanTexDecode(&image, 0, "authored:bad-table", &width, &height, &alpha));
	gevrBeanTexReset();
	free(mesh.tris);
    puts("Bean vertex layout and palette checks passed");
    return 0;
}
