#ifndef GEVR_BEAN_CONTROL_H
#define GEVR_BEAN_CONTROL_H
#include <stdio.h>

/* Renderer diagnostics share the external-files root used by the input and
 * profiling hooks. Android's working directory is its data child. */
static inline FILE *gevrBeanControlOpen(const char *name)
{
#ifdef __ANDROID__
    char path[192];
    snprintf(path,sizeof(path),"/sdcard/Android/data/com.gevr.port/files/%s",name);
    return fopen(path,"r");
#else
    return fopen(name,"r");
#endif
}
#endif
