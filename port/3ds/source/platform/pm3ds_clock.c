/*
 * pm3ds_clock.c — 3DS tick source, scaled to N64 osGetCount units.
 * Fidelity tier: N/A (3DS-side primitive)
 *
 * 3DS system tick: 268.123480 MHz.
 * N64 CPU cycles: 46.875 MHz.
 * Ratio ≈ 5.7194 — we use integer math: count_n64 = tick_3ds * 46875 / 268123.
 * Slight rounding error is fine — the game's timing math is robust to this.
 */

/* Host-test build expects -D_GNU_SOURCE on the cc command line (see
 * tests/Makefile) so clock_gettime and CLOCK_MONOTONIC are visible. The
 * sanity-compile snippet in the plan's Task 7 only passes if the caller
 * also adds -D_GNU_SOURCE (or _POSIX_C_SOURCE>=199309L). */

#include <stdint.h>
#ifndef PM3DS_HOST_TEST
#include <3ds.h>
#else
#include <time.h>
#endif

#include "pm3ds_clock.h"

#define N64_CPU_NUMERATOR    46875ull
#define TICK_3DS_DENOMINATOR 268123ull

static uint64_t g_base_tick = 0;

#ifdef PM3DS_HOST_TEST
static uint64_t fake_tick(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    /* pretend our "tick" is 268 MHz */
    return (uint64_t)ts.tv_sec * 268123000ull + (uint64_t)ts.tv_nsec * 268 / 1000ull;
}
#endif

void pm3ds_clock_init(void) {
#ifdef PM3DS_HOST_TEST
    g_base_tick = fake_tick();
#else
    g_base_tick = svcGetSystemTick();
#endif
}

uint32_t pm3ds_clock_n64_count(void) {
#ifdef PM3DS_HOST_TEST
    uint64_t now = fake_tick();
#else
    uint64_t now = svcGetSystemTick();
#endif
    uint64_t delta = now - g_base_tick;
    /* keep the multiplication in 64-bit to avoid overflow */
    return (uint32_t)(delta * N64_CPU_NUMERATOR / TICK_3DS_DENOMINATOR);
}

uint64_t pm3ds_clock_now_us(void) {
#ifdef PM3DS_HOST_TEST
    uint64_t now = fake_tick();
#else
    uint64_t now = svcGetSystemTick();
#endif
    /* 268123 ticks per microsecond? No — 268.123 ticks per microsecond.
     * ticks per us ≈ 268.123. So us = ticks * 1000 / 268123 (approx). */
    return (now - g_base_tick) * 1000ull / 268ull;
}
