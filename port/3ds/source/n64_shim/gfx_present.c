/*
 * gfx_present.c — PICA200 backend for the F3DEX2 interpreter (M12 rewrite).
 *
 * Owns the render target, the vertex shader, the per-frame vertex buffer
 * and all GPU render state. The translator (gfx_translate.c) hands it
 * transformed triangles and screen rectangles; this file decodes the RDP
 * state that governs them (cycle type, render mode -> depth/blend/alpha
 * compare/fog, scissor, combiner, textures) and batches draws until that
 * state changes.
 *
 * Coordinates: vertices arrive as homogeneous N64 PIXEL coordinates
 * (x 0..320, y 0..240 y-down, z = clip z, w = clip w). The shader's fixed
 * `screen` matrix pillarboxes that into the 400x240 top screen, applies the
 * 90-degree framebuffer rotation, and maps N64 z/w in [-1,1] to the PICA
 * depth range [-1,0] (near -> -1).
 */

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <3ds.h>
#include <citro3d.h>
#include "vshader_shbin.h"

#include "pm3ds_log.h"
#include "gfx_interp.h"
#include "pm3ds_settings.h"
#include "gfx_state.h"

unsigned pm3ds_gfx_task_count(void);
extern const void *pm3ds_gfx_drain_dl_list(const void **out, int max, int *count);

/* othermode_l */
#define RM_AC_MASK       0x3u
#define RM_AC_THRESHOLD  0x1u
#define RM_ZS_PRIM       0x4u
#define RM_Z_CMP         0x10u
#define RM_Z_UPD         0x20u
#define RM_ZMODE_DEC     0xC00u
#define RM_CVG_X_ALPHA   0x1000u
#define RM_ALPHA_CVG_SEL 0x2000u
#define RM_FORCE_BL      0x4000u
/* othermode_h */
#define OMH_CYC_MASK   (3u << 20)
#define OMH_CYC_2      (1u << 20)
#define OMH_CYC_COPY   (2u << 20)
#define OMH_CYC_FILL   (3u << 20)
#define OMH_TF_MASK    (3u << 12)
#define OMH_TF_POINT   0u
/* geometry mode */
#define GM_ZBUFFER 0x1u
#define GM_FOG     0x10000u

#define SCREEN_X_OFFSET 40.0f       /* (400 - 320) / 2 pillarbox */
#define PM3DS_VIEW_ORIGINAL 0       /* pixel-exact 320x240, pillarboxed */
#define PM3DS_VIEW_FILL     1       /* TV-safe box scaled 1.2 to full height */
#define PM3DS_VIEW_WIDE     2       /* Fill + the world drawn to the screen edges */

typedef struct {
    float x, y, z, w;
    uint8_t r, g, b, a;
    float u0, v0, u1, v1;
} gpu_vtx_t;

#define FRAME_MAX_VERTS (96 * 1024)

static C3D_RenderTarget *g_target, *g_target_right;
static DVLB_s          *g_dvlb;
static shaderProgram_s  g_prog;
static int              g_uloc_screen = -1;
/* NOISE combiner input: a random texture on unit 1 (when TEXEL1 is unused),
 * addressed by the vertex's N64 pixel position (one texel per pixel) plus an
 * offset that moves every frame. Texture coordinates come from the CPU: a
 * texcoord2 output from the shader hung the GPU on real hardware. */
static C3D_Tex          g_noise_tex;
static bool             g_noise_ok;
static bool             g_noise_uv;       /* the open batch samples noise on unit 1 */
static float            g_noise_ofs_u, g_noise_ofs_v;
static C3D_Mtx          g_screen_mtx;

/* N64 pixel (x, y) -> top-screen pixel (s*x + ox, s*y + oy). Paper Mario only
 * draws inside the 296x200 TV-safe box (12..308, 20..220) of its 320x240
 * frame, so "fill" scales that box by 1.2 to the full 240-pixel height;
 * "original" is the pixel-exact 320x240 image pillarboxed into 400. */
static float g_view_s = 1.0f, g_view_ox = SCREEN_X_OFFSET, g_view_oy = 0.0f;
static int   g_view_mode;   /* PM3DS_VIEW_* */

int pm3ds_view_mode(void) { return g_view_mode; }

/* Wide = Fill's 1.2 scale, plus the 3D world shown past the 4:3 edge: the
 * full-width scissor is opened up and full-width rectangles (clears,
 * backgrounds, fades) are stretched to the screen edges with matching
 * texture coordinates. The game draws whole maps, so the extra width is
 * real scenery; things culled by the game near the edges may pop in. */
void pm3ds_set_view_mode(int mode) {
    if (mode < PM3DS_VIEW_ORIGINAL || mode > PM3DS_VIEW_WIDE) mode = PM3DS_VIEW_WIDE;
    g_view_mode = mode;
    bool fill = mode != PM3DS_VIEW_ORIGINAL;
    g_view_s  = fill ? 1.2f : 1.0f;
    g_view_ox = 200.0f - 160.0f * g_view_s;
    g_view_oy = 120.0f - 120.0f * g_view_s;
    /* pixel-space homogeneous -> tilted PICA clip space (see header). Rows
     * act on (x, y, z, w), with screen x' = s*x + ox*w, y' = s*y + oy*w:
     *   out.x = w - y'/120          (screen y 0..240 -> fb x)
     *   out.y = w - x'/200          (screen x 0..400)
     *   out.z = 0.5 z - 0.5 w       (N64 [-1,1] -> PICA [-1,0], near -> -1)
     */
    Mtx_Zeros(&g_screen_mtx);
    g_screen_mtx.r[0].y = -g_view_s / 120.0f; g_screen_mtx.r[0].w = 1.0f - g_view_oy / 120.0f;
    g_screen_mtx.r[1].x = -g_view_s / 200.0f; g_screen_mtx.r[1].w = 1.0f - g_view_ox / 200.0f;
    g_screen_mtx.r[2].z = 0.5f;               g_screen_mtx.r[2].w = -0.5f;
    g_screen_mtx.r[3].w = 1.0f;
}
static C3D_AttrInfo     g_attr;
static C3D_BufInfo      g_buf;
static gpu_vtx_t       *g_vbuf;
static int              g_vcount;       /* vertices written this frame */
static int              g_batch_start;  /* first vertex of the open batch */
static C3D_FogLut       g_fog_lut;
static int16_t          g_fog_lut_mul = 0x7FFF, g_fog_lut_ofs = 0x7FFF;

/* Texture parameters the open batch was set up with (for UV generation). */
static struct {
    bool on;
    const gfx_tile_t *tile;
    float w, h;          /* POT-padded GPU texture size */
    float off_s, off_t;  /* texel origin of the tile in that texture (window texture) */
} g_tex[2];
static int  g_applied_tile = -1;
static bool g_applied_rect;
static bool g_state_valid;
static bool g_draw_enabled;

static gfx_state_t g_st;
static bool        g_st_inited;
static uint32_t    g_frame_no;

/* draw_box window contents (render-to-texture). One texture, reused by
 * every window in turn: the GPU runs the draws in order, and rebinding the
 * texture clears its cache. N64 pixel (x, y) -> texel (x, y), row 0 at v = 0
 * like every uploaded texture. */
#define RTT_W 512
#define RTT_H 256
static C3D_Tex           g_rtt_tex;
static C3D_RenderTarget *g_rtt_target;
static C3D_Mtx           g_rtt_mtx;
static bool              g_in_rtt;
static uint32_t          g_rtt_addr, g_rtt_stride, g_rtt_bytes;   /* the image it stands for */
static C3D_RenderTarget *g_cur_target;                            /* the eye being drawn */

