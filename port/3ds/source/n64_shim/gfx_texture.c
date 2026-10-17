/*
 * gfx_texture.c — N64 texture format decoders → RGBA8.
 * Fidelity tier: FAITHFUL — same pixels, different layout.
 */

#include <stdint.h>
#include <stddef.h>

static uint8_t replicate_nibble(uint8_t n) {
    return (uint8_t)((n << 4) | n);
}

void gfx_tex_decode_i4(const uint8_t *src, uint8_t *dst, size_t pixels) {
    for (size_t i = 0; i < pixels; i++) {
        uint8_t byte = src[i / 2];
        uint8_t nib  = (i & 1) ? (byte & 0x0F) : ((byte >> 4) & 0x0F);
        uint8_t v    = replicate_nibble(nib);
        dst[i * 4 + 0] = v;
        dst[i * 4 + 1] = v;
        dst[i * 4 + 2] = v;
        dst[i * 4 + 3] = v;
    }
}

void gfx_tex_decode_i8(const uint8_t *src, uint8_t *dst, size_t pixels) {
    for (size_t i = 0; i < pixels; i++) {
        uint8_t v = src[i];
        dst[i * 4 + 0] = v;
        dst[i * 4 + 1] = v;
        dst[i * 4 + 2] = v;
        dst[i * 4 + 3] = v;
    }
}

void gfx_tex_decode_ia4(const uint8_t *src, uint8_t *dst, size_t pixels) {
    for (size_t i = 0; i < pixels; i++) {
        uint8_t byte = src[i / 2];
        uint8_t nib  = (i & 1) ? (byte & 0x0F) : ((byte >> 4) & 0x0F);
        uint8_t intensity_bits = (uint8_t)(nib & 0xE);
        uint8_t I = (uint8_t)((intensity_bits << 4) | (intensity_bits << 1));
        uint8_t A = (nib & 1) ? 0xFF : 0x00;
        dst[i * 4 + 0] = I;
        dst[i * 4 + 1] = I;
        dst[i * 4 + 2] = I;
        dst[i * 4 + 3] = A;
    }
}

void gfx_tex_decode_ia8(const uint8_t *src, uint8_t *dst, size_t pixels) {
    for (size_t i = 0; i < pixels; i++) {
        uint8_t byte = src[i];
        uint8_t I = replicate_nibble((byte >> 4) & 0xF);
        uint8_t A = replicate_nibble(byte & 0xF);
        dst[i * 4 + 0] = I;
        dst[i * 4 + 1] = I;
        dst[i * 4 + 2] = I;
        dst[i * 4 + 3] = A;
    }
}

void gfx_tex_decode_ia16(const uint8_t *src, uint8_t *dst, size_t pixels) {
    for (size_t i = 0; i < pixels; i++) {
        uint8_t I = src[i * 2 + 0];
        uint8_t A = src[i * 2 + 1];
        dst[i * 4 + 0] = I;
        dst[i * 4 + 1] = I;
        dst[i * 4 + 2] = I;
        dst[i * 4 + 3] = A;
    }
}

void gfx_tex_decode_rgba16(const uint8_t *src, uint8_t *dst, size_t pixels) {
    for (size_t i = 0; i < pixels; i++) {
        uint16_t v = ((uint16_t)src[i * 2 + 0] << 8) | src[i * 2 + 1];
        uint8_t R = (uint8_t)((v >> 11) & 0x1F);
        uint8_t G = (uint8_t)((v >>  6) & 0x1F);
        uint8_t B = (uint8_t)((v >>  1) & 0x1F);
        uint8_t A = (v & 1) ? 0xFF : 0x00;
        dst[i * 4 + 0] = (uint8_t)((R << 3) | (R >> 2));
        dst[i * 4 + 1] = (uint8_t)((G << 3) | (G >> 2));
        dst[i * 4 + 2] = (uint8_t)((B << 3) | (B >> 2));
        dst[i * 4 + 3] = A;
    }
}

void gfx_tex_decode_rgba32(const uint8_t *src, uint8_t *dst, size_t pixels) {
    for (size_t i = 0; i < pixels; i++) {
        dst[i * 4 + 0] = src[i * 4 + 0];
        dst[i * 4 + 1] = src[i * 4 + 1];
        dst[i * 4 + 2] = src[i * 4 + 2];
        dst[i * 4 + 3] = src[i * 4 + 3];
    }
}

/* tlut is a flat array of 256 RGBA16 entries; we sample at index 0..255 */
void gfx_tex_decode_ci8_rgba16(const uint8_t *src, const uint8_t *tlut_rgba16,
                               uint8_t *dst, size_t pixels) {
    for (size_t i = 0; i < pixels; i++) {
        uint8_t idx = src[i];
        gfx_tex_decode_rgba16(tlut_rgba16 + idx * 2, dst + i * 4, 1);
    }
}

void gfx_tex_decode_ci4_rgba16(const uint8_t *src, const uint8_t *tlut_rgba16,
                               uint8_t palette, uint8_t *dst, size_t pixels) {
    /* CI4 uses the 4-bit "palette" tile field to choose which 16 TLUT
     * entries the indices index. Palette N selects TLUT[N*16..]. */
    const uint8_t *pal = tlut_rgba16 + (size_t)palette * 16 * 2;
    for (size_t i = 0; i < pixels; i++) {
        uint8_t byte = src[i / 2];
        uint8_t idx  = (i & 1) ? (byte & 0x0F) : ((byte >> 4) & 0x0F);
        gfx_tex_decode_rgba16(pal + idx * 2, dst + i * 4, 1);
    }
}

/* PICA200 stores textures in 8x8 morton (Z-order) tiles. This table
 * maps linear-pixel-offset -> tiled-byte-offset for an 8x8 RGBA8 block. */
static uint8_t g_morton8[64];
static int g_morton_inited = 0;

