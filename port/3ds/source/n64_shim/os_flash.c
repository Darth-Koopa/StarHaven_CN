/*
 * os_flash.c — N64 FlashRAM save shim (M8.7).
 * Fidelity tier: FAITHFUL — 128 KB in-memory image, persisted to
 * <SD folder>/save.fla (M13.3, pm3ds_brand.h). Loaded once at init (a missing
 * file means a fresh, erased chip); every page write / sector erase is
 * written through to the same offset in the file, so a crash or power-off
 * never loses a completed save. The bytes are the port's native structs,
 * so the file is not interchangeable with an N64 .fla; an N64 save put in
 * the folder as import.fla / import.srm is converted at boot (end of file).
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
#include <stdlib.h>
#include <string.h>
#include "pm3ds_brand.h"
#include "pm3ds_save_import.h"
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
#ifndef PM3DS_HOST_TEST
static void flash_import(void);
#endif

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
    if (g_save) flash_import();
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

/* ---- N64 save import ------------------------------------------------------
 * The cart's FlashRAM holds the same structs big-endian, so a save from an
 * emulator or a Virtual Console extraction is the same image with every 2- and
 * 4-byte field reversed. fio.c finds its blocks by the "Mario Story 006" magic
 * and a checksum summing the block as 32-bit words (crc1 taken as 0, crc2 as ~0),
 * so each valid block is swapped field by field and its checksum redone.
 * Project64 keeps the chip as little-endian words, so the input is tried as it
 * is and with 4- and 2-byte groups reversed; the order under which the blocks'
 * checksums hold is the right one. */

typedef struct { uint16_t off; uint8_t size; uint16_t count; } SwapRun;

/* every multi-byte scalar of SaveData / SaveGlobals as (offset, size, count) runs,
 * from the DWARF of the v1.1.0 elf (port/3ds/local/scripts/fla2starhaven.py has the same) */
static const SwapRun kSaveDataRuns[] = {
    {48, 4, 4}, {76, 2, 1}, {86, 2, 3}, {94, 2, 3}, {102, 2, 3}, {110, 2, 3}, {118, 2, 3},
    {126, 2, 3}, {134, 2, 3}, {142, 2, 3}, {150, 2, 3}, {158, 2, 3}, {166, 2, 3}, {174, 2, 269},
    {716, 2, 1}, {720, 2, 1}, {724, 2, 12}, {748, 4, 3}, {760, 2, 1}, {764, 4, 1}, {768, 2, 2},
    {772, 4, 26}, {876, 2, 2}, {880, 4, 1}, {884, 2, 2}, {888, 4, 1}, {892, 2, 1}, {1128, 2, 3},
    {1136, 4, 784}, {4784, 4, 8}, {4838, 2, 3}, {4844, 4, 1},
};
static const SwapRun kGlobalsRuns[] = { {48, 4, 5} };

#define SAVEDATA_SIZE  0x1380
#define GLOBALS_SIZE   0x80
#define BLOCK_STRIDE   0x4000   /* fio.c: block i at page i * 0x80; saves 0-5, globals 6-7 */
#define NUM_BLOCKS     8
#define SRM_SIZE       0x48800  /* RetroArch mupen64plus: eeprom, 4 paks, sram, then flash */
#define SRM_FLASH      0x28800

static const char kMagic[16] = "Mario Story 006";

static uint32_t rd32(const uint8_t *p, int be) {
    return be ? (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]
              : (uint32_t)p[3] << 24 | p[2] << 16 | p[1] << 8 | p[0];
}

static void wr32(uint8_t *p, uint32_t v, int be) {
    for (int i = 0; i < 4; i++) p[be ? 3 - i : i] = (uint8_t)(v >> (8 * i));
}

static uint32_t block_sum(const uint8_t *b, uint32_t len, int be) {
    uint32_t sum = 0xFFFFFFFF;   /* crc1 = 0 plus crc2 = ~0 */
    for (uint32_t i = 0; i < len; i += 4)
        if (i != 0x30 && i != 0x34) sum += rd32(b + i, be);
    return sum;
}

static int block_valid(const uint8_t *b, uint32_t len, int be) {
    uint32_t crc1 = rd32(b + 0x30, be), crc2 = rd32(b + 0x34, be);
    return memcmp(b, kMagic, sizeof(kMagic)) == 0 && crc1 == ~crc2 && crc1 == block_sum(b, len, be);
}

static uint32_t block_size(int i) { return i < 6 ? SAVEDATA_SIZE : GLOBALS_SIZE; }

/* out = the image with every width-byte group reversed (1: as it is); bytes past the file read erased */
static void reorder(const uint8_t *in, size_t len, uint8_t *out, int width) {
    for (uint32_t i = 0; i < PM3DS_FLASH_SIZE; i++) {
        uint32_t src = i - i % width + (width - 1 - i % width);
        out[i] = src < len ? in[src] : 0xFF;
    }
}

/* bit i set: block i valid */
static unsigned valid_blocks(const uint8_t *img, int be) {
    unsigned mask = 0;
    for (int i = 0; i < NUM_BLOCKS; i++)
        if (block_valid(img + i * BLOCK_STRIDE, block_size(i), be)) mask |= 1u << i;
    return mask;
}

static int count_saves(unsigned mask) {
    int n = 0;
    for (int i = 0; i < 6; i++) n += mask >> i & 1;
    return n;
}

