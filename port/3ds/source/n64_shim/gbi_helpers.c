/*
 * gbi_helpers.c — out-of-line GBI macro bodies.
 *
 * Some F3DEX2 build-side macros have many call sites in the decomp
 * (gSPDisplayList alone: ~198). Inlining the full Gfx-write at each
 * site bloated the binary by 6KB+ which Azahar's loader rejected
 * with "Error while loading App!". Routing those macros through a
 * single extern function keeps each call site cheap (just a `bl`)
 * and the total code growth tiny.
 */

#include <stdint.h>

typedef struct {
    unsigned int w0;
    unsigned int w1;
} Gwords;

typedef union {
    Gwords words;
    long long int force_structure_alignment;
} Gfx;

/* F3DEX2 G_DL = 0xde. Branch flag in the next nibble:
 *   0x00 = push current DL, continue at dl, return after dl ends
 *   0x01 = branch — replace current DL with dl, no return
 */

void pm3ds_gSPDisplayList_impl(void *pkt, const void *dl) {
    Gfx *g = (Gfx *)pkt;
    g->words.w0 = 0xde000000U;
    g->words.w1 = (unsigned int)(uintptr_t)dl;
}

void pm3ds_gSPBranchList_impl(void *pkt, const void *dl) {
    Gfx *g = (Gfx *)pkt;
    g->words.w0 = 0xde010000U;
    g->words.w1 = (unsigned int)(uintptr_t)dl;
}

/* M8.4: gSPMatrix has ~395 call sites. F3DEX2 layout:
 *   w0 = G_MTX<<24 | ((sizeof(Mtx)-1)/8)<<19 | 0<<8 | (p ^ G_MTX_PUSH)
 *   w1 = mtx ptr
 * G_MTX = 0xDA, sizeof(Mtx)=64 → ((64-1)/8)=7, <<19 = 0x380000.
 * w0 = 0xDA000000 | 0x00380000 | flags = 0xDA380000 | flags. */
void pm3ds_gSPMatrix_impl(void *pkt, const void *m, unsigned int p) {
    Gfx *g = (Gfx *)pkt;
    g->words.w0 = 0xDA380000U | (p ^ 1U); /* ^ G_MTX_PUSH (bit 0) */
    g->words.w1 = (unsigned int)(uintptr_t)m;
}

/* M8.4b: F3DEX2 G_VTX layout (~250 call sites):
 *   w0 = G_VTX<<24 | ((n)<<12) | ((v0+n)<<1)
 *   w1 = vtx ptr
 * G_VTX = 0x01. */
void pm3ds_gSPVertex_impl(void *pkt, const void *v, unsigned int n, unsigned int v0) {
    Gfx *g = (Gfx *)pkt;
    g->words.w0 = 0x01000000U | ((n & 0xFFU) << 12) | (((v0 + n) & 0x7FU) << 1);
    g->words.w1 = (unsigned int)(uintptr_t)v;
}

/* G_TRI1 = 0x05. w0 = G_TRI1<<24 | (v0*2<<16) | (v1*2<<8) | (v2*2). */
void pm3ds_gSP1Triangle_impl(void *pkt, unsigned int v0, unsigned int v1, unsigned int v2) {
    Gfx *g = (Gfx *)pkt;
    g->words.w0 = 0x05000000U | (((v0 * 2U) & 0xFFU) << 16) |
                                  (((v1 * 2U) & 0xFFU) << 8)  |
                                  ((v2 * 2U) & 0xFFU);
    g->words.w1 = 0;
}

/* G_TRI2 = 0x06. */
void pm3ds_gSP2Triangles_impl(void *pkt,
                              unsigned int v00, unsigned int v01, unsigned int v02,
                              unsigned int v10, unsigned int v11, unsigned int v12) {
    Gfx *g = (Gfx *)pkt;
    g->words.w0 = 0x06000000U | (((v00 * 2U) & 0xFFU) << 16) |
                                  (((v01 * 2U) & 0xFFU) << 8)  |
                                  ((v02 * 2U) & 0xFFU);
    g->words.w1 = (((v10 * 2U) & 0xFFU) << 16) |
                  (((v11 * 2U) & 0xFFU) << 8)  |
                  ((v12 * 2U) & 0xFFU);
}

/* M8.4d batch: F3DEX2 state-setter helpers. Each emits the 64-bit
 * wire format the translator dispatches on. Out-of-line because
 * call-site counts are high (97..299).
 *
 * Generic 8-byte write — used by helpers that don't need bit
 * twiddling beyond what the call site can constant-fold. */
