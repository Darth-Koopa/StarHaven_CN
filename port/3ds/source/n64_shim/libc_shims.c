/*
 * libc_shims.c — small libc shims to bridge gaps between IDO/newlib.
 * Fidelity tier: N/A
 *
 * Most libc symbols come from newlib via devkitARM. This file holds
 * the few that the decomp expects in N64 form and that newlib does
 * not provide compatibly.
 */

#include <stdint.h>
#include <string.h>

void bzero(void *s, size_t n) { memset(s, 0, n); }
void bcopy(const void *src, void *dst, size_t n) { memmove(dst, src, n); }

/* N64 SDK 'osSyncPrintf' alias — route to a simple stdout printf.
 * The decomp uses it in some error paths. */
#include <stdio.h>
#include <stdarg.h>
int osSyncPrintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return n;
}

/* libultra osPanic forwards into our pm3ds_panic system so the bottom
 * screen, stack walk, and START-to-exit loop are shared with native
 * PM3DS_ASSERT. The decomp passes (file, line, fmt, ...); we format
 * locally then hand a single-arg message to pm3ds_panic_impl, which
 * takes its own __FILE__/__LINE__ via macro normally — we pass through
 * the caller's. */
#include "pm3ds_panic.h"
__attribute__((noreturn))
void osPanic(const char *file, int line, const char *msg, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, msg);
    vsnprintf(buf, sizeof(buf), msg, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = '\0';
    pm3ds_panic_impl(file, line, "%s", buf);
    /* unreachable — pm3ds_panic_impl is noreturn */
    __builtin_unreachable();
}
