#include "gevr_vertex_ownership.h"
#include <assert.h>
#include <stdio.h>
static unsigned result, calls, finishes;
static unsigned poll(void *context, uint64_t timeout) {
    assert(context == &result);
    assert(timeout == GEVR_VERTEX_FENCE_TIMEOUT_NS);   /* one blocking wait, as before */
    calls++;
    return result;
}
static void finish(void *context) { assert(context == &result); finishes++; }
static void check(unsigned value, unsigned expectFinishes) {
    calls = finishes = 0;
    result = value;
    assert(gevrVertexAwaitOwnership(&result, poll, finish) == value);
    assert(calls == 1 && finishes == expectFinishes);
}
int main(void) {
    check(GEVR_FENCE_ALREADY, 0);
    check(GEVR_FENCE_SATISFIED, 0);
    check(GEVR_FENCE_TIMEOUT, 1);   /* GPU still reading after 100 ms: drain before reuse */
    check(GEVR_FENCE_FAILED, 1);
    check(0, 1);                    /* unrecognized result must also establish completion */
    puts("PASS: vertex ring waits once and only drains the GPU when the fence never signaled");
    return 0;
}
