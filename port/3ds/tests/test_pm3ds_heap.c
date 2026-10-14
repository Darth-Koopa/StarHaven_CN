#include <stdint.h>
#include <stddef.h>
#include "../include/pm3ds_heap.h"
#include "runner.h"

PM3DS_TEST(heap_init_and_alloc) {
    PM3DS_EXPECT(pm3ds_heap_init(1024 * 1024) != NULL);
    PM3DS_EXPECT(pm3ds_heap_capacity() == 1024 * 1024);

    void *a = pm3ds_heap_alloc(128);
    PM3DS_EXPECT(a != NULL);
    PM3DS_EXPECT(((uintptr_t)a & 7) == 0);

    void *b = pm3ds_heap_alloc(256);
    PM3DS_EXPECT(b != NULL);
    PM3DS_EXPECT(b != a);

    pm3ds_heap_free(a);
    pm3ds_heap_free(b);
    pm3ds_heap_shutdown();
}

PM3DS_TEST(heap_oom_returns_null) {
    PM3DS_EXPECT(pm3ds_heap_init(4096) != NULL);
    void *huge = pm3ds_heap_alloc(8192);
    PM3DS_EXPECT(huge == NULL);
    pm3ds_heap_shutdown();
}

PM3DS_TEST(heap_used_tracks_allocs) {
    PM3DS_EXPECT(pm3ds_heap_init(1024 * 1024) != NULL);
    PM3DS_EXPECT(pm3ds_heap_used() == 0);
    void *a = pm3ds_heap_alloc(1000);
    PM3DS_EXPECT(pm3ds_heap_used() >= 1000);
    pm3ds_heap_free(a);
    pm3ds_heap_shutdown();
}
