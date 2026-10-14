/*
 * m9_background_shim.c -- M9: fix the title-screen per-frame hang.
 *
 * THE BUG (localized 2026-08-03 from the PM3DS_TITLE_DBG breadcrumb run)
 * ---------------------------------------------------------------------
 * The last breadcrumb before the freeze was "bg: post gfx_draw_background" and
 * the next one ("bg: post nuGfxTaskStart") never printed, so the main thread
 * died inside gfx_task_background() (src/main_loop.c:194-205) while the audio
 * thread kept running. The only blocking thing in that window is
 *
 *     ASSERT(... (gMainGfxPos - gDisplayContext->backgroundGfx) ...
 *            < ARRAY_COUNT(gDisplayContext->backgroundGfx))     main_loop.c:200
 *
 * and in a non-DEBUG decomp build ASSERT expands, via IS_DEBUG_PANIC, to
 * `do {} while(true)` -- a SILENT INFINITE LOOP (include/macros.h:71). So the
 * hang is "the background display list overflowed backgroundGfx[0x200]".
 *
 * Why it overflowed. state_init_title_screen (src/state_title_screen.c:235-236)
 * does:
 *     load_map_bg("title_bg");          <- no-op'd by __wrap_load_map_bg (M8.10)
 *     set_background(&gBackgroundImage);
 * M8.10 skipped the LOAD but left the SET, on the assumption (recorded in
 * m6_load_engine_data_wrap.c) that "gBackgroundImage stays zero, so
 * set_background() leaves backgroundRaster = 0 and the bg-texture pass hits the
 * no-texture path harmlessly". That assumption is wrong twice over:
 *
 *   1. set_background() sets BACKGROUND_FLAG_TEXTURE *unconditionally*
 *      (src/background.c:51), so a zero raster does NOT bypass the textured
 *      path -- gfx_draw_background takes appendGfx_background_texture() either
 *      way (src/background_gfx.c:459-463).
 *
 *   2. gBackgroundImage does not "stay zero" at all. It is an N64
 *      undefined_sym (`gBackgroundImage = 0x80200000; // hard-coded inside
 *      mapfs`, ver/pal/undefined_syms.txt:2), so on this port it linked to a
 *      4-byte WEAK stub -- while BackgroundHeader is 0x10 bytes. The header
 *      read therefore ran off the end of the stub and picked up the
 *      neighbouring symbols: raster/palette came from gBackgroundImage and
 *      gRandSeed, startX/startY from gSoundManager, and width/height from the
 *      *function* stub get_clamped_angle_diff. Pure garbage, and gRandSeed even
 *      changes every frame.
 *
 * appendGfx_background_texture (src/background.c:231-251) then computes
 *     lineHeight = 2048 / backgroundMaxX;
 *     numLines   = backgroundMaxY / lineHeight;
 * and emits ~9 Gfx words per line into gDisplayContext->backgroundGfx, which
 * holds only 0x200 entries (include/common_structs.h:2279). Garbage dimensions
 * make numLines unbounded -> overflow -> ASSERT -> silent infinite loop.
 *
 * THE TWO FIXES HERE
 * ------------------
 *  1. A real, correctly sized, zero-initialised backing object for
 *     gBackgroundImage, so the header stops aliasing unrelated symbols -- and
 *     so a future un-skipped load_map_bg() has somewhere to decode into instead
 *     of smashing gRandSeed and friends.
 *
 *  2. __wrap_set_background() validates the header and refuses to turn on
 *     BACKGROUND_FLAG_TEXTURE for a background that was never loaded, falling
 *     back to the plain fill-rect path. This guards every caller (title,
 *     battle, pause, file-select, world), not just the title screen -- all of
 *     them are cross-TU from src/background.c, so --wrap intercepts them all.
 *
 * Note that fix 2 is required even with fix 1: a zeroed header would give
 * backgroundMaxX == 0, and appendGfx_background_texture would then divide by
 * zero at src/background.c:210/232 and can spin forever in the
 * `while (scrollValue < 0.0f) scrollValue += backgroundMaxX * 32;` loop at
 * src/background.c:206. Zeroing the storage is necessary but not sufficient;
 * the textured path must simply not be entered without a real background.
 */