/* per-frame stats */
static unsigned g_stat_draws, g_stat_verts;
extern unsigned pm3ds_tstat_ops, pm3ds_tstat_tris, pm3ds_tstat_rects, pm3ds_tstat_culled;

void pm3ds_gfx_present_init(void) {
    g_target = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    C3D_RenderTargetSetOutput(g_target, GFX_TOP, GFX_LEFT,
                              GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) |
                              GX_TRANSFER_RAW_COPY(0) |
                              GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                              GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |
                              GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));

    /* right-eye target for the 3D slider (only drawn while it is up) */
    g_target_right = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    C3D_RenderTargetSetOutput(g_target_right, GFX_TOP, GFX_RIGHT,
                              GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) |
                              GX_TRANSFER_RAW_COPY(0) |
                              GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                              GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |
                              GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));

    g_dvlb = DVLB_ParseFile((u32 *)vshader_shbin, vshader_shbin_size);
    shaderProgramInit(&g_prog);
    shaderProgramSetVsh(&g_prog, &g_dvlb->DVLE[0]);
    C3D_BindProgram(&g_prog);
    g_uloc_screen = shaderInstanceGetUniformLocation(g_prog.vertexShader, "screen");
    /* 64x64 random intensities, one texel per N64 pixel, like the RDP's
     * per-pixel noise */
    if (C3D_TexInit(&g_noise_tex, 64, 64, GPU_L8)) {
        uint8_t *px = (uint8_t *)linearAlloc(64 * 64);
        if (px) {
            uint32_t r = 0x12345678u;
            for (int i = 0; i < 64 * 64; i++) { r = r * 1664525u + 1013904223u; px[i] = (uint8_t)(r >> 24); }
            C3D_TexUpload(&g_noise_tex, px);   /* random in any texel order: no swizzle needed */
            linearFree(px);
            C3D_TexSetFilter(&g_noise_tex, GPU_NEAREST, GPU_NEAREST);
            C3D_TexSetWrap(&g_noise_tex, GPU_REPEAT, GPU_REPEAT);
            g_noise_ok = true;
        }
    }

    AttrInfo_Init(&g_attr);
    AttrInfo_AddLoader(&g_attr, 0, GPU_FLOAT, 4);           /* position */
    AttrInfo_AddLoader(&g_attr, 1, GPU_UNSIGNED_BYTE, 4);   /* color    */
    AttrInfo_AddLoader(&g_attr, 2, GPU_FLOAT, 2);           /* uv0      */
    AttrInfo_AddLoader(&g_attr, 3, GPU_FLOAT, 2);           /* uv1      */

    g_vbuf = (gpu_vtx_t *)linearAlloc(sizeof(gpu_vtx_t) * FRAME_MAX_VERTS);
    BufInfo_Init(&g_buf);
    BufInfo_Add(&g_buf, g_vbuf, sizeof(gpu_vtx_t), 4, 0x3210);

    pm3ds_set_view_mode(PM3DS_VIEW_WIDE);

    if (C3D_TexInitVRAM(&g_rtt_tex, RTT_W, RTT_H, GPU_RGBA8)) {
        g_rtt_target = C3D_RenderTargetCreateFromTex(&g_rtt_tex, GPU_TEXFACE_2D, 0, (GPU_DEPTHBUF)-1);
        if (!g_rtt_target) C3D_TexDelete(&g_rtt_tex);
    }
    if (!g_rtt_target) pm3ds_log(PM3DS_LOG_WARN, "gfx: no window texture; rotating windows draw without contents");
    /* pixel space -> texture target: x' = 2x/W - 1, y' = 2y/H - 1 (texture
     * row 0, which v = 0 samples, is at the bottom of a render target) */
    Mtx_Zeros(&g_rtt_mtx);
    g_rtt_mtx.r[0].x = 2.0f / RTT_W;  g_rtt_mtx.r[0].w = -1.0f;
    g_rtt_mtx.r[1].y = 2.0f / RTT_H;  g_rtt_mtx.r[1].w = -1.0f;
    g_rtt_mtx.r[2].z = 0.5f;          g_rtt_mtx.r[2].w = -0.5f;
    g_rtt_mtx.r[3].w = 1.0f;

    C3D_CullFace(GPU_CULL_NONE);   /* culling is done on the CPU in N64 terms */
    C3D_DepthMap(true, -1.0f, 0.0f);
}

/* ---- render state --------------------------------------------------------- */

static void set_scissor(const gfx_state_t *st) {
    if (g_in_rtt) {   /* texel space: framebuffer y = N64 y */
        int x0 = st->scissor.ulx >> 2, y0 = st->scissor.uly >> 2;
        int x1 = (st->scissor.lrx + 3) >> 2, y1 = (st->scissor.lry + 3) >> 2;
        if (x1 > RTT_W) x1 = RTT_W;
        if (y1 > RTT_H) y1 = RTT_H;
        if (x1 <= x0 || y1 <= y0) { x0 = y0 = 0; x1 = y1 = 1; }
        C3D_SetScissor(GPU_SCISSOR_NORMAL, (u32)x0, (u32)y0, (u32)x1, (u32)y1);
        return;
    }
    float ulx = st->scissor.ulx / 4.0f, uly = st->scissor.uly / 4.0f;
    float lrx = st->scissor.lrx / 4.0f, lry = st->scissor.lry / 4.0f;
    if (lrx > 320.0f) lrx = 320.0f;
    if (lry > 240.0f) lry = 240.0f;
    int left   = (int)(240.0f - (g_view_s * lry + g_view_oy)), right  = (int)(240.0f - (g_view_s * uly + g_view_oy));
    int top    = (int)(400.0f - (g_view_s * lrx + g_view_ox)), bottom = (int)(400.0f - (g_view_s * ulx + g_view_ox));
    if (g_view_mode == PM3DS_VIEW_WIDE && ulx <= 12.5f && lrx >= 307.5f) { top = 0; bottom = 400; }
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > 240) right = 240;
    if (bottom > 400) bottom = 400;
    if (right <= left || bottom <= top) { left = top = 0; right = bottom = 1; }
    C3D_SetScissor(GPU_SCISSOR_NORMAL, (u32)left, (u32)top, (u32)right, (u32)bottom);
}

static void update_fog_lut(const gfx_state_t *st) {
    if (st->fog_mul == g_fog_lut_mul && st->fog_offset == g_fog_lut_ofs) return;
    g_fog_lut_mul = st->fog_mul;
    g_fog_lut_ofs = st->fog_offset;
    /* depth d in [0,1] with near = 1 (C3D_DepthMap(-1, 0) of z in [-1,0]);
     * N64 z/w = 1 - 2d. PICA fog: out = mix(fog, color, lut) -> lut = 1 - fog. */
    float data[256];
    float vals[129];
    for (int i = 0; i <= 128; i++) {
        float d = i / 128.0f;
        float zn = 1.0f - 2.0f * d;
        float f = (zn * st->fog_mul + st->fog_offset) / 255.0f;
        if (f < 0.0f) f = 0.0f;
        if (f > 1.0f) f = 1.0f;
        vals[i] = 1.0f - f;
    }
    for (int i = 0; i < 128; i++) { data[i] = vals[i]; data[128 + i] = vals[i + 1] - vals[i]; }
    FogLut_FromArray(&g_fog_lut, data);
}

