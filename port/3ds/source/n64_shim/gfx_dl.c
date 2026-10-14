/*
 * gfx_dl.c — display-list submit queue + counter.
 * Fidelity tier: FAITHFUL — preserves libultra osSp* blocking semantics.
 *
 * The game thread calls osSpTaskStart to submit one Gfx[] head pointer.
 * The main thread drains the queue once per frame in gfx_present_frame.
 * Capacity is one DL deep: when full, the game thread blocks on
 * g_not_full until the main thread consumes the slot. This preserves
 * the libultra "task done" ordering the decomp's gfx thread expects.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef PM3DS_HOST_TEST
#include "../../tests/ctru_stub.h"
#else
#include <3ds.h>
#endif

typedef struct OSTask {
    uint32_t type;
    uint32_t flags;
    uint64_t ucode_boot;
    uint32_t ucode_boot_size;
    uint64_t ucode;
    uint32_t ucode_size;
    uint64_t ucode_data;
    uint32_t ucode_data_size;
    uint64_t dram_stack;
    uint32_t dram_stack_size;
    uint64_t output_buff;
    uint64_t output_buff_size;
    uint64_t data_ptr;       /* the actual Gfx[] head */
    uint32_t data_size;
    uint32_t yield_data_ptr;
    uint32_t yield_data_size;
} OSTask;

typedef void *OSMesg;
typedef struct OSMesgQueue OSMesgQueue;
int osSendMesg(OSMesgQueue *, OSMesg, int);

/* Single-slot ring. */
static LightLock     g_lock;
static LightEvent    g_not_empty;
static LightEvent    g_not_full;
static int           g_lock_inited = 0;
static OSTask const *g_pending_task = NULL;
static unsigned      g_tasks_submitted = 0;
static unsigned      g_tasks_dropped = 0;
static const void   *g_last_dropped = NULL;

unsigned    pm3ds_gfx_dropped_count(void) { return g_tasks_dropped; }
const void *pm3ds_gfx_last_dropped(void)  { return g_last_dropped; }

static void gfx_dl_ensure_init(void) {
    if (g_lock_inited) return;
    LightLock_Init(&g_lock);
    LightEvent_Init(&g_not_empty, RESET_ONESHOT);
    LightEvent_Init(&g_not_full,  RESET_ONESHOT);
    g_lock_inited = 1;
}

/* M12: per-frame list of submitted tasks. The game submits the background
 * list (gfx_task_background) and then the main list (gfx_draw_frame); the
 * old single slot kept only the last one, which threw the backdrop away
 * every frame (render audit [K1]). Both producer (retrace callback) and
 * drainer (pm3ds_gfx_present_frame) run on the main thread, so this never
 * blocks; if the game somehow submits more than the cap between two
 * presents, the oldest entry is dropped and counted. */
#define PM3DS_DL_QUEUE 8
static const void *g_queue[PM3DS_DL_QUEUE];
static int         g_queue_n = 0;

void osSpTaskStart(OSTask *t) {
    gfx_dl_ensure_init();
    LightLock_Lock(&g_lock);
    if (g_queue_n == PM3DS_DL_QUEUE) {
        g_tasks_dropped++;
        g_last_dropped = g_queue[0];
        memmove(&g_queue[0], &g_queue[1], sizeof(g_queue[0]) * (PM3DS_DL_QUEUE - 1));
        g_queue_n--;
    }
    g_queue[g_queue_n++] = (const void *)(uintptr_t)t->data_ptr;
    g_pending_task = t;
    g_tasks_submitted++;
    LightEvent_Signal(&g_not_empty);
    LightLock_Unlock(&g_lock);
}

/* Drain every queued display list (oldest first) into out[0..max). */
const void *pm3ds_gfx_drain_dl_list(const void **out, int max, int *count) {
    gfx_dl_ensure_init();
    LightLock_Lock(&g_lock);
    int n = g_queue_n < max ? g_queue_n : max;
    for (int i = 0; i < n; i++) out[i] = g_queue[i];
    g_queue_n = 0;
    g_pending_task = NULL;
    LightEvent_Signal(&g_not_full);
    LightLock_Unlock(&g_lock);
    *count = n;
    return n ? out[n - 1] : NULL;
}

/* Legacy single-list drain (host tests / debug paths). */
OSTask const *pm3ds_gfx_drain_task(void) {
    gfx_dl_ensure_init();
    LightLock_Lock(&g_lock);
    OSTask const *t = g_pending_task;
    g_pending_task = NULL;
    g_queue_n = 0;
    if (t) LightEvent_Signal(&g_not_full);
    LightLock_Unlock(&g_lock);
    return t;
}

const void *pm3ds_gfx_drain_dl(void) {
    OSTask const *t = pm3ds_gfx_drain_task();
    if (!t) return NULL;
    return (const void *)(uintptr_t)t->data_ptr;
}

unsigned pm3ds_gfx_task_count(void) { return g_tasks_submitted; }

/* Legacy stubs kept compatible with the milestone-1 code path. */
void osSpTaskLoad   (OSTask *t) { (void)t; }
void osSpTaskYield  (void)      {}
void osSpTaskYielded(OSTask *t) { (void)t; }

void pm3ds_gfx_signal_done(OSMesgQueue *mq, OSMesg msg) {
    if (mq) osSendMesg(mq, msg, /*NOBLOCK*/ 0);
}