static void gfx_tex_init_morton(void) {
    if (g_morton_inited) return;
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            int m = 0;
            for (int b = 0; b < 3; b++) {
                m |= ((x >> b) & 1) << (2 * b);
                m |= ((y >> b) & 1) << (2 * b + 1);
            }
            g_morton8[y * 8 + x] = (uint8_t)m;
        }
    }
    g_morton_inited = 1;
}

/*
 * Swizzle a w x h linear RGBA8 image into a pw x ph PICA200 morton-tiled
 * buffer, where pw/ph are the POT-padded dimensions (see pm3ds_tex_pot).
 *
 * The padding is why this exists: C3D_TexInit rejects non-POT and sub-8px
 * dimensions outright, so the title logo's 200x2 strips produced no texture at
 * all. We upload a legal pw x ph texture and let the UV normalization in
 * gfx_present.c divide by pw/ph, so only the real w x h region is ever sampled.
 *
 * The V-flip interacts with the padding and is easy to get wrong. The PICA
 * samples bottom-up: v=0 reads the LAST stored row. We want v=0 to land on the
 * image's TOP row, so GPU row y must hold source row (ph-1-y) — flipped about
 * the PADDED height, not h. The image therefore occupies the BOTTOM ph-h GPU
 * rows and the padding sits above it, where v never reaches. Out-of-range rows
 * and columns clamp to the nearest real texel (edge replication) rather than
 * being left undefined.
 */
void gfx_tex_swizzle_to_pica_padded(const uint8_t *linear_rgba8, uint8_t *tiled_rgba8,
                                    int w, int h, int pw, int ph) {
    gfx_tex_init_morton();
    for (int ty = 0; ty < ph; ty += 8) {
        for (int tx = 0; tx < pw; tx += 8) {
            uint8_t *tile = tiled_rgba8 + ((ty * pw) + tx * 8) * 4;
            for (int py = 0; py < 8; py++) {
                for (int px = 0; px < 8; px++) {
                    int gx = tx + px;
                    int gy = ty + py;
                    /* Flip about the padded height, then clamp into the real
                     * image so pad texels replicate the nearest edge. */
                    int sx = gx;             if (sx > w - 1) sx = w - 1;
                    int sy = (ph - 1) - gy;  if (sy > h - 1) sy = h - 1;
                    if (sy < 0) sy = 0;
                    const uint8_t *psrc = linear_rgba8 + ((size_t)sy * w + sx) * 4;
                    uint8_t *pdst = tile + g_morton8[py * 8 + px] * 4;
                    /* [R,G,B,A] -> [A,B,G,R] for GPU_RGBA8 (see below). */
                    pdst[0] = psrc[3]; pdst[1] = psrc[2]; pdst[2] = psrc[1]; pdst[3] = psrc[0];
                }
            }
        }
    }
}

void gfx_tex_swizzle_to_pica(const uint8_t *linear_rgba8, uint8_t *tiled_rgba8,
                             int w, int h) {
    gfx_tex_init_morton();
    for (int ty = 0; ty < h; ty += 8) {
        for (int tx = 0; tx < w; tx += 8) {
            uint8_t *tile = tiled_rgba8 + ((ty * w) + tx * 8) * 4;
            for (int py = 0; py < 8 && ty + py < h; py++) {
                for (int px = 0; px < 8 && tx + px < w; px++) {
                    int linear_x = tx + px;
                    int linear_y = ty + py;
                    /* M8.12 FIX: the PICA200 samples textures bottom-up — V=0
                     * reads the LAST stored row. Our morton WRITE offset matches
                     * the GPU's address formula, but without this flip every
                     * texture renders vertically mirrored (each logo strip's 16
                     * rows reversed = the "wrong orientation" scramble that
                     * offline interp-C reproduced). Store source row (h-1-y) at
                     * GPU row y so screen-top samples the image's top row. */
                    const uint8_t *psrc =
                        linear_rgba8 + (((h - 1) - linear_y) * w + linear_x) * 4;
                    uint8_t *pdst = tile + g_morton8[py * 8 + px] * 4;
                    /* Reorder [R,G,B,A] → [A,B,G,R] for PICA200 GPU_RGBA8.
                     * The PICA samples each texel as a u32 where the high
                     * byte is R and the low byte is A — i.e., in memory
                     * (little-endian), the byte order is A, B, G, R.
                     * Without this swap, brown (R=139,G=90,B=43,A=255)
                     * shaderside reads back as (R=255,G=43,B=90,A=139),
                     * which is the magenta we were seeing on hardware. */
                    pdst[0] = psrc[3]; pdst[1] = psrc[2]; pdst[2] = psrc[1]; pdst[3] = psrc[0];
                }
            }
        }
    }
}

/* LRU cache. Each entry owns its tiled_rgba8 buffer (linearAlloc on
 * device, malloc on host tests) and on device also owns a C3D_Tex
 * handle backed by the same buffer. */

#include <stdlib.h>
#include <string.h>

#ifndef PM3DS_HOST_TEST
#include <3ds.h>
#include <citro3d.h>
#include "pm3ds_log.h"
#include "pm3ds_addr.h"
#endif
/* pm3ds_tex_pot() — the PICA200 legal-texture-size rule, shared with
 * gfx_present.c's UV normalization so the two cannot disagree. */
#include "gfx_state.h"

#ifdef PM3DS_HOST_TEST
#define PM3DS_TEX_ALLOC(sz) malloc(sz)
#define PM3DS_TEX_FREE(p)   free(p)
#else
/* Staging only: the swizzled pixels are copied into the C3D texture's own
 * linear allocation by C3D_TexUpload and freed right after, so the scarce
 * linear heap holds each texture once. */
#define PM3DS_TEX_ALLOC(sz) malloc(sz)
#define PM3DS_TEX_FREE(p)   free(p)
#endif