#ifdef PM3DS_DRAWLOG_FRAME
/* frame whose draws are logged (DRAW / BIGDRAW / BIGTRI); PM3DS_MAGENTA_HUNT
 * moves it to the frame after a magenta-tinted one */
uint32_t pm3ds_drawlog_frame = PM3DS_DRAWLOG_FRAME;
#endif

static uint32_t rgba8(const uint8_t c[4]) {
    return (uint32_t)c[0] | ((uint32_t)c[1] << 8) | ((uint32_t)c[2] << 16) | ((uint32_t)c[3] << 24);
}

/* The colour combiner's final alpha with every texel and the shade fully
 * opaque: how far constants (prim/env alpha) scale the surface down. */
static float combined_alpha_max(const gfx_state_t *st, bool two_cycle) {
    uint32_t w0 = st->combine_w0, w1 = st->combine_w1;
    float prim = st->prim[3] / 255.0f, env = st->env[3] / 255.0f, plod = st->prim_lod_frac / 255.0f;
    float comb = 0.0f;
    for (int c = 0; c < (two_cycle ? 2 : 1); c++) {
        uint32_t a = c ? (w1 >> 21) & 7 : (w0 >> 12) & 7, b = c ? (w1 >> 3) & 7 : (w1 >> 12) & 7;
        uint32_t m = c ? (w1 >> 18) & 7 : (w0 >> 9) & 7, d = c ? w1 & 7 : (w1 >> 9) & 7;
        /* a/b/d: COMBINED TEXEL0 TEXEL1 PRIM SHADE ENV 1 0; c: LOD_FRAC .. PRIM_LOD_FRAC 0 */
        const float abd[8] = { comb, 1.0f, 1.0f, prim, 1.0f, env, 1.0f, 0.0f };
        const float cin[8] = { 0.0f, 1.0f, 1.0f, prim, 1.0f, env, plod, 0.0f };
        comb = (abd[a] - abd[b]) * cin[m] + abd[d];
        if (comb < 0.0f) comb = 0.0f;
        if (comb > 1.0f) comb = 1.0f;
    }
    return comb;
}

