#ifndef PM3DS_PANIC_H
#define PM3DS_PANIC_H

#include <stdarg.h>

void pm3ds_panic_install(void);
__attribute__((noreturn))
void pm3ds_panic_impl(const char *file, int line, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

#define pm3ds_panic(...) pm3ds_panic_impl(__FILE__, __LINE__, __VA_ARGS__)

#define PM3DS_ASSERT(cond) do { \
    if (!(cond)) pm3ds_panic("ASSERT failed: %s", #cond); \
} while (0)

#ifdef PM3DS_DEBUG
#define PM3DS_DEBUG_ASSERT(cond) PM3DS_ASSERT(cond)
#else
#define PM3DS_DEBUG_ASSERT(cond) ((void)0)
#endif

/* For unimplemented shims. Logs once, increments counter, returns from caller. */
void pm3ds_unimpl_once(const char *name);

#define PM3DS_UNIMPL_STUB_VOID(name) do { \
    static int _hit = 0; \
    if (!_hit) { _hit = 1; pm3ds_unimpl_once(#name); } \
} while (0)

#define PM3DS_UNIMPL_STUB_RET(name, retval) do { \
    static int _hit = 0; \
    if (!_hit) { _hit = 1; pm3ds_unimpl_once(#name); } \
    return (retval); \
} while (0)

#endif /* PM3DS_PANIC_H */
