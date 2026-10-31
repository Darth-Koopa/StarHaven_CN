#ifndef PM3DS_PORT_LD_ADDRS_H
#define PM3DS_PORT_LD_ADDRS_H

/* port/3ds/include/ld_addrs.h
 *
 * Redirects to the real generated linker-symbol header belonging to the
 * selected ROM version (ver/<version>/build/include/ld_addrs.h).
 *
 * M1 carried a 12-line stub here that also #defined
 * DMA_COPY_SEGMENT(segment) to call pm3ds_dma_copy_segment(#segment).
 * That override is REMOVED in M4: the decomp's include/macros.h:547
 * provides the real DMA_COPY_SEGMENT macro which expands to
 *   dma_copy(segment##_ROM_START, segment##_ROM_END, segment##_VRAM)
 * The needed `Addr` storage decls come from this generated header;
 * the `dma_copy` runtime helper will land in the n64_shim when the
 * first src/ file that calls DMA_COPY_SEGMENT enters SOURCES.
 */
#include "types.h"
#include "pm3ds_rom_layout.h"
#if VERSION_PAL
#include "../../../ver/pal/build/include/ld_addrs.h"
#elif VERSION_IQUE
#include "../../../ver/ique/build/include/ld_addrs.h"
#elif VERSION_JP
#include "../../../ver/jp/build/include/ld_addrs.h"
#elif VERSION_IQUE
#include "../../../ver/ique/build/include/ld_addrs.h"
#else
#include "../../../ver/us/build/include/ld_addrs.h"
#endif

#ifdef PM3DS_ASSET_ROOT
/* M13: the charset is linked in through INCLUDE_RAW/IMG/PAL, so every
 * charset_*_OFFSET symbol (and MsgLetterRasterOffsets etc.) is already a
 * host pointer. On N64 the segment sits at VRAM 0 and the code computes
 * charset_ROM_START + (s32)offset; a null base keeps that idiom correct at
 * every call site (msg.c, kmr_20/documents.c) without touching them. */
#define charset_ROM_START ((unsigned char*)0)
#endif

/* Raw cart data read at run time with dma_copy: cart-tagged offsets
 * (pm3ds_rom_layout.h) instead of the null-segment link stubs. */
#define title_bg_1_ROM_START ((unsigned char*)PM3DS_CART(PM3DS_ROM_TITLE_BG_1))  /* story pages; tape at +0x2A440 */
/* HUD icons (items, badges, key items, the battle target hand, ...): hud_element.c
 * reads rasters/palettes at icon_ROM_START + ICON_*_raster/palette. The weak link
 * stub left this pointing at port RAM, so every icon-blob graphic was garbage. */
#define icon_ROM_START ((unsigned char*)PM3DS_CART(PM3DS_ROM_ICON))
#if VERSION_PAL
/* language-select flag animation: 40 frames of 0x800 bytes (ver/pal/splat.yaml) */
#define titlemenu_flags_ROM_START ((unsigned char*)(0x10000000 + 0x25C5A0))
#define titlemenu_flags_ROM_END   ((unsigned char*)(0x10000000 + 0x2705A0))
#endif

/* Lava Piranha vine animations (ch5): the AnimScripts are compiled in
 * (src/world/model_anim/kzn) and hold no pointers, so the "DMA" copies the
 * native data; sizes are the PAL segment sizes (ver/pal/splat.yaml). */
