/*
 * pm3ds_netlog.c — stream the log to a PC on the LAN (dev builds).
 *
 * A hang or a slow load on real hardware leaves nothing a tester can read
 * off the screen. Built with PM3DS_NETLOG_IP (IPv4 as a 32-bit number) and
 * PM3DS_NETLOG_PORT, the port sends every log line, plus a once-a-second
 * heartbeat naming what the main thread is doing, to a listener on that PC:
 * one "LOG" request, then plain text lines for as long as the game runs.
 * Lines are buffered from the first log call, so a late connection still
 * gets the whole boot. If the PC is unreachable nothing else is affected.
 *
 * The breadcrumbs exist in every build: they cost a pointer store.
 */
#include <3ds.h>
#include <citro3d.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>

volatile const char *pm3ds_bc_main = "start";   /* what the main thread is doing */
volatile int pm3ds_boot_permille = -1;          /* ROM load progress, -1 = not loading */
volatile unsigned pm3ds_bc_frames;              /* frames presented */

#if defined(PM3DS_NETLOG_IP) && defined(PM3DS_NETLOG_PORT)

#include "pm3ds_log.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#define RING_SIZE (64 * 1024)
#define SOC_BUF_SIZE 0x40000

static char s_ring[RING_SIZE];
static u32 s_head, s_tail;          /* bytes queued / sent, monotonic */
static LightLock s_lock = 1;        /* 1 = unlocked: usable before init */
static Thread s_thread;
static int s_core;
static u64 s_t0;                    /* system tick at init */

void pm3ds_netlog_line(const char *s) {
    size_t n = strlen(s);
    LightLock_Lock(&s_lock);
    if (s_head - s_tail + n + 1 <= RING_SIZE) {   /* full: drop the line */
        for (size_t i = 0; i < n; i++) s_ring[(s_head + i) % RING_SIZE] = s[i];
        s_ring[(s_head + n) % RING_SIZE] = '\n';
        s_head += (u32)n + 1;
    }
    LightLock_Unlock(&s_lock);
}

static int send_all(int fd, const char *p, size_t n) {
    while (n > 0) {
        ssize_t k = send(fd, p, n, 0);
        if (k <= 0) return -1;
        p += k;
        n -= (size_t)k;
    }
    return 0;
}