static inline void gwrite(Gfx *g, unsigned int w0, unsigned int w1) {
    g->words.w0 = w0; g->words.w1 = w1;
}

/* G_POPMTX = 0xD8. F3DEX2: w0=op<<24|0x380002, w1=n*64. */
void pm3ds_gSPPopMatrix_impl(void *pkt, unsigned int n) {
    gwrite((Gfx *)pkt, 0xD8380002U, n * 64U);
}

/* G_SETPRIMCOLOR = 0xFA. w0=op<<24|(min<<8)|lod, w1=RGBA. */
void pm3ds_gDPSetPrimColor_impl(void *pkt, unsigned int min_lod, unsigned int lod,
                                unsigned int r, unsigned int g, unsigned int b, unsigned int a) {
    gwrite((Gfx *)pkt,
        0xFA000000U | ((min_lod & 0xFFU) << 8) | (lod & 0xFFU),
        ((r & 0xFFU) << 24) | ((g & 0xFFU) << 16) | ((b & 0xFFU) << 8) | (a & 0xFFU));
}

/* G_SETENVCOLOR = 0xFB. */
void pm3ds_gDPSetEnvColor_impl(void *pkt,
                               unsigned int r, unsigned int g, unsigned int b, unsigned int a) {
    gwrite((Gfx *)pkt, 0xFB000000U,
        ((r & 0xFFU) << 24) | ((g & 0xFFU) << 16) | ((b & 0xFFU) << 8) | (a & 0xFFU));
}

/* G_SETCOMBINE = 0xFC. Game passes a precomputed 64-bit combine value. */
void pm3ds_gDPSetCombine_impl(void *pkt, unsigned int w0_lo, unsigned int w1) {
    gwrite((Gfx *)pkt, 0xFC000000U | (w0_lo & 0x00FFFFFFU), w1);
}

/* G_SETOTHERMODE_L = 0xE2. F3DEX2 layout differs per call. */
void pm3ds_gSPSetOtherModeL_impl(void *pkt,
                                 unsigned int shift, unsigned int length, unsigned int data) {
    /* F3DEX2: w0 = op<<24 | ((32-shift-length)<<8) | (length-1)
     * Translator does its own decoding from w0/w1 so just pass through. */
    gwrite((Gfx *)pkt,
        0xE2000000U | (((32U - shift - length) & 0xFFU) << 8) | ((length - 1U) & 0xFFU),
        data);
}

/* G_MOVEWORD = 0xDB. gSPSegment uses index G_MW_SEGMENT=0x06, offset=seg*4. */
void pm3ds_gSPSegment_impl(void *pkt, unsigned int seg, unsigned int base) {
    gwrite((Gfx *)pkt,
        0xDB000000U | (0x06U << 16) | (((seg & 0xFU) * 4U) & 0xFFFFU),
        base);
}

/* G_SETTILESIZE = 0xF2. w0 = op<<24 | (uls<<12) | ult,
 *                       w1 = (tile<<24) | (lrs<<12) | lrt. */
void pm3ds_gDPSetTileSize_impl(void *pkt, unsigned int tile,
                               unsigned int uls, unsigned int ult,
                               unsigned int lrs, unsigned int lrt) {
    gwrite((Gfx *)pkt,
        0xF2000000U | (((uls & 0xFFFU)) << 12) | (ult & 0xFFFU),
        ((tile & 0x7U) << 24) | ((lrs & 0xFFFU) << 12) | (lrt & 0xFFFU));
}

/* G_SETSCISSOR = 0xED. */
void pm3ds_gDPSetScissor_impl(void *pkt, unsigned int mode,
                              unsigned int ulx, unsigned int uly,
                              unsigned int lrx, unsigned int lry) {
    gwrite((Gfx *)pkt,
        0xED000000U | (((ulx * 4U) & 0xFFFU) << 12) | ((uly * 4U) & 0xFFFU),
        ((mode & 0x3U) << 24) | (((lrx * 4U) & 0xFFFU) << 12) | ((lry * 4U) & 0xFFFU));
}

/* G_SETTILE = 0xF5. */
void pm3ds_gDPSetTile_impl(void *pkt,
                           unsigned int fmt, unsigned int siz,
                           unsigned int line, unsigned int tmem,
                           unsigned int tile, unsigned int palette,
                           unsigned int cmt, unsigned int maskt, unsigned int shiftt,
                           unsigned int cms, unsigned int masks, unsigned int shifts) {
    gwrite((Gfx *)pkt,
        0xF5000000U | ((fmt & 7U) << 21) | ((siz & 3U) << 19) |
        ((line & 0x1FFU) << 9) | (tmem & 0x1FFU),
        ((tile & 7U) << 24) | ((palette & 0xFU) << 20) |
        ((cmt & 3U) << 18) | ((maskt & 0xFU) << 14) | ((shiftt & 0xFU) << 10) |
        ((cms & 3U) << 8)  | ((masks & 0xFU) << 4)  | (shifts & 0xFU));
}

