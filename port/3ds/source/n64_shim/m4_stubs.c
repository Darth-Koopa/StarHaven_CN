/*
 * m4_stubs.c — graveyard for decomp symbols that we don't have a
 *              real implementation for yet.
 *
 * Each entry is a no-op that logs once-per-call-site via pm3ds_warn
 * (existing rate-limited site-keyed warn from pm3ds_log.c). This
 * file grows wave-by-wave in M4 phase 2; the wave's commit message
 * lists the additions.
 *
 * Stubs added here should be sized so we can audit them later:
 *   - return 0 / NULL / void
 *   - never sleep, never block, never recurse
 *   - one pm3ds_warn line per stub (de-duped by call site)
 *
 * If a stub turns out to be hot or to gate observable behavior,
 * promote it to a real shim implementation in the appropriate
 * n64_shim file and remove from here.
 */

#include <stdint.h>
#include <stddef.h>

#include "pm3ds_log.h"

/* The US boot_main sets up the N64 crash screen and the IS-Viewer debug
 * port; the port has neither (crash_screen.c and is_debug.c are excluded). */
__attribute__((weak)) void crash_screen_init(void) {}
__attribute__((weak)) void is_debug_init(void) {}

/* Convenience macro for a stub that returns a typed zero value.
 * Each stub function should look like:
 *
 *   int32_t example_decomp_symbol(int32_t arg) {
 *       PM3DS_M4_STUB("example_decomp_symbol");
 *       return 0;
 *   }
 *
 * pm3ds_warn() de-duplicates per return-address (i.e., per stub
 * function), so each unique stub logs exactly once at runtime. */
/* once per call site: TLB stubs run on every map change and used to flood
 * the persistent SD warning log (<SD folder>/log.txt) */
#define PM3DS_M4_STUB(name) do { static int warned_; if (!warned_) { warned_ = 1; pm3ds_warn("m4-stub: %s", (name)); } } while (0)

/* M6.1: typed stubs for libultra OS primitives that are declared with
 * specific prototypes in port/3ds/include/include_asm.h. The
 * gen_m6_link_stubs.py generator can't emit these as `void name(void)`
 * because the existing prototypes would fight the empty signature; do
 * them by hand here.
 *
 * None of these have meaningful semantics on the 3DS — there's no MMU
 * to map and no flash sector to erase. The decomp's main_loop / fio /
 * effects paths call them; we return success so the callers proceed.
 */

struct OSIoMesg;
struct OSMesgQueue;

int osMapTLB(int index, int pagemask, void* vaddr,
             int paddr0, int paddr1, int aspid) {
    (void)index; (void)pagemask; (void)vaddr;
    (void)paddr0; (void)paddr1; (void)aspid;
    PM3DS_M4_STUB("osMapTLB");
    return 0;
}
void osUnmapTLB(int index) { (void)index; PM3DS_M4_STUB("osUnmapTLB"); }
void osUnmapTLBAll(void)   { PM3DS_M4_STUB("osUnmapTLBAll"); }

/* osFlash* family moved to os_flash.c (M8.7) — real in-memory-backed
 * implementations that post the DMA-completion message fio_read_flash
 * blocks on. The no-op stubs here deadlocked the game thread in
 * state_step_startup -> fio_load_globals -> osRecvMesg(BLOCK). */

/* M6.1c: traced shims for nuSys gfx/cont functions that boot_main
 * calls directly. These are strong (non-weak) definitions and so
 * override the void-typed weak stubs in m6_link_stubs.c. Logging
 * lets us trace how far boot_main progresses through its init.
 *
 * pm3ds_warn dedupes per call-site, so each function logs exactly
 * once even though some are called repeatedly. */

void nuGfxDisplayOff(void)   { PM3DS_M4_STUB("nuGfxDisplayOff"); }
void nuGfxDisplayOn(void)    { PM3DS_M4_STUB("nuGfxDisplayOn"); }
void nuGfxInit(void)         { PM3DS_M4_STUB("nuGfxInit"); }

/* nuContInit / nuContDataGet: real ones in os_cont.c (M13). */

/* M7: store the registered retrace callback. port_main.c's frame
 * loop invokes this via pm3ds_invoke_gfx_retrace_cb each vblank to
 * drive the decomp's step_game_loop. */
void (*pm3ds_gfx_retrace_cb)(int) = (void (*)(int))0;
void (*pm3ds_gfx_prenmi_cb)(void) = (void (*)(void))0;

void nuGfxFuncSet(void *cb) {
    PM3DS_M4_STUB("nuGfxFuncSet");
    pm3ds_gfx_retrace_cb = (void (*)(int))cb;
}
void nuGfxPreNMIFuncSet(void *cb) {
    PM3DS_M4_STUB("nuGfxPreNMIFuncSet");
    pm3ds_gfx_prenmi_cb = (void (*)(void))cb;
}

/* general_heap_create has a working implementation in heap_impl.c
 * (M6.1d) that returns a real HeapNode and lets subsequent
 * heap_malloc / heap_free work. The traced stub used to live here;
 * removed since the real implementation is what we want. */
