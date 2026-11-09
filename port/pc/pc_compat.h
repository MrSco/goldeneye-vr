/*
 * The POSIX names the game uses that Windows spells differently or lacks
 * (docs/pc-platforms.md). Included ahead of every PC source by CMake
 * (-include), so the game and port sources build unchanged; the functions
 * are in pc_compat.c.
 */
#ifndef GEVR_PC_COMPAT_H
#define GEVR_PC_COMPAT_H

#include <float.h>    /* FLT_MAX: bionic and glibc bring it in through math.h */
#include <stddef.h>
#include <stdint.h>
#include <io.h>
#include <direct.h>
#include <sys/stat.h>

/* POSIX mkdir(path, mode): Windows' has no mode. After io.h, which declares
 * it. Variadic so the one-argument calls MinGW-aware code makes still work. */
#define mkdir(path, ...) mkdir(path)

/* no symbolic links to tell apart */
#define lstat stat

/* POSIX realpath(path, resolved): _fullpath does the same on Windows */
#include <limits.h>
#ifndef PATH_MAX
#define PATH_MAX 260
#endif
#define realpath(path, resolved) _fullpath((resolved), (path), PATH_MAX)

#ifdef __cplusplus
extern "C" {
#endif

/* libultra's byte-string calls (include/PR/os.h); port/src/libultra.c defines them */
void bzero(void *ptr, size_t size);
void bcopy(const void *src, void *dst, size_t n);
int bcmp(const void *s1, const void *s2, size_t n);

/* BSD (bionic has it): a uniform random number below upper */
uint32_t arc4random_uniform(uint32_t upper);

#ifdef __cplusplus
}
#endif

#endif
