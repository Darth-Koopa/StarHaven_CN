#include "game_fwd.h"

#include "ultra64.h"
#include "n64/math_shim.h"

// math.h is no longer included via math_shim.h, so pull it in here for
// the math_impl function bodies.
#include <math.h>

// ============================================================================
// Math Implementation (Standard C Library + 3DS Extensions)
// ============================================================================

void n64_mtxf_lookat(Matrix4f m, f32 eyeX, f32 eyeY, f32 eyeZ,
                     f32 atX, f32 atY, f32 atZ,
                     f32 upX, f32 upY, f32 upZ) {
    Vec3f f = { atX - eyeX, atY - eyeY, atZ - eyeZ };
    n64_vec3f_normalize(&f);

    Vec3f up = { upX, upY, upZ };
    n64_vec3f_normalize(&up);

    Vec3f s;
    n64_vec3f_cross(&s, &f, &up);
    n64_vec3f_normalize(&s);

    Vec3f u;
    n64_vec3f_cross(&u, &s, &f);

    m[0][0] = s.x;  m[0][1] = s.y;  m[0][2] = s.z;  m[0][3] = 0.0f;
    m[1][0] = u.x;  m[1][1] = u.y;  m[1][2] = u.z;  m[1][3] = 0.0f;
    m[2][0] = -f.x; m[2][1] = -f.y; m[2][2] = -f.z; m[2][3] = 0.0f;
    m[3][0] = -(s.x * eyeX + s.y * eyeY + s.z * eyeZ);
    m[3][1] = -(u.x * eyeX + u.y * eyeY + u.z * eyeZ);
    m[3][2] =  (f.x * eyeX + f.y * eyeY + f.z * eyeZ);
    m[3][3] = 1.0f;
}

void n64_mtxf_perspective(Matrix4f m, f32 fovy, f32 aspect, f32 near, f32 far) {
    f32 f = 1.0f / tanf(fovy * 0.5f);
    f32 nf = 1.0f / (near - far);

    m[0][0] = f / aspect; m[0][1] = 0.0f; m[0][2] = 0.0f;        m[0][3] = 0.0f;
    m[1][0] = 0.0f;       m[1][1] = f;    m[1][2] = 0.0f;        m[1][3] = 0.0f;
    m[2][0] = 0.0f;       m[2][1] = 0.0f; m[2][2] = (far + near) * nf;  m[2][3] = -1.0f;
    m[3][0] = 0.0f;       m[3][1] = 0.0f; m[3][2] = 2.0f * far * near * nf; m[3][3] = 0.0f;
}

static u32 s_randSeed = 1;

s32 n64_rand_int(s32 max) {
    if (max <= 0) return 0;
    s_randSeed = s_randSeed * 1103515245 + 12345;
    return ((s_randSeed >> 16) & 0x7FFF) % max;
}

void n64_rand_seed(u32 seed) {
    s_randSeed = seed;
}

// pm_atan2 (the game's 4-arg atan2, in DEGREES) now comes from src/43F0.c;
// the stand-in that lived here returned radians.

// libultra gu* helpers consumed by the decomp's matrix code. Moved here
// from the retired os_impl.c. These are tier N/A (no N64 semantics to
// match — they're plain linear algebra).
void guNormalize(f32* x, f32* y, f32* z) {
    f32 len = sqrtf((*x) * (*x) + (*y) * (*y) + (*z) * (*z));
    if (len != 0.0f) {
        *x /= len;
        *y /= len;
        *z /= len;
    }
}

void guMtxIdentF(f32 mf[4][4]) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            mf[i][j] = (i == j) ? 1.0f : 0.0f;
        }
    }
}
