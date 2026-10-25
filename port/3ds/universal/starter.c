/*
 * starter.c — main() of the universal release, which links PAL, US and the
 * official iQue Chinese build. tools/universal.py keeps PAL symbols as-is
 * and prefixes the US/iQue copies with us_ and ique_. This looks at the
 * player's ROM on the SD card and hands over to the matching copy.
 *
 * One ROM (or none): run its copy; with none, the PAL copy shows setup help.
 * Multiple supported ROMs on the card: ask which one to play.
 */
#include <3ds.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pm3ds_brand.h"

int pal_main(int argc, char **argv);   /* the PAL copy's port_main.c main(), renamed */
int us_main(int argc, char **argv);    /* the US copy's */
int ique_main(int argc, char **argv); /* official iQue Chinese copy */
void pm3ds_sd_migrate(void);           /* the PAL copy's (pm3ds_rom.c): the old SD folder */

/* The official iQue dump is 40 MiB, has a blank game-code field at 0x3B,
 * and is identified by CRC32 over the canonical .z64-order first 40 MiB.
 * Keep these constants in sync with pm3ds_rom_layout.h. */
#define IQUE_ROM_DATA_SIZE 0x2800000u
#define IQUE_ROM_CRC32     0x18C02AD5u

enum { ROM_ORDER_Z64, ROM_ORDER_V64, ROM_ORDER_N64, ROM_ORDER_BAD };

static int rom_order(const unsigned char *h) {
    uint32_t w = (uint32_t)h[0] << 24 | (uint32_t)h[1] << 16 | (uint32_t)h[2] << 8 | h[3];
    if (w == 0x80371240u) return ROM_ORDER_Z64;
    if (w == 0x37804012u) return ROM_ORDER_V64;
    if (w == 0x40123780u) return ROM_ORDER_N64;
    return ROM_ORDER_BAD;
}

static void to_z64(unsigned char *p, size_t n, int order) {
    if (order == ROM_ORDER_V64) {
        for (size_t i = 0; i + 1 < n; i += 2) {
            unsigned char t = p[i]; p[i] = p[i + 1]; p[i + 1] = t;
        }
    } else if (order == ROM_ORDER_N64) {
        for (size_t i = 0; i + 3 < n; i += 4) {
            unsigned char a = p[i], b = p[i + 1];
            p[i] = p[i + 3]; p[i + 1] = p[i + 2]; p[i + 2] = b; p[i + 3] = a;
        }
    }
}

static uint32_t s_crc_table[256];
static bool s_crc_table_ready;

static void crc_prepare(void) {
    if (s_crc_table_ready) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int bit = 0; bit < 8; bit++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        s_crc_table[i] = c;
    }
    s_crc_table_ready = true;
}

static uint32_t crc_update(uint32_t crc, const unsigned char *p, size_t n) {
    crc_prepare();
    crc = ~crc;
    while (n--) crc = (crc >> 8) ^ s_crc_table[(crc ^ *p++) & 0xFFu];
    return ~crc;
}

/* Only called for a candidate whose N64 header is valid but whose game ID
 * field is all zero. Stream it in small chunks so the launcher never needs a
 * 40 MiB allocation. CRC is calculated after normalizing .z64/.v64/.n64. */
static uint32_t rom_crc32_prefix(const char *path, uint32_t length, int order) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    unsigned char *buf = (unsigned char *)malloc(512 * 1024);
    if (!buf) { fclose(f); return 0; }
    uint32_t crc = 0;
    uint32_t done = 0;
    while (done < length) {
        size_t want = length - done < 512 * 1024 ? length - done : 512 * 1024;
        size_t got = fread(buf, 1, want, f);
        if (got != want) { crc = 0; break; }
        to_z64(buf, got, order);
        crc = crc_update(crc, buf, got);
        done += (uint32_t)got;
    }
    free(buf);
    fclose(f);
    return done == length ? crc : 0;
}

/* Return E=USA, P=Europe, Q=iQue China, or 0 for unsupported/unknown ROM. */
static char rom_region(const char *path) {
    unsigned char h[0x40];
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t n = fread(h, 1, sizeof h, f);
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    long file_size = ftell(f);
    fclose(f);
    if (n != sizeof h) return 0;
    int order = rom_order(h);
    if (order == ROM_ORDER_BAD) return 0;
    to_z64(h, n, order);
    if (!memcmp(h + 0x3B, "NMQ", 3))
        return (h[0x3E] == 'P' || h[0x3E] == 'E') ? (char)h[0x3E] : 0;

    /* The authentic iQue dump has zeroes where a normal N64 ID would be.
     * Require the known CRC as well as the blank ID; size alone is unsafe
     * because the USA release is also 40 MiB. */
    if (file_size >= (long)IQUE_ROM_DATA_SIZE &&
        h[0x3B] == 0 && h[0x3C] == 0 && h[0x3D] == 0 && h[0x3E] == 0 &&
        rom_crc32_prefix(path, IQUE_ROM_DATA_SIZE, order) == IQUE_ROM_CRC32)
        return 'Q';
    return 0;
}

