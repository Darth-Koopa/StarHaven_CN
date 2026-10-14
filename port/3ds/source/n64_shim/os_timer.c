/*
 * os_timer.c — libultra OS timer shim.
 * Fidelity tier: FAITHFUL — single timer thread services all timers,
 *                fires OSMesg into the configured queue at the requested
 *                cycle interval.
 */

/* Host-test build expects -D_GNU_SOURCE on the cc command line (see
 * tests/Makefile) so clock_gettime, CLOCK_MONOTONIC, and usleep are all
 * visible without re-defining feature-test macros after stdint.h has
 * already pulled in features.h. */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef PM3DS_HOST_TEST
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include "../../tests/ctru_stub.h"
#define PM_THREAD_T pthread_t
#define PM_THREAD_CREATE(thr, fn, arg) pthread_create((thr), NULL, (fn), (arg))
#else
#include <3ds.h>
#define PM_THREAD_T Thread
#define PM_THREAD_CREATE(thr, fn, arg) \
    do { *(thr) = threadCreate((fn), (arg), 0x1000, 0x30, -1, false); } while (0)
#endif

/* Re-declare types from os_mesgqueue.c to avoid pulling a header.
 * os_mesgqueue.c tags its struct as `struct OSMesgQueue` so this
 * forward-declared pointer is type-compatible at link time. */
typedef void *OSMesg;
typedef struct OSMesgQueue OSMesgQueue;
int osSendMesg(OSMesgQueue *, OSMesg, int);

typedef struct OSTimer {
    struct OSTimer *next;
    uint64_t        deadline_us;
    uint64_t        period_us;
    OSMesgQueue    *mq;
    OSMesg          msg;
} OSTimer;

#define MAX_TIMERS 32

static OSTimer       *g_active[MAX_TIMERS];
static int            g_active_count = 0;
static LightLock      g_lock;
static LightEvent     g_kick;
static PM_THREAD_T    g_thread;
static int            g_started = 0;

static uint64_t now_us(void) {
#ifdef PM3DS_HOST_TEST
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
#else
    return svcGetSystemTick() * 1000ull / 268ull;
#endif
}

#ifdef PM3DS_HOST_TEST
static void *timer_worker(void *arg) {
#else
static void  timer_worker(void *arg) {
#endif
    (void)arg;
    for (;;) {
        uint64_t now = now_us();
        uint64_t sleep_us = 16000;

        LightLock_Lock(&g_lock);
        for (int i = 0; i < g_active_count; ) {
            OSTimer *t = g_active[i];
            if (t->deadline_us <= now) {
                osSendMesg(t->mq, t->msg, /*OS_MESG_NOBLOCK*/ 0);
                if (t->period_us) {
                    t->deadline_us += t->period_us;
                    i++;
                } else {
                    g_active[i] = g_active[--g_active_count];
                }
            } else {
                uint64_t until = t->deadline_us - now;
                if (until < sleep_us) sleep_us = until;
                i++;
            }
        }
        LightLock_Unlock(&g_lock);

#ifdef PM3DS_HOST_TEST
        usleep((useconds_t)sleep_us);
#else
        svcSleepThread((int64_t)sleep_us * 1000);
#endif
    }
#ifdef PM3DS_HOST_TEST
    return NULL;
#endif
}

static void ensure_started(void) {
    if (g_started) return;
    LightLock_Init(&g_lock);
    LightEvent_Init(&g_kick, RESET_ONESHOT);
    PM_THREAD_CREATE(&g_thread, timer_worker, NULL);
    g_started = 1;
}

int osSetTimer(OSTimer *t, uint32_t cycles, uint32_t rep,
               OSMesgQueue *mq, OSMesg msg) {
    ensure_started();
    /* Convert N64 cycles (46.875 MHz) → microseconds. On host tests we
     * treat cycles as microseconds directly. */
#ifdef PM3DS_HOST_TEST
    uint64_t period_us = cycles;
    uint64_t rep_us    = rep;
#else
    uint64_t period_us = (uint64_t)cycles * 1000ull / 46875ull;
    uint64_t rep_us    = (uint64_t)rep    * 1000ull / 46875ull;
#endif

    LightLock_Lock(&g_lock);
    t->deadline_us = now_us() + period_us;
    t->period_us   = rep_us;
    t->mq          = mq;
    t->msg         = msg;
    if (g_active_count < MAX_TIMERS) {
        g_active[g_active_count++] = t;
    }
    LightLock_Unlock(&g_lock);
    LightEvent_Signal(&g_kick);
    return 0;
}

int osStopTimer(OSTimer *t) {
    LightLock_Lock(&g_lock);
    for (int i = 0; i < g_active_count; i++) {
        if (g_active[i] == t) {
            g_active[i] = g_active[--g_active_count];
            break;
        }
    }
    LightLock_Unlock(&g_lock);
    return 0;
}
