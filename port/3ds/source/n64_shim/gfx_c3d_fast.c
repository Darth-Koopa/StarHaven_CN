/*
 * gfx_c3d_fast.c — the per-draw citro3d work, made cheaper.
 *
 * On an Old 3DS every C3D_DrawElements cost ~30 us: the context flush scans
 * both shaders' 96 float-uniform dirty flags (we only change uniforms when a
 * screen starts), then fifteen GPUCMD_Add calls write the draw itself. With
 * 130-170 draws a frame that was several milliseconds.
 *
 *  - C3D_UpdateUniforms is wrapped (-Wl,--wrap): it only runs when a uniform
 *    is dirty or another shader program was bound (binding uploads the
 *    program's constants).
 *  - pm3ds_c3d_draw_tris writes the draw's registers straight into the
 *    command buffer, the same words C3D_DrawElements writes.
 *
 * Both need the head of citro3d's private context (flags, bound program). Its
 * layout is checked once at start; on a mismatch everything goes through
 * citro3d as before. -DPM3DS_C3D_VERIFY builds compare every fast draw and
 * every skipped uniform update with what citro3d itself writes.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <3ds.h>
#include <citro3d.h>

#include "pm3ds_log.h"

/* citro3d 1.7.1 source/internal.h: the start of C3D_Context */
typedef struct {
    gxCmdQueue_s gxQueue;
    u32 *cmdBuf;
    size_t cmdBufSize;
    float cmdBufUsage;
    u32 flags;
    shaderProgram_s *program;
} c3d_ctx_head_t;
extern c3d_ctx_head_t __C3D_Context;
#define C3DiF_Active   BIT(0)
#define C3DiF_DrawUsed BIT(1)

extern void C3Di_UpdateContext(void);
extern void __real_C3D_UpdateUniforms(GPU_SHADER_TYPE type);

static bool s_fast;

/* free slots in citro3d's GX command queue (32 entries, emptied each frame).
 * Every C3D_FrameSplit and every transfer takes one; a full queue is a
 * svcBreak in libctru's gxCmdQueueAdd (GitHub #57) */
int pm3ds_gx_queue_room(void) {
    return (int)__C3D_Context.gxQueue.maxEntries - (int)__C3D_Context.gxQueue.numEntries;
}

void pm3ds_c3d_fast_init(size_t cmdbuf_bytes) {
    /* C3D_Init keeps the buffer size in words, rounded up to 16 bytes */
    size_t sz = __C3D_Context.cmdBufSize;
    s_fast = (__C3D_Context.flags & C3DiF_Active) && __C3D_Context.cmdBuf != NULL &&
             sz == ((cmdbuf_bytes + 0xF) & ~(size_t)0xF) / 4;
    pm3ds_log(s_fast ? PM3DS_LOG_INFO : PM3DS_LOG_WARN, "gfx: fast citro3d path %s (flags=%08lX size=%u)",
              s_fast ? "on" : "OFF, context layout not as expected", (unsigned long)__C3D_Context.flags, (unsigned)sz);
}

static bool unif_dirty(GPU_SHADER_TYPE type) {
    const bool *fv = C3D_FVUnifDirty[type];
    u32 any = 0;
    for (int i = 0; i < C3D_FVUNIF_COUNT; i++) any |= fv[i];
    for (int i = 0; i < C3D_IVUNIF_COUNT; i++) any |= C3D_IVUnifDirty[type][i];
    return any || C3D_BoolUnifsDirty[type];
}

#ifdef PM3DS_STEP_PROF
/* profiling: which dirty flags C3Di_UpdateContext finds at each draw */
unsigned pm3ds_c3d_flag_draws, pm3ds_c3d_flag_cnt[32];
void pm3ds_c3d_count_flags(void) {
    u32 f = __C3D_Context.flags;
    pm3ds_c3d_flag_draws++;
    for (int b = 0; b < 32; b++)
        if (f & (1u << b)) pm3ds_c3d_flag_cnt[b]++;
}
#endif

#ifdef PM3DS_C3D_VERIFY
static unsigned s_ver_draws, s_ver_bad, s_ver_skips, s_ver_skip_bad;
#endif

void __wrap_C3D_UpdateUniforms(GPU_SHADER_TYPE type) {
    static shaderProgram_s *seen[2];
    shaderProgram_s *p = __C3D_Context.program;
    if (!s_fast || p != seen[type] || unif_dirty(type)) {
        seen[type] = p;
        __real_C3D_UpdateUniforms(type);
        return;
    }
#ifdef PM3DS_C3D_VERIFY
    {   /* a skipped update must have been a no-op */
        u32 *buf, size, off0, off1;
        GPUCMD_GetBuffer(&buf, &size, &off0);
        __real_C3D_UpdateUniforms(type);
        GPUCMD_GetBuffer(&buf, &size, &off1);
        s_ver_skips++;
        if (off1 != off0) {
            if (s_ver_skip_bad++ < 4) pm3ds_log(PM3DS_LOG_WARN, "C3DVERIFY skipped uniform update wrote %u words", (unsigned)(off1 - off0));
        }
    }
#endif
}