extern int LavaPiranha_ModelScript_00[];
#define world_model_anim_kzn_00_ROM_START ((unsigned char*)LavaPiranha_ModelScript_00)
#define world_model_anim_kzn_00_ROM_END ((unsigned char*)LavaPiranha_ModelScript_00 + 0xA50)
extern int LavaPiranha_ModelScript_01[];
#define world_model_anim_kzn_01_ROM_START ((unsigned char*)LavaPiranha_ModelScript_01)
#define world_model_anim_kzn_01_ROM_END ((unsigned char*)LavaPiranha_ModelScript_01 + 0xD30)
extern int LavaPiranha_ModelScript_02[];
#define world_model_anim_kzn_02_ROM_START ((unsigned char*)LavaPiranha_ModelScript_02)
#define world_model_anim_kzn_02_ROM_END ((unsigned char*)LavaPiranha_ModelScript_02 + 0x24C0)
extern int LavaPiranha_ModelScript_03[];
#define world_model_anim_kzn_03_ROM_START ((unsigned char*)LavaPiranha_ModelScript_03)
#define world_model_anim_kzn_03_ROM_END ((unsigned char*)LavaPiranha_ModelScript_03 + 0xA40)
extern int LavaPiranha_ModelScript_04[];
#define world_model_anim_kzn_04_ROM_START ((unsigned char*)LavaPiranha_ModelScript_04)
#define world_model_anim_kzn_04_ROM_END ((unsigned char*)LavaPiranha_ModelScript_04 + 0xF50)
extern int LavaPiranha_ModelScript_05[];
#define world_model_anim_kzn_05_ROM_START ((unsigned char*)LavaPiranha_ModelScript_05)
#define world_model_anim_kzn_05_ROM_END ((unsigned char*)LavaPiranha_ModelScript_05 + 0xD00)
extern int LavaPiranha_ModelScript_06[];
#define world_model_anim_kzn_06_ROM_START ((unsigned char*)LavaPiranha_ModelScript_06)
#define world_model_anim_kzn_06_ROM_END ((unsigned char*)LavaPiranha_ModelScript_06 + 0xAB0)
extern int LavaPiranha_ModelScript_07[];
#define world_model_anim_kzn_07_ROM_START ((unsigned char*)LavaPiranha_ModelScript_07)
#define world_model_anim_kzn_07_ROM_END ((unsigned char*)LavaPiranha_ModelScript_07 + 0xAA0)
extern int LavaPiranha_ModelScript_08[];
#define world_model_anim_kzn_08_ROM_START ((unsigned char*)LavaPiranha_ModelScript_08)
#define world_model_anim_kzn_08_ROM_END ((unsigned char*)LavaPiranha_ModelScript_08 + 0xDD0)
extern int LavaPiranha_ModelScript_09[];
#define world_model_anim_kzn_09_ROM_START ((unsigned char*)LavaPiranha_ModelScript_09)
#define world_model_anim_kzn_09_ROM_END ((unsigned char*)LavaPiranha_ModelScript_09 + 0xD30)
extern int LavaPiranha_ModelScript_0A[];
#define world_model_anim_kzn_0A_ROM_START ((unsigned char*)LavaPiranha_ModelScript_0A)
#define world_model_anim_kzn_0A_ROM_END ((unsigned char*)LavaPiranha_ModelScript_0A + 0x3050)
extern int LavaPiranha_ModelScript_0B[];
#define world_model_anim_kzn_0B_ROM_START ((unsigned char*)LavaPiranha_ModelScript_0B)
#define world_model_anim_kzn_0B_ROM_END ((unsigned char*)LavaPiranha_ModelScript_0B + 0x28F0)
extern int LavaPiranha_ModelScript_0C[];
#define world_model_anim_kzn_0C_ROM_START ((unsigned char*)LavaPiranha_ModelScript_0C)
#define world_model_anim_kzn_0C_ROM_END ((unsigned char*)LavaPiranha_ModelScript_0C + 0x2340)
extern int LavaPiranha_ModelScript_0D[];
#define world_model_anim_kzn_0D_ROM_START ((unsigned char*)LavaPiranha_ModelScript_0D)
#define world_model_anim_kzn_0D_ROM_END ((unsigned char*)LavaPiranha_ModelScript_0D + 0xAD0)
extern int LavaPiranha_ModelScript_0E[];
#define world_model_anim_kzn_0E_ROM_START ((unsigned char*)LavaPiranha_ModelScript_0E)
#define world_model_anim_kzn_0E_ROM_END ((unsigned char*)LavaPiranha_ModelScript_0E + 0xA80)
extern int LavaPiranha_ModelScript_0F[];
#define world_model_anim_kzn_0F_ROM_START ((unsigned char*)LavaPiranha_ModelScript_0F)
#define world_model_anim_kzn_0F_ROM_END ((unsigned char*)LavaPiranha_ModelScript_0F + 0x2A00)
extern int LavaPiranha_ModelScript_10[];
#define world_model_anim_kzn_10_ROM_START ((unsigned char*)LavaPiranha_ModelScript_10)
#define world_model_anim_kzn_10_ROM_END ((unsigned char*)LavaPiranha_ModelScript_10 + 0x2DA0)
extern int LavaPiranha_ModelScript_11[];
#define world_model_anim_kzn_11_ROM_START ((unsigned char*)LavaPiranha_ModelScript_11)
#define world_model_anim_kzn_11_ROM_END ((unsigned char*)LavaPiranha_ModelScript_11 + 0x540)
extern int LavaPiranha_ModelScript_12[];
#define world_model_anim_kzn_12_ROM_START ((unsigned char*)LavaPiranha_ModelScript_12)
#define world_model_anim_kzn_12_ROM_END ((unsigned char*)LavaPiranha_ModelScript_12 + 0x14C0)
extern int LavaPiranha_ModelScript_13[];
#define world_model_anim_kzn_13_ROM_START ((unsigned char*)LavaPiranha_ModelScript_13)
#define world_model_anim_kzn_13_ROM_END ((unsigned char*)LavaPiranha_ModelScript_13 + 0xCD0)
extern int LavaPiranha_ModelScript_14[];
#define world_model_anim_kzn_14_ROM_START ((unsigned char*)LavaPiranha_ModelScript_14)
#define world_model_anim_kzn_14_ROM_END ((unsigned char*)LavaPiranha_ModelScript_14 + 0xBA0)
extern int LavaPiranha_ModelScript_15[];
#define world_model_anim_kzn_15_ROM_START ((unsigned char*)LavaPiranha_ModelScript_15)
#define world_model_anim_kzn_15_ROM_END ((unsigned char*)LavaPiranha_ModelScript_15 + 0xC80)
extern int LavaPiranha_ModelScript_16[];
#define world_model_anim_kzn_16_ROM_START ((unsigned char*)LavaPiranha_ModelScript_16)
#define world_model_anim_kzn_16_ROM_END ((unsigned char*)LavaPiranha_ModelScript_16 + 0xBA0)
extern int LavaPiranha_ModelScript_17[];
#define world_model_anim_kzn_17_ROM_START ((unsigned char*)LavaPiranha_ModelScript_17)
#define world_model_anim_kzn_17_ROM_END ((unsigned char*)LavaPiranha_ModelScript_17 + 0x8E0)
extern int LavaPiranha_ModelScript_18[];
#define world_model_anim_kzn_18_ROM_START ((unsigned char*)LavaPiranha_ModelScript_18)
#define world_model_anim_kzn_18_ROM_END ((unsigned char*)LavaPiranha_ModelScript_18 + 0x900)
extern int LavaPiranha_ModelScript_19[];
#define world_model_anim_kzn_19_ROM_START ((unsigned char*)LavaPiranha_ModelScript_19)
#define world_model_anim_kzn_19_ROM_END ((unsigned char*)LavaPiranha_ModelScript_19 + 0x2310)
extern int LavaPiranha_ModelScript_1A[];
#define world_model_anim_kzn_1A_ROM_START ((unsigned char*)LavaPiranha_ModelScript_1A)
#define world_model_anim_kzn_1A_ROM_END ((unsigned char*)LavaPiranha_ModelScript_1A + 0x1A10)
extern int LavaPiranha_ModelScript_1B[];
#define world_model_anim_kzn_1B_ROM_START ((unsigned char*)LavaPiranha_ModelScript_1B)
#define world_model_anim_kzn_1B_ROM_END ((unsigned char*)LavaPiranha_ModelScript_1B + 0x730)
extern int LavaPiranha_ModelScript_1C[];
#define world_model_anim_kzn_1C_ROM_START ((unsigned char*)LavaPiranha_ModelScript_1C)
#define world_model_anim_kzn_1C_ROM_END ((unsigned char*)LavaPiranha_ModelScript_1C + 0x1940)
extern int LavaPiranha_ModelScript_1D[];
#define world_model_anim_kzn_1D_ROM_START ((unsigned char*)LavaPiranha_ModelScript_1D)
#define world_model_anim_kzn_1D_ROM_END ((unsigned char*)LavaPiranha_ModelScript_1D + 0x6F0)
extern int LavaPiranha_ModelScript_1E[];
#define world_model_anim_kzn_1E_ROM_START ((unsigned char*)LavaPiranha_ModelScript_1E)
#define world_model_anim_kzn_1E_ROM_END ((unsigned char*)LavaPiranha_ModelScript_1E + 0x200)
extern int LavaPiranha_ModelScript_1F[];
#define world_model_anim_kzn_1F_ROM_START ((unsigned char*)LavaPiranha_ModelScript_1F)
#define world_model_anim_kzn_1F_ROM_END ((unsigned char*)LavaPiranha_ModelScript_1F + 0x860)
extern int LavaPiranha_ModelScript_20[];
#define world_model_anim_kzn_20_ROM_START ((unsigned char*)LavaPiranha_ModelScript_20)
#define world_model_anim_kzn_20_ROM_END ((unsigned char*)LavaPiranha_ModelScript_20 + 0x2040)
extern int LavaPiranha_ModelScript_21[];
#define world_model_anim_kzn_21_ROM_START ((unsigned char*)LavaPiranha_ModelScript_21)
#define world_model_anim_kzn_21_ROM_END ((unsigned char*)LavaPiranha_ModelScript_21 + 0x27F0)
extern int LavaPiranha_ModelScript_22[];
#define world_model_anim_kzn_22_ROM_START ((unsigned char*)LavaPiranha_ModelScript_22)
#define world_model_anim_kzn_22_ROM_END ((unsigned char*)LavaPiranha_ModelScript_22 + 0x940)
extern int LavaPiranha_ModelScript_23[];
#define world_model_anim_kzn_23_ROM_START ((unsigned char*)LavaPiranha_ModelScript_23)
#define world_model_anim_kzn_23_ROM_END ((unsigned char*)LavaPiranha_ModelScript_23 + 0x1100)
extern int LavaPiranha_ModelScript_24[];
#define world_model_anim_kzn_24_ROM_START ((unsigned char*)LavaPiranha_ModelScript_24)
#define world_model_anim_kzn_24_ROM_END ((unsigned char*)LavaPiranha_ModelScript_24 + 0xAD0)

#endif /* PM3DS_PORT_LD_ADDRS_H */
