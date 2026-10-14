#include <stdint.h>
#include "../include/gfx_state.h"
#include "runner.h"

/* Host shims for what gfx_combiner.c calls into. */
static int g_texenv_calls = 0;
void C3D_TexEnvProgram(int stage, int a, int opa, int b, int opb,
                       int c, int opc, int fn) {
    (void)stage; (void)a; (void)opa; (void)b; (void)opb;
    (void)c; (void)opc; (void)fn;
    g_texenv_calls++;
}
void pm3ds_warn_site(const void *site, const char *fmt, ...) {
    (void)site; (void)fmt;
}
void pm3ds_unimpl_once(const char *name) { (void)name; }

#define PM3DS_HOST_TEST 1
#include "../source/n64_shim/gfx_combiner.c"

PM3DS_TEST(matched_mode_no_fallback) {
    gfx_state_t st = {0};
    st.setcombine = 0x0011FE38FE383F11ull;   /* G_CC_MODULATEIA */
    g_texenv_calls = 0;
    int fallback = gfx_combiner_program(&st);
    PM3DS_EXPECT(fallback == 0);
    PM3DS_EXPECT(g_texenv_calls == 1);
}

PM3DS_TEST(unmatched_mode_uses_fallback) {
    gfx_state_t st = {0};
    st.setcombine = 0xDEADBEEFCAFEBABEull;
    g_texenv_calls = 0;
    int fallback = gfx_combiner_program(&st);
    PM3DS_EXPECT(fallback == 1);
    PM3DS_EXPECT(g_texenv_calls == 1);
}