/* the words C3D_DrawElements(GPU_TRIANGLES, count, C3D_UNSIGNED_SHORT, idx) adds after its context flush */
#define DRAW_WORDS 30
#define HDR(reg, mask) (((u32)(mask) << 16) | (u32)(reg))
static const u32 k_draw[DRAW_WORDS] = {
    GPU_GEOMETRY_PRIM, HDR(GPUREG_PRIMITIVE_CONFIG, 2),
    1,                 HDR(GPUREG_RESTART_PRIMITIVE, 0xF),
    0,                 HDR(GPUREG_INDEXBUFFER_CONFIG, 0xF),   /* [4]: offset | 16-bit */
    0,                 HDR(GPUREG_NUMVERTICES, 0xF),          /* [6]: count */
    0,                 HDR(GPUREG_VERTEX_OFFSET, 0xF),
    0x100,             HDR(GPUREG_GEOSTAGE_CONFIG, 2),
    0x100,             HDR(GPUREG_GEOSTAGE_CONFIG2, 2),
    0,                 HDR(GPUREG_START_DRAW_FUNC0, 1),
    1,                 HDR(GPUREG_DRAWELEMENTS, 0xF),
    1,                 HDR(GPUREG_START_DRAW_FUNC0, 1),
    0,                 HDR(GPUREG_GEOSTAGE_CONFIG, 2),
    0,                 HDR(GPUREG_GEOSTAGE_CONFIG2, 2),
    1,                 HDR(GPUREG_VTX_FUNC, 0xF),
    0,                 HDR(GPUREG_PRIMITIVE_CONFIG, 8),
    0,                 HDR(GPUREG_PRIMITIVE_CONFIG, 8),
};
#define BUFFER_BASE_PADDR 0x18000000u   /* citro3d's BufInfo_Init base */

/* Draw `count` 16-bit indices as triangles (the attribute buffer is already
 * set). Same effect as C3D_DrawElements; false when it can't be done here
 * and the caller should use C3D_DrawElements. */
bool pm3ds_c3d_draw_tris(const u16 *idx, int count) {
    if (!s_fast) return false;
    u32 pa = osConvertVirtToPhys(idx);
    if (pa < BUFFER_BASE_PADDR) return false;
    C3Di_UpdateContext();
    if (gpuCmdBufOffset + DRAW_WORDS > gpuCmdBufSize) return false;   /* C3D_DrawElements: svcBreak */
#ifdef PM3DS_C3D_VERIFY
    u32 off0 = gpuCmdBufOffset;
    C3D_DrawElements(GPU_TRIANGLES, count, C3D_UNSIGNED_SHORT, idx);
    u32 ref[DRAW_WORDS + 8];
    u32 nref = gpuCmdBufOffset - off0;
    if (nref <= DRAW_WORDS + 8) memcpy(ref, gpuCmdBuf + off0, nref * 4);
    GPUCMD_SetBufferOffset(off0);
#endif
    u32 *b = gpuCmdBuf + gpuCmdBufOffset;
    memcpy(b, k_draw, sizeof k_draw);
    b[4] = (pa - BUFFER_BASE_PADDR) | (1u << 31);
    b[6] = (u32)count;
    gpuCmdBufOffset += DRAW_WORDS;
    __C3D_Context.flags |= C3DiF_DrawUsed;
#ifdef PM3DS_C3D_VERIFY
    s_ver_draws++;
    if (nref != DRAW_WORDS || memcmp(ref, b, sizeof k_draw)) {
        if (s_ver_bad++ < 4) {
            pm3ds_log(PM3DS_LOG_WARN, "C3DVERIFY draw mismatch: citro3d wrote %u words", (unsigned)nref);
            for (u32 i = 0; i < nref && i < DRAW_WORDS + 8; i += 2)
                pm3ds_log(PM3DS_LOG_WARN, "C3DVERIFY  %2u: c3d %08lX %08lX  fast %08lX %08lX", (unsigned)i,
                          (unsigned long)ref[i], (unsigned long)ref[i + 1],
                          (unsigned long)(i < DRAW_WORDS ? b[i] : 0), (unsigned long)(i + 1 < DRAW_WORDS ? b[i + 1] : 0));
        }
    }
    if ((s_ver_draws & 4095) == 0)
        pm3ds_log(PM3DS_LOG_INFO, "C3DVERIFY draws=%u bad=%u uniform skips=%u bad=%u", s_ver_draws, s_ver_bad, s_ver_skips, s_ver_skip_bad);
#endif
    return true;
}
