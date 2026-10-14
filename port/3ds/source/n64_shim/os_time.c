/*
 * os_time.c — osGetTime / osGetCount shims.
 * Fidelity tier: FAITHFUL — backed by svcGetSystemTick scaled to N64 cycles.
 */

#include <stdint.h>
#include "pm3ds_clock.h"

typedef uint64_t OSTime;

/* osGetTime — DELIBERATELY NOT PROVIDED at Milestone 1.
 *
 * libctru's os.c already exports `osGetTime` (Unix-epoch milliseconds).
 * The N64 contract is "CPU cycles since reset" — incompatible. At this
 * milestone there are no decomp callers in the link, so letting
 * libctru's strong symbol win avoids a multiple-definition link error
 * without harming any in-tree code.
 *
 * Milestone 4 (EVT / game-logic compile fidelity) brings the decomp's
 * src/ tree back into SOURCES; that milestone owns reconciling the two
 * semantics — most likely by wrapping libctru via -Wl,--wrap=osGetTime
 * and forwarding the wrapper to pm3ds_clock_now_us. See
 * docs/superpowers/specs/2026-05-13-papermario-3ds-boot-hal-design.md.
 *
 * In-tree callers should use pm3ds_clock_now_us() directly. */

/* osGetCount — the N64 cycle counter (osGetCount → 46.875 MHz cycles
 * since reset). libctru does NOT provide this symbol, so our N64-shaped
 * version is the only one and links cleanly. */
uint32_t osGetCount(void) {
    return pm3ds_clock_n64_count();
}

/* libultra exposes a settable wall-clock; on 3DS we don't honour writes. */
void osSetTime(uint64_t time) { (void)time; }

/* The decomp also references osGetTime_custom (an alias used in some
 * places). Forward to the same source. Distinct name so libctru's
 * osGetTime symbol can stand. */
OSTime osGetTime_custom(void) {
    return (OSTime)pm3ds_clock_now_us();
}
