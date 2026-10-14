/*
 * nugfx_task.c — M8: bridge nuGfxTaskStart to osSpTaskStart.
 *
 * The decomp's gfxRetrace_Callback runs gfx_draw_frame which builds
 * a Gfx[] display list and submits it via nuGfxTaskStart. On the
 * N64 nuSys, that wraps the Gfx* in an OSTask and hands it to the
 * RSP. On 3DS we want the same Gfx* to flow into our existing
 * F3DEX2→PICA200 translator (gfx_dl.c / gfx_present.c).
 *
 * Implementation: populate one of N rotating OSTask slots with the
 * supplied Gfx* and forward to osSpTaskStart, which queues the task
 * in the single-slot ring. The main thread's frame loop drains the
 * queue, walks the Gfx list, and submits to citro3d.
 *
 * Slot rotation: osSpTaskStart blocks if the ring is full, so only
 * one task is ever queued at once. But the slot used by a queued
 * task is still being read by the drainer when the next
 * nuGfxTaskStart call begins to populate the NEXT slot. With 4
 * slots we have generous headroom; 2 would technically suffice.
 */

#include <stdint.h>
#include <string.h>
#include <stddef.h>

#include "pm3ds_log.h"

/* OSTask layout MUST match the one inside gfx_dl.c byte-for-byte —
 * the drainer reads `data_ptr` at offset 88 (an earlier version of
 * the struct miscalculated padding and silently produced NULL DL
 * pointers, hence the persistent comment in port_main.c). */
typedef struct OSTask {
    uint32_t type;             uint32_t flags;
    uint64_t ucode_boot;       uint32_t ucode_boot_size;
    uint64_t ucode;            uint32_t ucode_size;
    uint64_t ucode_data;       uint32_t ucode_data_size;
    uint64_t dram_stack;       uint32_t dram_stack_size;
    uint64_t output_buff;      uint64_t output_buff_size;
    uint64_t data_ptr;         uint32_t data_size;
    uint32_t yield_data_ptr;   uint32_t yield_data_size;
} OSTask;

extern void osSpTaskStart(OSTask *t);

#define PM3DS_NUGFX_SLOTS 4
static OSTask  g_nugfx_slots[PM3DS_NUGFX_SLOTS];
static unsigned g_nugfx_idx = 0;

/* M8.8: nusys color-frame-buffer globals. The real nugfxtaskmgr.c that
 * defines these is excluded from the build (this file is its shim), but
 * decomp render code references them — e.g. screen_render_util.c's
 * appendGfx_draw_prev_frame_buffer walks nuGfxCfb[] to find the prior
 * frame for the motion-blur/transition texrects. This port uses a
 * single-buffer atomic-swap CFB model (os_vi.c), so we expose a 1-entry
 * table pointing at the current framebuffer. With nuGfxCfbNum == 1 the
 * decomp's `(i + N - 1) % N` previous-buffer math degenerates to "the
 * same buffer", which is the right answer when there's only one.
 *
 * Types mirror include/nu/nusys.h:
 *   extern u32   nuGfxCfbNum;   number of frame buffers
 *   extern u16** nuGfxCfb;      array of pointers to frame buffers */
extern void *osViGetCurrentFramebuffer(void);

/* M12: real nusys-style buffers. The PICA renders the picture, but the
 * game still compares and hands out these pointers: SETCIMG(nuGfxCfb_ptr)
 * vs SETZIMG(nuGfxZBuffer) is how the translator tells the screen from the
 * z-clear (both were NULL, so every frame looked like a z-clear and nothing
 * drew), and state_pause/state_battle/state_file_select wait for
 * nuGfxCfb_ptr to land on nuGfxCfb[1]/[2] before using a captured frame, so
 * the pointer must rotate like nusys does after each swap-buffer task. */
#define PM3DS_CFB_COUNT 3
static uint16_t g_cfb_store[PM3DS_CFB_COUNT][320 * 240] __attribute__((aligned(64)));
static uint16_t g_zbuf_store[320 * 240] __attribute__((aligned(64)));
uint32_t   nuGfxCfbNum = PM3DS_CFB_COUNT;
static uint16_t *g_nugfx_cfb_table[PM3DS_CFB_COUNT] = { g_cfb_store[0], g_cfb_store[1], g_cfb_store[2] };
uint16_t **nuGfxCfb = g_nugfx_cfb_table;
uint32_t   nuGfxCfbCounter = 1;
extern uint16_t *nuGfxCfb_ptr;     /* src/cam_main.c */
extern uint16_t *nuGfxZBuffer;     /* src/main.c */

__attribute__((constructor)) static void pm3ds_nugfx_buffers_init(void) {
    nuGfxCfb_ptr = g_cfb_store[0];
    nuGfxZBuffer = g_zbuf_store;
}

/* Kept for the retrace bridge; the buffers are owned here now. */
void pm3ds_nugfx_cfb_sync(void) {}

static void pm3ds_nugfx_swap(void) {
    nuGfxCfb_ptr = nuGfxCfb[nuGfxCfbCounter];
    nuGfxCfbCounter = (nuGfxCfbCounter + 1) % PM3DS_CFB_COUNT;
}

