#ifndef _IN_PLATFORM_H
#define _IN_PLATFORM_H

#if defined(_MSC_VER)
#define PD_CONSTRUCTOR
#else
#define PD_CONSTRUCTOR __attribute__((constructor))
#endif

#if defined(_MSC_VER)
#define PD_NORETURN __declspec(noreturn)
#else
#define PD_NORETURN __attribute__((noreturn))
#endif

#if defined(_MSC_VER)
#define PD_INLINE __forceinline
#else
#define PD_INLINE static inline __attribute__((always_inline))
#endif

#if defined(_MSC_VER)
#define PD_BE16(x) _byteswap_ushort(x)
#define PD_BE32(x) _byteswap_ulong(x)
#define PD_BE64(x) _byteswap_uint64(x)
#else
#define PD_BE16(x) __builtin_bswap16(x)
#define PD_BE32(x) __builtin_bswap32(x)
#define PD_BE64(x) __builtin_bswap64(x)
#endif
#define PD_BEPTR(x) (sizeof(void*) == 8 ? (uintptr_t)PD_BE64((uint64_t)(x)) : (uintptr_t)PD_BE32((uint32_t)(x)))

#endif /* _IN_PLATFORM_H */

