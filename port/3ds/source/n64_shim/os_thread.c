/*
 * os_thread.c — libultra thread shim, backed by libctru Thread.
 * Fidelity tier: FAITHFUL — game depends on real concurrency.
 *
 * N64 priorities (0–254, higher = higher pri) map onto libctru's 0x18–0x3F
 * (lower number = higher pri on 3DS). We invert and clamp.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#ifndef PM3DS_HOST_TEST
#include <3ds.h>
#endif

typedef void (*OSThreadEntry)(void *);

typedef struct OSThread {
    struct OSThread *next;
    int              priority;
    int              id;
    int              state;
    OSThreadEntry    entry;
    void            *arg;
    void            *stack_base;
    size_t           stack_bytes;
#ifndef PM3DS_HOST_TEST
    Thread           ctru_thread;
#endif
} OSThread;

#define OS_PRIORITY_MIN  0
#define OS_PRIORITY_MAX 254

static int g_next_id = 1;

static int n64_to_ctru_prio(int n64p) {
    /* Inverse-map and clamp into libctru's app-usable band. */
    int p = 0x3F - (n64p * (0x3F - 0x18) / OS_PRIORITY_MAX);
    if (p < 0x18) p = 0x18;
    if (p > 0x3F) p = 0x3F;
    return p;
}

void osCreateThread(OSThread *th, int id, OSThreadEntry entry, void *arg,
                    void *stack_top, int priority) {
    memset(th, 0, sizeof(*th));
    th->id          = id ? id : g_next_id++;
    th->entry       = entry;
    th->arg         = arg;
    th->priority    = priority;
    /* stack_top is the *end* of the stack in N64 land; we cannot infer
     * stack base size reliably. libctru will allocate its own stack. */
    th->stack_base  = stack_top;
    th->stack_bytes = 0;
}

void osStartThread(OSThread *th) {
#ifndef PM3DS_HOST_TEST
    th->ctru_thread = threadCreate(
        th->entry, th->arg,
        /* stack */ 0x4000,
        /* prio  */ n64_to_ctru_prio(th->priority),
        /* core  */ -1,
        /* detached */ false);
#else
    /* host: just run inline (boot path never actually calls osStartThread). */
    (void)th;
#endif
}

void osStopThread(OSThread *th)    { (void)th; }
void osDestroyThread(OSThread *th) {
#ifndef PM3DS_HOST_TEST
    if (th->ctru_thread) {
        threadJoin(th->ctru_thread, UINT64_MAX);
        threadFree(th->ctru_thread);
        th->ctru_thread = NULL;
    }
#else
    (void)th;
#endif
}

int  osGetThreadPri(OSThread *th) { return th ? th->priority : 0; }
void osSetThreadPri(OSThread *th, int p) { if (th) th->priority = p; }
int  osGetThreadId (OSThread *th) { return th ? th->id : 0; }

void osYieldThread(void) {
#ifndef PM3DS_HOST_TEST
    svcSleepThread(0);
#endif
}

/* Interrupt mask shim — stubbed. The 3DS does not let userland disable
 * interrupts, and the decomp paths that touch this are non-critical
 * (osSetIntMask before short atomic sections that we serialise via
 * LightLock elsewhere). Keep the value so osGetIntMask round-trips. */
typedef uint32_t OSIntMask;

static OSIntMask g_int_mask = 0;

void      osSetIntMask(OSIntMask im) { g_int_mask = im; }
OSIntMask osGetIntMask(void)         { return g_int_mask; }

/* OS_TV_NTSC = 0 — Paper Mario US is NTSC. The decomp reads osTvType
 * during boot to pick VI mode tables. We pin it to NTSC. */
int32_t osTvType = 0;

/* osInitialize is the libultra bring-up; on 3DS our platform init in
 * main.c does everything before any decomp code runs, so this is a
 * no-op. */
void osInitialize(void) { /* no-op — pm3ds_platform_init owns bring-up */ }

/* For a thread with nothing left to do (boot_main after init): sleep for
 * good instead of spinning in an empty loop. */
void pm3ds_park_thread(void) {
    for (;;) {
        svcSleepThread(1000000000LL);
    }
}
