#include <stdint.h>
#include <stddef.h>
#include "../include/gfx_state.h"
#include "mock_c3d.h"
MOCK_C3D_STORAGE
#include "runner.h"

/* Host stub — the real implementation is in source/platform/pm3ds_panic.c
 * which we don't link into host tests. */
void pm3ds_unimpl_once(const char *name) { (void)name; }

/* gfx_state_init / mark_dirty live in gfx_translate.c; pull just the
 * function bodies in by including the .c. */
#define PM3DS_HOST_TEST 1
#include "../source/n64_shim/gfx_translate.c"

PM3DS_TEST(state_init_zeros_all_substates) {
    gfx_state_t st;
    /* Pre-poison the struct so init must clear it. */
    for (size_t i = 0; i < sizeof(st); i++) ((uint8_t *)&st)[i] = 0xAA;
    gfx_state_init(&st);
    PM3DS_EXPECT(st.setcombine == 0);
    PM3DS_EXPECT(st.othermode_h == 0);
    PM3DS_EXPECT(st.othermode_l == 0);
    PM3DS_EXPECT(st.geometry_mode == 0);
    PM3DS_EXPECT(st.model_sp == 0);
    PM3DS_EXPECT(st.proj_sp == 0);
    PM3DS_EXPECT(st.pending_tri_count == 0);
    PM3DS_EXPECT(st.dirty == GFX_DIRTY_ALL);
}

PM3DS_TEST(mark_dirty_ors_bits) {
    gfx_state_t st;
    gfx_state_init(&st);
    st.dirty = 0;
    gfx_state_mark_dirty(&st, GFX_DIRTY_COMBINE);
    gfx_state_mark_dirty(&st, GFX_DIRTY_VIEWPORT);
    PM3DS_EXPECT(st.dirty == (GFX_DIRTY_COMBINE | GFX_DIRTY_VIEWPORT));
}

PM3DS_TEST(walk_terminates_on_enddl) {
    gfx_state_t st;
    gfx_state_init(&st);
    Gfx dl[3] = {
        { .w0 = (uint32_t)G_NOOP << 24, .w1 = 0 },
        { .w0 = (uint32_t)G_NOOP << 24, .w1 = 0 },
        { .w0 = (uint32_t)G_ENDDL << 24, .w1 = 0 },
    };
    int n = gfx_walk_dl(&st, dl);
    PM3DS_EXPECT(n == 2);   /* NOOP + NOOP, then ENDDL stops the walk */
}

PM3DS_TEST(walk_skips_unhandled_opcode) {
    gfx_state_t st;
    gfx_state_init(&st);
    Gfx dl[2] = {
        { .w0 = (uint32_t)0x42 << 24, .w1 = 0 },   /* unhandled */
        { .w0 = (uint32_t)G_ENDDL << 24, .w1 = 0 },
    };
    int n = gfx_walk_dl(&st, dl);
    PM3DS_EXPECT(n == 1);
}

PM3DS_TEST(setcombine_stored_and_dirty) {
    gfx_state_t st;
    gfx_state_init(&st);
    st.dirty = 0;
    Gfx g = {
        .w0 = ((uint32_t)G_SETCOMBINE << 24) | 0xAABBCC,
        .w1 = 0x11223344,
    };
    (void)handle_setcombine(&st, &g);
    PM3DS_EXPECT_EQ_U32((uint32_t)(st.setcombine & 0xFFFFFFFF), 0x11223344);
    PM3DS_EXPECT((st.dirty & GFX_DIRTY_COMBINE) != 0);
}

static Gfx s_sub_dl[2];
static Gfx s_outer_dl[3];

PM3DS_TEST(fillrect_emits_two_tris_one_draw) {
    mock_c3d_reset();
    gfx_state_t st;
    gfx_state_init(&st);
    Gfx dl[2] = {
        { .w0 = ((uint32_t)G_FILLRECT << 24) | (100u << 14) | (50u << 2),
          .w1 = (10u << 14) | (5u << 2) },
        { .w0 = ((uint32_t)G_ENDDL << 24), .w1 = 0 },
    };
    gfx_walk_dl(&st, dl);
    int draws = 0;
    int total_verts = 0;
    for (int i = 0; i < mc3d_log_n; i++) {
        if (mc3d_log_buf[i].kind == MC3D_DRAW_ARRAYS) {
            draws++;
            total_verts += (int)mc3d_log_buf[i].arg_u32[2];
        }
    }
    PM3DS_EXPECT(draws == 1);
    PM3DS_EXPECT(total_verts == 6);   /* 2 tris × 3 verts each */
}

PM3DS_TEST(settile_fields_decoded) {
    gfx_state_t st;
    gfx_state_init(&st);
    /* fmt=2 (IA), siz=1 (8b), line=0x10, tmem=0x100, tile=3, palette=5 */
    Gfx g = {
        .w0 = ((uint32_t)G_SETTILE << 24) | (2u << 21) | (1u << 19) | (0x10u << 9) | 0x100u,
        .w1 = (3u << 24) | (5u << 20),
    };
    (void)handle_settile(&st, &g);
    PM3DS_EXPECT(st.tiles[3].fmt == 2);
    PM3DS_EXPECT(st.tiles[3].siz == 1);
    PM3DS_EXPECT(st.tiles[3].line == 0x10);
    PM3DS_EXPECT(st.tiles[3].tmem == 0x100);
    PM3DS_EXPECT(st.tiles[3].palette == 5);
}

