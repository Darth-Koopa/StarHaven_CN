/*
 * os_mesgqueue.c — libultra OS message queue shim.
 * Fidelity tier: FAITHFUL — game's threading model assumes correct
 *                blocking/wakeup semantics, AND the struct must match
 *                the decomp's ABI exactly.
 *
 * M8.7 ABI FIX: the original shim struct (LightLock + 2 LightEvents +
 * ring fields, 40 bytes) was bigger than the decomp's OSMesgQueue
 * (include/PR/os_message.h, size = 0x18 = 24 bytes). Decomp code
 * allocates queues on the STACK (e.g. fio_read_flash's local
 * `OSMesgQueue mesgQueue`), so osCreateMesgQueue writing 40 bytes
 * smashed 16 bytes of the caller's frame — saved registers included.
 * Observed as gGameStatusPtr's cached register reading 0 inside
 * state_step_startup after fio_load_globals returned, while the global
 * itself stayed intact (the corruption travelled through the smashed
 * callee-saved register, not memory).
 *
 * The struct below overlays the decomp's exact 6-word layout:
 *   word 0  mtqueue    -> LightLock
 *   word 1  fullqueue  -> spare
 *   word 2  validCount -> count of queued messages
 *   word 3  first      -> ring head index
 *   word 4  msgCount   -> capacity
 *   word 5  msg        -> message buffer
 * Blocking uses a poll loop (svcSleepThread 200us) instead of
 * LightEvents — slightly less elegant than event wakeup, but it fits
 * the ABI and the longest real wait in this port is a frame.
 */

#include <stdint.h>
#include <string.h>

#ifdef PM3DS_HOST_TEST
#include "../../tests/ctru_stub.h"
#else
#include <3ds.h>
#endif

typedef void *OSMesg;

typedef struct OSMesgQueue {
    LightLock   lock;       /* decomp: OSThread *mtqueue   */
    int32_t     spare;      /* decomp: OSThread *fullqueue */
    int32_t     validCount; /* decomp: validCount          */
    int32_t     first;      /* decomp: first               */
    int32_t     msgCount;   /* decomp: msgCount (capacity) */
    OSMesg     *msg;        /* decomp: msg                 */
} OSMesgQueue;

/* Size must equal the decomp's os_message.h OSMesgQueue (0x18).
 *
 * This assert exists because the decomp STACK-ALLOCATES OSMesgQueues
 * (fio_read_flash), so any size disagreement smashes the caller's frame — the
 * M8.7 bug that made gGameStatusPtr transiently read NULL. Keep it.
 *
 * DEVICE ONLY. On the 3DS libctru's LightLock is an s32, so word 0 of our
 * overlay lines up and the struct is exactly 0x18. The host test harness
 * (tests/ctru_stub.h) types LightLock as pthread_mutex_t — 40 bytes on glibc —
 * so the total can never be 0x18 there. Asserting on the host was breaking the
 * BUILD of test_os_mesgqueue and test_os_timer, which is why neither had run in
 * a long time. There is no decomp ABI to match on the host, so the check has
 * nothing to protect there. */
#ifndef PM3DS_HOST_TEST
_Static_assert(sizeof(OSMesgQueue) == 0x18,
               "OSMesgQueue ABI mismatch with include/PR/os_message.h");
#endif

#define OS_MESG_NOBLOCK 0
#define OS_MESG_BLOCK   1

#define PM3DS_MQ_POLL_NS 200000LL /* 200 us between blocking polls */

static void mq_block_wait(void) {
#ifndef PM3DS_HOST_TEST
    svcSleepThread(PM3DS_MQ_POLL_NS);
#endif
}

void osCreateMesgQueue(OSMesgQueue *mq, OSMesg *buf, int capacity) {
    LightLock_Init(&mq->lock);
    mq->spare      = 0;
    mq->validCount = 0;
    mq->first      = 0;
    mq->msgCount   = capacity;
    mq->msg        = buf;
}

int osSendMesg(OSMesgQueue *mq, OSMesg msg, int mode) {
    LightLock_Lock(&mq->lock);
    while (mq->validCount == mq->msgCount) {
        if (mode == OS_MESG_NOBLOCK) {
            LightLock_Unlock(&mq->lock);
            return -1;
        }
        LightLock_Unlock(&mq->lock);
        mq_block_wait();
        LightLock_Lock(&mq->lock);
    }
    mq->msg[(mq->first + mq->validCount) % mq->msgCount] = msg;
    mq->validCount++;
    LightLock_Unlock(&mq->lock);
    return 0;
}

int osJamMesg(OSMesgQueue *mq, OSMesg msg, int mode) {
    LightLock_Lock(&mq->lock);
    while (mq->validCount == mq->msgCount) {
        if (mode == OS_MESG_NOBLOCK) {
            LightLock_Unlock(&mq->lock);
            return -1;
        }
        LightLock_Unlock(&mq->lock);
        mq_block_wait();
        LightLock_Lock(&mq->lock);
    }
    mq->first = (mq->first - 1 + mq->msgCount) % mq->msgCount;
    mq->msg[mq->first] = msg;
    mq->validCount++;
    LightLock_Unlock(&mq->lock);
    return 0;
}

int osRecvMesg(OSMesgQueue *mq, OSMesg *out, int mode) {
    LightLock_Lock(&mq->lock);
    while (mq->validCount == 0) {
        if (mode == OS_MESG_NOBLOCK) {
            LightLock_Unlock(&mq->lock);
            return -1;
        }
        LightLock_Unlock(&mq->lock);
        mq_block_wait();
        LightLock_Lock(&mq->lock);
    }
    if (out) *out = mq->msg[mq->first];
    mq->first = (mq->first + 1) % mq->msgCount;
    mq->validCount--;
    LightLock_Unlock(&mq->lock);
    return 0;
}
