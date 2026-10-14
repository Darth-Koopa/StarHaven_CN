#ifndef _N64_MATH_SHIM_H_
#define _N64_MATH_SHIM_H_

#include "os_types.h"

// math.h declares double round(double), but the game defines its own
// s32 round(f32).  Include math.h first so the standard prototype is
// parsed as-is, then rename all later occurrences of 'round' to avoid a
// conflicting-types error when the game defines its version.
#include <math.h>
#undef round
#define round pm_game_round
// The game's round (decomp src/43F0.c) is s32 round(f32). After the
// #define above any later use rewrites to pm_game_round; declare it
// here so callers don't trip -Wimplicit-function-declaration in
// translation units that include functions.h before math_shim.h.
s32 pm_game_round(f32 x);

// ============================================================================
// N64 Math Library Shim (gu* functions from libultra)
// ============================================================================
// The N64 uses Silicon Graphics' gu (Graphics Utilities) library for matrix
// math, vector operations, and trig. On 3DS, we use standard math.h.
// ============================================================================

// Trig functions (N64 uses s16 angles with 0 = 0, 0x8000 = PI)
// Paper Mario has: f32 cosine(s16 arg0);
// We provide compatibility wrappers.

static inline f32 n64_cos_s(s16 angle) {
    // N64 angle: 0x8000 = PI, 0x4000 = PI/2
    f32 rad = (f32)angle * (M_PI / 16384.0f);
    return cosf(rad);
}

static inline f32 n64_sin_s(s16 angle) {
    f32 rad = (f32)angle * (M_PI / 16384.0f);
    return sinf(rad);
}

static inline f32 n64_atan2_f(f32 y, f32 x) {
    return atan2f(y, x);
}

// N64 square root (returns float)
static inline f32 n64_sqrt_f(f32 x) {
    return sqrtf(x);
}

static inline f32 n64_inv_sqrt_f(f32 x) {
    return 1.0f / sqrtf(x);
}

// Vector operations
static inline void n64_vec3f_copy(Vec3f* dest, const Vec3f* src) {
    dest->x = src->x;
    dest->y = src->y;
    dest->z = src->z;
}

static inline void n64_vec3f_set(Vec3f* v, f32 x, f32 y, f32 z) {
    v->x = x;
    v->y = y;
    v->z = z;
}

static inline void n64_vec3f_add(Vec3f* dest, const Vec3f* a, const Vec3f* b) {
    dest->x = a->x + b->x;
    dest->y = a->y + b->y;
    dest->z = a->z + b->z;
}

static inline void n64_vec3f_sub(Vec3f* dest, const Vec3f* a, const Vec3f* b) {
    dest->x = a->x - b->x;
    dest->y = a->y - b->y;
    dest->z = a->z - b->z;
}

static inline void n64_vec3f_scale(Vec3f* dest, const Vec3f* v, f32 scale) {
    dest->x = v->x * scale;
    dest->y = v->y * scale;
    dest->z = v->z * scale;
}

static inline f32 n64_vec3f_dot(const Vec3f* a, const Vec3f* b) {
    return a->x * b->x + a->y * b->y + a->z * b->z;
}

static inline f32 n64_vec3f_length(const Vec3f* v) {
    return sqrtf(v->x * v->x + v->y * v->y + v->z * v->z);
}

static inline void n64_vec3f_normalize(Vec3f* v) {
    f32 len = n64_vec3f_length(v);
    if (len > 0.0f) {
        v->x /= len;
        v->y /= len;
        v->z /= len;
    }
}

static inline void n64_vec3f_cross(Vec3f* dest, const Vec3f* a, const Vec3f* b) {
    dest->x = a->y * b->z - a->z * b->y;
    dest->y = a->z * b->x - a->x * b->z;
    dest->z = a->x * b->y - a->y * b->x;
}

// Matrix operations
// N64 matrices are column-major 4x4 float arrays

static inline void n64_mtxf_identity(Matrix4f m) {
    m[0][0] = 1.0f; m[0][1] = 0.0f; m[0][2] = 0.0f; m[0][3] = 0.0f;
    m[1][0] = 0.0f; m[1][1] = 1.0f; m[1][2] = 0.0f; m[1][3] = 0.0f;
    m[2][0] = 0.0f; m[2][1] = 0.0f; m[2][2] = 1.0f; m[2][3] = 0.0f;
    m[3][0] = 0.0f; m[3][1] = 0.0f; m[3][2] = 0.0f; m[3][3] = 1.0f;
}

static inline void n64_mtxf_copy(Matrix4f dest, const Matrix4f src) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            dest[i][j] = src[i][j];
        }
    }
}

static inline void n64_mtxf_mul(Matrix4f dest, const Matrix4f a, const Matrix4f b) {
    Matrix4f tmp;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            tmp[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + a[i][3] * b[3][j];
        }
    }
    n64_mtxf_copy(dest, tmp);
}

static inline void n64_mtxf_translate(Matrix4f m, f32 x, f32 y, f32 z) {
    n64_mtxf_identity(m);
    m[3][0] = x;
    m[3][1] = y;
    m[3][2] = z;
}

static inline void n64_mtxf_scale(Matrix4f m, f32 x, f32 y, f32 z) {
    n64_mtxf_identity(m);
    m[0][0] = x;
    m[1][1] = y;
    m[2][2] = z;
}

// Look-at and perspective matrix generation
void n64_mtxf_lookat(Matrix4f m, f32 eyeX, f32 eyeY, f32 eyeZ,
                     f32 atX, f32 atY, f32 atZ,
                     f32 upX, f32 upY, f32 upZ);

void n64_mtxf_perspective(Matrix4f m, f32 fovy, f32 aspect, f32 near, f32 far);

// Random number generation (matches N64 rand_int behavior)
s32 n64_rand_int(s32 max);
void n64_rand_seed(u32 seed);

// Float to integer conversion (N64-specific rounding modes)
static inline s32 n64_float_to_s32(f32 f) {
    return (s32)f;
}

static inline s16 n64_float_to_s16(f32 f) {
    return (s16)f;
}

// Linear interpolation
static inline f32 n64_lerp_f(f32 a, f32 b, f32 t) {
    return a + (b - a) * t;
}

// Clamp
static inline f32 n64_clamp_f(f32 val, f32 min, f32 max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

static inline s32 n64_clamp_s(s32 val, s32 min, s32 max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

#endif