static void apply_state(gfx_state_t *st, bool rect) {
    uint32_t l = st->othermode_l;
    uint32_t cyc = st->othermode_h & OMH_CYC_MASK;
    gfx_cc_info_t info = { false, false, false };
#ifdef PM3DS_DRAWLOG_FRAME
    if (g_frame_no == pm3ds_drawlog_frame) {
        const gfx_tile_t *t = &st->tiles[st->tex_tile & 7];
        pm3ds_log(PM3DS_LOG_INFO, "DRAW rect=%d cc=%06X:%08X omh=%08X oml=%08X prim=%02X%02X%02X%02X env=%02X%02X%02X%02X fog=%02X%02X%02X%02X gm=%08X tex=%u/%u timg=%08X",
                  rect, (unsigned)st->combine_w0, (unsigned)st->combine_w1, (unsigned)st->othermode_h, (unsigned)l,
                  st->prim[0], st->prim[1], st->prim[2], st->prim[3], st->env[0], st->env[1], st->env[2], st->env[3],
                  st->fog[0], st->fog[1], st->fog[2], st->fog[3], (unsigned)st->geometry_mode, t->fmt, t->siz,
                  (unsigned)st->timg_addr);
    }
#endif

    /* --- combiner --- */
    if (cyc == OMH_CYC_FILL) {
        uint32_t c16 = st->fill_color >> 16;
        uint8_t r = (uint8_t)(((c16 >> 11) & 0x1F) * 255 / 31);
        uint8_t g = (uint8_t)(((c16 >> 6) & 0x1F) * 255 / 31);
        uint8_t b = (uint8_t)(((c16 >> 1) & 0x1F) * 255 / 31);
        C3D_TexEnv *env = C3D_GetTexEnv(0);
        C3D_TexEnvInit(env);
        C3D_TexEnvSrc(env, C3D_Both, GPU_CONSTANT, 0, 0);
        C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
        /* the alpha bit only matters in a window texture (its clear is transparent) */
        uint32_t a = (!g_in_rtt || (c16 & 1)) ? 0xFF000000u : 0;
        C3D_TexEnvColor(env, (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | a);
        for (int i = 1; i < 6; i++) C3D_TexEnvInit(C3D_GetTexEnv(i));
    } else if (cyc == OMH_CYC_COPY) {
        C3D_TexEnv *env = C3D_GetTexEnv(0);
        C3D_TexEnvInit(env);
        C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, 0, 0);
        C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
        for (int i = 1; i < 6; i++) C3D_TexEnvInit(C3D_GetTexEnv(i));
        info.uses_tex0 = true;
    } else {
        gfx_combiner_apply(st, cyc == OMH_CYC_2, &info);
    }

    /* --- textures --- */
    bool linear = cyc != OMH_CYC_COPY && (st->othermode_h & OMH_TF_MASK) != OMH_TF_POINT;
    for (int u = 0; u < 2; u++) {
        g_tex[u].on = false;
        bool want = u == 0 ? info.uses_tex0 : info.uses_tex1;
        if (!want) continue;
        int tile = (st->tex_tile + u) & 7;
        g_tex[u].off_s = g_tex[u].off_t = 0.0f;
        {   /* a load from the window texture's image: sample the GPU texture */
            uint32_t src;
            int col, row;
            if (g_rtt_bytes && gfx_tex_load_source(st, tile, &src, &col, &row) && src - g_rtt_addr < g_rtt_bytes) {
                uint32_t off = src - g_rtt_addr;
                GPU_TEXTURE_FILTER_PARAM f = linear ? GPU_LINEAR : GPU_NEAREST;
                C3D_TexSetFilter(&g_rtt_tex, f, f);
                C3D_TexSetWrap(&g_rtt_tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
                C3D_TexBind(u, &g_rtt_tex);
                g_tex[u].on = true;
                g_tex[u].tile = &st->tiles[tile];
                g_tex[u].w = RTT_W;
                g_tex[u].h = RTT_H;
                g_tex[u].off_s = (float)(col + (int)(off % g_rtt_stride) / 2);
                g_tex[u].off_t = (float)(row + (int)(off / g_rtt_stride));
                continue;
            }
        }
        unsigned w = 0, h = 0;
        gfx_tex_entry_t *e = gfx_tex_get(st, tile, &w, &h);
        if (!e) {
#ifdef PM3DS_TITLE_DBG
            extern int gfx_tex_miss_reason;
            static uint32_t seen[32][3];
            static int nseen;
            const gfx_tile_t *mt = &st->tiles[tile];
            uint32_t k[3] = { st->combine_w0 ^ st->combine_w1, (uint32_t)gfx_tex_miss_reason << 24 | mt->tmem,
                              (uint32_t)mt->fmt << 8 | mt->siz };
            int i;
            for (i = 0; i < nseen; i++) if (!memcmp(seen[i], k, sizeof(k))) break;
            if (i == nseen && nseen < 32) {
                memcpy(seen[nseen++], k, sizeof(k));
                pm3ds_log(PM3DS_LOG_WARN, "TEXMISS unit=%d tile=%d reason=%d fmt=%u siz=%u tmem=%u uls=%u ult=%u lrs=%u lrt=%u cc=%06X:%08X",
                          u, tile, gfx_tex_miss_reason, mt->fmt, mt->siz, mt->tmem, mt->uls, mt->ult, mt->lrs, mt->lrt,
                          (unsigned)st->combine_w0, (unsigned)st->combine_w1);
            }
#endif
            continue;
        }
        const gfx_tile_t *t = &st->tiles[tile];
        {   /* mask smaller than the tile: see gfx_tex_wrap_modes */
            extern void gfx_tex_wrap_modes(const gfx_tile_t *t, int *cms, int *cmt);
            int cms, cmt;
            gfx_tex_wrap_modes(t, &cms, &cmt);
            gfx_tex_bind_unit(e, u, linear, cms, cmt);
        }
        g_tex[u].on = true;
        g_tex[u].tile = t;
        g_tex[u].w = (float)pm3ds_tex_pot(w);
        g_tex[u].h = (float)pm3ds_tex_pot(h);
    }
    /* NOISE reads unit 1 when the combine mode leaves TEXEL1 unused (the
     * combiner maps it to a flat 0.5 otherwise) */
    g_noise_uv = info.uses_noise && !info.uses_tex1 && g_noise_ok;
    if (g_noise_uv) {
        static bool logged;
        if (!logged) {
            logged = true;
            pm3ds_log(PM3DS_LOG_INFO, "gfx: first NOISE draw (frame %u)", (unsigned)g_frame_no);
        }
        C3D_TexBind(1, &g_noise_tex);
    }
    g_applied_tile = st->tex_tile;
#ifdef PM3DS_TITLE_DBG
    {   /* one line per distinct render state that samples a texture */
        static uint32_t seen[64][4];
        static int nseen;
        if (g_tex[0].on && nseen < 64) {
            uint32_t k[4] = { st->combine_w0, st->combine_w1, st->othermode_h, st->othermode_l };
            int i;
            for (i = 0; i < nseen; i++) if (!memcmp(seen[i], k, sizeof(k))) break;
            if (i == nseen) {
                memcpy(seen[nseen++], k, sizeof(k));
                const gfx_tile_t *t = g_tex[0].tile;
                pm3ds_log(PM3DS_LOG_INFO, "RSTATE cc=%06X:%08X omh=%08X oml=%08X fmt=%u siz=%u gm=%08X rect=%d",
                          (unsigned)k[0], (unsigned)k[1], (unsigned)k[2], (unsigned)k[3], t->fmt, t->siz,
                          (unsigned)st->geometry_mode, rect);
            }
        }
    }
#endif

    /* --- depth --- */
    bool zcmp, zupd;
    if (cyc == OMH_CYC_FILL || cyc == OMH_CYC_COPY) {
        zcmp = zupd = false;
    } else if (rect) {
        zcmp = (l & RM_Z_CMP) && (l & RM_ZS_PRIM);
        zupd = (l & RM_Z_UPD) && (l & RM_ZS_PRIM);
    } else {
        bool zb = (st->geometry_mode & GM_ZBUFFER) != 0;
        zcmp = zb && (l & RM_Z_CMP);
        zupd = zb && (l & RM_Z_UPD);
    }
    /* blender formula MEM*A + MEM*B (or MEM without FORCE_BL): the colour
     * stays as it is and only depth is written -- the battle-start iris
     * draws its star into the z-buffer this way */
    /* the blender word that applies: the second cycle's in 2-cycle mode,
     * the first cycle's in 1-cycle mode (render modes like CLD_SURF + NOOP2
     * set only that one) */
    int bsh = cyc == OMH_CYC_2 ? 0 : 2;
    uint32_t p2_ = (l >> (28 + bsh)) & 3, m2_ = (l >> (20 + bsh)) & 3;
    bool keep_color = cyc != OMH_CYC_FILL && cyc != OMH_CYC_COPY && p2_ == 1 && (m2_ == 1 || !(l & RM_FORCE_BL));
    GPU_WRITEMASK wmask = keep_color ? (zupd ? GPU_WRITE_DEPTH : 0) : (zupd ? GPU_WRITE_ALL : GPU_WRITE_COLOR);
    if (g_in_rtt) C3D_DepthTest(false, GPU_ALWAYS, keep_color ? (GPU_WRITEMASK)0 : GPU_WRITE_COLOR);
    else C3D_DepthTest(zcmp || zupd || keep_color, zcmp ? GPU_GEQUAL : GPU_ALWAYS, wmask);
    /* decal: nudge toward the viewer so co-planar decals win */
    C3D_DepthMap(true, -1.0f, (l & RM_ZMODE_DEC) == RM_ZMODE_DEC ? 0.0002f : 0.0f);

    /* --- blend (the blender word that applies, see bsh) --- */
    uint32_t p2 = (l >> (28 + bsh)) & 3, a2 = (l >> (24 + bsh)) & 3, m2 = (l >> (20 + bsh)) & 3, b2 = (l >> (16 + bsh)) & 3;
    bool blend = false, additive = false;
    if (cyc != OMH_CYC_FILL && cyc != OMH_CYC_COPY && (l & RM_FORCE_BL) && p2 == 0 && m2 == 1) {
        if (b2 == 0 && a2 == 0) blend = true;          /* IN*A + MEM*(1-A) */
        else if (b2 == 2) { blend = true; additive = true; }  /* IN*A + MEM */
    }
    /* coverage x alpha with alpha as coverage (TEX_EDGE): the RDP writes any
     * pixel whose coverage (8 * alpha) is at least 1 and anti-aliases it
     * against memory. A plain 1/8 test plus blend reproduces that badly: soft
     * texture edges pick up the colour of neighbouring transparent texels and
     * write depth (fringes on foliage). So the cutoff stays at half the
     * texel alpha; only when a constant (prim/env alpha) scales the whole
     * surface down -- the Heart Block's heart at 72/255, which a fixed 0.5
     * cutoff dropped entirely, depth writes included -- is the cutoff scaled
     * with it and the surface blended. */
    float cvg_amax = 1.0f;
    bool cvg_surface = !blend && cyc != OMH_CYC_FILL && cyc != OMH_CYC_COPY &&
                       (l & RM_CVG_X_ALPHA) && (l & RM_ALPHA_CVG_SEL) && (l & RM_AC_MASK) != RM_AC_THRESHOLD;
    if (cvg_surface) cvg_amax = combined_alpha_max(st, cyc == OMH_CYC_2);
    bool cvg_blend = cvg_surface && cvg_amax < 0.99f;
    if (blend && additive) {
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE, GPU_SRC_ALPHA, GPU_ONE);
    } else if (blend || cvg_blend) {
        /* in a window texture the alpha left behind is what the window shows
         * through later: compose it with "over" */
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
                       g_in_rtt ? GPU_ONE : GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
    } else {
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
    }

    /* --- alpha compare --- */
    if ((l & RM_AC_MASK) == RM_AC_THRESHOLD) {
        int ref = st->blend[3];
        C3D_AlphaTest(true, ref ? GPU_GEQUAL : GPU_GREATER, ref);
    } else if (cvg_blend) {
        int ref = (int)(cvg_amax * 0x80);
        C3D_AlphaTest(true, GPU_GEQUAL, ref < 1 ? 1 : ref);
    } else if (cyc != OMH_CYC_FILL && !blend && (l & RM_CVG_X_ALPHA)) {
        /* coverage x alpha: a zero-alpha texel gets no coverage and is not
         * written (sprites use this without ALPHA_CVG_SEL) */
        C3D_AlphaTest(true, GPU_GEQUAL, 0x80);
    } else if (cyc == OMH_CYC_COPY) {
        C3D_AlphaTest(true, GPU_GREATER, 0);
    } else {
        C3D_AlphaTest(false, GPU_ALWAYS, 0);
    }

    /* --- fog (first-cycle blender = CLR_FOG with shade alpha) --- */
    if (!rect && (st->geometry_mode & GM_FOG) && ((l >> 30) & 3) == 3) {
        update_fog_lut(st);
        C3D_FogGasMode(GPU_FOG, GPU_PLAIN_DENSITY, false);
        C3D_FogColor(rgba8(st->fog) & 0x00FFFFFFu);
        C3D_FogLutBind(&g_fog_lut);
    } else {
        C3D_FogGasMode(GPU_NO_FOG, GPU_PLAIN_DENSITY, false);
    }

    set_scissor(st);
    g_applied_rect = rect;
    g_state_valid = true;
}