typedef struct gfx_tex_entry {
    uint32_t key_addr;
    uint8_t  key_fmt;
    uint8_t  key_siz;
    uint16_t key_w, key_h;
    uint8_t  key_palette;
    uint8_t *tiled_rgba8;
    size_t   bytes;
#ifndef PM3DS_HOST_TEST
    C3D_Tex  c3d_tex;
    int      c3d_tex_inited;
#endif
    struct gfx_tex_entry *lru_prev, *lru_next;
    struct gfx_tex_entry *hash_prev, *hash_next;   /* bucket of key_addr's page */
    int      in_hash;
    uint32_t ext_stride, ext_tlut, ext_frame;   /* M12 key extension */
    uint32_t ext_sample;                        /* M14.6: texel content sample */
} gfx_tex_entry_t;

/* GPU texture bytes (linear heap, shared with framebuffers, vertex buffers,
 * audio and the 8 MB pm3ds heap). 12 MB at most; the Old 3DS's 80 MB mode
 * has only ~9 MB of linear memory left when the first texture comes in, so
 * the cap is what is free then, less a reserve for everything else. */
#define GFX_TEX_CACHE_CAP_BYTES (12u * 1024u * 1024u)
#define GFX_TEX_CACHE_MIN_BYTES (4u * 1024u * 1024u)
#define GFX_TEX_LINEAR_RESERVE  (3u * 1024u * 1024u)

static size_t g_cache_cap;   /* set at the first texture */
static size_t cache_cap(void) {
    size_t cap = g_cache_cap;
    if (!cap) {
#ifndef PM3DS_HOST_TEST
        size_t free_now = linearSpaceFree();
        /* and the part of the DSP voices' sample cache not filled yet (audio_hwvoice.c) */
        extern u32 pm3ds_hwv_reserve(void);
        size_t reserve = GFX_TEX_LINEAR_RESERVE + pm3ds_hwv_reserve();
        cap = free_now > reserve ? free_now - reserve : 0;
        if (cap > GFX_TEX_CACHE_CAP_BYTES) cap = GFX_TEX_CACHE_CAP_BYTES;
        if (cap < GFX_TEX_CACHE_MIN_BYTES) cap = GFX_TEX_CACHE_MIN_BYTES;
#else
        cap = GFX_TEX_CACHE_CAP_BYTES;
#endif
        g_cache_cap = cap;
    }
    return cap;
}

static gfx_tex_entry_t *g_cache_head = NULL;   /* MRU */
static gfx_tex_entry_t *g_cache_tail = NULL;   /* LRU */
static size_t           g_cache_bytes = 0;

/* for the Wi-Fi log's heartbeat */
unsigned gfx_tex_cache_kb(void) { return (unsigned)(g_cache_bytes >> 10); }
unsigned gfx_tex_cache_cap_kb(void) { return (unsigned)(g_cache_cap >> 10); }

static void cache_unlink(gfx_tex_entry_t *e) {
    if (e->lru_prev) e->lru_prev->lru_next = e->lru_next;
    else             g_cache_head = e->lru_next;
    if (e->lru_next) e->lru_next->lru_prev = e->lru_prev;
    else             g_cache_tail = e->lru_prev;
    e->lru_prev = e->lru_next = NULL;
}

/* Entries are also hashed by the 4 KB page their source starts in, so a
 * lookup or an invalidation looks at a few entries instead of walking the
 * whole cache (a map load invalidates ~15k ranges; with ~1000 cached
 * textures the walks cost a second per battle on a 3DS). */
#define HASH_PAGE_SHIFT 12
#define HASH_BUCKETS    4096
static gfx_tex_entry_t *g_hash[HASH_BUCKETS];
static uint32_t g_max_span;   /* largest source span of any entry: how far back a range reaches */

static gfx_tex_entry_t **bucket_of(uint32_t addr) {
    return &g_hash[(addr >> HASH_PAGE_SHIFT) & (HASH_BUCKETS - 1)];
}

static void hash_link(gfx_tex_entry_t *e) {
    gfx_tex_entry_t **b = bucket_of(e->key_addr);
    e->hash_prev = NULL;
    e->hash_next = *b;
    if (*b) (*b)->hash_prev = e;
    *b = e;
    e->in_hash = 1;
}

static void hash_unlink(gfx_tex_entry_t *e) {
    if (!e->in_hash) return;
    if (e->hash_prev) e->hash_prev->hash_next = e->hash_next;
    else              *bucket_of(e->key_addr) = e->hash_next;
    if (e->hash_next) e->hash_next->hash_prev = e->hash_prev;
    e->hash_prev = e->hash_next = NULL;
    e->in_hash = 0;
}

static void cache_push_mru(gfx_tex_entry_t *e) {
    e->lru_prev = NULL;
    e->lru_next = g_cache_head;
    if (g_cache_head) g_cache_head->lru_prev = e;
    g_cache_head = e;
    if (!g_cache_tail) g_cache_tail = e;
}

#ifndef PM3DS_HOST_TEST
extern uint32_t pm3ds_gfx_frame_number(void);
#endif
static void cache_evict_until(size_t target) {
    while (g_cache_tail && g_cache_bytes > target) {
        gfx_tex_entry_t *victim = g_cache_tail;
#ifndef PM3DS_HOST_TEST
        /* The GPU reads bound textures at C3D_FrameEnd: never free one this
         * frame has used (the cache may overshoot its cap briefly instead). */
        if (victim->ext_frame == pm3ds_gfx_frame_number()) break;
#endif
        cache_unlink(victim);
        hash_unlink(victim);
        g_cache_bytes -= victim->bytes;
#ifndef PM3DS_HOST_TEST
        if (victim->c3d_tex_inited) C3D_TexDelete(&victim->c3d_tex);
#endif
        PM3DS_TEX_FREE(victim->tiled_rgba8);
        free(victim);
    }
}

