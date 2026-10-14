/* Exercises osCreateMesgQueue / osSendMesg / osRecvMesg / osJamMesg.
 * Runs against ctru_stub.h on host. */

#include <stdint.h>
#include <stdbool.h>
#include "ctru_stub.h"

/* Pull in the shim source directly so we don't need a .h for the OS types. */
#define PM3DS_HOST_TEST 1
#include "../source/n64_shim/os_mesgqueue.c"

#include "runner.h"

PM3DS_TEST(mq_send_then_recv_immediate) {
    OSMesgQueue mq;
    OSMesg      buf[4];
    osCreateMesgQueue(&mq, buf, 4);

    OSMesg sent = (OSMesg)(uintptr_t)0xABCD;
    PM3DS_EXPECT(osSendMesg(&mq, sent, OS_MESG_NOBLOCK) == 0);

    OSMesg got = NULL;
    PM3DS_EXPECT(osRecvMesg(&mq, &got, OS_MESG_NOBLOCK) == 0);
    PM3DS_EXPECT(got == sent);
}

PM3DS_TEST(mq_recv_empty_nonblock_returns_err) {
    OSMesgQueue mq;
    OSMesg      buf[2];
    osCreateMesgQueue(&mq, buf, 2);

    OSMesg got = NULL;
    PM3DS_EXPECT(osRecvMesg(&mq, &got, OS_MESG_NOBLOCK) != 0);
}

PM3DS_TEST(mq_jam_inserts_at_head) {
    OSMesgQueue mq;
    OSMesg      buf[3];
    osCreateMesgQueue(&mq, buf, 3);

    osSendMesg(&mq, (OSMesg)(uintptr_t)1, OS_MESG_NOBLOCK);
    osSendMesg(&mq, (OSMesg)(uintptr_t)2, OS_MESG_NOBLOCK);
    osJamMesg (&mq, (OSMesg)(uintptr_t)9, OS_MESG_NOBLOCK);

    OSMesg got;
    osRecvMesg(&mq, &got, OS_MESG_NOBLOCK);
    PM3DS_EXPECT(got == (OSMesg)(uintptr_t)9);
    osRecvMesg(&mq, &got, OS_MESG_NOBLOCK);
    PM3DS_EXPECT(got == (OSMesg)(uintptr_t)1);
    osRecvMesg(&mq, &got, OS_MESG_NOBLOCK);
    PM3DS_EXPECT(got == (OSMesg)(uintptr_t)2);
}

PM3DS_TEST(mq_send_full_nonblock_returns_err) {
    OSMesgQueue mq;
    OSMesg      buf[2];
    osCreateMesgQueue(&mq, buf, 2);

    PM3DS_EXPECT(osSendMesg(&mq, (OSMesg)1, OS_MESG_NOBLOCK) == 0);
    PM3DS_EXPECT(osSendMesg(&mq, (OSMesg)2, OS_MESG_NOBLOCK) == 0);
    PM3DS_EXPECT(osSendMesg(&mq, (OSMesg)3, OS_MESG_NOBLOCK) != 0);
}
