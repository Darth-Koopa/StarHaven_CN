/*
 * m10_pi_readrom.c -- strong override of nuPiReadRom.
 *
 * THE BUG THIS FIXES
 * ------------------
 * `nuPiReadRom(u32 rom_addr, void *buf, u32 size)` (include/nu/nusys.h:899) is
 * how the decomp pulls raw bytes out of N64 cart ROM. On this port it was a
 * WEAK NO-OP: `__attribute__((weak)) void nuPiReadRom(void) {}` in
 * m6_link_stubs.c -- every call silently wrote nothing and left the caller's
 * buffer untouched.
 *
 * That is the actual root of the sprite failure. spr_init_player_raster_cache()
 * does
 *     nuPiReadRom(SPRITE_ROM_START, &SpriteDataHeader, sizeof(SpriteDataHeader));
 *     SpriteDataHeader[0..2] += SPRITE_ROM_START;
 * With the no-op, SpriteDataHeader stayed all zeros and the += produced
 * {0x01DF0010, 0x01DF0010, 0x01DF0010} -- exactly what the run logged. Then
 * spr_load_sprite computed `base = SpriteDataHeader[1]`, read its "asset table"
 * from there, got garbage sizes, malloc'd NULL, and walked wild memory:
 * 859,731 unmapped accesses at src/101b90_len_8f0.c:114.
 *
 * The port already has a working cart reader -- pm3ds_pi_read(), the same one
 * dma_copy() and the texture cache use to pull from the loaded baserom buffer.
 * nuPiReadRom just needed wiring to it.
 *
 * NOTE the two prerequisites this depends on, both fixed alongside it:
 *  - SPRITE_ROM_START must be a real cart offset. Under -DSHIFT it expanded to
 *    the ADDRESS of the weak `sprites_ROM_START` linker stub (the same defect
 *    M8.8 fixed for mapfs_ROM_START); PM3DS_PORT_ROM_ADDRS selects the real PAL
 *    offset instead.
 *  - spr_init_player_raster_cache() must actually be called; the old partial
 *    spr_init_sprites wrap skipped it.
 *
 * Overrides the weak stub by being a strong definition (the stub's void-arg
 * signature is irrelevant at link time).
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "pm3ds_log.h"

/* From os_pi.c: reads `size` bytes at cart offset `addr` into `dst`.
 * Returns 0 on success. Handles both baserom offsets and host pointers. */
extern int pm3ds_pi_read(uint32_t addr, void *dst, uint32_t size);

void nuPiReadRom(uint32_t rom_addr, void *buf_ptr, uint32_t size) {
    if (!buf_ptr || size == 0u) {
        pm3ds_warn("nuPiReadRom: bad args (rom=0x%08X buf=%p size=%u)",
                   (unsigned)rom_addr, buf_ptr, (unsigned)size);
        return;
    }

    /* Sanity cap: real reads here are table headers and sprite blobs. A wild
     * size means the caller computed it from data we failed to load, and
     * memcpy'ing it would flood the log instead of reporting the problem. */
    if (size > 0x400000u) {
        pm3ds_warn("nuPiReadRom: implausible size 0x%X at rom=0x%08X — refused",
                   (unsigned)size, (unsigned)rom_addr);
        memset(buf_ptr, 0, 4);
        return;
    }

    if (pm3ds_pi_read(rom_addr, buf_ptr, size) != 0) {
        /* Zero the destination rather than leaving it undefined: callers treat
         * the buffer as valid on return, and zeros fail predictably (empty
         * table) instead of walking whatever was on the heap. */
        memset(buf_ptr, 0, size);
        pm3ds_warn("nuPiReadRom: read failed rom=0x%08X size=%u — zeroed",
                   (unsigned)rom_addr, (unsigned)size);
        return;
    }

    /* One-shot confirmation that real bytes are moving. */
    {
        static int logged = 0;
        if (!logged) {
            logged = 1;
            const unsigned char *b = (const unsigned char *)buf_ptr;
            pm3ds_log(PM3DS_LOG_INFO,
                      "nuPiReadRom: first read OK rom=0x%08X size=%u -> "
                      "%02X %02X %02X %02X %02X %02X %02X %02X",
                      (unsigned)rom_addr, (unsigned)size,
                      b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
        }
    }
}
