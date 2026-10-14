#include <stdint.h>
#include <stddef.h>
#include "runner.h"

#define PM3DS_HOST_TEST 1
#include "../source/n64_shim/gfx_texture.c"

PM3DS_TEST(decode_i4_pair) {
    uint8_t src[1] = { 0xAB };
    uint8_t dst[2 * 4];
    gfx_tex_decode_i4(src, dst, 2);
    /* high nibble first per N64 convention */
    PM3DS_EXPECT(dst[0] == 0xAA);
    PM3DS_EXPECT(dst[1] == 0xAA);
    PM3DS_EXPECT(dst[2] == 0xAA);
    PM3DS_EXPECT(dst[3] == 0xAA);
    PM3DS_EXPECT(dst[4] == 0xBB);
    PM3DS_EXPECT(dst[7] == 0xBB);
}

PM3DS_TEST(decode_i8) {
    uint8_t src[2] = { 0x80, 0xFF };
    uint8_t dst[2 * 4];
    gfx_tex_decode_i8(src, dst, 2);
    PM3DS_EXPECT(dst[0] == 0x80);
    PM3DS_EXPECT(dst[1] == 0x80);
    PM3DS_EXPECT(dst[3] == 0x80);
    PM3DS_EXPECT(dst[4] == 0xFF);
    PM3DS_EXPECT(dst[7] == 0xFF);
}

PM3DS_TEST(decode_ia8) {
    uint8_t src[1] = { 0xA5 };
    uint8_t dst[1 * 4];
    gfx_tex_decode_ia8(src, dst, 1);
    PM3DS_EXPECT(dst[0] == 0xAA);
    PM3DS_EXPECT(dst[3] == 0x55);
}

PM3DS_TEST(decode_ia16) {
    uint8_t src[2] = { 0x80, 0x40 };
    uint8_t dst[1 * 4];
    gfx_tex_decode_ia16(src, dst, 1);
    PM3DS_EXPECT(dst[0] == 0x80);
    PM3DS_EXPECT(dst[3] == 0x40);
}

PM3DS_TEST(decode_ci8) {
    /* TLUT of 4 entries — index 0 = red, 1 = green, 2 = blue, 3 = white */
    uint8_t tlut[4 * 2] = {
        0xF8, 0x01,      /* RGBA16 R=31 G=0 B=0 A=1 → red */
        0x07, 0xC1,      /* R=0 G=31 B=0 A=1 → green */
        0x00, 0x3F,      /* R=0 G=0 B=31 A=1 → blue */
        0xFF, 0xFF,      /* R=31 G=31 B=31 A=1 → white */
    };
    uint8_t src[4] = { 0, 1, 2, 3 };
    uint8_t dst[4 * 4];
    gfx_tex_decode_ci8_rgba16(src, tlut, dst, 4);
    PM3DS_EXPECT(dst[0]  > 0xF0); PM3DS_EXPECT(dst[1]  < 0x10); PM3DS_EXPECT(dst[2]  < 0x10);
    PM3DS_EXPECT(dst[4]  < 0x10); PM3DS_EXPECT(dst[5]  > 0xF0); PM3DS_EXPECT(dst[6]  < 0x10);
    PM3DS_EXPECT(dst[8]  < 0x10); PM3DS_EXPECT(dst[9]  < 0x10); PM3DS_EXPECT(dst[10] > 0xF0);
}

PM3DS_TEST(cache_insert_then_lookup) {
    gfx_tex_cache_reset();
    uint8_t *buf = (uint8_t *)malloc(64);
    gfx_tex_cache_insert(0x1000, 1, 2, 32, 32, 0, buf, 64);
    gfx_tex_entry_t *e = gfx_tex_cache_lookup(0x1000, 1, 2, 32, 32, 0);
    PM3DS_EXPECT(e != NULL);
    PM3DS_EXPECT(e->tiled_rgba8 == buf);
}

PM3DS_TEST(upload_then_lookup_returns_same_entry) {
    gfx_tex_cache_reset();
    uint8_t *buf = (uint8_t *)malloc(32 * 32 * 4);
    gfx_tex_entry_t *e = gfx_tex_upload(0x5000, 0, 0, 32, 32, 0,
                                        buf, 32 * 32 * 4);
    PM3DS_EXPECT(e != NULL);
    gfx_tex_entry_t *got = gfx_tex_cache_lookup(0x5000, 0, 0, 32, 32, 0);
    PM3DS_EXPECT(got == e);
}

PM3DS_TEST(cache_eviction_lru) {
    gfx_tex_cache_reset();
    size_t each = 1024 * 1024;
    for (int i = 0; i < 20; i++) {
        gfx_tex_cache_insert((uint32_t)(0x2000 + i), 0, 0, 16, 16, 0,
                             (uint8_t *)calloc(each, 1), each);
    }
    /* The first inserts should be evicted; the last should still be there. */
    PM3DS_EXPECT(gfx_tex_cache_lookup(0x2000, 0, 0, 16, 16, 0) == NULL);
    PM3DS_EXPECT(gfx_tex_cache_lookup(0x2013, 0, 0, 16, 16, 0) != NULL);
}

