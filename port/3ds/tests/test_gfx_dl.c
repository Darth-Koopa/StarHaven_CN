#include <stdint.h>
#include <stdbool.h>
#include "ctru_stub.h"

/* Host stub — gfx_dl.c references osSendMesg via pm3ds_gfx_signal_done,
 * but pm3ds_gfx_signal_done is never invoked from the tests here. We
 * provide a no-op so the link resolves. */
struct OSMesgQueue;
int osSendMesg(struct OSMesgQueue *mq, void *msg, int flags) {
    (void)mq; (void)msg; (void)flags; return 0;
}

#define PM3DS_HOST_TEST 1
#include "../source/n64_shim/gfx_dl.c"

#include "runner.h"

PM3DS_TEST(submit_then_drain) {
    OSTask t1 = {0};
    osSpTaskStart(&t1);
    PM3DS_EXPECT(pm3ds_gfx_task_count() >= 1);

    const OSTask *drained = pm3ds_gfx_drain_task();
    PM3DS_EXPECT(drained == &t1);

    /* Now empty. */
    PM3DS_EXPECT(pm3ds_gfx_drain_task() == NULL);
}

PM3DS_TEST(drain_empty_returns_null) {
    /* After the previous test the slot is empty, but call drain once
     * more to confirm idempotency. */
    PM3DS_EXPECT(pm3ds_gfx_drain_task() == NULL);
}
