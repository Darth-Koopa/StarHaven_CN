/* port/3ds/include/include_asm.h
 *
 * M4 phase 1.4 dispatch macro for the decomp's INCLUDE_ASM family.
 *
 * The decomp's include/include_asm.h would embed `__asm__(".include
 * \"ver/.../<name>.s\"")` directives for MIPS asm files — meaningless
 * on ARM. We force-include this file via CFLAGS `-include
 * include_asm.h` so the decomp's macro definitions never get a
 * chance to run.
 *
 * Each call site resolves one of three ways:
 *   1. C version exists under #ifdef NON_MATCHING in the same file
 *      (the default since phase 1.1 sets NON_MATCHING=1 globally) →
 *      INCLUDE_ASM expands to nothing; the C version wins.
 *   2. Shim provides the symbol → INCLUDE_ASM expands to nothing;
 *      linker picks shim version.
 *   3. Neither — INCLUDE_ASM expands to a panic stub via an entry
 *      in include_asm_overrides.h.
 *
 * INCLUDE_RAW / INCLUDE_RODATA likewise default to empty expansion.
 * INCLUDE_IMG / INCLUDE_PAL come from the decomp's
 * include/include_asset.h (1254 + 765 use sites — handled by the
 * decomp's incbin-style inclusion, not by us).
 */

#ifndef PORT_3DS_INCLUDE_ASM_H
#define PORT_3DS_INCLUDE_ASM_H

/* Decomp's include/types.h gates the nullptr fallback `(void*)0` behind
 * `!defined(__3DS__)` — assuming a 3DS toolchain provides the C2x
 * `nullptr` keyword. devkitARM doesn't (it's a C11/gnu11 compiler), so
 * 3DS C TUs get no nullptr at all unless we provide one. Force-define
 * before any other include happens. */
#if !defined(__cplusplus) && !defined(nullptr)
#define nullptr ((void*)0)
#endif

#include "include_asm_overrides.h"

// M4 wave 12: 8 effect names exist in our ver/pal/effects.yaml but lack
// `effect_<name>_ROM_*`/`_VRAM` symbols in ld_addrs.h (PAL version dropped
// or renamed these). Provide weak storage stubs so include/effects/
// effect_table.c expands without unresolved externs. Real DMA will never
// run for these on 3DS; they're placeholder filler.
// Declared as the same type used by ld_addrs.h's `extern Addr` lines.
#ifndef _PM3DS_EFFECT_ADDR_STUBS_DECLARED
#define _PM3DS_EFFECT_ADDR_STUBS_DECLARED
#define _PM3DS_EFFECT_STUB(name)                       \
    extern unsigned char effect_##name##_ROM_START[];  \
    extern unsigned char effect_##name##_ROM_END[];    \
    extern unsigned char effect_##name##_VRAM[];       \
    extern unsigned char effect_gfx_##name##_ROM_START[]; \
    extern unsigned char effect_gfx_##name##_ROM_END[]
_PM3DS_EFFECT_STUB(big_dust_puff);
_PM3DS_EFFECT_STUB(effect_02);
_PM3DS_EFFECT_STUB(effect_03);
_PM3DS_EFFECT_STUB(effect_04);
_PM3DS_EFFECT_STUB(effect_05);
_PM3DS_EFFECT_STUB(effect_2A);
_PM3DS_EFFECT_STUB(effect_6A);
_PM3DS_EFFECT_STUB(effect_76);

// Additional effects whose `effect_gfx_<name>` symbols are absent from
// ld_addrs.h (the yaml lists them as separate effects but in this version
// they share gfx blobs with sibling effects — would need `gfx:` overrides
// to dedupe properly). Only the effect_gfx_ side is stubbed; the
// effect_<name>_ROM_* side already lives in ld_addrs.h.
#define _PM3DS_EFFECT_GFX_STUB(name)                  \
    extern unsigned char effect_gfx_##name##_ROM_START[]; \
    extern unsigned char effect_gfx_##name##_ROM_END[]
_PM3DS_EFFECT_GFX_STUB(bombette_breaking);
_PM3DS_EFFECT_GFX_STUB(cloud_puff);
_PM3DS_EFFECT_GFX_STUB(cloud_trail);
_PM3DS_EFFECT_GFX_STUB(flower_splash);
_PM3DS_EFFECT_GFX_STUB(flower_trail);
_PM3DS_EFFECT_GFX_STUB(pink_sparkles);
_PM3DS_EFFECT_GFX_STUB(red_impact);
_PM3DS_EFFECT_GFX_STUB(shimmer_burst);
_PM3DS_EFFECT_GFX_STUB(shimmer_wave);
_PM3DS_EFFECT_GFX_STUB(smoke_burst);
_PM3DS_EFFECT_GFX_STUB(smoke_impact);
_PM3DS_EFFECT_GFX_STUB(smoke_ring);
_PM3DS_EFFECT_GFX_STUB(something_rotating);
_PM3DS_EFFECT_GFX_STUB(stars_burst);
_PM3DS_EFFECT_GFX_STUB(stars_shimmer);
_PM3DS_EFFECT_GFX_STUB(walking_dust);
_PM3DS_EFFECT_GFX_STUB(windy_leaves);

