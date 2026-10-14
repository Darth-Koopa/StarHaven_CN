#ifndef PM3DS_CTRU_STUB_H
#define PM3DS_CTRU_STUB_H

/*
 * Minimal libctru stub. Replaces the threading primitives the shim layer
 * uses, so that the pure-logic shims can be tested on a host OS.
 *
 * Semantics: cooperative — LightEvent_Wait blocks the calling pthread,
 * LightEvent_Signal wakes all waiters. Good enough for unit tests of the
 * mesg-queue and timer logic.
 */

#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>

typedef pthread_mutex_t LightLock;

static inline void LightLock_Init(LightLock *l)   { pthread_mutex_init(l, NULL); }
static inline void LightLock_Lock(LightLock *l)   { pthread_mutex_lock(l); }
static inline void LightLock_Unlock(LightLock *l) { pthread_mutex_unlock(l); }
static inline int  LightLock_TryLock(LightLock *l){ return pthread_mutex_trylock(l); }

typedef struct {
    pthread_mutex_t mtx;
    pthread_cond_t  cv;
    int             signalled;
    int             one_shot;
} LightEvent;

static inline void LightEvent_Init(LightEvent *e, int mode) {
    pthread_mutex_init(&e->mtx, NULL);
    pthread_cond_init(&e->cv, NULL);
    e->signalled = 0;
    e->one_shot = (mode == 1);
}

static inline void LightEvent_Wait(LightEvent *e) {
    pthread_mutex_lock(&e->mtx);
    while (!e->signalled) pthread_cond_wait(&e->cv, &e->mtx);
    if (e->one_shot) e->signalled = 0;
    pthread_mutex_unlock(&e->mtx);
}

static inline void LightEvent_Signal(LightEvent *e) {
    pthread_mutex_lock(&e->mtx);
    e->signalled = 1;
    pthread_cond_broadcast(&e->cv);
    pthread_mutex_unlock(&e->mtx);
}

static inline int LightEvent_TryWait(LightEvent *e) {
    pthread_mutex_lock(&e->mtx);
    int ok = e->signalled;
    if (ok && e->one_shot) e->signalled = 0;
    pthread_mutex_unlock(&e->mtx);
    return ok ? 0 : -1;
}

#define RESET_ONESHOT 1
#define RESET_STICKY  0

#endif /* PM3DS_CTRU_STUB_H */
