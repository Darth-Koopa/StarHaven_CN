/*
 * os_cache.c — libultra cache maintenance shims.
 * Fidelity tier: FAITHFUL (trivially — no N64 RDRAM/cache mismatch on 3DS).
 */

#include <stddef.h>
#ifndef PM3DS_HOST_TEST
#include <3ds.h>
#endif

void osInvalDCache(void *addr, int sz) {
    (void)addr; (void)sz;
#ifndef PM3DS_HOST_TEST
    __dsb();
#endif
}

void osWritebackDCache(void *addr, int sz) {
    (void)addr; (void)sz;
#ifndef PM3DS_HOST_TEST
    __dsb();
#endif
}

void osWritebackDCacheAll(void) {
#ifndef PM3DS_HOST_TEST
    __dsb();
#endif
}

void osInvalICache(void *addr, int sz) {
    (void)addr; (void)sz;
#ifndef PM3DS_HOST_TEST
    __dsb();
#endif
}