PM3DS_TEST(dl_recurses_then_returns) {
    mock_c3d_reset();
    gfx_state_t st;
    gfx_state_init(&st);

    s_sub_dl[0] = (Gfx){ .w0 = ((uint32_t)G_TRI1 << 24) | (0u << 16) | (2u << 8) | 4u, .w1 = 0 };
    s_sub_dl[1] = (Gfx){ .w0 = ((uint32_t)G_ENDDL << 24), .w1 = 0 };

    s_outer_dl[0] = (Gfx){ .w0 = ((uint32_t)G_DL << 24) | 0u, .w1 = (uint32_t)(uintptr_t)s_sub_dl };
    s_outer_dl[1] = (Gfx){ .w0 = ((uint32_t)G_TRI1 << 24) | (0u << 16) | (2u << 8) | 4u, .w1 = 0 };
    s_outer_dl[2] = (Gfx){ .w0 = ((uint32_t)G_ENDDL << 24), .w1 = 0 };

    gfx_walk_dl(&st, s_outer_dl);

    int n_draws = 0;
    for (int i = 0; i < mc3d_log_n; i++) {
        if (mc3d_log_buf[i].kind == MC3D_DRAW_ARRAYS) n_draws++;
    }
    /* sub flushes its one tri at its ENDDL; outer flushes its one tri at its ENDDL */
    PM3DS_EXPECT(n_draws == 2);
}

PM3DS_TEST(tri1_then_enddl_emits_one_drawcall) {
    mock_c3d_reset();
    gfx_state_t st;
    gfx_state_init(&st);
    Gfx dl[2] = {
        { .w0 = ((uint32_t)G_TRI1 << 24) | (0u << 16) | (2u << 8) | 4u, .w1 = 0 },
        { .w0 = ((uint32_t)G_ENDDL << 24), .w1 = 0 },
    };
    gfx_walk_dl(&st, dl);
    /* exactly one MC3D_DRAW_ARRAYS in the log */
    int n_draws = 0;
    for (int i = 0; i < mc3d_log_n; i++) {
        if (mc3d_log_buf[i].kind == MC3D_DRAW_ARRAYS) n_draws++;
    }
    PM3DS_EXPECT(n_draws == 1);
    /* 1 tri × 3 verts; the third arg_u32 is `count` */
    int found_three = 0;
    for (int i = 0; i < mc3d_log_n; i++) {
        if (mc3d_log_buf[i].kind == MC3D_DRAW_ARRAYS &&
            mc3d_log_buf[i].arg_u32[2] == 3u) {
            found_three = 1;
        }
    }
    PM3DS_EXPECT(found_three == 1);
}

PM3DS_TEST(setprimcolor_round_trips) {
    gfx_state_t st;
    gfx_state_init(&st);
    Gfx g = { .w0 = (uint32_t)G_SETPRIMCOLOR << 24, .w1 = 0x11223344 };
    (void)handle_setprimcolor(&st, &g);
    PM3DS_EXPECT(st.prim_rgba[0] == 0x11);
    PM3DS_EXPECT(st.prim_rgba[1] == 0x22);
    PM3DS_EXPECT(st.prim_rgba[2] == 0x33);
    PM3DS_EXPECT(st.prim_rgba[3] == 0x44);
    PM3DS_EXPECT((st.dirty & GFX_DIRTY_COLORS) != 0);
}

PM3DS_TEST(movemem_viewport_decodes) {
    gfx_state_t st;
    gfx_state_init(&st);
    /* static so the linker places it in .data (low 32-bit address
     * space under -no-pie); passing as Gfx.w1 is safe. */
    static int16_t vp[8] = { 160, 120, 511, 0, 160, 120, 511, 0 };
    Gfx g = {
        .w0 = ((uint32_t)0xDC << 24) | G_MV_VIEWPORT,
        .w1 = (uint32_t)(uintptr_t)vp,
    };
    (void)handle_movemem(&st, &g);
    PM3DS_EXPECT(st.viewport.scale[0] == 160);
    PM3DS_EXPECT(st.viewport.trans[2] == 511);
    PM3DS_EXPECT((st.dirty & GFX_DIRTY_VIEWPORT) != 0);
}

PM3DS_TEST(setscissor_decodes_corners) {
    gfx_state_t st;
    gfx_state_init(&st);
    /* uls=20, ult=10, lrs=320, lrt=240 — 12-bit fields, no fixed shift. */
    Gfx g = {
        .w0 = ((uint32_t)G_SETSCISSOR << 24) | (20u << 12) | 10u,
        .w1 = (320u << 12) | 240u,
    };
    (void)handle_setscissor(&st, &g);
    PM3DS_EXPECT(st.scissor.uls == 20);
    PM3DS_EXPECT(st.scissor.ult == 10);
    PM3DS_EXPECT(st.scissor.lrs == 320);
    PM3DS_EXPECT(st.scissor.lrt == 240);
}
