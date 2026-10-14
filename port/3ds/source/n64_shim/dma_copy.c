/*
 * dma_copy.c — strong override of the N64 PI DMA helper.
 *
 * In the decomp this calls nuPiReadRom to pull bytes from N64 cart
 * ROM into RDRAM. On 3DS our "ROM" is just the loaded baserom buffer
 * and the segment storage symbols (logos_ROM_START etc., defined in
 * segment_storage.s) already hold the data — populated at boot by
 * pm3ds_pi_set_baserom copying from the baserom buffer at the right
 * offset. So the runtime "DMA" is just memcpy from one host pointer
 * to another.
 *
 * Overrides the weak `void dma_copy(void) {}` stub in m6_link_stubs.c
 * (the linker resolves strong over weak — the void-arg signature is
 * irrelevant at link time).
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pm3ds_log.h"
#include "pm3ds_addr.h"

/* M8.6c: bounds-check the source address. Many segments still have
 * weak 0-byte OFFSET symbols (e.g. charset_*_OFFSET in src/charset/
 * charset.c, where INCLUDE_RAW's .incbin paths point at extracted
 * asset bins that don't exist in our build tree). The decomp
 * computes `base = seg_ROM_START + (s32)OFFSET_symbol`, treating the
 * OFFSET symbol's ADDRESS as a byte count. With the offset symbol
 * at a huge BSS address, base lands far outside any mapped memory
 * and memcpy floods Azahar with millions of unmapped-read errors.
 *
 * Skip the copy when src..src+len doesn't pass a sanity check. The
 * caller's `dest` still has whatever zeros it was initialized with
 * — render paths that consume that data render nothing instead of
 * crashing. Future work: extract assets so the OFFSET symbols are
 * real binary data. */
void gfx_tex_invalidate_range(const void *addr, size_t len);

unsigned int dma_copy(const unsigned char *romStart,
                      const unsigned char *romEnd,
                      void                *vramDest) {
    if (!romStart || !romEnd || !vramDest) return 0;
    if (romEnd <= romStart) return 0;
    size_t length = (size_t)(romEnd - romStart);

    /* Sanity: source range must be plausibly small. Real segments
     * top out around 4 MB. */
    if (length > (4u * 1024u * 1024u)) {
        static int warned = 0;
        if (!warned) { warned = 1;
            pm3ds_log(PM3DS_LOG_WARN, "dma_copy: implausible size %zu (skip)", length);
        }
        return 0;
    }

    /* M11: one shared host-vs-ROM rule (pm3ds_addr.h). The old fixed
     * 0x00100000..0x00500000 image window broke once the native world
     * pushed __end__ to ~0x00DD0000. */
    uintptr_t src = (uintptr_t)romStart;
    {   /* N64 code overlays (e.g. player actions -> world_action_CLASS_VRAM)
         * are linked natively here, and their VRAM symbols are stubs in
         * .text. Real hardware maps .text/.rodata read-only (Azahar does
         * not), so such a copy is a data abort: skip it. */
        extern char __start__[], __exidx_end[];
        uintptr_t d = (uintptr_t)vramDest;
        if (d >= (uintptr_t)__start__ && d < (uintptr_t)__exidx_end) {
            static uintptr_t seen[16];
            static int nseen;
            int i;
            for (i = 0; i < nseen && seen[i] != d; i++) {}
            if (i == nseen && nseen < 16) {
                seen[nseen++] = d;
                pm3ds_log(PM3DS_LOG_INFO, "dma_copy: skip overlay load into read-only %p (src %p len %u)",
                          vramDest, romStart, (unsigned)length);
            }
            return 0;
        }
    }
    uint32_t rom = pm3ds_addr_rom_offset(src, length);
#ifdef PM3DS_DMA_TRACE
    {
        /* One line per distinct caller: who still reads which kind. */
        static uintptr_t seen[96];
        static int nseen;
        uintptr_t ra = (uintptr_t)__builtin_return_address(0);
        int i;
        for (i = 0; i < nseen && seen[i] != ra; i++) {}
        if (i == nseen && nseen < 96) {
            seen[nseen++] = ra;
            pm3ds_log(PM3DS_LOG_INFO, "DMATRACE caller=%p src=%p len=%u -> %s 0x%x",
                      (void *)ra, (void *)romStart, (unsigned)length,
                      rom == (uint32_t)-1 ? "host" : "rom", (unsigned)rom);
        }
    }
#endif
    if (rom != (uint32_t)-1) {
        extern size_t pm3ds_baserom_size(void);
        extern void pm3ds_rom_read(uint32_t off, void *dst, uint32_t len);
        if ((size_t)rom + length <= pm3ds_baserom_size()) {
            pm3ds_rom_read(rom, vramDest, (uint32_t)length);
            gfx_tex_invalidate_range(vramDest, length);
            return (unsigned int)length;
        }
        static int warned = 0;
        if (!warned) { warned = 1;
            pm3ds_log(PM3DS_LOG_WARN,
                      "dma_copy: ROM offset 0x%x len=%u past baserom (skip; further suppressed)",
                      (unsigned)rom, (unsigned)length);
        }
        return 0;
    }

    memcpy(vramDest, romStart, length);
    gfx_tex_invalidate_range(vramDest, length);
    return (unsigned int)length;
}
