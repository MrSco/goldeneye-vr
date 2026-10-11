#ifndef GEVR_BEAN_LOG_H
#define GEVR_BEAN_LOG_H

/* Android does not persist our stderr diagnostics in gevr.log. Use the same
 * logger as the native model converter so exported reports contain mappings. */
#ifdef GEVR
#include "system.h"
#define GEVR_BEAN_LOG(...) sysLogPrintf(LOG_NOTE, __VA_ARGS__)
#else
#include <stdio.h>
#define GEVR_BEAN_LOG(...) do { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)
#endif

#endif
