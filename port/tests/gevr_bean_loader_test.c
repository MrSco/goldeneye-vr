/* Exercise the production vertex reader and joint selection without game data. */
#include "../src/gevr_bean_load.c"
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
	put32(texture + 0x44, 0xffffffff);
	assert(!gevrBeanTexDecode(&image, 0, "authored:bad-table", &width, &height, &alpha));
	gevrBeanTexReset();
	free(mesh.tris);
    puts("Bean vertex layout and palette checks passed");
    return 0;
}