#include <stdint.h>
#include "pm3ds_log.h"

/* ------------------------------------------------------------------------ */
/* 1. Real backing store for gBackgroundImage                                */
/* ------------------------------------------------------------------------ */

/*
 * The decomp declares `extern BackgroundHeader gBackgroundImage;` (0x10 bytes,
 * include/variables.h:42) but load_map_bg() decodes an ENTIRE background --
 * header, CI8 raster and 256-entry palette -- starting at that address. On N64
 * the symbol pointed into a reserved region at 0x80200000; reserve the same
 * shape here. The first 0x10 bytes are the header the decomp sees, the rest is
 * room for the raster + palette.
 *
 * Deliberately typed as a byte array, and this translation unit deliberately
 * does not include the decomp headers, so the two declarations can never
 * disagree at compile time. The `= {0}` matters: it makes this a real
 * definition in .bss rather than a COMMON symbol (the port builds with
 * -fcommon), which is what lets it override the weak stub at link time.
 */
#define PM3DS_BG_STORE_SIZE 0x20000 /* 128 KB: 320x240 CI8 + palette + slack */

unsigned char gBackgroundImage[PM3DS_BG_STORE_SIZE] __attribute__((aligned(8))) = { 0 };

/* ------------------------------------------------------------------------ */
/* 2. set_background() guard                                                 */
/* ------------------------------------------------------------------------ */

/* Mirror of the decomp's BackgroundHeader (include/common_structs.h:2004). */
typedef struct {
    void          *raster;  /* 0x00 */
    void          *palette; /* 0x04 */
    unsigned short startX;  /* 0x08 */
    unsigned short startY;  /* 0x0A */
    unsigned short width;   /* 0x0C */
    unsigned short height;  /* 0x0E */
} Pm3dsBackgroundHeader;

/* GameStatus field offsets (include/common_structs.h). The n64_shim layer keeps
 * gGameStatusPtr opaque and pokes fields by offset -- same idiom as
 * m6_load_engine_data_wrap.c. */
#define GS_BG_FLAGS   0x148 /* s16 */
#define GS_BG_MIN_X   0x14A /* s16 */
#define GS_BG_MIN_Y   0x14C /* s16 */
#define GS_BG_MAX_X   0x14E /* s16 */
#define GS_BG_MAX_Y   0x150 /* s16 */
#define GS_BG_RASTER  0x154 /* IMG_PTR */
#define GS_BG_PALETTE 0x158 /* PAL_PTR */

#define BACKGROUND_FLAG_TEXTURE 0x01 /* include/enums.h:6312 */

#define PM3DS_SCREEN_WIDTH  320 /* include/macros.h:116-117 */
#define PM3DS_SCREEN_HEIGHT 240

extern void *gGameStatusPtr;

/*
 * Is this header one we can safely hand to appendGfx_background_texture?
 *
 * The bounds are what keep the emitted display list inside backgroundGfx[0x200]:
 * numLines == height / (2048 / width), at ~9 Gfx per line. width <= 1024 keeps
 * lineHeight >= 2 and height <= 512 then caps numLines at 256 -- still more
 * than the array can take in the worst case, but any real Paper Mario
 * background is ~296x200 (33 lines, ~300 Gfx), and anything outside these
 * bounds is certainly garbage rather than a tight-but-valid background.
 */
static int pm3ds_bg_header_ok(const Pm3dsBackgroundHeader *bg) {
    if (bg == 0) {
        return 0;
    }
    /* A background that was never loaded has no pixels and no colours. */
    if (bg->raster == 0 || bg->palette == 0) {
        return 0;
    }
    /* width == 0 would divide by zero at src/background.c:210 and 232. */
    if (bg->width == 0 || bg->width > 1024) {
        return 0;
    }
    if (bg->height == 0 || bg->height > 512) {
        return 0;
    }
    return 1;
}

extern void __real_set_background(void *bg);

