#ifndef PM3DS_GFX_STATE_H
#define PM3DS_GFX_STATE_H
/*
 * gfx_state.h — F3DEX2/RDP state mirror shared by the translator
 * (gfx_translate.c), the GPU backend (gfx_present.c), the combiner compiler
 * (gfx_combiner.c) and the texture importer (gfx_texture.c).
 *
 * M12 rewrite: the RSP vertex pipeline runs on the CPU (fast3d-style).
 * Vertices are transformed by the full modelview x projection, lit, fogged
 * and handed to the GPU as homogeneous coordinates in N64 *pixel* space
 * (x = 0..320, y = 0..240, y down); one fixed shader matrix maps that onto
 * the rotated 400x240 top screen and the PICA depth range.
 */
#include <stdint.h>
#include <stdbool.h>

/*
 * PICA200 texture dimensions must be a power of two in [8, 1024]
 * (citro3d checkTexSize). N64 tiles routinely violate both rules, so
 * textures are padded up to a legal size and UVs are normalised by the
 * PADDED size. The importer and the UV path both use this helper.
 */
static inline unsigned pm3ds_tex_pot(unsigned v) {
    unsigned p = 8;
    if (v > 1024u) return 1024u;
    while (p < v) p <<= 1;
    return p;
}

#define GFX_VTX_POOL_SIZE   64     /* F3DEX2 has 32; headroom for odd loaders */
#define GFX_MTX_STACK_DEPTH 32
#define GFX_NUM_TILES       8
#define GFX_MAX_LIGHTS      8      /* directional lights + ambient slot */
#define GFX_TMEM_LOADS      16     /* remembered LOADBLOCK/LOADTILE records */

/* Tile descriptor (G_SETTILE / G_SETTILESIZE). */
typedef struct {
    uint8_t  fmt, siz;
    uint16_t line;       /* row stride in 64-bit TMEM words */
    uint16_t tmem;       /* TMEM address in 64-bit words */
    uint8_t  palette;
    uint8_t  cms, cmt, masks, maskt, shifts, shiftt;
    uint16_t uls, ult, lrs, lrt;  /* 10.2 fixed */
} gfx_tile_t;

/* One remembered TMEM load: where the bytes came from and how. */
typedef struct {
    uint16_t tmem;        /* destination TMEM word address */
    uint16_t words;       /* 64-bit words written */
    uint32_t addr;        /* source image address (SETTIMG at load time) */
    uint8_t  img_siz;     /* SETTIMG siz at load time */
    uint16_t img_width;   /* SETTIMG width at load time (texels of img_siz) */
    uint8_t  is_tile;     /* 1 = LOADTILE (2D rect), 0 = LOADBLOCK */
    uint16_t uls, ult;    /* LOADTILE origin, 10.2 */
    uint32_t serial;      /* load order, newest wins */
} gfx_tmem_load_t;

/* Transformed vertex (after G_VTX). */
typedef struct {
    float cx, cy, cz, cw;  /* clip space (N64 convention, y up) */
    float px, py;          /* homogeneous N64 pixel x,y (multiply of cw) */
    float s, t;            /* texcoords after G_TEXTURE scale, s10.5 units */
    uint8_t r, g, b, a;    /* shade */
    uint32_t serial;       /* new on every load/change: the backend reuses its GPU copy until then */
} gfx_vtx_t;

typedef struct {
    uint8_t col[3];
    int8_t  dir[3];
} gfx_light_t;