void nuGfxTaskStart(void *gfxList_ptr, uint32_t gfxListSize,
                    uint32_t ucode, uint32_t flag)
{
    static int first_logged = 0;
    static int first_nonempty_logged = 0;
    if (!first_logged) {
        first_logged = 1;
        pm3ds_log(PM3DS_LOG_INFO,
                  "M8: first nuGfxTaskStart (gfx=%p size=%u)",
                  gfxList_ptr, (unsigned)gfxListSize);
    }

    /* size=0 = "empty DL" = the caller built a list but added no
     * Gfx commands before submitting (gfx_draw_frame's early-return
     * path; gfx_task_background when its internal draws are
     * stubbed). The buffer contents are then undefined — walking it
     * would read garbage and probably loop forever because no
     * G_ENDDL terminator was emitted. Drop these on the floor. */
    if (gfxListSize == 0) {
        (void)ucode; (void)flag;
        return;
    }

    /*
     * M9 safety net, REQUIRED by the assert change. include/macros.h now routes
     * a failed ASSERT to pm3ds_assert_failed() (log + RETURN) instead of
     * `do {} while(true)`, so gfx_task_background() and gfx_draw_frame() can now
     * reach here AFTER their display-list bounds ASSERT has already failed
     * (src/main_loop.c:200 and :289) -- with a size that runs past the end of
     * the buffer they were filling. Walking that would feed matrixStack and the
     * sibling DisplayContext to the translator as Gfx words.
     *
     * Before the assert change this could not happen (the assert hung first), so
     * this clamp is the price of making asserts non-fatal -- and it is the one
     * place where "log and continue" is genuinely unsafe without a guard.
     *
     * Which buffer we were handed is inferred from `flag`: gfx_task_background
     * submits with NU_SC_NOSWAPBUFFER (src/main_loop.c:203-204) and
     * gfx_draw_frame with NU_SC_TASK_LODABLE|NU_SC_SWAPBUFFER (:293-294). The
     * decomp's nuGfxTaskStart signature carries no capacity, so this is a
     * heuristic; if a future caller passes some other flag it degrades to the
     * larger cap, which is still bounded and still safe.
     *
     * Counts mirror include/common_structs.h:2266-2279. NOTE the version split:
     * this port builds -DVERSION_PAL=1 (port/3ds/Makefile:152), so mainGfx is
     * 0x2200 entries, NOT the 0x2080 of the US build.
     */
#if VERSION_JP
#define PM3DS_MAIN_GFX_COUNT 0x2000
#elif VERSION_IQUE || VERSION_PAL
#define PM3DS_MAIN_GFX_COUNT 0x2200
#else
#define PM3DS_MAIN_GFX_COUNT 0x2080
#endif
#define PM3DS_BG_GFX_COUNT 0x200

/* This TU deliberately does not include the decomp headers -- it mirrors their
 * types locally (see the note above about include/nu/nusys.h). Mirror the one
 * flag we need too, from port/3ds/include/n64/gfx_shim.h:36. Beware: the decomp's
 * own include/nu/nusys.h:74-80 gives these DIFFERENT values (NOSWAPBUFFER == 0),
 * and the port header shadows it for src/main_loop.c by include order. The value
 * below must track the header main_loop.c actually sees -- the port one. */
#define PM3DS_NU_SC_NOSWAPBUFFER 0x04

    {
        uint32_t cap = (flag & PM3DS_NU_SC_NOSWAPBUFFER)
                           ? (uint32_t)(PM3DS_BG_GFX_COUNT * 8u)
                           : (uint32_t)(PM3DS_MAIN_GFX_COUNT * 8u);
        if (gfxListSize > cap) {
            pm3ds_warn("nuGfxTaskStart: DL %u bytes > %u cap (flag=0x%x) -- "
                       "source buffer overflowed, clamping",
                       (unsigned)gfxListSize, (unsigned)cap, (unsigned)flag);
            gfxListSize = cap;
        }
    }

    if (!first_nonempty_logged) {
        first_nonempty_logged = 1;
        pm3ds_log(PM3DS_LOG_INFO,
                  "M8: first NON-EMPTY nuGfxTaskStart (gfx=%p size=%u)",
                  gfxList_ptr, (unsigned)gfxListSize);
    }

    unsigned idx = g_nugfx_idx;
    g_nugfx_idx = (idx + 1) & (PM3DS_NUGFX_SLOTS - 1);

    OSTask *t = &g_nugfx_slots[idx];
    memset(t, 0, sizeof(*t));
    t->data_ptr  = (uint64_t)(uintptr_t)gfxList_ptr;
    t->data_size = gfxListSize;

    /* ucode/flag (F3DEX vs F3DEX2; SC_NOSWAPBUFFER etc.) ignored
     * for now — our translator only handles one combiner permutation
     * and doesn't switch microcodes mid-frame. M9+ work. */
    (void)ucode;

    osSpTaskStart(t);
    /* the task "completes" immediately; a swap-buffer task advances the cfb */
    if (!(flag & PM3DS_NU_SC_NOSWAPBUFFER)) pm3ds_nugfx_swap();
}