/* Close the open batch: one draw call for everything since g_batch_start. */
void gfx_backend_flush(gfx_state_t *st) {
    (void)st;
    int n = g_vcount - g_batch_start;
    if (n > 0) {
        C3D_DrawArrays(GPU_TRIANGLES, g_batch_start, n);
        g_stat_draws++;
    }
    g_batch_start = g_vcount;
}

static unsigned g_stat_offscreen;
static bool begin_prims(gfx_state_t *st, bool rect, int nverts) {
    if (!g_draw_enabled || !(st->cimg_is_screen || st->cimg_rtt)) { g_stat_offscreen++; return false; }
    if (g_vcount + nverts > FRAME_MAX_VERTS) {
        static int warned;
        if (!warned++) pm3ds_log(PM3DS_LOG_WARN, "gfx: frame vertex budget (%d) exhausted", FRAME_MAX_VERTS);
        return false;
    }
    if (st->dirty || !g_state_valid || rect != g_applied_rect || st->tex_tile != g_applied_tile) {
        gfx_backend_flush(st);
        apply_state(st, rect);
        st->dirty = 0;
    }
    return true;
}

/* N64 texel space -> normalized GPU texcoords for one texture unit. */
static inline void tex_uv(int unit, float s, float t, float *u, float *v) {
    if (!g_tex[unit].on) { *u = *v = 0.0f; return; }
    const gfx_tile_t *tl = g_tex[unit].tile;
    if (tl->shifts) s = tl->shifts <= 10 ? s / (float)(1 << tl->shifts) : s * (float)(1 << (16 - tl->shifts));
    if (tl->shiftt) t = tl->shiftt <= 10 ? t / (float)(1 << tl->shiftt) : t * (float)(1 << (16 - tl->shiftt));
    *u = (s / 32.0f - tl->uls / 4.0f + g_tex[unit].off_s) / g_tex[unit].w;
    *v = (t / 32.0f - tl->ult / 4.0f + g_tex[unit].off_t) / g_tex[unit].h;
}

static inline void put_vtx(const gfx_vtx_t *v) {
    gpu_vtx_t *o = &g_vbuf[g_vcount++];
    o->x = v->px; o->y = v->py; o->z = v->cz; o->w = v->cw;
    o->r = v->r; o->g = v->g; o->b = v->b; o->a = v->a;
    tex_uv(0, v->s, v->t, &o->u0, &o->v0);
    if (g_noise_uv) {   /* pixel position / 64 + this frame's offset */
        float iw = (v->cw > 1e-6f || v->cw < -1e-6f) ? 1.0f / v->cw : 0.0f;
        o->u1 = v->px * iw * (1.0f / 64.0f) + g_noise_ofs_u;
        o->v1 = v->py * iw * (1.0f / 64.0f) + g_noise_ofs_v;
    } else {
        tex_uv(1, v->s, v->t, &o->u1, &o->v1);
    }
}

void gfx_backend_tri(gfx_state_t *st, const gfx_vtx_t *a, const gfx_vtx_t *b, const gfx_vtx_t *c) {
    if (!begin_prims(st, false, 3)) return;
    if (st->othermode_l & RM_ZS_PRIM) {
        /* depth source = primitive: every pixel tests and writes the prim
         * depth (the battle "Nice"/"Good" text sits in front of the actors
         * this way); keep x/y, make z/w that constant */
        float z = (st->prim_depth & 0x7FFF) / 16383.5f - 1.0f;
        gfx_vtx_t v[3] = { *a, *b, *c };
        for (int i = 0; i < 3; i++) v[i].cz = z * v[i].cw;
        put_vtx(&v[0]); put_vtx(&v[1]); put_vtx(&v[2]);
        return;
    }
    put_vtx(a); put_vtx(b); put_vtx(c);
}

extern float pm3ds_stereo_shift;
static bool g_stereo_far;   /* walking the background list: put it at infinity */
#define PM3DS_STEREO_MAX_SHIFT 5.0f   /* pixels per eye at infinity, slider at max */

void gfx_backend_rect(gfx_state_t *st, float x0, float y0, float x1, float y1,
                      float s0, float t0, float s1, float t1, bool flip, bool textured) {
    if (!begin_prims(st, true, 6)) return;
    if (g_stereo_far && !g_in_rtt) { x0 += pm3ds_stereo_shift; x1 += pm3ds_stereo_shift; }
#ifdef PM3DS_DRAWLOG_FRAME
    if (g_frame_no == pm3ds_drawlog_frame && (x1 - x0) * (y1 - y0) > 20000) {
        const gfx_tile_t *t = &st->tiles[st->tex_tile & 7];
        pm3ds_log(PM3DS_LOG_INFO, "BIGDRAW rect %.0f,%.0f-%.0f,%.0f tex=%d fmt=%u siz=%u cc=%06X:%08X prim=%02X%02X%02X%02X oml=%08X omh=%08X timg=%08X",
                  x0, y0, x1, y1, textured, t->fmt, t->siz, (unsigned)st->combine_w0, (unsigned)st->combine_w1,
                  st->prim[0], st->prim[1], st->prim[2], st->prim[3], (unsigned)st->othermode_l, (unsigned)st->othermode_h,
                  (unsigned)st->timg_addr);
    }
#endif
    if (g_view_mode == PM3DS_VIEW_WIDE && !g_in_rtt && !st->bg_wide && x0 <= 12.5f && x1 >= 307.5f && x1 > x0) {
        /* full-width rectangle: carry it to the screen edges (the backdrop
         * does that itself, with its wrapped texels: gfx_translate.c) */
        float xl = -g_view_ox / g_view_s, xr = (400.0f - g_view_ox) / g_view_s;
        float nx0 = x0 < xl ? x0 : xl, nx1 = x1 > xr ? x1 : xr;
        if (textured) {
            if (!flip) { float d = (s1 - s0) / (x1 - x0); s0 += (nx0 - x0) * d; s1 += (nx1 - x1) * d; }
            else       { float d = (t1 - t0) / (x1 - x0); t0 += (nx0 - x0) * d; t1 += (nx1 - x1) * d; }
        }
        x0 = nx0; x1 = nx1;
    }
    float z = -1.0f;
    if (st->othermode_l & RM_ZS_PRIM) z = (st->prim_depth & 0x7FFF) / 16383.5f - 1.0f;
    gfx_vtx_t v[4];
    memset(v, 0, sizeof(v));
    const float xs[4] = { x0, x1, x1, x0 }, ys[4] = { y0, y0, y1, y1 };
    for (int i = 0; i < 4; i++) {
        v[i].px = xs[i]; v[i].py = ys[i]; v[i].cz = z; v[i].cw = 1.0f;
        v[i].r = v[i].g = v[i].b = v[i].a = 0xFF;
    }
    if (textured) {
        /* corners: 0 = (s0,t0) at upper-left; flip swaps the s/t axes */
        float ss[4], tt[4];
        if (!flip) { ss[0] = s0; tt[0] = t0; ss[1] = s1; tt[1] = t0; ss[2] = s1; tt[2] = t1; ss[3] = s0; tt[3] = t1; }
        else       { ss[0] = s0; tt[0] = t0; ss[1] = s0; tt[1] = t1; ss[2] = s1; tt[2] = t1; ss[3] = s1; tt[3] = t0; }
        for (int i = 0; i < 4; i++) { v[i].s = ss[i]; v[i].t = tt[i]; }
    }
    put_vtx(&v[0]); put_vtx(&v[1]); put_vtx(&v[2]);
    put_vtx(&v[0]); put_vtx(&v[2]); put_vtx(&v[3]);
}

