/*
 * debug_dl.c — canned title-screen-shaped display list, only built
 * when PM3DS_DEBUG_DL is set.
 *
 * Variants (selected by the PM3DS_DEBUG_DL numeric value):
 *   1: M2 baseline — SETFILLCOLOR magenta → FILLRECT → ENDDL.
 *   2: M3 — SETTIMG (logo offset) → SETTILE → LOADBLOCK → SETTILESIZE
 *           → TEXRECT (full screen) → ENDDL. Requires the romfs build.
 */

#ifdef PM3DS_DEBUG_DL

#include <stdint.h>

#if PM3DS_DEBUG_DL >= 2
/* Optional file written by port/3ds/tools/pack_romfs.py with the
 * title-screen logo offset/width/height/format. If absent (e.g., the
 * developer hasn't run pack_romfs.py), fall back to the variant-1
 * magenta path so the build still produces a working .3dsx. */
#if __has_include("debug_dl_assets.h")
#include "debug_dl_assets.h"
#endif
#endif

typedef struct { uint32_t w0, w1; } DebugGfx;

#if PM3DS_DEBUG_DL == 1 || !defined(PM3DS_LOGO_OFFSET) || PM3DS_LOGO_WIDTH == 0
/* Variant 1 — solid magenta FILLRECT. */
const DebugGfx pm3ds_debug_dl[] = {
    /* G_SETFILLCOLOR magenta 5551 */
    { ((uint32_t)0xF7u << 24), 0xF83Fu },
    /* G_FILLRECT 0,0..318,238 (inclusive maxes) */
    { ((uint32_t)0xF6u << 24) | ((319u - 1u) << 14) | ((239u - 1u) << 2), 0u },
    /* G_ENDDL */
    { ((uint32_t)0xDFu << 24), 0u },
};

#else
/* Variant 2 — TEXRECT a real Paper Mario texture from the embedded
 * baserom at PM3DS_LOGO_OFFSET. The exact bit-packing of each Gfx
 * matches libultra macros gsDPSetTextureImage / gsDPSetTile /
 * gsDPLoadBlock / gsDPSetTileSize / gsSPTextureRectangle. */
const DebugGfx pm3ds_debug_dl[] = {
    /* G_SETTIMG: fmt=RGBA(0)/IA(3)/I(4)/CI(2), siz=4b(0)/8b(1)/16b(2)/32b(3), width-1 */
    { ((uint32_t)0xFDu << 24) | ((uint32_t)PM3DS_LOGO_FMT << 21)
                              | ((uint32_t)PM3DS_LOGO_SIZ << 19)
                              | ((PM3DS_LOGO_WIDTH - 1u) & 0xFFFu),
      (uint32_t)PM3DS_LOGO_OFFSET },
    /* G_SETTILE: fmt/siz again, line=0 (TMEM not used by our shim), tmem=0, tile=0 */
    { ((uint32_t)0xF5u << 24) | ((uint32_t)PM3DS_LOGO_FMT << 21)
                              | ((uint32_t)PM3DS_LOGO_SIZ << 19),
      0u },
    /* G_LOADBLOCK: tile 0, count = w*h - 1 */
    { ((uint32_t)0xF3u << 24),
      ((uint32_t)((PM3DS_LOGO_WIDTH * PM3DS_LOGO_HEIGHT) - 1u) << 12) },
    /* G_SETTILESIZE: tile 0, uls=0, ult=0, lrs=(w-1)<<2, lrt=(h-1)<<2 */
    { ((uint32_t)0xF2u << 24),
      ((uint32_t)((PM3DS_LOGO_WIDTH - 1u) << 2) << 12) |
      ((uint32_t)((PM3DS_LOGO_HEIGHT - 1u) << 2)) },
    /* G_TEXRECT: top screen (0,0)-(320,240) */
    { ((uint32_t)0xE4u << 24) | (320u << 12) | 240u,
      0u },
    /* G_RDPHALF_1: s=0, t=0 */
    { ((uint32_t)0xE1u << 24), 0u },
    /* G_RDPHALF_2: dsdx=1024 (1.0 in 5.10 fixed), dtdy=1024 */
    { ((uint32_t)0xF1u << 24), (1024u << 16) | 1024u },
    /* G_ENDDL */
    { ((uint32_t)0xDFu << 24), 0u },
};
#endif

const unsigned pm3ds_debug_dl_count =
    sizeof(pm3ds_debug_dl) / sizeof(pm3ds_debug_dl[0]);

#endif /* PM3DS_DEBUG_DL */
