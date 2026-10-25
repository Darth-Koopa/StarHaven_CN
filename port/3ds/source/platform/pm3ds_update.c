/*
 * pm3ds_update.c — tell the player when a newer release is out.
 *
 * Once per launch a background thread asks GitHub where the repository's
 * latest-release page leads: it answers with a redirect to
 * .../releases/tag/vX.Y.Z, so one request and the Location header are all it
 * takes (no JSON, no API rate limit). When that version is newer than this
 * build, the bottom screen shows a notice (bottom_screen.c). Nothing is
 * downloaded or installed. No Wi-Fi, no answer or a refused connection: no
 * notice. Options -> Update check turns it off (pm3ds_settings.c).
 */
#include <3ds.h>
#include <stdio.h>
#include <string.h>

#include "pm3ds_brand.h"
#include "pm3ds_log.h"
#include "pm3ds_settings.h"
#include "pm3ds_update.h"

#define LATEST_URL   "https://github.com/" PM3DS_GITHUB_REPO "/releases/latest"
#ifdef PM3DS_UPDATE_TEST_VERSION   /* test builds: pretend to be an older version, so the notice shows */
#define THIS_VERSION PM3DS_UPDATE_TEST_VERSION
#else
#define THIS_VERSION PM3DS_VERSION_CODE(PM3DS_VERSION_MAJOR, PM3DS_VERSION_MINOR, PM3DS_VERSION_MICRO)
#endif

static volatile int s_latest;   /* newer release found, 0 none (or dismissed) */

/* "https://github.com/<repo>/releases/tag/v1.2.3" -> 10203; 0 if it isn't one */
static int parse_tag(const char *loc) {
    const char *t = strstr(loc, "/releases/tag/");
    if (!t) return 0;
    t += 14;
    if (*t == 'v' || *t == 'V') t++;
    int a = -1, b = -1, c = 0;
    if (sscanf(t, "%d.%d.%d", &a, &b, &c) < 2) return 0;
    if (a < 0 || a > 99 || b < 0 || b > 99 || c < 0 || c > 99) return 0;
    return PM3DS_VERSION_CODE(a, b, c);
}

/* the console may still be joining its network right after launch */
static bool wait_for_wifi(void) {
    if (R_FAILED(acInit())) return false;
    bool up = false;
    for (int i = 0; i < 15 && !up; i++) {
        u32 status = 0;
        up = R_SUCCEEDED(ACU_GetWifiStatus(&status)) && status != 0;
        if (!up) svcSleepThread(2000 * 1000 * 1000LL);
    }
    acExit();
    return up;
}

static void check_thread(void *arg) {
    (void)arg;
    if (!wait_for_wifi()) {
        pm3ds_log(PM3DS_LOG_WARN, "update check: no Wi-Fi");
        return;
    }
    if (R_FAILED(httpcInit(0))) {
        pm3ds_log(PM3DS_LOG_WARN, "update check: httpc unavailable");
        return;
    }
    char loc[256] = "";
    u32 status = 0;
    httpcContext ctx;
    /* each step's result code goes to the log: the console's own SSL module does
     * the TLS, and what it can't do only shows on hardware (GitHub #51) */
    Result rc = httpcOpenContext(&ctx, HTTPC_METHOD_GET, LATEST_URL, 0);
    const char *step = "open";
    if (R_SUCCEEDED(rc)) {
        /* the 3DS's root certificates are old; only a version number is read
         * from the answer, and the notice's text is fixed */
        httpcSetSSLOpt(&ctx, SSLCOPT_DisableVerify);
        httpcSetKeepAlive(&ctx, HTTPC_KEEPALIVE_DISABLED);
        httpcAddRequestHeaderField(&ctx, "User-Agent", PM3DS_APP_NAME "/" PM3DS_VERSION_STR);
        step = "request";
        rc = httpcBeginRequest(&ctx);
        if (R_SUCCEEDED(rc)) {
            step = "status";
            rc = httpcGetResponseStatusCodeTimeout(&ctx, &status, 20 * 1000 * 1000 * 1000LL);
        }
        if (R_SUCCEEDED(rc) && status >= 300 && status < 400) {
            step = "location";
            rc = httpcGetResponseHeader(&ctx, "Location", loc, sizeof loc - 1);
        }
        httpcCloseContext(&ctx);
    }
    if (R_FAILED(rc)) pm3ds_log(PM3DS_LOG_WARN, "update check: %s failed, rc %08lX", step, (unsigned long)rc);
    httpcExit();
    int latest = parse_tag(loc);
    /* a failed check is a warning, so it reaches log.txt (GitHub #51: "doesn't work for me") */
    pm3ds_log(latest ? PM3DS_LOG_INFO : PM3DS_LOG_WARN, "update check: HTTP %lu, latest %d.%d.%d", (unsigned long)status,
              latest / 10000, latest / 100 % 100, latest % 100);
    if (latest > THIS_VERSION && latest != pm3ds_cfg.update_skip) s_latest = latest;
}

void pm3ds_update_check_start(void) {
    if (!pm3ds_cfg.update_check) return;
    /* below the main thread, above the game's (port_main.c); it mostly waits */
    if (!threadCreate(check_thread, NULL, 0x4000, 0x38, -2, true))
        pm3ds_log(PM3DS_LOG_WARN, "update check: no thread");
}

int pm3ds_update_available(void) { return s_latest; }

void pm3ds_update_dismiss(bool skip) {
    if (skip && s_latest) {
        pm3ds_cfg.update_skip = s_latest;
        pm3ds_settings_save();
    }
    s_latest = 0;
}
