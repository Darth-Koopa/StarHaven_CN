/*
 * m10_texheader_endian.c -- byte-swap a TextureHeader read from cart data.
 *
 * load_texture_by_name() (src/model.c:2062) walks a map's texture archive with
 *     dma_copy(romOffset, romOffset + sizeof(gCurrentTextureHeader), &gCurrentTextureHeader);
 *     rasterSize = header->mainW * header->mainH;
 * The header is raw BIG-ENDIAN cart data, so on ARM mainW/mainH come out
 * byte-reversed, rasterSize is garbage, and the archive walk runs off the end.
 * Measured: repeating `unmapped Read8 @ 0x88332280 at PC 0x001653F0`, ending in
 * Azahar's `video_core/gpu.cpp:312: Unreachable code! Read from unknown GPU
 * address`.
 *
 * Fifth instance of this same class in the port (after the mapfs asset table,
 * Yay0 headers, SpriteDataHeader, spr_asset_entry and the sprite blob offsets).
 *
 * TWO KINDS OF FIX ARE NEEDED HERE, and the second is easy to miss:
 *
 * 1. The u16 dimensions at 0x20..0x27 -- ordinary byte swaps.
 *
 * 2. The BITFIELD bytes at 0x2A..0x2E. A byte is a byte, so no "swap" applies,
 *    but C packs bitfields from the opposite end depending on the compiler's
 *    endianness. The decomp's
 *        u8 auxFmt : 4;  u8 mainFmt : 4;
 *    puts auxFmt in the HIGH nibble when compiled big-endian (as the N64 data
 *    was authored) and in the LOW nibble here. So each 4:4 byte needs its
 *    nibbles exchanged, and the 6:2 byte at 0x2A needs its two fields rotated.
 *    Skipping this yields plausible-looking but wrong formats/depths -- the
 *    nastiest sort of bug, because nothing crashes.
 *
 * TextureHeader layout (include/common_structs.h:1038-1057, size 0x30):
 *   0x00 s8 name[32]        (bytes, untouched)
 *   0x20 u16 auxW  0x22 u16 mainW  0x24 u16 auxH  0x26 u16 mainH
 *   0x28 u8 isVariant  0x29 u8 extraTiles          (plain bytes)
 *   0x2A auxCombineType:6, auxCombineSubType:2
 *   0x2B auxFmt:4,   mainFmt:4
 *   0x2C auxBitDepth:4, mainBitDepth:4
 *   0x2D auxWrapW:4, mainWrapW:4
 *   0x2E auxWrapH:4, mainWrapH:4
 *   0x2F u8 filtering       (plain byte)
 */

#include <stdint.h>

static void swap_u16_at(uint8_t *p) {
    uint8_t t = p[0];
    p[0] = p[1];
    p[1] = t;
}

static uint8_t swap_nibbles(uint8_t v) {
    return (uint8_t)((v >> 4) | (v << 4));
}

void pm3ds_texheader_endian_fix(void *hdr) {
    uint8_t *b = (uint8_t *)hdr;
    if (!b) return;

    swap_u16_at(b + 0x20);   /* auxW  */
    swap_u16_at(b + 0x22);   /* mainW */
    swap_u16_at(b + 0x24);   /* auxH  */
    swap_u16_at(b + 0x26);   /* mainH */

    /* 6:2 -- big-endian puts the 6-bit field in the high bits. */
    {
        uint8_t v = b[0x2A];
        b[0x2A] = (uint8_t)((v >> 2) | ((v & 0x03u) << 6));
    }

    /* 4:4 pairs. */
    b[0x2B] = swap_nibbles(b[0x2B]);
    b[0x2C] = swap_nibbles(b[0x2C]);
    b[0x2D] = swap_nibbles(b[0x2D]);
    b[0x2E] = swap_nibbles(b[0x2E]);
}