/* G_TEXTURE = 0xD7. gSPTexture(pkt, s, t, level, tile, on). */
void pm3ds_gSPTexture_impl(void *pkt,
                           unsigned int s, unsigned int t,
                           unsigned int level, unsigned int tile, unsigned int on) {
    gwrite((Gfx *)pkt,
        0xD7000000U | (0U /*BOWTIE*/) | ((level & 7U) << 11) | ((tile & 7U) << 8) | (on & 0xFFU),
        ((s & 0xFFFFU) << 16) | (t & 0xFFFFU));
}

/* G_TEXRECT = 0xE4.
 * M8.8: takes the three target slots EXPLICITLY. The old single-pkt
 * form wrote g+0..g+2 while the call-site macro only evaluated
 * `gMainGfxPos++` once — so the very next DL command overwrote the
 * RDPHALF words and handle_texrect bailed on its opcode check (the
 * "texrects dispatch but draw nothing" symptom). The macro in
 * port/3ds/include/PR/gbi.h now evaluates (pkt) three times, exactly
 * like the real GBI's three-statement expansion. */
void pm3ds_gSPTextureRectangle_impl(void *pkt0, void *pkt1, void *pkt2,
                                    unsigned int xl, unsigned int yl,
                                    unsigned int xh, unsigned int yh,
                                    unsigned int tile,
                                    unsigned int s, unsigned int t,
                                    unsigned int dsdx, unsigned int dtdy) {
    /* TEXRECT is a 3-Gfx sequence: setup, G_RDPHALF_1 (s/t), G_RDPHALF_2 (dsdx/dtdy). */
    gwrite((Gfx *)pkt0,
        0xE4000000U | (((xh & 0xFFFU)) << 12) | (yh & 0xFFFU),
        ((tile & 7U) << 24) | (((xl & 0xFFFU)) << 12) | (yl & 0xFFFU));
    gwrite((Gfx *)pkt1, 0xE1000000U, ((s & 0xFFFFU) << 16) | (t & 0xFFFFU));
    gwrite((Gfx *)pkt2, 0xF1000000U, ((dsdx & 0xFFFFU) << 16) | (dtdy & 0xFFFFU));
}

/* gSPScisTextureRectangle — the SCISSORED texrect (include/PR/gbi.h:4503).
 *
 * This used to be #define'd straight to gSPTextureRectangle, dropping the
 * clamping entirely, and that was a real bug with a very confusing symptom.
 *
 * The title logo slides in from below: uly = (TITLE_POS_TOP + 2*i + yOffset)<<2
 * with yOffset = TITLE_LOGO_YOFFSET(-100) * moveAlpha, so during the slide the
 * y coordinates are NEGATIVE. The unscissored emitter masked them into the
 * 12-bit field, turning e.g. -340 into 3756 — i.e. screen y ~939 on a 240-tall
 * screen. Instead of being clipped at the top of the screen, every strip was
 * flung far below it. Measured on device: "RECTDBG screen (60,950)-(260,952)".
 *
 * The real macro clamps the rect corners to >= 0 and, because clamping xl/yl
 * moves the rect's origin, compensates the s/t texture coordinates by the
 * amount that was clipped away (scaled by dsdx/dtdy >> 7). The MIN/MAX pair
 * picks the correct direction for negative texture steps. Replicated exactly. */
static int pm3ds_scis_adjust(int lo, int d) {
    /* Texture-coordinate compensation for the clipped-away part of the rect. */
    if (lo >= 0) return 0;
    int v = (lo * d) >> 7;
    return (d < 0) ? (v > 0 ? v : 0)      /* MAX(v, 0) */
                   : (v < 0 ? v : 0);     /* MIN(v, 0) */
}