gfx_tex_entry_t *gfx_tex_cache_lookup(uint32_t addr, uint8_t fmt, uint8_t siz,
                                      uint16_t w, uint16_t h, uint8_t palette) {
    for (gfx_tex_entry_t *e = *bucket_of(addr); e; e = e->hash_next) {
        if (e->key_addr == addr && e->key_fmt == fmt && e->key_siz == siz &&
            e->key_w == w && e->key_h == h && e->key_palette == palette) {
            cache_unlink(e);
            cache_push_mru(e);
            return e;
        }
    }
    return NULL;
}

gfx_tex_entry_t *gfx_tex_cache_insert(uint32_t addr, uint8_t fmt, uint8_t siz,
                                      uint16_t w, uint16_t h, uint8_t palette,
                                      uint8_t *tiled_rgba8, size_t bytes) {
    cache_evict_until(cache_cap() > bytes ? cache_cap() - bytes : 0);
    gfx_tex_entry_t *e = (gfx_tex_entry_t *)calloc(1, sizeof(*e));
    if (!e) { PM3DS_TEX_FREE(tiled_rgba8); return NULL; }
    e->key_addr = addr; e->key_fmt = fmt; e->key_siz = siz;
    e->key_w = w; e->key_h = h; e->key_palette = palette;
    e->tiled_rgba8 = tiled_rgba8; e->bytes = bytes;
    cache_push_mru(e);
    hash_link(e);
    g_cache_bytes += bytes;
    return e;
}

void gfx_tex_cache_reset(void) {
    while (g_cache_head) {
        gfx_tex_entry_t *e = g_cache_head;
        cache_unlink(e);
        hash_unlink(e);
#ifndef PM3DS_HOST_TEST
        if (e->c3d_tex_inited) C3D_TexDelete(&e->c3d_tex);
#endif
        PM3DS_TEX_FREE(e->tiled_rgba8);
        free(e);
    }
    g_cache_bytes = 0;
}

/* On a cache miss, the translator calls gfx_tex_upload to land the
 * RGBA8 source onto the GPU. Returns the cache entry to bind on
 * subsequent draws (or NULL on OOM).
 *
 * tiled_rgba8 must be a PM3DS_TEX_ALLOC'd buffer in PICA200 morton-tile
 * layout, exactly `bytes` long. Ownership transfers to the cache. */
#ifndef PM3DS_HOST_TEST
static int g_upload_fmt = GPU_RGBA8;   /* GPU format of the next gfx_tex_upload */
#endif
gfx_tex_entry_t *gfx_tex_upload(uint32_t addr, uint8_t fmt, uint8_t siz,
                                uint16_t w, uint16_t h, uint8_t palette,
                                uint8_t *tiled_rgba8, size_t bytes) {
    gfx_tex_entry_t *e = gfx_tex_cache_insert(addr, fmt, siz, w, h, palette,
                                              tiled_rgba8, bytes);
    if (!e) return NULL;
#ifndef PM3DS_HOST_TEST
    /* The cache key keeps the LOGICAL w/h (that is what the tile descriptor
     * says and what lookups use), but the GPU texture must be POT-padded --
     * `tiled_rgba8` is expected to already BE pw x ph, laid out by
     * gfx_tex_swizzle_to_pica_padded. */
    unsigned pw = pm3ds_tex_pot(w);
    unsigned ph = pm3ds_tex_pot(h);
    /* CHECK the return. C3D_TexInit fails (allocating nothing) for any non-POT
     * or sub-8px dimension, and this used to set c3d_tex_inited
     * unconditionally -- so gfx_tex_bind happily bound an uninitialised
     * C3D_Tex. That is exactly how the title screen reported 58 successful
     * texture binds while drawing nothing at all. Never claim a texture we do
     * not actually have. */
    bool ok = C3D_TexInit(&e->c3d_tex, (u16)pw, (u16)ph, (GPU_TEXCOLOR)g_upload_fmt);
    if (!ok && g_cache_bytes > bytes) {
        /* out of linear memory: drop older textures and retry once */
        cache_unlink(e);
        cache_evict_until(g_cache_bytes > 2 * bytes ? (g_cache_bytes - bytes) / 2 : 0);
        cache_push_mru(e);
        ok = C3D_TexInit(&e->c3d_tex, (u16)pw, (u16)ph, (GPU_TEXCOLOR)g_upload_fmt);
    }
    if (!ok) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            pm3ds_log(PM3DS_LOG_WARN,
                      "C3D_TexInit REJECTED padded %ux%u (logical %ux%u) — "
                      "texture dropped; draws will be untextured",
                      pw, ph, (unsigned)w, (unsigned)h);
        }
        /* don't cache the failure: the next frame retries */
        cache_unlink(e);
        g_cache_bytes -= e->bytes;
        PM3DS_TEX_FREE(e->tiled_rgba8);
        free(e);
        return NULL;
    }
    C3D_TexUpload(&e->c3d_tex, tiled_rgba8);
    /* the frame flushes only its vertices (gfx_present.c); svc, not the GSP
     * IPC, which is slow when the system core is lent to the audio mixer */
    svcFlushProcessDataCache(CUR_PROCESS_HANDLE, (u32)e->c3d_tex.data, e->c3d_tex.size);
    { extern unsigned pm3ds_tstat_uploads; pm3ds_tstat_uploads++; }
    PM3DS_TEX_FREE(e->tiled_rgba8);
    e->tiled_rgba8 = NULL;
    C3D_TexSetFilter(&e->c3d_tex, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&e->c3d_tex, GPU_REPEAT, GPU_REPEAT);
    e->c3d_tex_inited = 1;
#endif
    return e;
}

#ifndef PM3DS_HOST_TEST
/* ---- M12: TMEM-model importer --------------------------------------------- */

extern int pm3ds_pi_read(uint32_t addr, void *dst, uint32_t size);
extern uint32_t pm3ds_gfx_frame_number(void);

