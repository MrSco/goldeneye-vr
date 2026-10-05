#ifndef GEVR_VERTEX_OWNERSHIP_H
#define GEVR_VERTEX_OWNERSHIP_H
#include <stdint.h>
/* GL sync return values, kept independent of the GL loader for native testing. */
enum { GEVR_FENCE_ALREADY = 0x911a, GEVR_FENCE_TIMEOUT = 0x911b,
       GEVR_FENCE_SATISFIED = 0x911c, GEVR_FENCE_FAILED = 0x911d };
#define GEVR_VERTEX_FENCE_TIMEOUT_NS 100000000ull
typedef unsigned (*GevrFencePoll)(void *, uint64_t timeout);
typedef void (*GevrFenceFinish)(void *);
/* One blocking wait, as the ring always did: it returns as soon as the GPU
 * passes the fence, so the normal path is unchanged. A 100 ms timeout, a
 * failed wait or an unknown result ends in a completion barrier instead of
 * reusing storage the GPU may still read. The caller must not delete the
 * fence or write the segment until this returns. */
static inline unsigned gevrVertexAwaitOwnership(void *context, GevrFencePoll poll,
                                               GevrFenceFinish finish) {
    const unsigned result = poll(context, GEVR_VERTEX_FENCE_TIMEOUT_NS);
    if (result != GEVR_FENCE_ALREADY && result != GEVR_FENCE_SATISFIED)
        finish(context);
    return result;
}
#endif
