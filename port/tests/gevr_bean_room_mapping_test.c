#include <ultra64.h>
#include <PR/gbi.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#undef assert
#define assert(test)                                                                                         \
	do {                                                                                                     \
		if (!(test)) {                                                                                       \
			fprintf(stderr, "room assertion: %s at %d\n", #test, __LINE__);                                  \
			exit(2);                                                                                         \
		}                                                                                                    \
	} while (0)
#include "bg.h"
#include "../src/gevr_bean_rooms.inc.h"
#include "../src/gevr_bean_room_scale.inc.h"
#include "gevr_bean_int.h"
void videoFreeCachedTexture(const void *p) { (void)p; }
void sysLogPrintf(s32 level, const char *fmt, ...) { (void)level; (void)fmt; }

struct levelentry levelinfotable[1];
s32 levelentry_index;
s_room_info g_BgRoomInfo[MAXROOMCOUNT];
bg_room_data *ptr_bgdata_room_fileposition_list;
static const uint8_t *nativeBg;
static unsigned nativeBgSize;
void obLoadBGFileBytesAtOffset(u8 *name, u8 *target, s32 offset, s32 size)
{
	(void)name;
	assert(offset >= 0 && offset + size <= nativeBgSize + 16);
	memcpy(target, nativeBg + offset, size);
}

int main(int argc, char **argv)
{
	/* Equal/overlapping boxes cannot identify which room owns these planes. */
	float a[16][3] = {{-10, -10, 0}, {10, -10, 0}, {0, 10, 0}};
	float b[16][3] = {{-10, -10, 4}, {10, -10, 4}, {0, 10, 4}};
	assert(roomReferenceTri(2, a, 7, 0, 1, 2));
	assert(roomReferenceTri(7, b, 7, 0, 1, 2));
	s_tree = calloc(4, sizeof(*s_tree));
	roomReferenceTree(0, s_referenceCount);
	float point[3] = {0, 0, 3.9f}, distance = 1e30f;
	int room = -1;
	roomReferenceNearest(0, point, &distance, &room);
	assert(room == 7 && fabsf(distance - 0.01f) < 0.0001f);
	point[2] = 0.1f;
	distance = 1e30f;
	roomReferenceNearest(0, point, &distance, &room);
	assert(room == 2);
	/* Outside the triangle, the nearest edge matters, not its plane alone. */
	point[0] = 20;
	point[1] = 0;
	point[2] = 0;
	assert(roomTriangleDistance(point, &s_reference[0]) > 100);
	roomReferenceFree();
	/* A BVH query must agree with a complete comparison over all triangles. */
	for (int i = 0; i < 127; i++) {
		float triangle[16][3] = {{0}};
		for (int v = 0; v < 3; v++)
			for (int k = 0; k < 3; k++)
				triangle[v][k] = (float)((i * 37 + v * 17 + k * 29) % 101);
		assert(roomReferenceTri(i + 1, triangle, 7, 0, 1, 2));
	}
	s_tree = calloc(s_referenceCount * 2, sizeof(*s_tree));
	roomReferenceTree(0, s_referenceCount);
	for (int i = 0; i < 251; i++) {
		float p[3] = {(i * 13) % 131 - 10, (i * 7) % 127 - 10, (i * 23) % 137 - 10};
		float brute = 1e30f;
		distance = 1e30f;
		for (int t = 0; t < s_referenceCount; t++) {
			float d = roomTriangleDistance(p, &s_reference[t]);
			if (d < brute)
				brute = d;
		}
		roomReferenceNearest(0, p, &distance, &room);
		assert(fabsf(distance - brute) < 0.001f);
	}
	roomReferenceFree();
	if (argc > 1) {
		FILE *file = fopen(argv[1], "rb");
		assert(file);
		fseek(file, 0, SEEK_END);
		nativeBgSize = ftell(file);
		rewind(file);
		uint8_t *bytes = calloc(nativeBgSize + 16, 1);
		assert(bytes);
		assert(fread(bytes, 1, nativeBgSize, file) == nativeBgSize);
		fclose(file);
		nativeBg = bytes;
		unsigned table = roomBe32(bytes + 4) & 0xffffff, rooms = 1;
		while (table + (rooms + 1) * 24 <= nativeBgSize && roomBe32(bytes + table + rooms * 24 + 4))
			rooms++;
		assert(rooms <= MAXROOMCOUNT);
		ptr_bgdata_room_fileposition_list = calloc(rooms, sizeof(bg_room_data));
		for (unsigned r = 0; r < rooms; r++) {
			const uint8_t *record = bytes + table + r * 24;
			void **address = (void **)&ptr_bgdata_room_fileposition_list[r];
			int *sizes = &g_BgRoomInfo[r].csize_point_index_binary;
			for (int k = 0; k < 3; k++) {
				unsigned start = roomBe32(record + k * 4), end = nativeBgSize;
				address[k] = (void *)(uintptr_t)start;
				if (!start)
					continue;
				start &= 0xffffff;
				for (unsigned rr = 1; rr <= rooms; rr++)
					for (int kk = 0; kk < 3; kk++) {
						unsigned next = roomBe32(bytes + table + rr * 24 + kk * 4) & 0xffffff;
						if (next > start && next < end)
							end = next;
					}
				sizes[k] = end - start;
			}
			for (int k = 0; k < 3; k++) {
				uint32_t value = roomBe32(record + 12 + k * 4);
				memcpy(&ptr_bgdata_room_fileposition_list[r].pos.f[k], &value, 4);
			}
		}
		assert(roomReferenceBuild(rooms));
		assert(s_referenceCount > 10);
		for (int t = 0; t < s_referenceCount; t += s_referenceCount / 128 + 1) {
			float p[3];
			for (int k = 0; k < 3; k++)
				p[k] = (s_reference[t].p[0][k] + s_reference[t].p[1][k] + s_reference[t].p[2][k]) / 3;
			distance = 1e30f;
			room = -1;
			roomReferenceNearest(0, p, &distance, &room);
			assert(distance < 0.1f && room > 0 && room < rooms);
		}

        if (argc == 5) {
            struct gevrBeanMesh *mesh = NULL;
            assert(gevrBeanLoad(argv[2], 1, &mesh));
            float scale = beanRoomFileScale(strtof(argv[3], NULL), strtof(argv[4], NULL));
            assert(scale > 0);
            int samples = 0, matched = 0;
            for (int t = 0; t < mesh->ntris; t += mesh->ntris / 2048 + 1) {
                float point[3];
                for (int k = 0; k < 3; k++) point[k] = mesh->tris[t].p[k] * scale;
                distance = 1e30f; room = -1;
                roomReferenceNearest(0, point, &distance, &room);
                samples++; matched += distance < 4.0f;
            }
            printf("Bean original/native geometry: %d/%d samples within 2 units (%.1f%%), scale %.8f\n",
                   matched, samples, 100.f * matched / samples, scale);
            fflush(stdout);
            assert(samples > 100 && matched * 10 >= samples * 6);
            gevrBeanMeshFree(mesh);
            gevrBeanTexReset();
        }
		printf("Original room references: %u rooms, %d triangles\n", rooms, s_referenceCount);
		roomReferenceFree();
		free(ptr_bgdata_room_fileposition_list);
		free(bytes);
	}
	puts("Bean nearest-triangle room mapping and BVH checks passed");
}