void __wrap_set_background(void *bgv) {
    const Pm3dsBackgroundHeader *bg = (const Pm3dsBackgroundHeader *)bgv;

    if (pm3ds_bg_header_ok(bg)) {
        __real_set_background(bgv);
        return;
    }

    /*
     * Not loaded, or not sane. Leave BACKGROUND_FLAG_TEXTURE clear so
     * gfx_draw_background takes the plain gDPFillRectangle path
     * (src/background_gfx.c:459-460) and fills the screen with the camera's
     * bgColor instead of walking a garbage raster.
     *
     * backgroundMin* is the origin and backgroundMax* is a SIZE added to it
     * (src/background_gfx.c:405-408), so origin 0 + full screen size covers the
     * whole frame and skips the black border fills at :471-489.
     */
    if (gGameStatusPtr != 0) {
        unsigned char *gs = (unsigned char *)gGameStatusPtr;

        *(short *)(gs + GS_BG_FLAGS) &= (short)~BACKGROUND_FLAG_TEXTURE;
        *(short *)(gs + GS_BG_MIN_X) = 0;
        *(short *)(gs + GS_BG_MIN_Y) = 0;
        *(short *)(gs + GS_BG_MAX_X) = PM3DS_SCREEN_WIDTH;
        *(short *)(gs + GS_BG_MAX_Y) = PM3DS_SCREEN_HEIGHT;
        *(void **)(gs + GS_BG_RASTER) = 0;
        *(void **)(gs + GS_BG_PALETTE) = 0;
    }

    pm3ds_warn("set_background: header not loaded/sane "
               "(raster=%p palette=%p %ux%u) -> textured background disabled",
               bg ? bg->raster : 0, bg ? bg->palette : 0,
               bg ? (unsigned)bg->width : 0u, bg ? (unsigned)bg->height : 0u);
}

/* ------------------------------------------------------------------------ */
/* 3. Post-decode fixup for map backgrounds (M14.6)                          */
/* ------------------------------------------------------------------------ */
/*
 * The bg assets ("kmr_bg", "nok_bg", ...) decode to a BackgroundHeader
 * followed by a CI8 raster and an RGBA16 palette. The header is big-endian
 * and its raster/palette pointers are N64 addresses linked against
 * gBackgroundImage = 0x80200000 (ver/pal/undefined_syms.txt). Swap the
 * header and rebase the pointers onto our gBackgroundImage. The raster and
 * palette bytes stay in N64 order, which is what the renderer expects.
 */
#define PM3DS_BG_N64_BASE 0x80200000u

static unsigned int pm3ds_bs32(unsigned int v) { return __builtin_bswap32(v); }
static unsigned short pm3ds_bs16(unsigned short v) { return __builtin_bswap16(v); }

void pm3ds_fix_bg_header(void *bgv, unsigned int size, const char *name) {
    Pm3dsBackgroundHeader *bg = (Pm3dsBackgroundHeader *)bgv;
    unsigned int raster = pm3ds_bs32((unsigned int)(uintptr_t)bg->raster);
    unsigned int palette = pm3ds_bs32((unsigned int)(uintptr_t)bg->palette);

    if (size > PM3DS_BG_STORE_SIZE) {
        pm3ds_warn("bg %s: %u bytes exceeds the %u-byte store", name, size, (unsigned)PM3DS_BG_STORE_SIZE);
    }
    bg->startX = pm3ds_bs16(bg->startX);
    bg->startY = pm3ds_bs16(bg->startY);
    bg->width = pm3ds_bs16(bg->width);
    bg->height = pm3ds_bs16(bg->height);
    if (raster - PM3DS_BG_N64_BASE < size && palette - PM3DS_BG_N64_BASE < size) {
        bg->raster = gBackgroundImage + (raster - PM3DS_BG_N64_BASE);
        bg->palette = gBackgroundImage + (palette - PM3DS_BG_N64_BASE);
    } else {
        pm3ds_warn("bg %s: unexpected pointers raster=%08X palette=%08X (size %u)", name, raster, palette, size);
        bg->raster = 0;
        bg->palette = 0;
    }
    pm3ds_log(PM3DS_LOG_INFO, "bg %s: %ux%u at (%u,%u), %u bytes", name, (unsigned)bg->width,
              (unsigned)bg->height, (unsigned)bg->startX, (unsigned)bg->startY, size);
}
