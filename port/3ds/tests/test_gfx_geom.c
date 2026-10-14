#include <stdint.h>
#include "../include/gfx_state.h"
#include "mock_c3d.h"
MOCK_C3D_STORAGE
#include "runner.h"

void pm3ds_unimpl_once(const char *name) { (void)name; }

#define PM3DS_HOST_TEST 1
#include "../source/n64_shim/gfx_translate.c"
#include "../source/n64_shim/gfx_geom.c"

PM3DS_TEST(model_push_pop_round_trips) {
    gfx_state_t st;
    gfx_state_init(&st);
    float m[16];
    for (int i = 0; i < 16; i++) m[i] = (float)i;

    gfx_geom_model_push(&st, m);
    PM3DS_EXPECT(st.model_sp == 1);
    for (int i = 0; i < 16; i++) PM3DS_EXPECT(st.model_stack[0][i] == m[i]);

    gfx_geom_model_pop(&st);
    PM3DS_EXPECT(st.model_sp == 0);
}

PM3DS_TEST(model_push_full_no_overflow) {
    gfx_state_t st;
    gfx_state_init(&st);
    float m[16] = {0};
    for (int i = 0; i < GFX_MTX_STACK_MODEL + 2; i++) {
        gfx_geom_model_push(&st, m);
    }
    PM3DS_EXPECT(st.model_sp == GFX_MTX_STACK_MODEL);  /* clamped */
}

PM3DS_TEST(s15p16_to_float_roundtrips_1_0) {
    /* 1.0 in s15.16 is 0x00010000. */
    int32_t fixed[16] = {0};
    fixed[0] = fixed[5] = fixed[10] = fixed[15] = 0x00010000;
    float out[16];
    gfx_geom_s15p16_to_float(fixed, out);
    PM3DS_EXPECT(out[0] == 1.0f);
    PM3DS_EXPECT(out[5] == 1.0f);
    PM3DS_EXPECT(out[10] == 1.0f);
    PM3DS_EXPECT(out[15] == 1.0f);
}

/* Static so the linker places it in low 32-bit address space under -no-pie;
 * Gfx.w1 is uint32_t to match the N64 ABI and can carry the address. */
static int32_t s_identity_s15p16[16] = {0};

static n64_vtx_t s_verts[3] = {0};

PM3DS_TEST(sp_vtx_loads_into_pool) {
    gfx_state_t st;
    gfx_state_init(&st);
    s_verts[0] = (n64_vtx_t){ .x = 10, .y = 20, .z = 30, .s = 100, .t = 200, .r = 1, .g = 2, .b = 3, .a = 4 };
    s_verts[1] = (n64_vtx_t){ .x = 11, .y = 21, .z = 31, .s = 101, .t = 201, .r = 5, .g = 6, .b = 7, .a = 8 };
    s_verts[2] = (n64_vtx_t){ .x = 12, .y = 22, .z = 32, .s = 102, .t = 202, .r = 9, .g =10, .b =11, .a =12 };
    /* Load 3 vertices into slots 0..2. F3DEX2 places (v0+n) at bit 1:
     * _SHIFTL((v0)+(n),1,7) — a SINGLE shift, so bits [7:1] hold 3, not 6.
     * (The old constant 6<<1 pre-doubled the value to cancel a matching /2
     * bug in handle_sp_vtx; both are fixed now — this matches the port's
     * own pm3ds_gSPVertex_impl and authored map DLs.) */
    Gfx g = {
        .w0 = ((uint32_t)0x01 << 24) | (3u << 12) | (3u << 1),
        .w1 = (uint32_t)(uintptr_t)s_verts,
    };
    (void)handle_sp_vtx(&st, &g);
    PM3DS_EXPECT(st.vtx_pool[0].x == 10);
    PM3DS_EXPECT(st.vtx_pool[1].y == 21);
    PM3DS_EXPECT(st.vtx_pool[2].r == 9);
}

PM3DS_TEST(sp_matrix_load_model_push) {
    gfx_state_t st;
    gfx_state_init(&st);
    s_identity_s15p16[0]  = 0x00010000;
    s_identity_s15p16[5]  = 0x00010000;
    s_identity_s15p16[10] = 0x00010000;
    s_identity_s15p16[15] = 0x00010000;
    Gfx g = {
        .w0 = ((uint32_t)0xDA << 24) | 0x00,    /* G_SP_MATRIX, param=push|load|model */
        .w1 = (uint32_t)(uintptr_t)s_identity_s15p16,
    };
    (void)handle_sp_matrix(&st, &g);
    PM3DS_EXPECT(st.model_sp == 1);
    PM3DS_EXPECT(st.model_stack[0][0] == 1.0f);
}