#define OMH_TT_MASK  (3u << 14)
#define OMH_TT_IA16  (3u << 14)

static void decode_tlut_entry(const uint8_t *e, int ia16, uint8_t *dst) {
    if (ia16) { dst[0] = dst[1] = dst[2] = e[0]; dst[3] = e[1]; }
    else      gfx_tex_decode_rgba16(e, dst, 1);
}

/* Decode `w` texels of one row from `src` into RGBA8 `dst`. */
static void decode_row(const gfx_state_t *st, const gfx_tile_t *t, const uint8_t *src,
                       uint8_t *dst, int w) {
    int ia16 = (st->othermode_h & OMH_TT_MASK) == OMH_TT_IA16;
    switch ((t->fmt << 4) | t->siz) {
        case 0x02: gfx_tex_decode_rgba16(src, dst, (size_t)w); break;
        case 0x03: gfx_tex_decode_rgba32(src, dst, (size_t)w); break;
        case 0x30: gfx_tex_decode_ia4(src, dst, (size_t)w); break;
        case 0x31: gfx_tex_decode_ia8(src, dst, (size_t)w); break;
        case 0x32: gfx_tex_decode_ia16(src, dst, (size_t)w); break;
        case 0x40: gfx_tex_decode_i4(src, dst, (size_t)w); break;
        case 0x41: gfx_tex_decode_i8(src, dst, (size_t)w); break;
        case 0x42: gfx_tex_decode_ia16(src, dst, (size_t)w); break;   /* I16 ~ IA16 */
        case 0x20: {   /* CI4 */
            const uint8_t *pal = st->tlut + (size_t)(t->palette & 0xF) * 16 * 2;
            for (int x = 0; x < w; x++) {
                uint8_t b = src[x >> 1];
                uint8_t idx = (x & 1) ? (b & 0xF) : (b >> 4);
                decode_tlut_entry(pal + idx * 2, ia16, dst + x * 4);
            }
            break;
        }
        case 0x21:     /* CI8 */
            for (int x = 0; x < w; x++) decode_tlut_entry(st->tlut + src[x] * 2, ia16, dst + x * 4);
            break;
        case 0x10: case 0x11: case 0x12:   /* YUV: unsupported, mid-grey */
        default:
            memset(dst, 0x80, (size_t)w * 4);
            break;
    }
}

static const gfx_tmem_load_t *find_load(const gfx_state_t *st, uint16_t tmem) {
    const gfx_tmem_load_t *best = NULL;
    for (int i = 0; i < GFX_TMEM_LOADS; i++) {
        const gfx_tmem_load_t *l = &st->loads[i];
        if (!l->serial) continue;
        if (tmem < l->tmem || tmem >= l->tmem + (l->words ? l->words : 1)) continue;
        if (!best || l->serial > best->serial) best = l;
    }
    return best;
}

bool gfx_tex_load_source(const gfx_state_t *st, int tile, uint32_t *addr, int *col, int *row) {
    const gfx_tile_t *t = &st->tiles[tile & 7];
    const gfx_tmem_load_t *ld = find_load(st, t->tmem);
    if (!ld) return false;
    *addr = ld->addr;
    *col = ld->is_tile ? ld->uls >> 2 : 0;
    *row = ld->is_tile ? ld->ult >> 2 : 0;
    if (t->line && t->tmem > ld->tmem) *row += (t->tmem - ld->tmem) / t->line;
    return true;
}

/* key extension stored alongside the legacy (addr,fmt,siz,w,h,palette) key */
typedef struct { uint32_t stride, tlut, frame, sample; } tex_key_ext_t;
static tex_key_ext_t *ext_of(gfx_tex_entry_t *e) { return (tex_key_ext_t *)&e->ext_stride; }


/* Texture data read from the cart never changes; everything else (map
 * texture heap, sprite rasters, decoded backgrounds, generated images) can be
 * overwritten at the same address, so it gets a content sample in the key. */
static int tex_src_is_static(uint32_t src) {
    return pm3ds_addr_rom_offset(src, 1) != (uint32_t)-1;
}

/* FNV over 32-bit words (a quarter of the multiplies); the texture is in
 * RAM here (tex_src_is_static sends cart data elsewhere), read in place */
