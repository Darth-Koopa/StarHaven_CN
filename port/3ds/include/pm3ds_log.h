#ifndef PM3DS_LOG_H
#define PM3DS_LOG_H

#include <stdarg.h>

typedef enum {
    PM3DS_LOG_TRACE = 0,
    PM3DS_LOG_DEBUG,
    PM3DS_LOG_INFO,
    PM3DS_LOG_WARN,
    PM3DS_LOG_ERROR
} pm3ds_log_level_t;

void pm3ds_log_init(void);
void pm3ds_log(pm3ds_log_level_t lvl, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
void pm3ds_log_va(pm3ds_log_level_t lvl, const char *fmt, va_list ap);

/* Per-site rate-limited warn (used by pm3ds_warn macro below). */
void pm3ds_warn_site(const void *site, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

#define pm3ds_warn(...) \
    pm3ds_warn_site((const void *)__builtin_return_address(0), __VA_ARGS__)

#define PM3DS_LOG_DEFAULT_FLOOR PM3DS_LOG_INFO

/* Runtime counters surfaced in the on-screen debug overlay. */
typedef struct {
    unsigned unimpl_calls;
    unsigned warn_count;
    unsigned panic_count;
} pm3ds_stats_t;

extern pm3ds_stats_t pm3ds_stats;

#endif /* PM3DS_LOG_H */
