/*
 * os_flash.c — N64 FlashRAM save shim (M8.7).
 * Fidelity tier: FAITHFUL — 128 KB in-memory image, persisted to
 * <SD folder>/save.fla (M13.3, pm3ds_brand.h). Loaded once at init (a missing
 * file means a fresh, erased chip); every page write / sector erase is
 * written through to the same offset in the file, so a crash or power-off
 * never loses a completed save. The bytes are the port's native structs,
 * so the file is not interchangeable with an N64 .fla.
 *
 * Why this exists: the weak m4-stub osFlashReadArray is a no-op, but
 * fio_read_flash() follows it with osRecvMesg(mq, NULL, OS_MESG_BLOCK)
 * waiting for the DMA-completion message the real driver posts. With
 * the no-op stub no message ever arrives and the game thread deadlocks
 * — observed at frame 18 inside state_step_startup -> fio_load_globals.
 *
 * Page model: one page = 0x80 bytes, matching both the real FlashRAM
 * block size and sizeof(SaveGlobals) (common_structs.h documents the
 * struct as exactly 0x80), which is the unit fio.c's page arithmetic
 * assumes. 1024 pages = 128 KB, the standard FlashRAM capacity.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "pm3ds_brand.h"
#include "pm3ds_rom_layout.h"
#ifndef PM3DS_HOST_TEST
#include <sys/stat.h>
#endif
#include "pm3ds_log.h"

#define FLASH_SAVE_DIR   PM3DS_SD_DIR
#define FLASH_SAVE_PATH  FLASH_SAVE_DIR "/" PM3DS_SAVE_NAME   /* one per version: the layouts differ */

#define FLASH_PAGE_SIZE   0x80
#define FLASH_NUM_PAGES   1024
#define FLASH_SECTOR_PAGES 128   /* 16 KB erase granularity */

static uint8_t g_flash[FLASH_NUM_PAGES * FLASH_PAGE_SIZE];
static int     g_flash_inited = 0;

/* Pending write-buffer pointer, armed by osFlashWriteBuffer and
 * consumed by osFlashWriteArray — mirrors the real two-step protocol. */
static const uint8_t *g_pending_write = 0;

/* include_asm.h (force-included by the build) already forward-declares
 * struct OSIoMesg / struct OSMesgQueue and the osFlash* prototypes —
 * our definitions below must match it exactly. */
struct OSIoMesg;
struct OSMesgQueue;
extern int osSendMesg(struct OSMesgQueue *mq, void *msg, int mode);
#define OS_MESG_NOBLOCK 0

static FILE *g_save;   /* kept open for write-through */

static void flash_lazy_init(void) {
    if (g_flash_inited) return;
    g_flash_inited = 1;
    memset(g_flash, 0xFF, sizeof(g_flash));  /* erased flash reads 0xFF */
#ifndef PM3DS_HOST_TEST
    mkdir("sdmc:/3ds", 0777);
    mkdir(FLASH_SAVE_DIR, 0777);
    g_save = fopen(FLASH_SAVE_PATH, "r+b");
    if (g_save) {
        size_t got = fread(g_flash, 1, sizeof(g_flash), g_save);
        pm3ds_log(PM3DS_LOG_INFO, "os_flash: loaded %u bytes from " FLASH_SAVE_PATH, (unsigned)got);
    } else {
        g_save = fopen(FLASH_SAVE_PATH, "w+b");
        if (g_save) {
            fwrite(g_flash, 1, sizeof(g_flash), g_save);
            fflush(g_save);
        }
        pm3ds_log(PM3DS_LOG_INFO, "os_flash: new save file %s", g_save ? "created" : "UNAVAILABLE (in-memory only)");
    }
#endif
}

static void flash_persist(uint32_t offset, uint32_t len) {
#ifndef PM3DS_HOST_TEST
    if (!g_save) return;
    if (fseek(g_save, (long)offset, SEEK_SET) == 0) {
        fwrite(g_flash + offset, 1, len, g_save);
        fflush(g_save);
    }
#else
    (void)offset; (void)len;
#endif
}

int osFlashInit(void) {
    flash_lazy_init();
    return 0;
}

/* mb/priority unused: the "DMA" completes synchronously. */
int osFlashReadArray(struct OSIoMesg *mb, int priority, unsigned int page_num,
                     void *dramAddr, unsigned int n_pages, struct OSMesgQueue *mq) {
    (void)mb; (void)priority;
    flash_lazy_init();
    if (page_num + n_pages > FLASH_NUM_PAGES) {
        pm3ds_log(PM3DS_LOG_WARN, "os_flash: read beyond device (page=%lu n=%lu)",
                  (unsigned long)page_num, (unsigned long)n_pages);
        memset(dramAddr, 0xFF, n_pages * FLASH_PAGE_SIZE);
    } else {
        memcpy(dramAddr, g_flash + page_num * FLASH_PAGE_SIZE,
               n_pages * FLASH_PAGE_SIZE);
    }
    return osSendMesg(mq, 0, OS_MESG_NOBLOCK);
}

int osFlashWriteBuffer(struct OSIoMesg *mb, int priority, void *dramAddr,
                       struct OSMesgQueue *mq) {
    (void)mb; (void)priority;
    flash_lazy_init();
    g_pending_write = (const uint8_t *)dramAddr;
    return osSendMesg(mq, 0, OS_MESG_NOBLOCK);
}

int osFlashWriteArray(int pageNum) {
    flash_lazy_init();
    if (g_pending_write && pageNum >= 0 && pageNum < FLASH_NUM_PAGES) {
        memcpy(g_flash + pageNum * FLASH_PAGE_SIZE, g_pending_write,
               FLASH_PAGE_SIZE);
        flash_persist((uint32_t)pageNum * FLASH_PAGE_SIZE, FLASH_PAGE_SIZE);
    }
    g_pending_write = 0;
    return 0;
}

int osFlashSectorErase(int offset) {
    uint32_t page_num = (uint32_t)offset;
    uint32_t sector_start = (page_num / FLASH_SECTOR_PAGES) * FLASH_SECTOR_PAGES;
    flash_lazy_init();
    if (sector_start < FLASH_NUM_PAGES) {
        memset(g_flash + sector_start * FLASH_PAGE_SIZE, 0xFF,
               FLASH_SECTOR_PAGES * FLASH_PAGE_SIZE);
        flash_persist(sector_start * FLASH_PAGE_SIZE, FLASH_SECTOR_PAGES * FLASH_PAGE_SIZE);
    }
    return 0;  /* 0 = success; fio_erase_flash's PAL retry loop breaks on it */
}

int osFlashAllErase(void) {
    flash_lazy_init();
    memset(g_flash, 0xFF, sizeof(g_flash));
    flash_persist(0, sizeof(g_flash));
    return 0;
}