/* ---- window texture -------------------------------------------------------- */

/* The whole texture to transparent black, drawn in the command stream (a GX
 * fill would run before the frame's draws, not between two windows). */
static void rtt_clear(void) {
    if (g_vcount + 6 > FRAME_MAX_VERTS) return;
    C3D_TexEnv *env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_Both, GPU_CONSTANT, 0, 0);
    C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
    C3D_TexEnvColor(env, 0);
    for (int i = 1; i < 6; i++) C3D_TexEnvInit(C3D_GetTexEnv(i));
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
    C3D_FogGasMode(GPU_NO_FOG, GPU_PLAIN_DENSITY, false);
    C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
    static const float xs[6] = { 0, RTT_W, RTT_W, 0, RTT_W, 0 }, ys[6] = { 0, 0, RTT_H, 0, RTT_H, RTT_H };
    for (int i = 0; i < 6; i++) {
        gpu_vtx_t *o = &g_vbuf[g_vcount++];
        memset(o, 0, sizeof(*o));
        o->x = xs[i]; o->y = ys[i]; o->z = -1.0f; o->w = 1.0f;
    }
    C3D_DrawArrays(GPU_TRIANGLES, g_batch_start, 6);
    g_batch_start = g_vcount;
    g_state_valid = false;
}

bool gfx_backend_rtt_begin(gfx_state_t *st, uint32_t addr, uint16_t width) {
    if (!g_rtt_target || !g_draw_enabled || width == 0 || width > RTT_W) return false;
    {
        static bool logged;
        if (!logged) {
            logged = true;
            pm3ds_log(PM3DS_LOG_INFO, "gfx: first window texture draw (frame %u)", (unsigned)g_frame_no);
        }
    }
    gfx_backend_flush(st);
    C3D_FrameDrawOn(g_rtt_target);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g_uloc_screen, &g_rtt_mtx);
    g_in_rtt = true;
    rtt_clear();
    g_rtt_addr = addr;
    g_rtt_stride = (uint32_t)width * 2u;
    g_rtt_bytes = g_rtt_stride * RTT_H;
    st->dirty = GFX_DIRTY_ALL;
    return true;
}

void gfx_backend_rtt_end(gfx_state_t *st) {
    gfx_backend_flush(st);
    C3D_FrameDrawOn(g_cur_target);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g_uloc_screen, &g_screen_mtx);
    g_in_rtt = false;
    g_state_valid = false;
    st->dirty = GFX_DIRTY_ALL;
}

bool pm3ds_gfx_rtt_available(void) { return g_rtt_target != NULL; }

/* ---- frame ------------------------------------------------------------------ */

#define MAX_DLS_PER_FRAME 8

extern uint64_t pm3ds_t_game, pm3ds_t_present, pm3ds_t_wait;
extern volatile const char *pm3ds_bc_main;   /* platform/pm3ds_netlog.c */
extern volatile unsigned pm3ds_bc_frames;
extern unsigned pm3ds_tstat_uploads;
extern char pm3ds_perf_text[3][64];
uint64_t pm3ds_t_audio;
unsigned pm3ds_tstat_uploads;

/* Pause backdrop: the game wants the current frame as 320x240 RGBA5551 in
 * N64 byte order (gfx_transfer_frame_to_depth). Copy the previous top-screen
 * image off the render target with the GX engine and convert the 4:3 area. */
static uint16_t *g_capture_dst;
void pm3ds_request_frame_capture(uint16_t *dst) { g_capture_dst = dst; }

