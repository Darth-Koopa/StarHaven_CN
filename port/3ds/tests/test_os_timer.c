#include <stdint.h>
#include <unistd.h>
#include "ctru_stub.h"

#define PM3DS_HOST_TEST 1
#include "../source/n64_shim/os_mesgqueue.c"
#include "../source/n64_shim/os_timer.c"

#include "runner.h"

PM3DS_TEST(timer_fires_into_queue) {
    OSMesgQueue mq;
    OSMesg      buf[4];
    osCreateMesgQueue(&mq, buf, 4);

    OSTimer t;
    /* OS_USEC_TO_CYCLES would normally be MIPS-specific; on host the
     * timer treats the period as microseconds directly via the shim
     * (cycles = us internally on PM3DS_HOST_TEST). */
    osSetTimer(&t, /*cycles*/ 50000, /*rep*/ 0, &mq, (OSMesg)(uintptr_t)0x7);

    OSMesg got = NULL;
    /* Block until the timer fires (max 1 s safety). */
    int rc = osRecvMesg(&mq, &got, OS_MESG_BLOCK);
    PM3DS_EXPECT(rc == 0);
    PM3DS_EXPECT(got == (OSMesg)(uintptr_t)0x7);
}