/* Scan all folders shared with the ROM loader. Identify supported versions
 * from the header, not the file name; the iQue dump is validated by CRC. */
static void find_versions(bool *eu, bool *us, bool *ique) {
    static const char *const dirs[] = { PM3DS_ROM_DIRS };
    for (size_t i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
        DIR *d = opendir(dirs[i]);
        if (!d) continue;
        size_t l = strlen(dirs[i]);
        const char *sep = l && dirs[i][l - 1] == '/' ? "" : "/";
        struct dirent *e;
        char path[300];
        while ((e = readdir(d)) != NULL) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            if (e->d_type == DT_DIR) continue;
            int n = snprintf(path, sizeof path, "%s%s%s", dirs[i], sep, e->d_name);
            if (n < 0 || (size_t)n >= sizeof path) continue;
            char r = rom_region(path);
            *eu |= r == 'P';
            *us |= r == 'E';
            *ique |= r == 'Q';
        }
        closedir(d);
    }
}
    }
}

/* ---- the top screen while asking: the boot screens' look, drawn by hand ---- */
static u8 *s_top;   /* BGR8, column-major from the bottom (the panel's native layout) */

static void put(int x, int y, u8 r, u8 g, u8 b) {
    if (x < 0 || x >= 400 || y < 0 || y >= 240) return;
    u8 *p = s_top + ((size_t)x * 240 + (239 - y)) * 3;
    p[0] = b; p[1] = g; p[2] = r;
}

static void box(int x0, int y0, int x1, int y1, u8 r, u8 g, u8 b) {
    for (int x = x0; x < x1; x++)
        for (int y = y0; y < y1; y++) put(x, y, r, g, b);
}

/* the console's 8x8 font at an integer scale, centred on x = 200 */
static void text(int y, const char *str, int scale, u8 r, u8 g, u8 b) {
    PrintConsole *pc = consoleGetDefault();
    int x = 200 - (int)strlen(str) * 4 * scale;
    for (; *str; str++, x += 8 * scale) {
        unsigned char c = (unsigned char)*str;
        if (!pc->font.gfx || c >= pc->font.numChars) continue;
        const u8 *gl = (const u8 *)pc->font.gfx + c * 8;
        for (int j = 0; j < 8; j++)
            for (int i = 0; i < 8; i++)
                if (gl[j] & (0x80 >> i)) box(x + i * scale, y + j * scale, x + (i + 1) * scale, y + (j + 1) * scale, r, g, b);
    }
}

static void draw_top(void) {
    gfxSetDoubleBuffering(GFX_TOP, false);
    s_top = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
    for (int y = 0; y < 240; y++) {   /* orange gradient */
        int r = 250 - (250 - 222) * y / 239, g = 196 - (196 - 130) * y / 239, b = 110 - (110 - 60) * y / 239;
        box(0, y, 400, y + 1, (u8)r, (u8)g, (u8)b);
    }
    box(79, 65, 327, 181, 80, 40, 10);        /* shadow */
    box(76, 62, 324, 178, 116, 72, 40);       /* rim */
    box(78, 64, 322, 176, 255, 247, 222);     /* panel */
    text(82, PM3DS_APP_NAME_CAPS, 2, 116, 72, 40);
    text(116, "Paper Mario ROMs found", 1, 116, 72, 40);
    text(140, "Choose one below", 1, 116, 72, 40);
    gfxFlushBuffers();
}

/* Several supported ROMs are present: A=USA, B=Europe, X=iQue China. */
static char ask(bool eu, bool us, bool ique) {
    gfxInitDefault();
    draw_top();
    consoleInit(GFX_BOTTOM, NULL);
    printf("\n\n  " PM3DS_APP_NAME "\n\n"
           "  Choose your ROM:\n\n");
    if (us) printf("    A   USA\n");
    if (eu) printf("    B   Europe\n");
    if (ique) printf("    X   iQue China (Chinese)\n");
    printf("\n    START   Exit\n");
    char pick = 0;
    while (!pick && aptMainLoop()) {
        hidScanInput();
        u32 k = hidKeysDown();
        if ((k & KEY_A) && us) pick = 'E';
        else if ((k & KEY_B) && eu) pick = 'P';
        else if ((k & KEY_X) && ique) pick = 'Q';
        else if (k & KEY_START) break;
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    gfxExit();
    return pick;
}

int main(int argc, char **argv) {
    pm3ds_sd_migrate();
    bool eu = false, us = false, ique = false;
    find_versions(&eu, &us, &ique);
    int count = (int)eu + (int)us + (int)ique;
    char pick;
    if (count == 1) pick = us ? 'E' : eu ? 'P' : 'Q';
    else if (count > 1) pick = ask(eu, us, ique);
    else pick = 'P'; /* PAL build shows the existing setup/help screen. */
    if (pick == 'E') return us_main(argc, argv);
    if (pick == 'P') return pal_main(argc, argv);
    if (pick == 'Q') return ique_main(argc, argv);
    return 0;
}