// M4 wave 12: N64 MMU primitives referenced by src/effects.c's
// `osMapTLB(i, OS_PM_4K, ..., -1, -1);` calls. There's no MMU on 3DS
// (we run code from FCRAM directly), so these are link-stubbed in
// port/3ds/source/n64_shim/heap_impl.c or m4_stubs.c as no-ops.
int osMapTLB(int index, int pagemask, void* vaddr, int paddr0, int paddr1, int aspid);
void osUnmapTLB(int index);
void osUnmapTLBAll(void);
#define OS_PM_4K 0

// M4 wave 12: N64 flash APIs used by src/fio.c. On 3DS we save to the SD
// card via libctru — the n64_shim layer redirects (or no-ops) these.
// Signatures must match upstream include/PR/os_flash.h or callers fail with
// pointer/integer conversion errors. Opaque forward decls for OSIoMesg and
// OSMesgQueue work since the stubs don't dereference.
struct OSIoMesg;
struct OSMesgQueue;
int osFlashInit(void);
int osFlashReadArray(struct OSIoMesg* mb, int priority, unsigned int page_num,
                     void* dramAddr, unsigned int n_pages, struct OSMesgQueue* mq);
int osFlashWriteArray(int pageNum);
int osFlashWriteBuffer(struct OSIoMesg* mb, int priority, void* dramAddr, struct OSMesgQueue* mq);
int osFlashSectorErase(int offset);
#endif

#ifndef INCLUDE_ASM
#define INCLUDE_ASM(TYPE, FOLDER, NAME, ARGS...)
#endif
#ifndef INCLUDE_ASM_SHIFT
#define INCLUDE_ASM_SHIFT(TYPE, FOLDER, NAME, ARGS...)
#endif
#ifndef INCLUDE_RODATA
#define INCLUDE_RODATA(SEGMENT, NAME)
#endif
/* Note: INCLUDE_RAW is from include_asset.h, not INCLUDE_ASM family;
 * its override lives further down in the asset-macro section. */

/* Prevent the decomp's include/include_asm.h from defining its own
 * MIPS-asm-based versions if/when it gets included downstream. */
#define __INCLUDE_ASM_H__

/* Pre-empt the decomp's include/include_asset.h with port-specific
 * versions of INCLUDE_IMG / INCLUDE_PAL / INCLUDE_RAW. The decomp's
 * versions use .incbin to embed PNG/PAL/RAW binary files at link
 * time; on 3DS the real asset data lives in romfs, not in the ELF.
 *
 * Our versions declare-and-provide-empty-storage: a weak 1-byte
 * array per symbol. Code that references SYMBOLNAME[0] gets zero;
 * code that iterates past offset 0 reads garbage. Real asset
 * data is fetched via pm3ds_pi_read at runtime when the offset is
 * resolved to a baserom location.
 *
 * The weak attribute lets a future romfs-aware override (e.g., in
 * src/ root via msg_data.c) replace these with real pointer
 * tables without link conflicts. */
#define _H_INCLUDE_ASSET   /* prevent decomp's include_asset.h body */
#ifdef PM3DS_ASSET_ROOT
/* M13: real data. port/3ds/tools/gen_include_assets.py extracts each
 * image/palette/charset from the baserom (byte-identical to upstream's
 * build products) into PM3DS_ASSET_ROOT; unresolved ones get a zeroed
 * placeholder. Data stays raw big-endian N64 bytes, which is what the
 * texture decoders expect. */
/* One .data section per file, not per asset: N64 code reads past the image
 * it names (the damage starburst's 32x64 load spans two 32x32 images), so
 * assets must keep their ROM order next to the file's other data and must
 * not be dropped by --gc-sections when only reached that way. */
#define PM3DS_INCBIN_(SYMBOLNAME, PATH) \
    __asm__(".globl " #SYMBOLNAME "\n" \
            ".pushsection .data,\"aw\"\n" \
            ".balign 8\n" \
            ".type " #SYMBOLNAME ", %object\n" \
            #SYMBOLNAME ":\n" \
            ".incbin \"" PATH "\"\n" \
            ".popsection\n")
#define INCLUDE_IMG(FILENAME, SYMBOLNAME) \
    extern unsigned char SYMBOLNAME[]; PM3DS_INCBIN_(SYMBOLNAME, PM3DS_ASSET_ROOT FILENAME ".bin")
#define INCLUDE_PAL(FILENAME, SYMBOLNAME) \
    extern unsigned short SYMBOLNAME[]; PM3DS_INCBIN_(SYMBOLNAME, PM3DS_ASSET_ROOT FILENAME ".bin")
#define INCLUDE_RAW(FILENAME, SYMBOLNAME) \
    extern unsigned char SYMBOLNAME[]; PM3DS_INCBIN_(SYMBOLNAME, PM3DS_ASSET_ROOT "raw/" FILENAME)
#else
#define INCLUDE_IMG(FILENAME, SYMBOLNAME) \
    __attribute__((weak)) unsigned char SYMBOLNAME[1] = {0};
#define INCLUDE_PAL(FILENAME, SYMBOLNAME) \
    __attribute__((weak)) unsigned short SYMBOLNAME[1] = {0};
#define INCLUDE_RAW(FILENAME, SYMBOLNAME) \
    __attribute__((weak)) unsigned char SYMBOLNAME[1] = {0};
#endif

#endif
