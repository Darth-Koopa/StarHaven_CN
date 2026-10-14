/*
 * pm3ds_log.c — bottom-screen console + svcOutputDebugString logger.
 * Fidelity tier: N/A (3DS-side primitive)
 *
 * Logs are formatted directly to the bottom screen via libctru's console
 * (initialised in main.c) AND to svcOutputDebugString for Citra capture.
 * No ring buffer at this milestone; if logging shows up on the hot path
 * the perf milestone owns introducing async flushing.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "pm3ds_brand.h"

#ifndef PM3DS_HOST_TEST
#include <3ds.h>
#endif

#include "pm3ds_log.h"

pm3ds_stats_t pm3ds_stats = {0};

static pm3ds_log_level_t g_floor = PM3DS_LOG_DEFAULT_FLOOR;

static const char *level_str(pm3ds_log_level_t l) {
    switch (l) {
        case PM3DS_LOG_TRACE: return "TRACE";
        case PM3DS_LOG_DEBUG: return "DEBUG";
        case PM3DS_LOG_INFO:  return "INFO ";
        case PM3DS_LOG_WARN:  return "WARN ";
        case PM3DS_LOG_ERROR: return "ERROR";
    }
    return "?????";
}

#define LOG_RING 16
static char g_ring[LOG_RING][72];
static int g_ring_head, g_ring_count;

/* i = 0 is the newest line; NULL past the end */
const char *pm3ds_log_recent(int i) {
    if (i < 0 || i >= g_ring_count) return NULL;
    return g_ring[(g_ring_head - 1 - i + LOG_RING) % LOG_RING];
}

void pm3ds_log_init(void) {
    g_floor = PM3DS_LOG_DEFAULT_FLOOR;
}

void pm3ds_log_va(pm3ds_log_level_t lvl, const char *fmt, va_list ap) {
    if (lvl < g_floor) return;

    char buf[256];
    int n = snprintf(buf, sizeof(buf), "[%s] ", level_str(lvl));
    if (n < 0 || (size_t)n >= sizeof(buf)) return;
    vsnprintf(buf + n, sizeof(buf) - (size_t)n, fmt, ap);
    /* keep buffer NUL-terminated even if vsnprintf truncated */
    buf[sizeof(buf) - 1] = '\0';

    {   /* last lines for the bottom screen's Debug page */
        snprintf(g_ring[g_ring_head], sizeof g_ring[0], "%s", buf);
        g_ring_head = (g_ring_head + 1) % LOG_RING;
        if (g_ring_count < LOG_RING) g_ring_count++;
    }
#ifndef PM3DS_HOST_TEST
    svcOutputDebugString(buf, (int)strlen(buf));
    { extern void pm3ds_netlog_line(const char *s); pm3ds_netlog_line(buf); }
    if (lvl >= PM3DS_LOG_WARN) {
        /* warnings persist on the SD card so hardware-only problems leave a
         * trace (<SD folder>/log.txt, capped per session) */
        static FILE *f;
        static int lines, opened;
        if (!opened) {
            opened = 1;
            f = fopen(PM3DS_SD_DIR "/log.txt", "a");
            if (f) fprintf(f, "=== session start ===\n");
        }
        if (f && lines < 300) {
            lines++;
            fprintf(f, "%s\n", buf);
            fflush(f);
        }
    }
#endif
}

void pm3ds_log(pm3ds_log_level_t lvl, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    pm3ds_log_va(lvl, fmt, ap);
    va_end(ap);
}

/*
 * Per-site rate limiter for warns. Uses a tiny hash table keyed by the
 * caller's __builtin_return_address(0). Each entry tracks the last log
 * time; one warning per second per site.
 */
#define WARN_SITE_BUCKETS 64

typedef struct {
    const void *site;
    uint64_t    last_ns;
} warn_site_entry_t;

static warn_site_entry_t g_warn_sites[WARN_SITE_BUCKETS] = {0};

static uint64_t now_ns(void) {
#ifdef PM3DS_HOST_TEST
    return 0;
#else
    return osGetTime() * 1000000ull;
#endif
}

void pm3ds_warn_site(const void *site, const char *fmt, ...) {
    uintptr_t h = (uintptr_t)site;
    size_t idx = ((h * 2654435761u) >> 12) & (WARN_SITE_BUCKETS - 1);

    uint64_t now = now_ns();
    if (g_warn_sites[idx].site == site &&
        (now - g_warn_sites[idx].last_ns) < 1000000000ull) {
        /* same site, within 1 s — suppressed */
        pm3ds_stats.warn_count++;
        return;
    }
    g_warn_sites[idx].site    = site;
    g_warn_sites[idx].last_ns = now;
    pm3ds_stats.warn_count++;

    va_list ap;
    va_start(ap, fmt);
    pm3ds_log_va(PM3DS_LOG_WARN, fmt, ap);
    va_end(ap);
}
