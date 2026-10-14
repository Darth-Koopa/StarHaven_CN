/*
 * yay0.c — Nintendo Yay0 decompressor (M8.9).
 *
 * Strong override of the weak `void decode_yay0(void){}` stub in
 * m6_link_stubs.c. The decomp's real implementation is MIPS assembly
 * (src/os/decode_yay0.s), which can't assemble for ARM, so the port
 * needs a C reimplementation.
 *
 * Yay0 is Nintendo's LZ-style format. All multi-byte header/link fields
 * are BIG-ENDIAN (N64 ROM order), so we read them high-byte-first — the
 * same reason load_asset_by_name needs byte-swapping. Layout:
 *   0x00  'Y''a''y''0'                    magic
 *   0x04  u32 decompressedSize            (BE)
 *   0x08  u32 linkTableOffset             (BE) — offset to 16-bit links
 *   0x0C  u32 chunkDataOffset             (BE) — offset to literal bytes
 *   0x10  mask bitfield (32-bit words, MSB first)
 *
 * Decode loop, per mask bit (MSB→LSB of each 32-bit mask word):
 *   bit 1 → copy one literal byte from the chunk-data stream
 *   bit 0 → read a 16-bit link (BE): top nibble = length-2 (or, if 0,
 *           an extra count byte from the chunk stream gives length+0x12);
 *           low 12 bits = back-distance-1. Copy length bytes from the
 *           already-written output at (cur - distance - 1).
 */

#include <stdint.h>

static inline uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

void decode_yay0(void *srcv, void *dstv) {
    const uint8_t *src = (const uint8_t *)srcv;
    uint8_t       *dst = (uint8_t *)dstv;

    uint32_t decompressedSize = be32(src + 0x04);
    uint32_t linkOffset       = be32(src + 0x08);
    uint32_t chunkOffset      = be32(src + 0x0C);

    uint32_t maskPos  = 0x10;       /* mask bitfield starts right after header */
    uint32_t linkPos  = linkOffset;
    uint32_t chunkPos = chunkOffset;

    uint32_t outPos = 0;
    uint32_t maskBits = 0;
    int      maskLeft = 0;

    while (outPos < decompressedSize) {
        if (maskLeft == 0) {
            maskBits = be32(src + maskPos);
            maskPos += 4;
            maskLeft = 32;
        }

        if (maskBits & 0x80000000u) {
            /* Literal byte. */
            dst[outPos++] = src[chunkPos++];
        } else {
            /* Back-reference. 16-bit link, big-endian. */
            uint32_t link = ((uint32_t)src[linkPos] << 8) | src[linkPos + 1];
            linkPos += 2;

            uint32_t dist   = (link & 0x0FFFu) + 1;
            uint32_t length = (link >> 12) & 0xF;
            if (length == 0) {
                length = (uint32_t)src[chunkPos++] + 0x12;
            } else {
                length += 2;
            }

            uint32_t from = outPos - dist;
            for (uint32_t i = 0; i < length && outPos < decompressedSize; i++) {
                dst[outPos++] = dst[from++];
            }
        }

        maskBits <<= 1;
        maskLeft--;
    }
}
