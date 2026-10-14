#ifndef _PM_TYPES_H_
#define _PM_TYPES_H_

#include "n64/os_types.h"

// Minimal types needed for 3DS shim / game interop.
// GameStatus is defined in include/common_structs.h (full 0x178-byte struct).
// We only define primitive vector/color types here that are needed before
// common_structs.h is parsed.

typedef struct Vec1f {
    f32 x;
} Vec1f;

typedef struct Vec2f {
    f32 x;
    f32 y;
} Vec2f;

typedef struct Vec3f {
    f32 x;
    f32 y;
    f32 z;
} Vec3f;

typedef struct Vec4f {
    f32 x;
    f32 y;
    f32 z;
    f32 yaw;
} Vec4f;

typedef struct Color_RGBA8 {
    u8 r, g, b, a;
} Color_RGBA8;

typedef struct Color_RGB8 {
    u8 r, g, b;
} Color_RGB8;

extern u32 gRandSeed;

#endif
