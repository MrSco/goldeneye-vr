/* Two reusable arenas, matching the lifetime of the game's two frame lists.
 * Detailed model poses must not exhaust GoldenEye's 256 KiB auxiliary arena. */
#define BEAN_FRAME_LIMIT (32u * 1024u * 1024u)
struct beanFrameChunk {
	struct beanFrameChunk *next;
	size_t size, used;
	_Alignas(16) uint8_t bytes[];
};
static struct beanFrameChunk *s_frameChunks[2];
static int s_frameIndex;
static unsigned s_frameCount; /* frames begun, for storage lists may still read */

void gevrBeanBeginFrame(void)
{
	s_nopaqueModels = 0;
	s_frameIndex ^= 1;
	s_frameCount++;
	beanGunCollect();
	for (struct beanFrameChunk *c = s_frameChunks[s_frameIndex]; c; c = c->next)
		c->used = 0;
}
static void *beanFrameAllocate(size_t size)
{
	size = (size + 15) & ~(size_t)15;
	if (!size || size > BEAN_FRAME_LIMIT)
		return NULL;
	size_t allocated = 0;
	struct beanFrameChunk **link = &s_frameChunks[s_frameIndex];
	while (*link) {
		struct beanFrameChunk *c = *link;
		if (size <= c->size - c->used) {
			void *p = c->bytes + c->used;
			c->used += size;
			return p;
		}
		allocated += c->size;
		link = &c->next;
	}
	size_t capacity = 65536;
	while (capacity < size)
		capacity *= 2;
	if (capacity > BEAN_FRAME_LIMIT - allocated)
		return NULL;
	struct beanFrameChunk *c = malloc(sizeof(*c) + capacity);
	if (!c)
		return NULL;
	c->next = NULL;
	c->size = capacity;
	c->used = size;
	*link = c;
	return c->bytes;
}
static void beanFrameReset(void)
{
	for (int i = 0; i < 2; i++) {
		struct beanFrameChunk *c = s_frameChunks[i];
		while (c) {
			struct beanFrameChunk *next = c->next;
			free(c);
			c = next;
		}
		s_frameChunks[i] = NULL;
	}
	s_frameIndex = 0;
}