static void capture_frame(uint16_t *dst) {
    uint32_t *tmp = (uint32_t *)linearAlloc(240 * 400 * 4);
    if (!tmp) { pm3ds_log(PM3DS_LOG_WARN, "capture: no linear memory for the frame copy (free %u)", (unsigned)linearSpaceFree()); return; }
    /* outside a frame: citro3d queues GX commands issued mid-frame until
     * C3D_FrameEnd, so use its synchronous transfer (waits for the GPU) */
    C3D_SyncDisplayTransfer((u32 *)g_target->frameBuf.colorBuf, GX_BUFFER_DIM(240, 400), tmp, GX_BUFFER_DIM(240, 400),
                            GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
                            GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                            GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    GSPGPU_InvalidateDataCache(tmp, 240 * 400 * 4);
    /* LCD layout: column-major, x = screen column, bottom row first */
    for (int y = 0; y < 240; y++) {
        int sy = (int)(g_view_s * y + g_view_oy);
        for (int x = 0; x < 320; x++) {
            int sx = (int)(g_view_s * x + g_view_ox);
            uint32_t p = (sx >= 0 && sx < 400 && sy >= 0 && sy < 240) ? tmp[sx * 240 + (239 - sy)] : 0x000000FFu;
            uint16_t v = (uint16_t)((((p >> 24) & 0xFF) >> 3) << 11 | (((p >> 16) & 0xFF) >> 3) << 6 |
                                    (((p >> 8) & 0xFF) >> 3) << 1 | 1);
            dst[y * 320 + x] = (uint16_t)((v >> 8) | (v << 8));
        }
    }
    /* the texture cache tells rewritten images apart by a few sampled bytes
     * per row; this frame's edges are often as black as the last one's, so
     * strips captured frames ago came back (torn, blocky battle-start wipe) */
    extern void gfx_tex_invalidate_range(const void *addr, size_t len);
    gfx_tex_invalidate_range(dst, 320 * 240 * sizeof(uint16_t));
    {
        uint64_t sum = 0;
        for (int i = 0; i < 240 * 400; i += 97) sum += (tmp[i] >> 8) & 0xFF;
        pm3ds_log(PM3DS_LOG_INFO, "CAPTURE frame -> %p, avg green %u, sample %08X",
                  (void *)dst, (unsigned)(sum / (240 * 400 / 97)), (unsigned)tmp[120 * 240 + 120]);
    }
    linearFree(tmp);
}

/* Translate and draw the frame's display lists (once per eye in stereo). */
static void render_lists(const void *const *lists, int nlists) {
#if defined(PM3DS_DRAWLOG_ALLTRI) && defined(PM3DS_DRAWLOG_FRAME)
    { extern int pm3ds_gfx_dbg_vtx; if (g_frame_no == pm3ds_drawlog_frame) pm3ds_gfx_dbg_vtx = 400; }
#endif
    uint64_t t0 = svcGetSystemTick();
    /* C3D_FRAME_SYNCDRAW in two steps, so a hang report can tell a missing
     * vblank from a GPU that never finished the previous frame */
    pm3ds_bc_main = "vblank wait";
    C3D_FrameSync();
    pm3ds_bc_main = "gpu wait";
    C3D_FrameBegin(0);
    pm3ds_t_wait += svcGetSystemTick() - t0;
    if (g_frame_no <= 5)
        pm3ds_log(PM3DS_LOG_INFO, "gfx: frame %u begins after %llu ms", (unsigned)g_frame_no,
                  (unsigned long long)((svcGetSystemTick() - t0) / (SYSCLOCK_ARM11 / 1000)));
    pm3ds_bc_main = "draw lists";
    C3D_RenderTargetClear(g_target, C3D_CLEAR_ALL, 0x000000FFu, 0);
    C3D_FrameDrawOn(g_target);
    g_cur_target = g_target;
    g_in_rtt = false;
    g_rtt_bytes = 0;
    C3D_BindProgram(&g_prog);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g_uloc_screen, &g_screen_mtx);
    {   /* a new noise pattern every frame */
        static uint32_t r = 0x9E3779B9u;
        r = r * 1664525u + 1013904223u;
        g_noise_ofs_u = (float)((r >> 8) & 63) / 64.0f;
        g_noise_ofs_v = (float)((r >> 16) & 63) / 64.0f;
    }
    C3D_SetAttrInfo(&g_attr);
    C3D_SetBufInfo(&g_buf);

    g_vcount = g_batch_start = 0;
    g_state_valid = false;
    g_draw_enabled = true;
    g_stat_draws = 0;
    pm3ds_tstat_ops = pm3ds_tstat_tris = pm3ds_tstat_rects = pm3ds_tstat_culled = 0;
    g_stat_offscreen = 0;

    { extern int pm3ds_gfx_dbg_vtx;
      (void)pm3ds_gfx_dbg_vtx; }
    /* 3D slider: walk the lists once per eye with a horizontal disparity
     * (see pm3ds_stereo_shift); the first list is the background, which is
     * pushed to infinity. Mono costs nothing extra. */
    float slider = osGet3DSliderState();
    bool stereo = slider > 0.02f;
    static bool stereo_on;
    if (stereo != stereo_on) {
        gfxSet3D(stereo);
        stereo_on = stereo;
    }
    for (int eye = 0; eye < (stereo ? 2 : 1); eye++) {
        if (eye == 1) {
            gfx_interp_second_eye();
            gfx_backend_flush(&g_st);
            C3D_RenderTargetClear(g_target_right, C3D_CLEAR_ALL, 0x000000FFu, 0);
            C3D_FrameDrawOn(g_target_right);
            g_cur_target = g_target_right;
            g_rtt_bytes = 0;
            g_state_valid = false;
        }
        pm3ds_stereo_shift = stereo ? (eye == 0 ? -1.0f : 1.0f) * slider * PM3DS_STEREO_MAX_SHIFT : 0.0f;
        for (int i = 0; i < nlists; i++) {
            g_st.cimg_is_screen = true;
            g_stereo_far = stereo && i == 0 && nlists > 1;
            g_st.bg_wide = g_view_mode == PM3DS_VIEW_WIDE && i == 0 && nlists > 1 &&
                           gfx_interp_bg_edges(&g_st.bg_left, &g_st.bg_right);
            g_st.bg_wide_l = -g_view_ox / g_view_s - 8.0f;            /* 8: room for the 3D shift */
            g_st.bg_wide_r = (400.0f - g_view_ox) / g_view_s + 8.0f;
            g_st.bg_shift = i == 0 && nlists > 1 ? gfx_interp_bg_shift(&g_st.bg_left, &g_st.bg_right) : 0.0f;
            gfx_walk_dl(&g_st, lists[i]);
            g_st.bg_shift = 0.0f;
            g_st.bg_wide = false;
            if (g_st.cimg_rtt) {   /* a list that ended inside a window texture */
                gfx_backend_rtt_end(&g_st);
                g_st.cimg_rtt = false;
            }
        }
        g_stereo_far = false;
    }
    pm3ds_stereo_shift = 0.0f;
    gfx_backend_flush(&g_st);
    g_stat_verts = (unsigned)g_vcount;

    pm3ds_bc_main = "bottom screen";
    {
        extern void pm3ds_bottom_screen_render(C3D_RenderTarget *, C3D_RenderTarget *);
        pm3ds_bottom_screen_render(g_target, stereo ? g_target_right : NULL);
    }
    pm3ds_bc_main = "frame end";
    C3D_FrameEnd(0);
    pm3ds_bc_main = "frame done";
}

/* loading frames drawn / skipped because the GPU was still busy (netlog) */
volatile unsigned pm3ds_load_frames_ok, pm3ds_load_frames_busy;

/* Boot: the loading panel on both screens while the ROM loads. At most ~10
 * a second, and never waits for the GPU or vsync: the load is what matters. */
void pm3ds_gfx_present_loading(int permille) {
    static uint64_t last;
    uint64_t now = svcGetSystemTick();
    if (last && now - last < SYSCLOCK_ARM11 / 10 && permille < 1000) return;
    if (!C3D_FrameBegin(C3D_FRAME_NONBLOCK)) {
        pm3ds_load_frames_busy++;
        return;
    }
    pm3ds_load_frames_ok++;
    last = now;
    C3D_RenderTargetClear(g_target, C3D_CLEAR_ALL, 0x000000FFu, 0);
    { extern void pm3ds_ui_loading(C3D_RenderTarget *, int); pm3ds_ui_loading(g_target, permille); }
    C3D_FrameEnd(0);
}

/* Boot problems (no ROM...): the message on both screens */
void pm3ds_gfx_present_message(const char *const *lines, int n) {
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C3D_RenderTargetClear(g_target, C3D_CLEAR_ALL, 0x000000FFu, 0);
    { extern void pm3ds_ui_message(C3D_RenderTarget *, const char *const *, int); pm3ds_ui_message(g_target, lines, n); }
    C3D_FrameEnd(0);
}

static uint32_t g_shown;   /* frames put on screen (60 a second with interpolation) */

