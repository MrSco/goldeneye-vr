#ifndef _MATH_EXT_H_
#define _MATH_EXT_H_

#include_next <math.h>

/* Some useful constants */
#ifndef M_E
#define M_E        2.7182818284590452354
#endif
#ifndef M_LOG2E
#define M_LOG2E    1.4426950408889634074
#endif
#ifndef M_LOG10E
#define M_LOG10E   0.43429448190325182765
#endif
#ifndef M_LN2
#define M_LN2      0.69314718055994530942
#endif
#ifndef M_LN10
#define M_LN10     2.30258509299404568402
#endif
#ifndef M_PI
#define M_PI       3.14159265358979323846
#endif
#ifndef M_PI_2
#define M_PI_2     1.57079632679489661923
#endif
#ifndef M_PI_4
#define M_PI_4     0.78539816339744830962
#endif
#ifndef M_1_PI
#define M_1_PI     0.31830988618379067154
#endif
#ifndef M_2_PI
#define M_2_PI     0.63661977236758134308
#endif
#ifndef M_2_SQRTPI
#define M_2_SQRTPI 1.12837916709551257390
#endif
#ifndef M_SQRT2
#define M_SQRT2    1.41421356237309504880
#endif
#ifndef M_SQRT1_2
#define M_SQRT1_2  0.70710678118654752440
#endif

// extra defines
#ifndef M_PI_F
#define M_PI_F        3.1415927f
#endif
#ifndef M_MINUS_PI_F
#define M_MINUS_PI_F -3.1415927f
#endif
#ifndef M_TAU
#define M_TAU         6.28318530717958647692
#endif
#ifndef M_TAU_F
#define M_TAU_F       6.2831855f
#endif
#ifndef M_PI_2F
#define M_PI_2F     1.5707964f
#endif
#ifndef M_LN2F
#define M_LN2F      0.69813174f
#endif
#ifndef M_HALF_PI
#define M_HALF_PI       (M_PI_F / 2)
#endif
#ifndef M_THREE_HALF_PI
#define M_THREE_HALF_PI (3 * M_HALF_PI)
#endif
#ifndef M_U16_MAX_VALUE_F
#define M_U16_MAX_VALUE_F 65536.0f
#endif
#ifndef M_U32_MAX_VALUE_F
#define M_U32_MAX_VALUE_F 4294967296.0f
#endif
#ifndef FLT_EPSILON
#define FLT_EPSILON       1.19209290E-07F
#endif

#ifndef GAME_TICKRATE
#define GAME_TICKRATE 60.0f
#endif

#ifndef SECS_TO_TIMER60
#define SECS_TO_TIMER60(SECS) ((SECS) * GAME_TICKRATE)
#endif
#ifndef MINS_TO_TIMER60
#define MINS_TO_TIMER60(MINS) (SECS_TO_TIMER60((MINS) * GAME_TICKRATE))
#endif
#ifndef DEG2BYTE
#define DEG2BYTE(DEG)         ((char)(256.0f / 360.0f * (DEG)))
#endif
#ifndef RAD2BYTE
#define RAD2BYTE(RAD)         ((char)(256.0f / M_TAU_F * (RAD)))
#endif
#ifndef DegToRad
#define DegToRad(DEG)         ((float)((DEG) * M_TAU_F / 360.0f))
#endif
#ifndef DegToRad1Fact
#define DegToRad1Fact(DEG)    ((float)((DEG) * (float)(M_TAU / 360.0)))
#endif
#ifndef mDegToHalfRad
#define mDegToHalfRad(x)      (((x) * M_PI_F) / 360.0f)
#endif
#ifndef RadToDeg
#define RadToDeg(RAD)         ((float)((RAD) * (360.0f / M_TAU_F)))
#endif
#ifndef ByteToRadian
#define ByteToRadian(Byte)    (((Byte) * M_TAU_F) * (1.0f / 256.0f))
#endif

#ifndef ABS
#define ABS(x)    ((x) < 0 ? -(x) : (x))
#endif
#ifndef SGN
#define SGN(x)    ((x) < 0 ? -1 : (x) > 0 ? 1 : 0)
#endif
#ifndef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif
#ifndef MAX
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#endif
#ifndef SQR
#define SQR(x)    ((x) * (x))
#endif

#ifndef IDO_POINT_ONE
#define IDO_POINT_ONE 0.10000001f
#endif

#endif /* _MATH_EXT_H_ */