int pm3ds_save_import_convert(const uint8_t *in, size_t len, uint8_t *out) {
    static const int widths[] = { 1, 4, 2 };
    int best_width = 0, native = 0;
    unsigned best = 0, mask;

    if (len >= SRM_SIZE) {
        in += SRM_FLASH;
        len = PM3DS_FLASH_SIZE;
    }
    for (int w = 0; w < 3; w++) {
        reorder(in, len, out, widths[w]);
        mask = valid_blocks(out, 1);
        if (count_saves(mask) > count_saves(best)) { best = mask; best_width = widths[w]; }
    }
    reorder(in, len, out, 1);
    mask = valid_blocks(out, 0);   /* already the port's order (a Star Haven save) */
    if (count_saves(mask) > count_saves(best)) { best = mask; native = 1; }
    if (count_saves(best) == 0) return 0;

    if (!native) reorder(in, len, out, best_width);
    for (int i = 0; i < NUM_BLOCKS; i++) {
        uint8_t *b = out + i * BLOCK_STRIDE;
        uint32_t size = block_size(i);
        if (!(best >> i & 1)) {
            memset(b, 0xFF, BLOCK_STRIDE);   /* empty or stale: erased */
            continue;
        }
        memset(b + size, 0xFF, BLOCK_STRIDE - size);
        if (native) continue;
        const SwapRun *runs = i < 6 ? kSaveDataRuns : kGlobalsRuns;
        int nruns = i < 6 ? (int)(sizeof(kSaveDataRuns) / sizeof(*runs)) : 1;
        for (int r = 0; r < nruns; r++)
            for (uint32_t o = runs[r].off; o < runs[r].off + (uint32_t)runs[r].size * runs[r].count; o += runs[r].size)
                for (int k = 0; k < runs[r].size / 2; k++) {
                    uint8_t t = b[o + k];
                    b[o + k] = b[o + runs[r].size - 1 - k];
                    b[o + runs[r].size - 1 - k] = t;
                }
        uint32_t crc = block_sum(b, size, 0);
        wr32(b + 0x30, crc, 0);
        wr32(b + 0x34, ~crc, 0);
    }
    /* the game keeps several copies of a file: count the player's file slots */
    unsigned slots = 0;
    for (int i = 0; i < 6; i++)
        if (best >> i & 1) slots |= 1u << (rd32(out + i * BLOCK_STRIDE + 0x38, 0) & 3);
    return count_saves(slots);
}

static int s_import_result;
static char s_import_backup[32];

int pm3ds_save_import_result(void) { return s_import_result; }
const char *pm3ds_save_import_backup(void) { return s_import_backup; }
void pm3ds_save_import_dismiss(void) { s_import_result = 0; }

#ifndef PM3DS_HOST_TEST
/* the old save is kept as save.fla.bak (or .bak2 ... .bak9 if those are taken) */
static int backup_save(void) {
    int i;
    for (i = 0; i < PM3DS_FLASH_SIZE && g_flash[i] == 0xFF; i++) {}
    if (i == PM3DS_FLASH_SIZE) return 1;   /* nothing saved yet */
    for (int n = 1; n <= 9; n++) {
        char path[80];
        FILE *f;
        snprintf(s_import_backup, sizeof(s_import_backup), n == 1 ? "%s.bak" : "%s.bak%d", PM3DS_SAVE_NAME, n);
        snprintf(path, sizeof(path), "%s/%s", FLASH_SAVE_DIR, s_import_backup);
        if ((f = fopen(path, "rb"))) { fclose(f); continue; }
        if (!(f = fopen(path, "wb"))) break;
        size_t put = fwrite(g_flash, 1, sizeof(g_flash), f);
        if (fclose(f) == 0 && put == sizeof(g_flash)) return 1;
        break;
    }
    s_import_backup[0] = '\0';
    return 0;
}

static void flash_import(void) {
    static const char *const names[] = { FLASH_SAVE_DIR "/import.fla", FLASH_SAVE_DIR "/import.srm" };
    for (int i = 0; i < 2; i++) {
        FILE *f = fopen(names[i], "rb");
        if (!f) continue;
        uint8_t *in = malloc(SRM_SIZE), *img = malloc(PM3DS_FLASH_SIZE);
        size_t len = in ? fread(in, 1, SRM_SIZE, f) : 0;
        fclose(f);
        int n = in && img ? pm3ds_save_import_convert(in, len, img) : 0;
        char retired[80];
        pm3ds_log(PM3DS_LOG_INFO, "os_flash: %s: %u bytes, %d save files", names[i], (unsigned)len, n);
        if (n > 0 && !backup_save()) {
            pm3ds_log(PM3DS_LOG_WARN, "os_flash: could not back up the save, import skipped");
            n = -2;
        }
        if (n != -2) {
            /* retire the file first: an import that ran again on every boot would undo the player's progress */
            snprintf(retired, sizeof(retired), "%s.%s", names[i], n > 0 ? "done" : "bad");
            remove(retired);
            if (rename(names[i], retired) != 0 && remove(names[i]) != 0) {
                pm3ds_log(PM3DS_LOG_WARN, "os_flash: could not rename %s, import skipped", names[i]);
                n = -2;
            }
        }
        if (n > 0) {
            memcpy(g_flash, img, sizeof(g_flash));
            flash_persist(0, sizeof(g_flash));
        }
        if (n != -2) s_import_result = n > 0 ? n : -1;
        free(in);
        free(img);
        return;
    }
}
#endif