static int open_conn(void) {
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (fd < 0) return -1;
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(PM3DS_NETLOG_PORT);
    a.sin_addr.s_addr = htonl(PM3DS_NETLOG_IP);
    if (connect(fd, (struct sockaddr *)&a, sizeof a) < 0) {
        close(fd);
        return -1;
    }
    char hello[192];
    u32 me = (u32)gethostid();
    bool n3ds = false;
    APT_CheckNew3DS(&n3ds);
    int n = snprintf(hello, sizeof hello,
                     "LOG /pm3ds HTTP/1.1\r\nHost: pm3ds\r\n\r\n"
                     "# 3DS %u.%u.%u.%u (%s), netlog thread on core %d, built " __DATE__ " " __TIME__ "\n",
                     (unsigned)(me & 255), (unsigned)((me >> 8) & 255), (unsigned)((me >> 16) & 255),
                     (unsigned)(me >> 24), n3ds ? "New 3DS" : "Old 3DS", s_core);
    if (send_all(fd, hello, (size_t)n) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* GPU state for hang reports, read through GSP (registers relative to
 * 0x1EB00000) by a thread of its own: a hung GPU takes GSP down with it, so
 * a read may never return, and the heartbeat then shows the probe going
 * stale instead of stopping. */
static volatile u32 s_gpu_busy, s_psc0, s_psc1, s_ppf, s_pdc0_sel, s_pdc1_sel;
static volatile u64 s_probe_ms;     /* osGetTime of the last completed probe */
static volatile Result s_probe_res;

static void probe_thread(void *arg) {
    (void)arg;
    for (;;) {
        u32 v = 0;
        Result r = GSPGPU_ReadHWRegs(0x400034, &v, 4);   /* busy: 26 PSC0, 27 PSC1, 30 PPF, 31 P3D */
        if (R_SUCCEEDED(r)) s_gpu_busy = v;
        if (R_SUCCEEDED(GSPGPU_ReadHWRegs(0x40001C, &v, 4))) s_psc0 = v;   /* fill: bit0 busy, bit1 done */
        if (R_SUCCEEDED(GSPGPU_ReadHWRegs(0x40002C, &v, 4))) s_psc1 = v;
        if (R_SUCCEEDED(GSPGPU_ReadHWRegs(0x400C18, &v, 4))) s_ppf = v;    /* transfer: bit0 busy, bit8 done */
        if (R_SUCCEEDED(GSPGPU_ReadHWRegs(0x400478, &v, 4))) s_pdc0_sel = v;   /* framebuffer shown */
        if (R_SUCCEEDED(GSPGPU_ReadHWRegs(0x400578, &v, 4))) s_pdc1_sel = v;
        s_probe_res = r;
        s_probe_ms = osGetTime();
        svcSleepThread(250 * 1000 * 1000LL);
    }
}

static void heartbeat(void) {
    extern u32 nuAuFrameCounter;
    extern u32 __ctru_heap_size;
    extern volatile unsigned pm3ds_load_frames_ok, pm3ds_load_frames_busy;
    struct mallinfo mi = mallinfo();
    char hb[320];
    u64 now = osGetTime();
    snprintf(hb, sizeof hb,
             "HB t=%.1fs main=%s load=%d frames=%u audio=%u heapfree=%uK linearfree=%uK "
             "| ldf=%u/%u vbl=%lu/%lu gpu=%08lX psc=%lX/%lX ppf=%lX pdc=%lX/%lX probe=%ldms r=%08lX",
             (double)(svcGetSystemTick() - s_t0) / SYSCLOCK_ARM11, (const char *)pm3ds_bc_main, pm3ds_boot_permille,
             pm3ds_bc_frames, (unsigned)nuAuFrameCounter,
             (unsigned)((__ctru_heap_size - mi.arena + mi.fordblks) >> 10),
             (unsigned)(linearSpaceFree() >> 10),
             pm3ds_load_frames_ok, pm3ds_load_frames_busy,
             (unsigned long)C3D_FrameCounter(0), (unsigned long)C3D_FrameCounter(1),
             (unsigned long)s_gpu_busy, (unsigned long)s_psc0, (unsigned long)s_psc1, (unsigned long)s_ppf,
             (unsigned long)s_pdc0_sel, (unsigned long)s_pdc1_sel,
             s_probe_ms ? (long)(now - s_probe_ms) : -1L, (unsigned long)s_probe_res);
    pm3ds_netlog_line(hb);
}

/* HOME, sleep and exit stop the game's counters too: name them, so a report
 * can tell them from a hang */
static aptHookCookie s_apt_cookie;

static void apt_hook(APT_HookType hook, void *param) {
    (void)param;
    static const char *const names[APTHOOK_COUNT] = { "suspend", "restore", "sleep", "wakeup", "exit" };
    pm3ds_log(PM3DS_LOG_INFO, "apt: %s", hook < APTHOOK_COUNT ? names[hook] : "?");
    if (hook == APTHOOK_ONEXIT) svcSleepThread(250 * 1000 * 1000LL);   /* let the sender get it out */
}

static void netlog_thread(void *arg) {
    (void)arg;
    static char buf[4096];
    int fd = -1;
    u64 next_try = 0, next_beat = 0;
    for (;;) {
        u64 now = osGetTime();
        if (fd < 0 && now >= next_try) {
            fd = open_conn();
            next_try = now + 5000;
        }
        if (now >= next_beat) {
            next_beat = now + 1000;
            heartbeat();
        }
        while (fd >= 0) {
            LightLock_Lock(&s_lock);
            u32 n = s_head - s_tail;
            if (n > sizeof buf) n = sizeof buf;
            for (u32 i = 0; i < n; i++) buf[i] = s_ring[(s_tail + i) % RING_SIZE];
            LightLock_Unlock(&s_lock);
            if (n == 0) break;
            if (send_all(fd, buf, n) < 0) {   /* keep the lines for the next connection */
                close(fd);
                fd = -1;
                break;
            }
            LightLock_Lock(&s_lock);
            s_tail += n;
            LightLock_Unlock(&s_lock);
        }
        svcSleepThread(100 * 1000 * 1000LL);
    }
}

void pm3ds_netlog_init(void) {
    s_t0 = svcGetSystemTick();
    u32 *soc = (u32 *)memalign(0x1000, SOC_BUF_SIZE);
    if (!soc) return;
    if (R_FAILED(socInit(soc, SOC_BUF_SIZE))) {
        free(soc);
        return;
    }
    /* New 3DS: core 2 (shared with the audio mixer), so a hang on core 0
     * cannot silence the report; otherwise the default core, just above the
     * game threads */
    bool n3ds = false;
    APT_CheckNew3DS(&n3ds);
    s_core = 2;
    s_thread = n3ds ? threadCreate(netlog_thread, NULL, 0x4000, 0x30, 2, true) : NULL;
    if (!s_thread) {
        s_core = -2;
        s_thread = threadCreate(netlog_thread, NULL, 0x4000, 0x2F, -2, true);
    }
    threadCreate(probe_thread, NULL, 0x1000, 0x30, n3ds ? 2 : -2, true);
    aptHook(&s_apt_cookie, apt_hook, NULL);
}

#else

void pm3ds_netlog_line(const char *s) { (void)s; }
void pm3ds_netlog_init(void) {}

#endif
