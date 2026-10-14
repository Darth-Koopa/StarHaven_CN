/*
 * m9_assert_shim.c -- M9: make a failed decomp ASSERT visible instead of fatal.
 *
 * include/macros.h expands IS_DEBUG_PANIC to `do {} while(true)` when DEBUG is
 * not defined, and this port builds without DEBUG (src/is_debug.c, which holds
 * the real is_debug_panic(), is in the Makefile's M4_EXCLUDE list). So every
 * ASSERT() in the decomp is a SILENT INFINITE LOOP: no message, no crash, no
 * unmapped-memory trace -- just a frozen main thread while the audio thread
 * keeps running. That is exactly how the title-screen hang presented, and
 * localizing it cost a full build/run/instrument cycle.
 *
 * With -DPM3DS_ASSERT_LOG=1 (set in port/3ds/Makefile) macros.h routes
 * IS_DEBUG_PANIC here instead, so the assertion text, file and line reach the
 * log and the frame keeps going.
 *
 * WHY CONTINUE RATHER THAN HALT: on a port, a logged assertion tells us what
 * broke and lets the next frame surface the next problem; a hang tells us
 * nothing at all. The decomp's own release behaviour was already "keep the
 * broken state" (the assert never fired on real hardware), and every ASSERT
 * site we have hit so far reports damage that has *already* happened -- the
 * backgroundGfx bounds check at src/main_loop.c:200, for instance, runs after
 * the overflowing writes. Continuing is therefore no more dangerous than
 * hanging, and far more informative.
 *
 * Caveat worth remembering: a few decomp call sites may assume IS_DEBUG_PANIC
 * does not return (e.g. `ASSERT(p != NULL);` followed by a dereference). Those
 * will now fault instead of hanging -- which is still the better outcome, since
 * a fault gives a PC we can feed to addr2line.
 */

#include "pm3ds_log.h"

#include <stdint.h>

void pm3ds_assert_failed(const char *statement, const char *file, int line);

void pm3ds_assert_failed(const char *statement, const char *file, int line) {
    pm3ds_stats.panic_count++;

    /*
     * Rate-limit per assertion SITE, not per call: a failing assert usually
     * fails again on the very next frame, and at 60 fps an unthrottled log
     * would bury everything else. pm3ds_warn_site() suppresses repeats of the
     * same site within one second; __FILE__ is a single string literal per
     * translation unit, so mixing in the line number keeps distinct asserts in
     * the same file on distinct keys.
     *
     * XOR-with-shift rather than plain addition: `file + line` would collide
     * whenever two asserts sit `line` bytes apart in the string pool, which is
     * exactly the layout you get for asserts in adjacent files.
     */
    const void *site =
        (const void *)((uintptr_t)file ^ ((uintptr_t)(unsigned)line << 20));

    pm3ds_warn_site(site, "ASSERT FAILED: %s (%s:%d)",
                    statement ? statement : "?",
                    file ? file : "?",
                    line);
}