typedef struct gfx_state {
    /* --- RSP ------------------------------------------------------------ */
    float    mv[GFX_MTX_STACK_DEPTH][4][4];
    int      mv_sp;                 /* index of the current modelview */
    float    proj[4][4];
    float    mvp[4][4];
    bool     mvp_valid;

    uint32_t geometry_mode;
    int16_t  vp_scale[4], vp_trans[4];   /* Vp, units of 1/4 pixel */

    gfx_light_t lights[GFX_MAX_LIGHTS];  /* [0..num_lights-1] directional, [num_lights] ambient */
    int      num_lights;
    float    light_dir_model[GFX_MAX_LIGHTS][3];
    bool     lights_valid;
    int8_t   lookat_dir[2][3];
    float    lookat_model[2][3];

    int16_t  fog_mul, fog_offset;

    uint16_t tex_scale_s, tex_scale_t;
    uint8_t  tex_tile;              /* render tile from G_TEXTURE */
    uint8_t  tex_on;

    gfx_vtx_t vtx[GFX_VTX_POOL_SIZE];

    uint32_t segments[16];

    /* --- RDP ------------------------------------------------------------ */
    uint32_t othermode_h, othermode_l;
    uint32_t combine_w0, combine_w1;   /* raw G_SETCOMBINE (w0 without opcode) */
    uint8_t  prim[4], env[4], blend[4], fog[4];
    uint8_t  prim_lod_frac;
    uint8_t  key_center[3], key_scale[3];   /* SETKEYR/GB: combiner CENTER and SCALE */
    int16_t  k4, k5;                        /* SETCONVERT: combiner K4 and K5 */
    uint16_t prim_depth;
    uint32_t fill_color;

    struct { uint16_t ulx, uly, lrx, lry; } scissor;   /* 10.2 */

    uint32_t timg_addr;
    uint8_t  timg_fmt, timg_siz;
    uint16_t timg_width;

    gfx_tile_t tiles[GFX_NUM_TILES];
    gfx_tmem_load_t loads[GFX_TMEM_LOADS];
    uint32_t load_serial;

    uint8_t  tlut[512];             /* 256 RGBA16/IA16 entries, raw big-endian */
    uint32_t tlut_src[16];          /* source address per 16-entry bank (cache key) */
    uint32_t tlut_hash[16];         /* content hash per bank, kept by G_LOADTLUT (texture cache key) */

    uint32_t cimg_addr, zimg_addr;
    uint16_t cimg_width;
    bool     cimg_is_screen;        /* false while rendering into a texture/zbuffer */
    bool     cimg_shading_pal;      /* target is SpriteShadingPalette: emulate on CPU */
    float    bg_shift;              /* 60 fps: shift the sky backdrop's rectangles (px), 0 = off */
    float    bg_left, bg_right;     /* the backdrop's screen edges */
    bool     bg_wide;               /* Wide view: carry the backdrop's edge strips on to... */
    float    bg_wide_l, bg_wide_r;  /* ...these x (N64 pixels), with its wrapped texels */
    bool     cimg_rtt;              /* drawing a window's contents into the GPU texture */
    bool     box_rtt_tag;           /* PM3DS_GFX_TAG_BOX_RTT seen: the next SETCIMG is one */
    bool     screen_copy_tag;       /* PM3DS_GFX_TAG_SCREEN_COPY seen: the next SETCIMG is one */

    /* --- bookkeeping ---------------------------------------------------- */
    uint32_t dirty;                 /* GFX_DIRTY_* for the backend */
} gfx_state_t;

enum {
    GFX_DIRTY_COMBINE = 1u << 0,
    GFX_DIRTY_MODE    = 1u << 1,   /* othermode, geometry cull, scissor */
    GFX_DIRTY_TEXTURE = 1u << 2,
    GFX_DIRTY_COLORS  = 1u << 3,
    GFX_DIRTY_FOG     = 1u << 4,
    GFX_DIRTY_ALL     = 0xFFFFu,
};

/* --- translator (gfx_translate.c) --------------------------------------- */
void gfx_state_init(gfx_state_t *st);
int  gfx_walk_dl(gfx_state_t *st, const void *dl);
uint32_t gfx_seg_resolve(const gfx_state_t *st, uint32_t addr);

/* --- backend (gfx_present.c) --------------------------------------------- */
/* Emit one triangle (indices into st->vtx) / a screen rect; the backend
 * batches until render state changes. */
void gfx_backend_tri(gfx_state_t *st, const gfx_vtx_t *a, const gfx_vtx_t *b, const gfx_vtx_t *c);
void gfx_backend_rect(gfx_state_t *st, float x0, float y0, float x1, float y1,
                      float s0, float t0, float s1, float t1, bool flip, bool textured);
void gfx_backend_flush(gfx_state_t *st);
/* draw_box's rotating windows: contents go into a GPU texture between
 * begin and end; texture loads from `addr` then sample it. begin returns
 * false (draws stay dropped) when there is no such texture. */
bool gfx_backend_rtt_begin(gfx_state_t *st, uint32_t addr, uint16_t width);
void gfx_backend_rtt_end(gfx_state_t *st);
void gfx_backend_screen_copy(gfx_state_t *st, uint32_t addr, uint16_t width);

/* --- textures (gfx_texture.c) --------------------------------------------- */
typedef struct gfx_tex_entry gfx_tex_entry_t;
/* Import (or find cached) the texture that `tile` currently describes.
 * Returns NULL if nothing sensible is loaded. out_w and out_h receive the
 * LOGICAL size; the GPU texture is pm3ds_tex_pot() of each. */
gfx_tex_entry_t *gfx_tex_get(const gfx_state_t *st, int tile, unsigned *out_w, unsigned *out_h);
void gfx_tex_bind_unit(gfx_tex_entry_t *e, int unit, int filter_linear, int cms, int cmt);
/* Where the TMEM contents `tile` reads came from: the source image address
 * and the texel column/row of the loaded rectangle's origin in it. */
bool gfx_tex_load_source(const gfx_state_t *st, int tile, uint32_t *addr, int *col, int *row);
void gfx_tlut_rehash(gfx_state_t *st, int bank);
void gfx_tex_cache_reset(void);

/* --- combiner (gfx_combiner.c) -------------------------------------------- */
typedef struct {
    bool uses_tex0, uses_tex1;
    bool uses_noise;     /* NOISE input: bind the random texture to unit 1 */
} gfx_cc_info_t;
/* Program the PICA TexEnv stages for the current combine mode. */
void gfx_combiner_apply(const gfx_state_t *st, bool two_cycle, gfx_cc_info_t *info);
void gfx_combiner_invalidate(void);   /* the TexEnv stages were set elsewhere */
void gfx_combiner_info(const gfx_state_t *st, bool two_cycle, gfx_cc_info_t *info);

#endif /* PM3DS_GFX_STATE_H */