void pm3ds_gfx_present_frame(void) {
    g_frame_no++;
    pm3ds_bc_frames = g_frame_no;
    if (!g_st_inited) { gfx_state_init(&g_st); g_st_inited = true; }

#ifdef PM3DS_CAPTURE_TEST
    if (g_frame_no % 600 == 0) {
        static uint16_t test_buf[320 * 240];
        capture_frame(test_buf);
    }
#endif
#if defined(PM3DS_MAGENTA_HUNT) && defined(PM3DS_DRAWLOG_FRAME)
    {   /* debug: read the previous frame back; if it is tinted magenta, log
         * every draw of this one */
        static int hits;
        uint32_t *tmp = hits < 2 ? (uint32_t *)linearAlloc(240 * 400 * 4) : NULL;
        if (tmp) {
            C3D_SyncDisplayTransfer((u32 *)g_target->frameBuf.colorBuf, GX_BUFFER_DIM(240, 400), tmp, GX_BUFFER_DIM(240, 400),
                                    GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
                                    GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                                    GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
            GSPGPU_InvalidateDataCache(tmp, 240 * 400 * 4);
            uint32_t r = 0, g = 0, b = 0, n = 0;
            for (int x = 60; x < 340; x += 7) {
                for (int y = 20; y < 220; y += 7, n++) {
                    uint32_t px = tmp[x * 240 + y];
                    r += px >> 24; g += (px >> 16) & 0xFF; b += (px >> 8) & 0xFF;
                }
            }
            r /= n; g /= n; b /= n;
            if (r > g + 40 && b > g + 40) {
                hits++;
                pm3ds_drawlog_frame = g_frame_no;
                pm3ds_log(PM3DS_LOG_WARN, "MAGENTA frame %u avg %u,%u,%u: logging this frame's draws",
                          (unsigned)g_frame_no, (unsigned)r, (unsigned)g, (unsigned)b);
            }
            linearFree(tmp);
        }
    }
#endif
    if (g_capture_dst) {   /* the previous frame is still on the target */
        capture_frame(g_capture_dst);
        g_capture_dst = NULL;
    }
    /* All display lists the game submitted since the last frame, in order
     * (background list, then the main list). With nothing new (loading,
     * DISABLE_DRAW_FRAME) the screen keeps the last frame: only keep the
     * 30 Hz pace. */
    static const void *last[MAX_DLS_PER_FRAME];
    static int nlast;
    const void *now[MAX_DLS_PER_FRAME];
    int n = 0;
    pm3ds_gfx_drain_dl_list(now, MAX_DLS_PER_FRAME, &n);
    if (n > 0) { memcpy(last, now, sizeof(now[0]) * n); nlast = n; }
    bool render = n > 0 || nlast == 0;
    /* 60 fps: one game step spans two frames. New lists are drawn twice,
     * first half way from the last step's matrices (gfx_interp.c), then as
     * they are. Stereo stays at 30 (four walks a step would not fit). */
    bool fps60 = pm3ds_fps60;
    static float rate;
    if (rate != (fps60 ? 60.0f : 30.0f)) {
        rate = fps60 ? 60.0f : 30.0f;
        C3D_FrameRate(rate);
    }
    int frames = fps60 ? 2 : 1;
    if (render) {
        bool interp = fps60 && n > 0 && osGet3DSliderState() <= 0.02f && !pm3ds_fast_forward();
#ifdef PM3DS_INTERP_ONLY   /* test: show only the in-between frames (pairing errors stay on screen) */
        if (interp) {
            gfx_interp_begin_walk(0.5f, true);
            render_lists(last, nlast);
            g_shown++;
            frames--;
            gfx_interp_end_step();
            goto drawn;
        }
#endif
        if (interp) {
            gfx_interp_begin_walk(0.5f, false);
            render_lists(last, nlast);
            g_shown++;
            frames--;
        }
        gfx_interp_begin_walk(1.0f, n > 0);
        render_lists(last, nlast);
        g_shown++;
        frames--;
        if (n > 0) gfx_interp_end_step();
    }
#ifdef PM3DS_INTERP_ONLY
drawn:
#endif
    while (frames-- > 0) {   /* keep the 30 Hz step: the screen holds its frame */
        uint64_t t0 = svcGetSystemTick();
        pm3ds_bc_main = "frame sync";
        C3D_FrameSync();
        pm3ds_t_wait += svcGetSystemTick() - t0;
    }

#ifdef PM3DS_TITLE_DBG
    if ((g_frame_no % 300) == 0) {
        pm3ds_log(PM3DS_LOG_INFO, "GFXSTAT f=%lu dls=%d ops=%u tris=%u culled=%u offscr=%u rects=%u verts=%u draws=%u",
                  (unsigned long)g_frame_no, nlast, pm3ds_tstat_ops, pm3ds_tstat_tris, pm3ds_tstat_culled,
                  g_stat_offscreen, pm3ds_tstat_rects, g_stat_verts, g_stat_draws);
    }
#endif
    { extern void pm3ds_game_debug_tick(uint32_t); pm3ds_game_debug_tick(g_frame_no); }
    { extern void pm3ds_bottom_screen_tick(unsigned); pm3ds_bottom_screen_tick(g_frame_no); }
    /* once a second: measured frame rate and where the CPU time went
     * (game = gfxRetrace_Callback incl. step+draw list build, render = DL
     * translation + GPU submit + vsync wait), for hardware reports */
    {
        static uint64_t last_tick;
        static uint32_t last_frame;
        uint64_t now = svcGetSystemTick();
        if (last_tick == 0) {
            last_tick = now;
            last_frame = g_frame_no;
        } else if (now - last_tick >= SYSCLOCK_ARM11) {
            double secs = (double)(now - last_tick) / SYSCLOCK_ARM11;
            uint32_t frames = g_frame_no - last_frame;
            static uint32_t last_shown;
            double fps = (g_shown - last_shown) / secs;
            last_shown = g_shown;
            double ms_game = frames ? (double)pm3ds_t_game * 1000.0 / SYSCLOCK_ARM11 / frames : 0.0;
            double ms_wait = frames ? (double)pm3ds_t_wait * 1000.0 / SYSCLOCK_ARM11 / frames : 0.0;
            double ms_rend = frames ? (double)pm3ds_t_present * 1000.0 / SYSCLOCK_ARM11 / frames - ms_wait : 0.0;
            snprintf(pm3ds_perf_text[0], sizeof pm3ds_perf_text[0], "FPS %4.1f/%d  game %4.1fms  render %4.1fms", fps, pm3ds_fps60 ? 60 : 30, ms_game, ms_rend);
            snprintf(pm3ds_perf_text[1], sizeof pm3ds_perf_text[1], "idle %4.1fms  tris %u  draws %u", ms_wait, pm3ds_tstat_tris, g_stat_draws);
            {   /* audio CPU per second of wall time, as a share of one core:
                 * the DSP work on the mixer's core, the engine on the game's */
                extern uint64_t pm3ds_t_audio, pm3ds_t_audio_engine;
                double aud = (double)pm3ds_t_audio * 100.0 / SYSCLOCK_ARM11 / secs;
                double eng = (double)pm3ds_t_audio_engine * 100.0 / SYSCLOCK_ARM11 / secs;
                extern int s_audio_core;
                snprintf(pm3ds_perf_text[2], sizeof pm3ds_perf_text[2], "tex %u/s  audio %.1f%% core%d  eng %.1f%%",
                         pm3ds_tstat_uploads, aud, s_audio_core, eng);
                pm3ds_log(PM3DS_LOG_INFO, "PERF2 audio=%.1f%% engine=%.1f%% up=%u", aud, eng, pm3ds_tstat_uploads);
                pm3ds_t_audio = pm3ds_t_audio_engine = 0;
            }
            pm3ds_log(PM3DS_LOG_INFO, "PERF fps=%.1f game=%.2fms render=%.2fms idle=%.2fms tris=%u draws=%u up=%u",
                      fps, ms_game, ms_rend, ms_wait, pm3ds_tstat_tris, g_stat_draws, pm3ds_tstat_uploads);
            pm3ds_tstat_uploads = 0;
            last_tick = now;
            last_frame = g_frame_no;
            pm3ds_t_game = pm3ds_t_present = pm3ds_t_wait = 0;
        }
    }
}

void pm3ds_gfx_present_shutdown(void) {
    if (g_vbuf) { linearFree(g_vbuf); g_vbuf = NULL; }
    shaderProgramFree(&g_prog);
    if (g_dvlb) { DVLB_Free(g_dvlb); g_dvlb = NULL; }
    if (g_target) { C3D_RenderTargetDelete(g_target); g_target = NULL; }
}

uint32_t pm3ds_gfx_frame_number(void) { return g_frame_no; }

uint64_t pm3ds_t_game, pm3ds_t_present, pm3ds_t_wait;

/* for the bottom screen's Debug page and its own draw pass */
char pm3ds_perf_text[3][64];
shaderProgram_s *pm3ds_gfx_program(void) { return &g_prog; }
int pm3ds_gfx_uloc_screen(void) { return g_uloc_screen; }
