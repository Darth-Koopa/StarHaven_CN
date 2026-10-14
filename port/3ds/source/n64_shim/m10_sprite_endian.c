/*
 * m10_sprite_endian.c -- byte-swap a decoded SpriteAnimData blob.
 *
 * WHY THIS IS *ONLY* AN ENDIAN PASS
 * ---------------------------------
 * Unlike the map ShapeFile, sprite data does NOT need rebasing: the decomp
 * already relocates it itself with
 *     #define SPR_SWIZZLE(base, offset) ((void*)((s32)(offset) + (s32)(base)))
 * i.e. the blob stores OFFSETS and spr_load_sprite adds the base. What breaks on
 * ARM is that those offsets are stored BIG-ENDIAN, so every value the swizzle
 * loop reads is byte-reversed and lands on a wild address.
 *
 * Measured: 859,633 unmapped accesses at src/101b90_len_8f0.c:147 — the
 * `while (true) { ptr1 = *data; ... }` loop walking the raster list.
 *
 * Swap them once, up front, and the decomp's own swizzling then works unchanged.
 *
 * WHAT GETS SWAPPED (src/sprite.h:69-84)
 *   SpriteAnimData 0x14+: rastersOffset, palettesOffset, maxComponents,
 *                         colorVariations, then animListStart[] (VLA)
 *   raster list @rastersOffset : u32 offsets, terminated by PTR_LIST_END
 *   palette list @palettesOffset: same
 *   SpriteRasterCacheEntry 0x8 : image (u32); width/height/palette/quadCacheIndex
 *                                are BYTES and must NOT be touched
 *   anim tree (M10.2)          : each anim entry -> PTR_LIST_END-terminated u32
 *                                component-offset list; each SpriteAnimComponent
 *                                (0xC: u32 cmdList, s16 cmdListSize in BYTES,
 *                                Vec3s compOffset) mixes one u32 with four s16s;
 *                                each cmdList is a u16 command stream needing
 *                                16-BIT swaps. Only the top-level list was
 *                                swapped before, so spr_swizzle_anim_offsets
 *                                (src/101b90_len_8f0.c:67/77) deref'd raw
 *                                big-endian offsets — measured as stray unmapped
 *                                reads during the logo frames. Structures shared
 *                                between anims are tracked in a seen-set: a
 *                                second swap would restore big-endian.
 *
 * PTR_LIST_END is (void*)-1 == 0xFFFFFFFF, which is byte-order invariant, so
 * list termination is detectable before and after swapping. That is what makes
 * a one-pass walk possible without knowing the list lengths in advance.
 *
 * Raster/palette PIXEL data is deliberately never swapped — it is byte data.
 */

#include <stdint.h>
#include <stddef.h>

#include "pm3ds_log.h"

#define PM3DS_PTR_LIST_END 0xFFFFFFFFu
#define PM3DS_MAX_LIST     4096      /* runaway guard for a corrupt blob */

static uint32_t bswap32(uint32_t v) {
    return ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8) |
           ((v & 0x00FF0000u) >> 8)  | ((v & 0xFF000000u) >> 24);
}

static void swap16_at(uint8_t *blob, uint32_t size, uint32_t off) {
    if (off >= size || size - off < 2u) return;   /* overflow-safe */
    uint8_t t = blob[off];
    blob[off] = blob[off + 1];
    blob[off + 1] = t;
}

/* Offsets already swapped this call. File-static (not stack) to keep frames
 * small; reset on every entry. Sized from measurement: the largest PAL sprite
 * (NPC N105) needs 734 distinct offsets (anim heads + components + cmdLists),
 * so 1024 leaves headroom. Overflow is loud, not silent — an unrecorded offset
 * that is later re-referenced would be double-swapped back to big-endian. */
#define PM3DS_MAX_SEEN 1024
static uint32_t g_seen_off[PM3DS_MAX_SEEN];
static uint32_t g_seen_cnt;

/* Returns 1 if `off` was already visited; otherwise records it and returns 0. */
static int seen_before(uint32_t off) {
    uint32_t i;
    for (i = 0; i < g_seen_cnt; i++) {
        if (g_seen_off[i] == off) return 1;
    }
    if (g_seen_cnt < PM3DS_MAX_SEEN) {
        g_seen_off[g_seen_cnt++] = off;
    } else {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            pm3ds_warn("sprite-endian: seen-set cap %u exceeded — shared "
                       "structures past this point risk double-swap",
                       (unsigned)PM3DS_MAX_SEEN);
        }
    }
    return 0;
}

/* Swap the u32 at `off`, returning the swapped value. Bounds-checked
 * (overflow-safe: off near UINT32_MAX must not wrap past the check). */
static uint32_t swap_at(uint8_t *blob, uint32_t size, uint32_t off, int *ok) {
    if (off >= size || size - off < 4u) { *ok = 0; return 0; }
    uint32_t *p = (uint32_t *)(void *)(blob + off);
    uint32_t v = bswap32(*p);
    *p = v;
    *ok = 1;
    return v;
}