/* Byte offset of GPU pixel (gx,gy) inside a pw-wide morton-tiled RGBA8 buffer. */
static size_t morton_off(int gx, int gy, int pw) {
    int tx = gx & ~7, ty = gy & ~7;
    int px = gx &  7, py = gy &  7;
    int m = 0;
    for (int b = 0; b < 3; b++) {
        m |= ((px >> b) & 1) << (2 * b);
        m |= ((py >> b) & 1) << (2 * b + 1);
    }
    return (size_t)(((ty * pw) + tx * 8) + m) * 4;
}

PM3DS_TEST(swizzle_8x8_carries_through) {
    /* Pixel (x,y) carries R=x, G=y.
     *
     * NOTE: this test used to expect G==5 at GPU row 5 and had been failing
     * silently since M8.12, which made the swizzle flip rows vertically (the
     * PICA samples bottom-up, so v=0 reads the LAST stored row). GPU row y now
     * holds SOURCE row (h-1-y), so GPU (3,5) carries source row 8-1-5 = 2. The
     * flip is the correct behaviour; the expectation was simply never updated. */
    uint8_t linear[8 * 8 * 4];
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            uint8_t *p = linear + (y * 8 + x) * 4;
            p[0] = (uint8_t)x; p[1] = (uint8_t)y; p[2] = 0; p[3] = 0xFF;
        }
    }
    uint8_t tiled[8 * 8 * 4];
    gfx_tex_swizzle_to_pica(linear, tiled, 8, 8);
    size_t o = morton_off(3, 5, 8);
    /* PICA GPU_RGBA8 layout: byte[0]=A, byte[1]=B, byte[2]=G, byte[3]=R. */
    PM3DS_EXPECT(tiled[o + 0] == 0xFF);  /* A */
    PM3DS_EXPECT(tiled[o + 1] == 0);     /* B */
    PM3DS_EXPECT(tiled[o + 2] == 2);     /* G = source row 8-1-5 */
    PM3DS_EXPECT(tiled[o + 3] == 3);     /* R = column, unflipped */
}

PM3DS_TEST(tex_pot_rounds_to_legal_pica_sizes) {
    /* citro3d rejects anything that is not a power of two in [8,1024]; a 200x2
     * tile (the title-screen logo) is illegal on BOTH axes. */
    PM3DS_EXPECT(pm3ds_tex_pot(1)    == 8);
    PM3DS_EXPECT(pm3ds_tex_pot(2)    == 8);
    PM3DS_EXPECT(pm3ds_tex_pot(8)    == 8);
    PM3DS_EXPECT(pm3ds_tex_pot(9)    == 16);
    PM3DS_EXPECT(pm3ds_tex_pot(128)  == 128);
    PM3DS_EXPECT(pm3ds_tex_pot(200)  == 256);
    PM3DS_EXPECT(pm3ds_tex_pot(1024) == 1024);
    PM3DS_EXPECT(pm3ds_tex_pot(2000) == 1024);   /* clamped */
}

PM3DS_TEST(swizzle_padded_places_image_in_bottom_rows) {
    /* A 5x2 image padded to 8x8 — the shape that broke the title screen.
     * The V-flip must be about the PADDED height, so source row 0 lands on GPU
     * row ph-1 (where v=0 samples) and the padding sits ABOVE the image, out of
     * the sampled v range. Flipping about h instead would put pad at v=0. */
    enum { W = 5, H = 2, PW = 8, PH = 8 };
    uint8_t linear[W * H * 4];
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint8_t *p = linear + (y * W + x) * 4;
            p[0] = (uint8_t)(x + 1); p[1] = (uint8_t)(y + 1); p[2] = 0; p[3] = 0xFF;
        }
    }
    uint8_t tiled[PW * PH * 4];
    gfx_tex_swizzle_to_pica_padded(linear, tiled, W, H, PW, PH);

    /* GPU row PH-1 == source row 0; GPU row PH-2 == source row 1. */
    size_t o = morton_off(2, PH - 1, PW);
    PM3DS_EXPECT(tiled[o + 3] == 3);   /* R = column 2 + 1 */
    PM3DS_EXPECT(tiled[o + 2] == 1);   /* G = source row 0 + 1 */
    o = morton_off(2, PH - 2, PW);
    PM3DS_EXPECT(tiled[o + 2] == 2);   /* G = source row 1 + 1 */

    /* Rows above the image clamp to the nearest real row (edge replication),
     * never garbage. */
    o = morton_off(2, 0, PW);
    PM3DS_EXPECT(tiled[o + 2] == 2);   /* clamped to source row H-1 */

    /* Columns past W clamp to the last real column. */
    o = morton_off(PW - 1, PH - 1, PW);
    PM3DS_EXPECT(tiled[o + 3] == W);   /* R = column W-1 + 1 */
}

PM3DS_TEST(decode_rgba16) {
    /* 11111-00000-11111-1 = R=31 G=0 B=31 A=1 → R≈0xFF G=0 B≈0xFF A=0xFF */
    uint8_t src[2] = { 0xF8, 0x3F };
    uint8_t dst[1 * 4];
    gfx_tex_decode_rgba16(src, dst, 1);
    PM3DS_EXPECT(dst[0] >= 0xF8);
    PM3DS_EXPECT(dst[1] == 0x00);
    PM3DS_EXPECT(dst[2] >= 0xF8);
    PM3DS_EXPECT(dst[3] == 0xFF);
}
