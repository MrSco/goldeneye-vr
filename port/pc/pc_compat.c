/* The functions pc_compat.h declares for the PC build. */
/* Windows only: glibc has arc4random_uniform and the BSD string calls. */
#ifdef _WIN32

#define _CRT_RAND_S
#include <stdlib.h>
#include <stdint.h>

uint32_t arc4random_uniform(uint32_t upper)
{
	unsigned int r;

	if (upper < 2) {
		return 0;
	}
	/* rand_s is the CRT's cryptographic generator; reject the top slice so every value is equally likely */
	const uint32_t limit = UINT32_MAX - UINT32_MAX % upper;
	do {
		if (rand_s(&r) != 0) {
			r = (unsigned int)rand();
		}
	} while (r >= limit);
	return r % upper;
}
#endif   /* _WIN32 */
