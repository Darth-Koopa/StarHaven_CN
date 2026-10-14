#ifndef PM3DS_MOCK_C3D_H
#define PM3DS_MOCK_C3D_H

#include <stdint.h>
#include <string.h>

/* Records a fixed-size, append-only log of C3D-shaped calls the
 * translator made. Tests inspect the log via mock_c3d_log() and
 * reset it via mock_c3d_reset(). */

typedef enum {
    MC3D_FRAME_BEGIN,
    MC3D_FRAME_END,
    MC3D_FRAME_DRAW_ON,
    MC3D_RT_CLEAR,
    MC3D_TEXENV_PROGRAM,
    MC3D_TEX_BIND,
    MC3D_TEX_UPLOAD,
    MC3D_SET_SCISSOR,
    MC3D_SET_VIEWPORT,
    MC3D_SET_MTX_MODEL,
    MC3D_SET_MTX_PROJ,
    MC3D_DRAW_ARRAYS,
} mc3d_kind_t;

typedef struct {
    mc3d_kind_t kind;
    uint32_t    arg_u32[4];   /* freeform payload per kind */
    void       *arg_ptr;
} mc3d_entry_t;

#define MC3D_LOG_CAP 256
extern mc3d_entry_t mc3d_log_buf[MC3D_LOG_CAP];
extern int          mc3d_log_n;

static inline void mock_c3d_reset(void) {
    memset(mc3d_log_buf, 0, sizeof(mc3d_log_buf));
    mc3d_log_n = 0;
}

static inline void mc3d_push(mc3d_kind_t k, uint32_t a0, uint32_t a1,
                             uint32_t a2, uint32_t a3, void *p) {
    if (mc3d_log_n >= MC3D_LOG_CAP) return;
    mc3d_log_buf[mc3d_log_n++] = (mc3d_entry_t){
        .kind = k, .arg_u32 = {a0, a1, a2, a3}, .arg_ptr = p,
    };
}

/* Storage for the log lives in whichever test TU pulls this header
 * in. Drop one MOCK_C3D_STORAGE line at file scope in the test .c. */
#define MOCK_C3D_STORAGE \
    mc3d_entry_t mc3d_log_buf[MC3D_LOG_CAP]; \
    int          mc3d_log_n = 0;

/* The C3D_* names the translator code under test actually calls. */
static inline void C3D_FrameBegin(int flags) {
    mc3d_push(MC3D_FRAME_BEGIN, (uint32_t)flags, 0, 0, 0, NULL);
}
static inline void C3D_FrameEnd(int flags) {
    mc3d_push(MC3D_FRAME_END, (uint32_t)flags, 0, 0, 0, NULL);
}
static inline void C3D_FrameDrawOn(void *rt) {
    mc3d_push(MC3D_FRAME_DRAW_ON, 0, 0, 0, 0, rt);
}
static inline void C3D_RenderTargetClear(void *rt, int what,
                                         uint32_t color, uint32_t depth) {
    mc3d_push(MC3D_RT_CLEAR, (uint32_t)what, color, depth, 0, rt);
}
static inline void C3D_SetScissor(int mode, uint32_t l, uint32_t b,
                                  uint32_t r, uint32_t t) {
    mc3d_push(MC3D_SET_SCISSOR, (uint32_t)mode, l, b, r, NULL);
    /* `t` carried in arg_u32[3] of the most recent entry. */
    if (mc3d_log_n > 0) mc3d_log_buf[mc3d_log_n - 1].arg_u32[3] = t;
}
static inline void C3D_SetViewport(uint32_t x, uint32_t y,
                                   uint32_t w, uint32_t h) {
    mc3d_push(MC3D_SET_VIEWPORT, x, y, w, h, NULL);
}
static inline void C3D_DrawArrays(int prim, int first, int count) {
    mc3d_push(MC3D_DRAW_ARRAYS, (uint32_t)prim, (uint32_t)first,
              (uint32_t)count, 0, NULL);
}

#endif /* PM3DS_MOCK_C3D_H */
