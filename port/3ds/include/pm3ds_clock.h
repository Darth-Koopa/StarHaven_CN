#ifndef PM3DS_CLOCK_H
#define PM3DS_CLOCK_H

#include <stdint.h>

/*
 * Tick source for the libultra time shims.
 *
 * pm3ds_clock_n64_count: returns a value compatible with osGetCount()
 *   (N64 CPU cycles, 46.875 MHz), derived from svcGetSystemTick() scaled.
 *
 * pm3ds_clock_now_us: monotonic microseconds since pm3ds_clock_init.
 */

void     pm3ds_clock_init(void);
uint32_t pm3ds_clock_n64_count(void);
uint64_t pm3ds_clock_now_us(void);

#endif /* PM3DS_CLOCK_H */
