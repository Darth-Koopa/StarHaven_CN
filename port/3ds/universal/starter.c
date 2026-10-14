/*
 * starter.c — main() of the release package, which holds the game twice:
 * built for the European ROM and for the US one (tools/universal.py links
 * both; the US copy's symbols carry a us_ prefix). This looks at the
 * player's ROM on the SD card and hands over to the matching copy.
 *
 * One ROM (or none): run its copy; with none, the PAL copy shows what to do.
 * Both versions on the card: ask which to play.
 */
#include <3ds.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pm3ds_brand.h"

int pal_main(int argc, char **argv);   /* the PAL copy's port_main.c main(), renamed */
int us_main(int argc, char **argv);    /* the US copy's */
void pm3ds_sd_migrate(void);           /* the PAL copy's (pm3ds_rom.c): the old SD folder */

/* the region letter of an N64 Paper Mario ROM file in any byte order:
 * 'P' (Europe), 'E' (USA); 0 for anything else */
static char rom_region(const char *path) {
    unsigned char h[0x40];
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t n = fread(h, 1, sizeof h, f);
    fclose(f);
    if (n != sizeof h) return 0;
    uint32_t w = (uint32_t)h[0] << 24 | (uint32_t)h[1] << 16 | (uint32_t)h[2] << 8 | h[3];
    if (w == 0x37804012u) {                       /* .v64: 16-bit halves swapped */
        for (size_t i = 0; i < n; i += 2) {
            unsigned char t = h[i];
            h[i] = h[i + 1];
            h[i + 1] = t;
        }
    } else if (w == 0x40123780u) {                /* .n64: 32-bit words reversed */
        for (size_t i = 0; i < n; i += 4) {
            unsigned char a = h[i], b = h[i + 1];
            h[i] = h[i + 3];
            h[i + 1] = h[i + 2];
            h[i + 2] = b;
            h[i + 3] = a;
        }
    } else if (w != 0x80371240u) {
        return 0;
    }
    if (memcmp(h + 0x3B, "NMQ", 3)) return 0;     /* game code: Paper Mario */
    return (h[0x3E] == 'P' || h[0x3E] == 'E') ? (char)h[0x3E] : 0;
}

static bool has_rom_ext(const char *name) {
    size_t l = strlen(name);
    if (l < 4 || name[l - 4] != '.') return false;
    const char *e = name + l - 3;
    return !strcasecmp(e, "z64") || !strcasecmp(e, "n64") || !strcasecmp(e, "v64");
}

/* which versions are on the card, as the copies look for them (pm3ds_rom.c) */
static void find_versions(bool *eu, bool *us) {
    DIR *d = opendir(PM3DS_SD_DIR);
    if (!d) return;
    struct dirent *e;
    char path[300];
    while ((e = readdir(d)) != NULL) {
        if (!has_rom_ext(e->d_name)) continue;
        snprintf(path, sizeof path, PM3DS_SD_DIR "/%s", e->d_name);
        char r = rom_region(path);
        *eu |= r == 'P';
        *us |= r == 'E';
    }
    closedir(d);
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
    text(116, "Two Paper Mario ROMs found", 1, 116, 72, 40);
    text(140, "Choose one on the", 1, 116, 72, 40);
    text(152, "bottom screen", 1, 116, 72, 40);
    gfxFlushBuffers();
}

/* both versions present: 'E' (USA), 'P' (Europe), 0 if the player closed the app */
static char ask(void) {
    gfxInitDefault();
    draw_top();
    consoleInit(GFX_BOTTOM, NULL);
    printf("\n\n  " PM3DS_APP_NAME "\n\n"
           "  Your SD card has two ROMs.\n"
           "  Which one do you want to play?\n\n"
           "    A   USA\n"
           "    B   Europe (English, German,\n"
           "        French, Spanish)\n");
    char pick = 0;
    while (!pick && aptMainLoop()) {
        hidScanInput();
        u32 k = hidKeysDown();
        if (k & KEY_A) pick = 'E';
        else if (k & KEY_B) pick = 'P';
        gfxFlushBuffers();      /* without these the console never reaches the screen */
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    gfxExit();
    return pick;
}

int main(int argc, char **argv) {
    pm3ds_sd_migrate();
    bool eu = false, us = false;
    find_versions(&eu, &us);
    char pick = us && !eu ? 'E' : eu && !us ? 'P' : us && eu ? ask() : 'P';
    if (pick == 'E') return us_main(argc, argv);
    if (pick == 'P') return pal_main(argc, argv);
    return 0;
}