/*
 * Swap a PTR_LIST_END-terminated array of u32 offsets starting at `off`.
 * Returns the number of entries swapped (excluding the terminator).
 * `entry_cb_swap_image`: when non-zero, each entry is treated as an offset to a
 * SpriteRasterCacheEntry whose first word (image) also needs swapping.
 */
static uint32_t swap_offset_list(uint8_t *blob, uint32_t size, uint32_t off,
                                 int swap_entry_image) {
    uint32_t n = 0;
    for (; n < PM3DS_MAX_LIST; n++) {
        uint32_t e = off + n * 4u;
        if (e + 4u > size) break;
        uint32_t raw = *(uint32_t *)(void *)(blob + e);
        if (raw == PM3DS_PTR_LIST_END) break;    /* endian-invariant sentinel */
        uint32_t v = bswap32(raw);
        *(uint32_t *)(void *)(blob + e) = v;
        if (swap_entry_image && v < size && size - v >= 8u) {   /* overflow-safe */
            int ok;
            (void)swap_at(blob, size, v, &ok);   /* SpriteRasterCacheEntry.image */
        }
    }
    return n;
}

/*
 * blob = the decoded SpriteAnimData, size = its decompressed length.
 * Call ONCE, immediately after decode_yay0 and BEFORE any swizzling.
 */
void pm3ds_sprite_endian_fix(void *blob, uint32_t size) {
    uint8_t *b = (uint8_t *)blob;
    if (!b || size < 0x14u) {
        pm3ds_warn("sprite-endian: bad blob=%p size=%u", blob, (unsigned)size);
        return;
    }
    g_seen_cnt = 0;

    int ok = 0;
    uint32_t rasters  = swap_at(b, size, 0x00, &ok); if (!ok) return;
    uint32_t palettes = swap_at(b, size, 0x04, &ok); if (!ok) return;
    (void)swap_at(b, size, 0x08, &ok);   /* maxComponents    */
    (void)swap_at(b, size, 0x0C, &ok);   /* colorVariations  */

    /* animListStart[] is a VLA of offsets right after the header, also
     * PTR_LIST_END-terminated. */
    uint32_t anims = swap_offset_list(b, size, 0x10, 0);

    uint32_t nrast = 0, npal = 0;
    if (rasters  < size) nrast = swap_offset_list(b, size, rasters,  1);
    if (palettes < size) npal  = swap_offset_list(b, size, palettes, 0);

    /* The anim tree below the top level (M10.2) — see the header comment.
     * Every offset is a plain blob offset: spr_load_sprite swizzles with
     * base 0, so no rebasing is involved, only swapping. */
    uint32_t comps = 0, cmdwords = 0;
    for (uint32_t ai = 0; ai < anims; ai++) {
        uint32_t a = *(uint32_t *)(void *)(b + 0x10u + ai * 4u);   /* now LE */
        if (a < 0x14u || a >= size || seen_before(a)) continue;
        for (uint32_t ci = 0; ci < PM3DS_MAX_LIST; ci++) {
            uint32_t e = a + ci * 4u;
            if (e + 4u > size) break;
            uint32_t raw = *(uint32_t *)(void *)(b + e);
            if (raw == PM3DS_PTR_LIST_END) break;   /* endian-invariant */
            uint32_t c = bswap32(raw);
            *(uint32_t *)(void *)(b + e) = c;
            /* overflow-safe: c near 0xFFFFFFFF (a corrupt near-terminator
             * value) would wrap c+0xC past the check and index blob-2. */
            if (c < 0x14u || c >= size || size - c < 0xCu || seen_before(c)) continue;
            comps++;

            int okc = 0;
            uint32_t cl = swap_at(b, size, c + 0x00, &okc);  /* cmdList offset */
            swap16_at(b, size, c + 0x04);                    /* cmdListSize    */
            swap16_at(b, size, c + 0x06);                    /* compOffset.x   */
            swap16_at(b, size, c + 0x08);                    /* compOffset.y   */
            swap16_at(b, size, c + 0x0A);                    /* compOffset.z   */

            /* cmdListSize is BYTES (sprite.c:517 indexes cmdList[size/2]);
             * the stream itself is u16 commands, so 16-bit swaps. */
            uint16_t clbytes = (uint16_t)(b[c + 0x04] | (b[c + 0x05] << 8));
            if (okc && cl >= 0x14u && cl < size && !seen_before(cl)) {
                uint32_t words = (uint32_t)clbytes / 2u;
                for (uint32_t wi = 0; wi < words; wi++) {
                    uint32_t o = cl + wi * 2u;
                    if (o + 2u > size) break;
                    swap16_at(b, size, o);
                    cmdwords++;
                }
            }
        }
    }

    static int logged = 0;
    if (!logged) {
        logged = 1;
        pm3ds_log(PM3DS_LOG_INFO,
                  "sprite-endian: size=%u rastersOff=0x%X palettesOff=0x%X "
                  "anims=%u rasters=%u palettes=%u comps=%u cmdwords=%u",
                  (unsigned)size, (unsigned)rasters, (unsigned)palettes,
                  (unsigned)anims, (unsigned)nrast, (unsigned)npal,
                  (unsigned)comps, (unsigned)cmdwords);
    }
}