static uint32_t fnv1a_words(uint32_t h, const uint8_t *p, size_t n) {
    size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        uint32_t w;
        memcpy(&w, p + i, 4);
        h = (h ^ w) * 16777619u;
    }
    for (; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

static uint32_t tex_sample_hash(uint32_t src, uint32_t stride, uint32_t row_bytes, int h) {
    uint32_t n = row_bytes < 64 ? row_bytes : 64;
    uint32_t hash = 2166136261u;
    int rows[3] = { 0, h / 2, h - 1 };
    for (int i = 0; i < 3; i++) {
        const uint8_t *row = (const uint8_t *)(uintptr_t)(src + (uint32_t)rows[i] * stride);
        hash = fnv1a_words(hash, row, n);
        if (row_bytes > n) hash = fnv1a_words(hash, row + row_bytes - n, n);   /* the row's tail too */
    }
    return hash;
}

/* Content samples taken this frame. RAM holds still while a frame's lists are
 * drawn, so a texture drawn again in the same frame reuses its sample instead
 * of reading six rows again (each one a cache miss on an Old 3DS). Any
 * invalidation starts the memo over (g_sample_epoch). */
#define SAMPLE_MEMO 64
static struct { uint32_t src, stride, size, frame, epoch, sample; } g_sample_memo[SAMPLE_MEMO];
static uint32_t g_sample_epoch = 1;
#ifdef PM3DS_STEP_PROF
unsigned pm3ds_prof_tex_lookups, pm3ds_prof_tex_chain, pm3ds_prof_tex_memo;
#endif

static uint32_t tex_sample(uint32_t src, uint32_t stride, uint32_t row_bytes, int h) {
#ifdef PM3DS_HOST_TEST
    return tex_sample_hash(src, stride, row_bytes, h);
#else
    uint32_t size = row_bytes << 16 | (uint32_t)h, frame = pm3ds_gfx_frame_number();
    __typeof__(g_sample_memo[0]) *m = &g_sample_memo[((src >> 3) ^ (src >> 12)) & (SAMPLE_MEMO - 1)];
    if (m->src == src && m->stride == stride && m->size == size && m->frame == frame && m->epoch == g_sample_epoch) {
#ifdef PM3DS_STEP_PROF
        pm3ds_prof_tex_memo++;
#endif
        return m->sample;
    }
    m->src = src;
    m->stride = stride;
    m->size = size;
    m->frame = frame;
    m->epoch = g_sample_epoch;
    return m->sample = tex_sample_hash(src, stride, row_bytes, h);
#endif
}

/* Drop cached textures sourced from [addr, addr+len): called when DMA or a
 * Yay0 decode overwrites memory. Entries the GPU may still read this frame are
 * only re-keyed (unreachable) and left for the LRU to free. */
#ifdef PM3DS_STEP_PROF
uint64_t pm3ds_prof_inval_ticks;
uint32_t pm3ds_prof_inval_calls;
unsigned pm3ds_prof_tex_entries(void) { unsigned n = 0; for (gfx_tex_entry_t *e = g_cache_head; e; e = e->lru_next) n++; return n; }
#endif
void gfx_tex_invalidate_range(const void *addr, size_t len) {
#ifdef PM3DS_STEP_PROF
    uint64_t t0 = svcGetSystemTick();
    pm3ds_prof_inval_calls++;
#endif
    uint32_t a = (uint32_t)(uintptr_t)addr, end = a + (uint32_t)len;
    if (end <= a) return;
    g_sample_epoch++;
    /* entries starting up to g_max_span before the range can reach into it */
    uint32_t first = (a > g_max_span ? a - g_max_span : 0) >> HASH_PAGE_SHIFT;
    uint32_t last = (end - 1) >> HASH_PAGE_SHIFT;
    if (last - first >= HASH_BUCKETS) {   /* a huge range: every bucket */
        first = 0;
        last = HASH_BUCKETS - 1;
    }
    for (uint32_t pg = first; pg <= last; pg++) {
        gfx_tex_entry_t *e = g_hash[pg & (HASH_BUCKETS - 1)];
        while (e) {
            gfx_tex_entry_t *next = e->hash_next;
            uint32_t s0 = e->key_addr, s1 = s0 + e->ext_stride * e->key_h;
            if (s0 < end && a < s1) {
                hash_unlink(e);
                e->key_addr = 0;
            }
            e = next;
        }
    }
#ifdef PM3DS_STEP_PROF
    pm3ds_prof_inval_ticks += svcGetSystemTick() - t0;
#endif
}

int gfx_tex_miss_reason;   /* why the last gfx_tex_get returned NULL */

/* GPU format per N64 source: 16-bit wherever it is lossless (RGBA16 and
 * RGBA16-palette CI are 5551 already, I/IA and IA16-palette CI fit LA8).
 * Half the linear memory and texture-cache traffic of RGBA8. */
static int gpu_format_for(const gfx_state_t *st, const gfx_tile_t *t) {
    switch ((t->fmt << 4) | t->siz) {
        case 0x02:
            return GPU_RGBA5551;
        case 0x20: case 0x21:
            return (st->othermode_h & OMH_TT_MASK) == OMH_TT_IA16 ? GPU_LA8 : GPU_RGBA5551;
        case 0x30: case 0x31: case 0x32: case 0x40: case 0x41: case 0x42:
            return GPU_LA8;
        default:
            return GPU_RGBA8;
    }
}

/* gfx_tex_swizzle_to_pica_padded for the 16-bit formats: same tiling, flip
 * and edge clamp, texels packed from the decoded RGBA8 (exact for these
 * sources: the decoders expand 5->8 bits by bit replication). */
static void swizzle_pack16(const uint8_t *rgba, uint16_t *out, int w, int h, int pw, int ph, int fmt) {
    gfx_tex_init_morton();
    for (int ty = 0; ty < ph; ty += 8) {
        for (int tx = 0; tx < pw; tx += 8) {
            uint16_t *tile = out + (ty * pw) + tx * 8;
            for (int py = 0; py < 8; py++) {
                int sy = (ph - 1) - (ty + py);
                if (sy > h - 1) sy = h - 1;
                if (sy < 0) sy = 0;
                const uint8_t *srow = rgba + (size_t)sy * w * 4;
                for (int px = 0; px < 8; px++) {
                    int sx = tx + px;
                    if (sx > w - 1) sx = w - 1;
                    const uint8_t *p = srow + sx * 4;
                    tile[g_morton8[py * 8 + px]] = fmt == GPU_LA8
                        ? (uint16_t)((p[0] << 8) | p[3])
                        : (uint16_t)(((p[0] >> 3) << 11) | ((p[1] >> 3) << 6) | ((p[2] >> 3) << 1) | (p[3] >> 7));
                }
            }
        }
    }
}

/* The RDP clamps a coordinate to the tile first and then wraps/mirrors it
 * within the mask. A mask smaller than the tile therefore repeats (or
 * mirrors) inside the tile -- effects build symmetric sprites like the damage
 * starburst from one mirrored half this way -- and, with the clamp bit, stops
 * at the tile edge (the darkness stencil is one light hole, not a grid of
 * them). Without clamp the texture stays mask-sized and the GPU repeats or
 * mirrors it; with clamp it is expanded to tile size with the wrapping baked
 * in and the GPU clamps at the edge. */
static int tex_expand(int clamp_mirror, int mask, int tile_size) {
    /* only for small tiles: the fright jar clamps a 128-texel mask at a
     * 500-texel tile edge it never reaches, and expanding that to 512x512 on
     * every scroll step stalled the game for a second. Past the size limit
     * the mask-sized texture repeats/mirrors, which is the same until the
     * coordinates reach the clamp edge. */
    return (clamp_mirror & 2) && mask && (1 << mask) < tile_size && tile_size <= 256;
}

/* The other direction: a wrap mask LARGER than the tile, without clamp. The
 * tile origin (uls/ult) then only shifts the coordinates, which wrap at the
 * mask over whatever TMEM holds past the tile -- emote bubbles pick their
 * animation frame from a 96-texel strip by moving uls this way. The GPU
 * texture spans the whole mask (data past the loaded rows/columns is zero). */
static int tex_grow(int clamp_mirror, int mask, int tile_size) {
    return !(clamp_mirror & 2) && mask && (1 << mask) > tile_size && mask <= 10;
}

void gfx_tex_wrap_modes(const gfx_tile_t *t, int *cms, int *cmt) {
    int tile_w = (int)((t->lrs - t->uls) >> 2) + 1, tile_h = (int)((t->lrt - t->ult) >> 2) + 1;
    /* the RDP clamps an axis whose mask is 0 whatever the clamp bit says;
     * repeating it instead lets bilinear filtering bleed the opposite edge in
     * (seams in the got-item starburst, the wrong colour at the left edge of
     * Wide's widened backgrounds) */
    *cms = t->masks ? t->cms : 2;
    *cmt = t->maskt ? t->cmt : 2;
    if (t->masks && (1 << t->masks) < tile_w) *cms = tex_expand(t->cms, t->masks, tile_w) ? 2 : (t->cms & 1);
    if (t->maskt && (1 << t->maskt) < tile_h) *cmt = tex_expand(t->cmt, t->maskt, tile_h) ? 2 : (t->cmt & 1);
}

static inline int tex_wrap_coord(int x, int mask, int mirror) {
    int m = (1 << mask) - 1, v = x & m;
    return (mirror && ((x >> mask) & 1)) ? m - v : v;
}

gfx_tex_entry_t *gfx_tex_get(const gfx_state_t *st, int tile_idx, unsigned *out_w, unsigned *out_h) {
    gfx_tex_miss_reason = 0;
    const gfx_tile_t *t = &st->tiles[tile_idx & 7];
    int tile_w = (int)((t->lrs - t->uls) >> 2) + 1;
    int tile_h = (int)((t->lrt - t->ult) >> 2) + 1;
    if (t->lrs < t->uls) tile_w = 0;
    if (t->lrt < t->ult) tile_h = 0;
    /* source size (mask-limited) and the size of the texture we upload */
    int w = tile_w, h = tile_h;
    if (t->masks && (w <= 0 || (1 << t->masks) < w)) w = 1 << t->masks;
    if (t->maskt && (h <= 0 || (1 << t->maskt) < h)) h = 1 << t->maskt;
    int expand_s = tex_expand(t->cms, t->masks, tile_w);
    int expand_t = tex_expand(t->cmt, t->maskt, tile_h);
    /* only when the tile origin is moved: from origin 0 the coordinates stay
     * inside the tile anyway (HUD icons wider than their image would pick up
     * neighbouring rows) */
    int grow_s = w > 0 && t->uls != 0 && tex_grow(t->cms, t->masks, tile_w);
    int grow_t = h > 0 && t->ult != 0 && tex_grow(t->cmt, t->maskt, tile_h);
    int ow = expand_s ? tile_w : grow_s ? (1 << t->masks) : w;
    int oh = expand_t ? tile_h : grow_t ? (1 << t->maskt) : h;
    if (w <= 0 || h <= 0 || w > 1024 || h > 1024) { gfx_tex_miss_reason = 1; return NULL; }

    const gfx_tmem_load_t *ld = find_load(st, t->tmem);
    if (!ld) { gfx_tex_miss_reason = 2; return NULL; }
    if (ld->addr < 0x00100000u) { gfx_tex_miss_reason = 3; return NULL; }

    uint32_t bits = 4u << t->siz;
    if ((grow_s || grow_t) && t->line) {
        /* decode only what TMEM holds: a row is `line` words, and the load
         * filled `words` from its own start */
        int row_texels = (int)((uint32_t)t->line * 64 / bits);
        int rows = (int)((ld->words - (t->tmem - ld->tmem)) / t->line);
        if (grow_s) w = row_texels < ow ? row_texels : ow;
        if (grow_t) h = rows < oh ? rows : oh;
        if (w <= 0 || h <= 0) { gfx_tex_miss_reason = 1; return NULL; }
    } else {
        grow_s = grow_t = 0;
        ow = expand_s ? tile_w : w;
        oh = expand_t ? tile_h : h;
    }
    uint32_t row_bytes = ((uint32_t)w * bits + 7) / 8;
    uint32_t src, stride;
    if (ld->is_tile) {
        uint32_t lbits = 4u << ld->img_siz;
        stride = ((uint32_t)ld->img_width * lbits) / 8;
        src = ld->addr + (ld->ult >> 2) * stride + ((ld->uls >> 2) * lbits) / 8
            + (uint32_t)(t->tmem - ld->tmem) * 8;
    } else {
        stride = t->line ? (uint32_t)t->line * 8 : row_bytes;
        src = ld->addr + (uint32_t)(t->tmem - ld->tmem) * 8;
    }
    if (stride < row_bytes) stride = row_bytes;

    /* key the palette by content: games rewrite palettes in place (fog
     * blends, flashing), so the source address alone goes stale */
    uint32_t tlut_key = 0;
    if (t->fmt == 2) {
        if (t->siz == 0) {
            tlut_key = st->tlut_hash[t->palette & 0xF];
        } else {
            tlut_key = 2166136261u;
            for (int b = 0; b < 16; b++) tlut_key = (tlut_key ^ st->tlut_hash[b]) * 16777619u;
        }
        tlut_key ^= (st->othermode_h & OMH_TT_MASK);
    }
    uint32_t sample = tex_src_is_static(src) ? 0 : tex_sample(src, stride, row_bytes, h);
    uint8_t key_pal = (uint8_t)(t->palette | ((st->othermode_h & OMH_TT_MASK) >> 10));
    /* an expanded texture differs by its baked-in mirroring */
    tlut_key ^= (uint32_t)((expand_s ? 1 : 0) | (expand_s && (t->cms & 1) ? 2 : 0) |
                           (expand_t ? 4 : 0) | (expand_t && (t->cmt & 1) ? 8 : 0) |
                           (grow_s ? 16 : 0) | (grow_t ? 32 : 0)) << 24;

#ifdef PM3DS_STEP_PROF
    pm3ds_prof_tex_lookups++;
#endif
    for (gfx_tex_entry_t *e = *bucket_of(src); e; e = e->hash_next) {
        tex_key_ext_t *x = ext_of(e);
#ifdef PM3DS_STEP_PROF
        pm3ds_prof_tex_chain++;
#endif
        if (e->key_addr == src && e->key_fmt == t->fmt && e->key_siz == t->siz &&
            e->key_w == ow && e->key_h == oh && e->key_palette == key_pal &&
            x->stride == stride && x->tlut == tlut_key && x->sample == sample) {
            /* the entries this frame has used lead the LRU list already (in
             * the order of first use), so only the first use moves one */
            uint32_t frame = pm3ds_gfx_frame_number();
            if (x->frame != frame) {
                cache_unlink(e);
                cache_push_mru(e);
                x->frame = frame;
            }
            *out_w = (unsigned)ow; *out_h = (unsigned)oh;
            return e->c3d_tex_inited ? e : NULL;
        }
    }

    /* miss: read rows, decode, swizzle, upload */
    uint8_t *row = (uint8_t *)malloc(stride);
    uint8_t *rgba = (uint8_t *)malloc((size_t)w * h * 4);
    if (!row || !rgba) { free(row); free(rgba); return NULL; }
    for (int y = 0; y < h; y++) {
        pm3ds_pi_read(src + (uint32_t)y * stride, row, row_bytes);
        decode_row(st, t, row, rgba + (size_t)y * w * 4, w);
    }
    free(row);
    if (expand_s || expand_t || grow_s || grow_t) {
        uint8_t *big = (uint8_t *)malloc((size_t)ow * oh * 4);
        if (!big) { free(rgba); return NULL; }
        for (int y = 0; y < oh; y++) {
            int sy = expand_t ? tex_wrap_coord(y, t->maskt, t->cmt & 1) : y;
            for (int x = 0; x < ow; x++) {
                int sx = expand_s ? tex_wrap_coord(x, t->masks, t->cms & 1) : x;
                uint8_t *d = big + ((size_t)y * ow + x) * 4;
                if (sx < w && sy < h) memcpy(d, rgba + ((size_t)sy * w + sx) * 4, 4);
                else memset(d, 0, 4);   /* past the loaded data (grow) */
            }
        }
        free(rgba);
        rgba = big;
        w = ow;
        h = oh;
    }

    unsigned pw = pm3ds_tex_pot((unsigned)w), ph = pm3ds_tex_pot((unsigned)h);
    int gpu_fmt = gpu_format_for(st, t);
    size_t tiled_bytes = (size_t)pw * ph * (gpu_fmt == GPU_RGBA8 ? 4 : 2);
    uint8_t *tiled = (uint8_t *)PM3DS_TEX_ALLOC(tiled_bytes);
    if (!tiled) { free(rgba); return NULL; }
    if (gpu_fmt == GPU_RGBA8) gfx_tex_swizzle_to_pica_padded(rgba, tiled, w, h, (int)pw, (int)ph);
    else                      swizzle_pack16(rgba, (uint16_t *)tiled, w, h, (int)pw, (int)ph, gpu_fmt);
    free(rgba);

    g_upload_fmt = gpu_fmt;
    gfx_tex_entry_t *e = gfx_tex_upload(src, t->fmt, t->siz, (uint16_t)w, (uint16_t)h, key_pal, tiled, tiled_bytes);
    g_upload_fmt = GPU_RGBA8;
    if (!e) return NULL;
    tex_key_ext_t *x = ext_of(e);
    x->stride = stride;
    x->tlut = tlut_key;
    x->sample = sample;
    if (stride * e->key_h > g_max_span) g_max_span = stride * e->key_h;
    x->frame = pm3ds_gfx_frame_number();
    *out_w = (unsigned)w; *out_h = (unsigned)h;
    return e->c3d_tex_inited ? e : NULL;
}

void gfx_tex_bind_unit(gfx_tex_entry_t *e, int unit, int filter_linear, int cms, int cmt) {
    if (!e || !e->c3d_tex_inited) return;
    GPU_TEXTURE_FILTER_PARAM f = filter_linear ? GPU_LINEAR : GPU_NEAREST;
    C3D_TexSetFilter(&e->c3d_tex, f, f);
    GPU_TEXTURE_WRAP_PARAM ws = (cms & 2) ? GPU_CLAMP_TO_EDGE : (cms & 1) ? GPU_MIRRORED_REPEAT : GPU_REPEAT;
    GPU_TEXTURE_WRAP_PARAM wt = (cmt & 2) ? GPU_CLAMP_TO_EDGE : (cmt & 1) ? GPU_MIRRORED_REPEAT : GPU_REPEAT;
    C3D_TexSetWrap(&e->c3d_tex, ws, wt);
    extern void pm3ds_tex_bind(int unit, C3D_Tex *t);
    pm3ds_tex_bind(unit, &e->c3d_tex);
}
#endif
