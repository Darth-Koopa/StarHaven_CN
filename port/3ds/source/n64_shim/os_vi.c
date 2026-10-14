/*
 * os_vi.c — Video Interface shim.
 * Fidelity tier: MINIMAL — records mode + pending swap; actual presentation
 *                is performed by main.c's outer frame loop.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>

typedef struct OSViMode OSViMode;
struct OSViMode { uint32_t type; uint32_t state[16]; };

static _Atomic(void *) g_pending_cfb = NULL;
static _Atomic(void *) g_current_cfb = NULL;
static OSViMode       *g_mode        = NULL;

void osViSetMode(OSViMode *mode) {
    g_mode = mode;
}

void osViBlack(int active) { (void)active; }
void osViSetSpecialFeatures(uint32_t mask) { (void)mask; }
void osViSetXScale(float xs) { (void)xs; }
void osViSetYScale(float ys) { (void)ys; }
void osViSetEvent(void *mq, void *msg, uint32_t retraceCount) {
    (void)mq; (void)msg; (void)retraceCount;
}

void osViSwapBuffer(void *frameBuffer) {
    atomic_store_explicit(&g_pending_cfb, frameBuffer, memory_order_release);
}

void *osViGetNextFramebuffer(void) {
    return atomic_load_explicit(&g_pending_cfb, memory_order_acquire);
}

void *osViGetCurrentFramebuffer(void) {
    return atomic_load_explicit(&g_current_cfb, memory_order_acquire);
}

/* Consumed by main.c's frame loop. Returns the buffer the game wants
 * displayed, and rotates it to current. NULL if nothing pending. */
void *pm3ds_vi_consume_pending(void) {
    void *p = atomic_exchange_explicit(&g_pending_cfb, NULL, memory_order_acq_rel);
    if (p) atomic_store_explicit(&g_current_cfb, p, memory_order_release);
    return p;
}
