#ifndef _ASSERT_EXT_H_
#define _ASSERT_EXT_H_

#include_next <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void osSyncPrintf(const char *fmt, ...);

#ifndef assertmsg
#define assertmsg(EX, MSG) if (!(EX)) osSyncPrintf(MSG)
#endif

#ifndef assertmsg2
#define assertmsg2(EX, MSG) \
    if (!(EX)) osSyncPrintf("%s, file %s, line %d\n", MSG, __FILE__, __LINE__)
#endif

/* Some libultra sources include "include/assert.h" by relative path; if the
 * system assert.h was not pulled in, force a host assert so we do not link
 * against a phantom assert() symbol. */
#if defined(GEVR) && !defined(assert)
#include <stdlib.h>
#define assert(EX) ((void)((EX) || (abort(), 0)))
#endif

#if defined(RMONDEBUG)
    #define Debug rmonPrintf
#else
    #define Debug
#endif

#ifdef __cplusplus
}
#endif

#endif