void pm3ds_gSPScisTextureRectangle_impl(void *pkt0, void *pkt1, void *pkt2,
                                        unsigned int xl, unsigned int yl,
                                        unsigned int xh, unsigned int yh,
                                        unsigned int tile,
                                        unsigned int s, unsigned int t,
                                        unsigned int dsdx, unsigned int dtdy) {
    /* The decomp compares these as s16 before clamping. */
    int sxl = (short)xl, syl = (short)yl;
    int sxh = (short)xh, syh = (short)yh;
    int sdsdx = (short)dsdx, sdtdy = (short)dtdy;

    int cxl = sxl > 0 ? sxl : 0;
    int cyl = syl > 0 ? syl : 0;
    int cxh = sxh > 0 ? sxh : 0;
    int cyh = syh > 0 ? syh : 0;

    unsigned int s_adj = (unsigned int)((int)(short)s - pm3ds_scis_adjust(sxl, sdsdx));
    unsigned int t_adj = (unsigned int)((int)(short)t - pm3ds_scis_adjust(syl, sdtdy));

    gwrite((Gfx *)pkt0,
        0xE4000000U | ((((unsigned)cxh) & 0xFFFU) << 12) | (((unsigned)cyh) & 0xFFFU),
        ((tile & 7U) << 24) | ((((unsigned)cxl) & 0xFFFU) << 12) | (((unsigned)cyl) & 0xFFFU));
    gwrite((Gfx *)pkt1, 0xE1000000U, ((s_adj & 0xFFFFU) << 16) | (t_adj & 0xFFFFU));
    gwrite((Gfx *)pkt2, 0xF1000000U, ((dsdx & 0xFFFFU) << 16) | (dtdy & 0xFFFFU));
}

/* G_GEOMETRYMODE = 0xD9 with set/clear masks. */
void pm3ds_gSPSetGeometryMode_impl(void *pkt, unsigned int word) {
    gwrite((Gfx *)pkt, 0xD9000000U, word);
}
void pm3ds_gSPClearGeometryMode_impl(void *pkt, unsigned int word) {
    gwrite((Gfx *)pkt, 0xD9000000U | ((~word) & 0x00FFFFFFU), 0U);
}

/* G_SETCIMG = 0xFF.  gDPSetColorImage(pkt, fmt, siz, width, img). */
void pm3ds_gDPSetColorImage_impl(void *pkt,
                                 unsigned int fmt, unsigned int siz,
                                 unsigned int width, const void *img) {
    gwrite((Gfx *)pkt,
        0xFF000000U | ((fmt & 7U) << 21) | ((siz & 3U) << 19) | (((width - 1U)) & 0xFFFU),
        (unsigned int)(uintptr_t)img);
}

/* G_SETTIMG = 0xFD — same field layout as SETCIMG but a different
 * opcode. M8.8: gDPSetTextureImage used to route to the 0xFF impl
 * above, so texture addresses never reached handle_settimg and
 * current_image_addr stayed stale for every texture decode. */
void pm3ds_gDPSetTextureImage_impl(void *pkt,
                                   unsigned int fmt, unsigned int siz,
                                   unsigned int width, const void *img) {
    gwrite((Gfx *)pkt,
        0xFD000000U | ((fmt & 7U) << 21) | ((siz & 3U) << 19) | (((width - 1U)) & 0xFFFU),
        (unsigned int)(uintptr_t)img);
}

/* G_LOADTLUT = 0xF0. */
void pm3ds_gDPSetTextureLUT_impl(void *pkt, unsigned int lut_mode) {
    /* gDPSetTextureLUT is actually G_RDPSETOTHERMODE_H bits. F3DEX2: SetOtherModeH. */
    gwrite((Gfx *)pkt,
        0xE3000000U | (((32U - 14U - 2U) & 0xFFU) << 8) | ((2U - 1U) & 0xFFU),
        (lut_mode & 0x3U) << 14);
}

/* gDPSetCycleType: G_SETOTHERMODE_H bit field. */
void pm3ds_gDPSetCycleType_impl(void *pkt, unsigned int type) {
    gwrite((Gfx *)pkt,
        0xE3000000U | (((32U - 20U - 2U) & 0xFFU) << 8) | ((2U - 1U) & 0xFFU),
        (type & 0x3U) << 20);
}

/* G_SETFILLCOLOR = 0xF7. w1 = packed RGBA5551 in both halves. */
void pm3ds_gDPSetFillColor_impl(void *pkt, unsigned int color) {
    gwrite((Gfx *)pkt, 0xF7000000U, color);
}

/* G_FILLRECT = 0xF6. handle_fillrect decodes the same layout. */
void pm3ds_gDPFillRectangle_impl(void *pkt,
                                 unsigned int ulx, unsigned int uly,
                                 unsigned int lrx, unsigned int lry) {
    gwrite((Gfx *)pkt,
        0xF6000000U | ((lrx & 0x3FFU) << 14) | ((lry & 0x3FFU) << 2),
        ((ulx & 0x3FFU) << 14) | ((uly & 0x3FFU) << 2));
}
