#ifndef _GBI_H_
#define _GBI_H_

// 3DS stub for N64 GBI (Graphics Binary Interface)
// Provides enough macro definitions to allow game code to compile.
// Actual rendering is handled via Citro3D in gfx_impl.c.

#include "PR/ultratypes.h"

// Gfx is a 64-bit display list command. Wave 12 (src/animator.c, etc.)
// needs `.words.w0/w1` access, so Gfx becomes a union of Gwords and an
// alignment marker rather than the previous `typedef u64 Gfx`. Layout/size
// stays at 8 bytes. port/3ds/include/n64/gfx_shim.h previously redefined
// Gfx as u64 — it now drops that line so PR/gbi.h's union wins.
typedef struct {
    unsigned int w0;
    unsigned int w1;
} Gwords;

typedef union {
    Gwords words;
    long long int force_structure_alignment;
} Gfx;

// N64 4x4 matrix (used by common_structs.h, functions.h, etc.)
// real N64 layout: 8 words integer, 8 words fraction (fixed point)
// On 3DS we only need the storage size to match.
#define MTX_SIZE 16
typedef long Mtx_t[4][4];
typedef union {
    Mtx_t m;
    long long int force_structure_alignment;
} Mtx;

// ---------------------------------------------------------------------------
// Display list building macros (stubs - expand to nothing or minimal)
// ---------------------------------------------------------------------------

#define gDPNoOpTag(pkt, tag)
/* M8.4f: gbi_custom.h patched to add trailing semicolons inside its
 * brace-wrapped macros, so we can route these through helpers. */
extern void pm3ds_gDPSetTile_impl(void *pkt,
                                  unsigned int fmt, unsigned int siz,
                                  unsigned int line, unsigned int tmem,
                                  unsigned int tile, unsigned int palette,
                                  unsigned int cmt, unsigned int maskt, unsigned int shiftt,
                                  unsigned int cms, unsigned int masks, unsigned int shifts);
/* M8.8: SETTIMG must emit 0xFD, not 0xFF — routing through the
 * SetColorImage impl meant texture addresses never reached the
 * translator's handle_settimg. */
extern void pm3ds_gDPSetTextureImage_impl(void *pkt,
                                          unsigned int fmt, unsigned int siz,
                                          unsigned int width, const void *img);
#define gDPSetTextureImage(pkt, fmt, siz, width, timg) \
    pm3ds_gDPSetTextureImage_impl((pkt), (unsigned int)(fmt), (unsigned int)(siz), \
                                         (unsigned int)(width), (const void *)(timg))
#define gDPSetTile(pkt, fmt, siz, line, tmem, tile, palette, cmt, maskt, shiftt, cms, masks, shifts) \
    pm3ds_gDPSetTile_impl((pkt), (unsigned int)(fmt), (unsigned int)(siz), \
                                  (unsigned int)(line), (unsigned int)(tmem), \
                                  (unsigned int)(tile), (unsigned int)(palette), \
                                  (unsigned int)(cmt), (unsigned int)(maskt), (unsigned int)(shiftt), \
                                  (unsigned int)(cms), (unsigned int)(masks), (unsigned int)(shifts))
#define gDPLoadSync(pkt)
#define gDPLoadTile(pkt, tile, uls, ult, lrs, lrt)
#define gDPLoadBlock(pkt, tile, uls, ult, lrs, dxt)
#define gDPPipeSync(pkt)
#define gDPTileSync(pkt)
/* M8.2 bisection: + gSPDisplayList + gSPBranchList. */
#define gDPFullSync(pkt) do {                       \
    Gfx *_g = (Gfx*)(pkt);                          \
    _g->words.w0 = 0xe9000000U;                     \
    _g->words.w1 = 0U;                              \
} while (0)
#define gSPEndDisplayList(pkt) do {                 \
    Gfx *_g = (Gfx*)(pkt);                          \
    _g->words.w0 = 0xdf000000U;                     \
    _g->words.w1 = 0U;                              \
} while (0)
/* M8.2: gSPDisplayList is called from ~198 sites; inlining the
 * macro at each site grew the binary by 6KB which Azahar refused
 * to load. Even via an extern helper, each `bl` instruction at
 * the 198 call sites pushes the code segment past Azahar's loader
 * threshold (somewhere around 750 KB).
 *
 * Probe2: noop gSPDisplayList. It still pretends the DL was
 * embedded — gMainGfxPos is advanced by the caller's postincrement
 * (gSPDisplayList(gMainGfxPos++, dl)). The decomp's animator code
 * paths see DL nodes with empty contents, but render_* in
 * gfx_draw_frame are themselves weak stubs at this milestone, so
 * no DL builder runs in the only-DL-from-main_loop path we care
 * about. Re-enable when the binary fits or we ship a real impl.
 *
 * Gated on PM3DS_GSPDL_REAL=1 — opt-in for builds that can afford
 * the size growth (real 3DS hardware, future Azahar versions). */
#ifdef PM3DS_GSPDL_REAL
extern void pm3ds_gSPDisplayList_impl(void *pkt, const void *dl);
#define gSPDisplayList(pkt, dl) \
    pm3ds_gSPDisplayList_impl((pkt), (const void *)(dl))
#else
/* Empty so call sites that omit trailing semicolons (e.g. when used
 * as the last "statement" in a wrapper {} block before another macro
 * call) still parse — turning this into an expression breaks
 * src/effects/water_block.c and similar. */
#define gSPDisplayList(pkt, dl)
#endif

extern void pm3ds_gSPBranchList_impl(void *pkt, const void *dl);
#define gSPBranchList(pkt, dl) \
    pm3ds_gSPBranchList_impl((pkt), (const void *)(dl))

// Static display-list macros (used in Gfx[] initializers)
#define gsDPNoOpTag(tag)            0
#define gsDPSetTextureImage(fmt, siz, width, timg) 0
#define gsSPTexture(s, t, level, tile, on) 0
#define gsDPSetCycleType(type)      0
#define gsDPSetTexturePersp(type)   0
#define gsDPSetTextureDetail(type)  0
#define gsDPSetTextureLOD(type)     0
#define gsDPSetTextureFilter(type)  0
#define gsDPSetTextureLUT(type)     0
#define gsDPSetTextureConvert(type) 0
#define gsSPEndDisplayList()        0
#define gsSPSetOtherMode(mode, shift, len, modeval) 0
#define gsSPSetGeometryMode(word)   0
#define gsSPClearGeometryMode(word) 0
#define gsDPSetRenderMode(c0, c1)   0
#define gsDPSetCombineMode(a, b)      0

// Additional static macros that appear in data initializers in the game
#define gsDPPipeSync()              0
#define gsDPTileSync()              0
#define gsDPFullSync()              0
#define gsSPDisplayList(dl)         0
#define gsSPBranchList(dl)          0
#define gsSPSegment(seg, base)      0
#define gsSPViewport(v)             0
#define gsSPMatrix(m, p)            0
#define gsSPPopMatrix(n)            0
#define gsSPVertex(v, n, v0)        0
#define gsSPTextureRectangle(xl, yl, xh, yh, tile, s, t, dsdx, dtdy) 0
#define gsSPScisTextureRectangle(xl, yl, xh, yh, tile, s, t, dsdx, dtdy) 0
#define gsDPFillRectangle(ulx, uly, lrx, lry) 0
#define gsDPSetScissor(mode, ulx, uly, lrx, lry) 0
#define gsDPSetColorImage(fmt, siz, width, img) 0
#define gsDPSetDepthImage(img)      0
#define gsDPSetFillColor(color)     0
#define gsDPSetPrimColor(m, l, r, g, b, a) 0
#define gsDPSetEnvColor(r, g, b, a) 0
#define gsDPSetBlendColor(r, g, b, a) 0
#define gsDPSetCombineLERP(a0, b0, c0, d0, Aa0, Ab0, Ac0, Ad0, a1, b1, c1, d1, Aa1, Ab1, Ac1, Ad1) 0
#define gsDPSetAlphaCompare(type)   0
#define gsDPSetAlphaDither(type)    0
#define gsDPSetColorDither(type)    0
#define gsDPSetDepthSource(type)    0

// M4 wave 5+: gs* macros needed by display-list/Gfx[] data in src/entity/*.c
// (Shadow.c, Block.c etc.) and assets/.../gfx.inc.c. All collapse to 0 — the
// F3DEX2 to PICA200 translator walks Gfx[] entries by opcode, so 0-valued
// filler entries are harmless padding.
#define gsSP2Triangles(v00, v01, v02, flag0, v10, v11, v12, flag1) 0
#define gsSP1Triangle(v0, v1, v2, flag) 0
#define gsSPLightColor(n, col)      0
#define gsSPSetLights1(name)        0
#define gsSPCullDisplayList(v0, vn) 0
#define gsDPSetFogColor(r, g, b, a) 0
#define gsDPSetTile(fmt, siz, line, tmem, tile, palette, cmt, maskt, shiftt, cms, masks, shifts) 0
#define gsDPSetTileSize(t, uls, ult, lrs, lrt) 0
#define gsDPLoadSync()              0
#define gsDPLoadBlock(tile, uls, ult, lrs, dxt) 0
// _4b variants intentionally omit siz (implicit G_IM_SIZ_4b) — must match
// upstream gbi.h's argument count.
#define gsDPLoadTextureTile_4b(timg, fmt, width, height, uls, ult, lrs, lrt, pal, cms, cmt, masks, maskt, shifts, shiftt) 0
#define gsDPLoadTextureTile(timg, fmt, siz, width, height, uls, ult, lrs, lrt, pal, cms, cmt, masks, maskt, shifts, shiftt) 0
#define gsDPLoadTextureBlock(timg, fmt, siz, width, height, pal, cms, cmt, masks, maskt, shifts, shiftt) 0
#define gsDPLoadTextureBlock_4b(timg, fmt, width, height, pal, cms, cmt, masks, maskt, shifts, shiftt) 0
#define gsDPLoadMultiTile(timg, tmem, tile, fmt, siz, width, height, uls, ult, lrs, lrt, pal, cms, cmt, masks, maskt, shifts, shiftt) 0
#define gsDPLoadMultiTile_4b(timg, tmem, tile, fmt, width, height, uls, ult, lrs, lrt, pal, cms, cmt, masks, maskt, shifts, shiftt) 0
#define gsDPNoOp()                  0
#define gDPNoOp(pkt)

// M4 wave 12: opcode constants for src/animator.c walking display lists.
#define G_NOOP_GBI      0x00
#define G_VTX           0x01
#define G_MODIFYVTX     0x02
#define G_CULLDL        0x03
#define G_TRI1          0x05
#define G_TRI2          0x06
#define G_QUAD          0x07
#define G_DL            0xDE
#define G_ENDDL         0xDF
#define G_RDPHALF_1     0xE1
#define G_RDPHALF_2     0xF1

// _SHIFTR(v, s, w) — extract w-bit field at shift s.
#define _SHIFTR(v, s, w)    (((unsigned int)(v) >> (s)) & ((1U << (w)) - 1U))
#define _SHIFTL(v, s, w)    ((((unsigned int)(v) & ((1U << (w)) - 1U)) << (s)))

// gSPFogPosition — display list fog setter, packet form.
#define gSPFogPosition(pkt, min, max)
#define gsSPFogPosition(min, max)   0

// M4 wave 12: gSPSetOtherMode (packet form) + gdSPDefLights1 needed by
// src/entity_model.c. The gs* form is already defined above as 4 args
// (no pkt); the g* form takes 5 args including pkt.
#define gSPSetOtherMode(pkt, cmd, shift, len, modeval)
// Used as initializers for static Lights1/Lights2 storage. The expansion
// must satisfy the Lights{1,2} struct shape. Zero-init suffices — runtime
// rendering uses the PICA200 path and ignores N64 light state at compile
// time.
#define gdSPDefLights1(ar, ag, ab, r1, g1, b1, x1, y1, z1) {0}
#define gdSPDefLights2(ar, ag, ab, r1, g1, b1, x1, y1, z1, r2, g2, b2, x2, y2, z2) {0}
#define gSPSetLights2(pkt, name)
#define gsSPSetLights2(name)        0

// M4 wave 12: F3DEX2 blender/rasterizer mode constants referenced by
// render-mode (RM_*) macros in src/imgfx.c et al. All defined as 0 — the
// PICA200 translator computes its own GPU state from the gs* opcode
// stream, so these never affect actual rendering. They only need to
// satisfy the C preprocessor at compile time.
#define AA_EN           0
#define IM_RD           0
#define ALPHA_CVG_SEL   0
#define CVG_DST_FULL    0
#define CVG_DST_CLAMP   0
#define CVG_DST_WRAP    0
#define CVG_DST_SAVE    0
#define CVG_X_ALPHA     0
#define ZMODE_OPA       0
#define ZMODE_INTER     0
#define ZMODE_XLU       0
#define ZMODE_DEC       0
#define FORCE_BL        0
#define G_BL_A_IN       0
#define G_BL_A_MEM      0
#define G_BL_A_FOG      0
#define G_BL_A_SHADE    0
#define G_BL_CLR_IN     0
#define G_BL_CLR_MEM    0
#define G_BL_CLR_FOG    0
#define G_BL_CLR_BL     0
#define G_BL_1MA        0
#define G_BL_1MA_MEM    0
#define G_BL_1          0
#define G_BL_0          0
#define GBL_c1(p, a, m, b)  0
#define GBL_c2(p, a, m, b)  0
#define RM_ZB_CLD_SURF(clk)  0
#define RM_ZB_OVL_SURF(clk)  0
#define RM_OPA_SURF(clk)     0

// Z-buffer mode bits (used in render-mode macro expansions).
#define Z_CMP   0
#define Z_UPD   0

// M4 wave 12 cont: more constants and macros for src/model.c.
#define G_MAXZ              0x3FF
#define G_MDSFT_TEXTFILT    12
#define gSPSetLights0(pkt, name)
#define gSPSetLights3(pkt, name)
#define gSPSetLights4(pkt, name)
#define gSPSetLights5(pkt, name)
#define gSPSetLights6(pkt, name)
#define gSPSetLights7(pkt, name)
#define gsSPSetLights0(name)   0
#define gsSPSetLights3(name)   0
#define gsSPSetLights4(name)   0
#define gsSPSetLights5(name)   0
#define gsSPSetLights6(name)   0
#define gsSPSetLights7(name)   0
#define gDPLoadTLUTCmd(pkt, tile, count)
#define gsDPLoadTLUTCmd(tile, count)  0
#define gDPScisFillRectangle(pkt, ulx, uly, lrx, lry)
#define gsDPScisFillRectangle(ulx, uly, lrx, lry)  0
#define gsDPLoadTile(tile, uls, ult, lrs, lrt)  0
#define gSPLoadGeometryMode(pkt, word)
#define gsSPLoadGeometryMode(word)   0
#define gsDPLoadTLUT_pal16(pal, dram) 0
#define gsDPLoadTLUT_pal256(dram)   0
#define gsDPLoadTLUT(count, tmemaddr, dram) 0
#define gsDPSetPrimDepth(z, dz)     0
#define gDPSetPrimDepth(pkt, z, dz)

// M4 wave 6: additional g* (packet-form) macros referenced by src/effects/*.c.
// Empty define since these emit display-list commands to a packet pointer
// at runtime; the F3DEX2 -> PICA200 translator already handles the static
// gs* form, and the runtime DL builder isn't exercised at compile time.
#define gDPLoadMultiTile(pkt, timg, tmem, tile, fmt, siz, width, height, uls, ult, lrs, lrt, pal, cms, cmt, masks, maskt, shifts, shiftt)
#define gDPLoadMultiTile_4b(pkt, timg, tmem, tile, fmt, width, height, uls, ult, lrs, lrt, pal, cms, cmt, masks, maskt, shifts, shiftt)
#define gDPSetKeyGB(pkt, cG, sG, wG, cB, sB, wB)
#define gDPSetKeyR(pkt, cR, sR, wR)
#define gsDPSetKeyR(cR, sR, wR) 0
#define gsDPSetKeyGB(cG, sG, wG, cB, sB, wB) 0
#define gsDPSetConvert(k0, k1, k2, k3, k4, k5) 0
#define gDPSetScissorFrac(pkt, mode, ulx, uly, lrx, lry)
#define gDPLoadTLUT(pkt, count, tmemaddr, dram)
#define gDPSetFogColor(pkt, r, g, b, a)
extern void pm3ds_gSP1Triangle_impl(void *pkt, unsigned int v0, unsigned int v1, unsigned int v2);
extern void pm3ds_gSP2Triangles_impl(void *pkt,
                                     unsigned int v00, unsigned int v01, unsigned int v02,
                                     unsigned int v10, unsigned int v11, unsigned int v12);
#define gSP1Triangle(pkt, v0, v1, v2, flag) \
    (pm3ds_gSP1Triangle_impl((pkt), (unsigned int)(v0), (unsigned int)(v1), (unsigned int)(v2)), (void)(flag))
#define gSP2Triangles(pkt, v00, v01, v02, flag0, v10, v11, v12, flag1) \
    (pm3ds_gSP2Triangles_impl((pkt), (unsigned int)(v00), (unsigned int)(v01), (unsigned int)(v02), \
                                       (unsigned int)(v10), (unsigned int)(v11), (unsigned int)(v12)), \
     (void)(flag0), (void)(flag1))
#define gSPLookAt(pkt, l)
#define gSPLookAtX(pkt, l)
#define gSPLookAtY(pkt, l)
#define gDPSetHilite1Tile(pkt, tile, hilite, width, height)
#define gDPSetHilite2Tile(pkt, tile, hilite, width, height)
#define gSPModifyVertex(pkt, vtx, where, val)
#define gSPLightColor(pkt, n, col)
#define gSPSetLights1(pkt, name)
#define gSPCullDisplayList(pkt, v0, vn)
#define gSPClipRatio(pkt, r)

// M4 wave 8 additions for src/battle/*.c
#define gsDPPipelineMode(mode)      0
#define gDPPipelineMode(pkt, mode)
#define gsDPSetCombineKey(type)     0
#define gDPSetCombineKey(pkt, type)
#define gsSPPerspNormalize(s)       0
#define gSPPerspNormalize(pkt, s)

/* M8.4d wired helpers. */
extern void pm3ds_gDPSetPrimColor_impl(void *pkt, unsigned int min_lod, unsigned int lod,
                                       unsigned int r, unsigned int g, unsigned int b, unsigned int a);
extern void pm3ds_gDPSetEnvColor_impl(void *pkt, unsigned int r, unsigned int g, unsigned int b, unsigned int a);
extern void pm3ds_gDPSetCombine_impl(void *pkt, unsigned int w0_lo, unsigned int w1);
extern void pm3ds_gSPSetOtherModeL_impl(void *pkt, unsigned int shift, unsigned int length, unsigned int data);
extern void pm3ds_gDPSetScissor_impl(void *pkt, unsigned int mode,
                                     unsigned int ulx, unsigned int uly,
                                     unsigned int lrx, unsigned int lry);
extern void pm3ds_gDPSetCycleType_impl(void *pkt, unsigned int type);
extern void pm3ds_gDPSetTextureLUT_impl(void *pkt, unsigned int lut_mode);
extern void pm3ds_gDPSetColorImage_impl(void *pkt, unsigned int fmt, unsigned int siz,
                                        unsigned int width, const void *img);

#define gDPSetScissor(pkt, mode, ulx, uly, lrx, lry) \
    pm3ds_gDPSetScissor_impl((pkt), (unsigned int)(mode), (unsigned int)(ulx), (unsigned int)(uly), \
                                    (unsigned int)(lrx), (unsigned int)(lry))
#define gDPSetColorImage(pkt, fmt, siz, width, img) \
    pm3ds_gDPSetColorImage_impl((pkt), (unsigned int)(fmt), (unsigned int)(siz), \
                                       (unsigned int)(width), (const void *)(img))
#define gDPSetDepthImage(pkt, img) \
    pm3ds_gDPSetColorImage_impl((pkt), 0U, 0U, 0U, (const void *)(img)) /* SETZIMG uses similar shape; precise opcode is 0xFE — translator distinguishes by w0 high byte */
extern void pm3ds_gDPSetFillColor_impl(void *pkt, unsigned int color);
#define gDPSetFillColor(pkt, color) \
    pm3ds_gDPSetFillColor_impl((pkt), (unsigned int)(color))
#define gDPSetPrimColor(pkt, m, l, r, g, b, a) \
    pm3ds_gDPSetPrimColor_impl((pkt), (unsigned int)(m), (unsigned int)(l), \
                                       (unsigned int)(r), (unsigned int)(g), (unsigned int)(b), (unsigned int)(a))
#define gDPSetEnvColor(pkt, r, g, b, a) \
    pm3ds_gDPSetEnvColor_impl((pkt), (unsigned int)(r), (unsigned int)(g), \
                                      (unsigned int)(b), (unsigned int)(a))
#define gDPSetBlendColor(pkt, r, g, b, a) \
    pm3ds_gDPSetEnvColor_impl((pkt), (unsigned int)(r), (unsigned int)(g), \
                                      (unsigned int)(b), (unsigned int)(a)) /* SETBLENDCOLOR has different opcode; placeholder reuses setenv shape */
/* gDPSetCombineMode/LERP: the decomp expansion produces 16 comma-separated
 * combiner-token names (G_CC_SHADE etc.), not a 2-arg pair we can route
 * to a helper. Proper encoding is M9+ work; leave as no-op so the build
 * compiles. Combiner state will appear flat-shaded until we either
 * implement the LERP macro expansion or pre-encode them via the
 * decomp's gsDPSetCombineLERP table. */
#define gDPSetCombineMode(pkt, a, b)
#define gDPSetCombineLERP(pkt, a0, b0, c0, d0, Aa0, Ab0, Ac0, Ad0, a1, b1, c1, d1, Aa1, Ab1, Ac1, Ad1)
/* gDPSetRenderMode also expands its args via G_RM_NOOP, G_RM_FOG_SHADE_A
 * etc. which contain commas — can't be passed to a 3-arg helper. Defer. */
#define gDPSetRenderMode(pkt, c0, c1)
#define gDPSetCycleType(pkt, type) \
    pm3ds_gDPSetCycleType_impl((pkt), (unsigned int)(type))
#define gDPSetTextureFilter(pkt, type)
#define gDPSetTexturePersp(pkt, type)
#define gDPSetTextureLUT(pkt, type) \
    pm3ds_gDPSetTextureLUT_impl((pkt), (unsigned int)(type))
#define gDPSetTextureLOD(pkt, type)
#define gDPSetTextureDetail(pkt, type)
#define gDPSetTextureConvert(pkt, type)
#define gDPSetAlphaCompare(pkt, type)
#define gDPSetAlphaDither(pkt, type)
#define gDPSetColorDither(pkt, type)
#define gDPSetDepthSource(pkt, type)

extern void pm3ds_gSPTexture_impl(void *pkt, unsigned int s, unsigned int t,
                                  unsigned int level, unsigned int tile, unsigned int on);
#define gSPTexture(pkt, s, t, level, tile, on) \
    pm3ds_gSPTexture_impl((pkt), (unsigned int)(s), (unsigned int)(t), \
                                  (unsigned int)(level), (unsigned int)(tile), (unsigned int)(on))
extern void pm3ds_gSPVertex_impl(void *pkt, const void *v, unsigned int n, unsigned int v0);
#define gSPVertex(pkt, v, n, v0) \
    pm3ds_gSPVertex_impl((pkt), (const void *)(v), (unsigned int)(n), (unsigned int)(v0))
/* M8.4: gSPMatrix has ~395 call sites. Out-of-line helper to keep
 * binary growth small enough that Azahar's loader still accepts. */
extern void pm3ds_gSPMatrix_impl(void *pkt, const void *m, unsigned int p);
#define gSPMatrix_REAL(pkt, m, p) \
    pm3ds_gSPMatrix_impl((pkt), (const void *)(m), (unsigned int)(p))

/* M8.4: gSPMatrix wired through helper (see top of this file for
 * the PM3DS_GSPDL_REAL pattern). */
#define gSPMatrix(pkt, m, p) \
    pm3ds_gSPMatrix_impl((pkt), (const void *)(m), (unsigned int)(p))
extern void pm3ds_gSPPopMatrix_impl(void *pkt, unsigned int n);
extern void pm3ds_gSPSegment_impl(void *pkt, unsigned int seg, unsigned int base);
extern void pm3ds_gSPTextureRectangle_impl(void *pkt0, void *pkt1, void *pkt2,
                                           unsigned int xl, unsigned int yl,
                                           unsigned int xh, unsigned int yh,
                                           unsigned int tile,
                                           unsigned int s, unsigned int t,
                                           unsigned int dsdx, unsigned int dtdy);
extern void pm3ds_gSPSetGeometryMode_impl(void *pkt, unsigned int word);
extern void pm3ds_gSPClearGeometryMode_impl(void *pkt, unsigned int word);

#define gSPPopMatrix(pkt, n) \
    pm3ds_gSPPopMatrix_impl((pkt), (unsigned int)(n))
#define gSPSegment(pkt, seg, base) \
    pm3ds_gSPSegment_impl((pkt), (unsigned int)(seg), (unsigned int)(base))
#define gSPViewport(pkt, v) /* G_MOVEMEM; translator dispatches G_VIEWPORT via index — left empty for now */
/* M8.8: TEXRECT spans 3 Gfx slots, so (pkt) — conventionally
 * `gMainGfxPos++` — must be evaluated three times, exactly like the
 * real GBI's three-statement expansion. The old single-eval form let
 * the next DL command overwrite the RDPHALF words. */
#define gSPTextureRectangle(pkt, xl, yl, xh, yh, tile, s, t, dsdx, dtdy) \
    do {                                                                 \
        void *_pm3ds_tr0 = (pkt);                                        \
        void *_pm3ds_tr1 = (pkt);                                        \
        void *_pm3ds_tr2 = (pkt);                                        \
        pm3ds_gSPTextureRectangle_impl(_pm3ds_tr0, _pm3ds_tr1, _pm3ds_tr2, \
                                       (unsigned int)(xl), (unsigned int)(yl), \
                                       (unsigned int)(xh), (unsigned int)(yh), (unsigned int)(tile), \
                                       (unsigned int)(s), (unsigned int)(t), \
                                       (unsigned int)(dsdx), (unsigned int)(dtdy)); \
    } while (0)
/* M9c: was aliased to gSPTextureRectangle, which DROPPED the negative-coord
 * clamping the scissored form exists to do. Sliding UI (the title logo comes in
 * from below with a negative y offset) then had its coordinates masked into the
 * 12-bit field, landing at y~950 on a 240-tall screen instead of being clipped.
 * Now routed to a faithful implementation. Like gSPTextureRectangle this spans
 * 3 Gfx slots, so (pkt) must be evaluated three times. */
extern void pm3ds_gSPScisTextureRectangle_impl(void *pkt0, void *pkt1, void *pkt2,
                                               unsigned int xl, unsigned int yl,
                                               unsigned int xh, unsigned int yh,
                                               unsigned int tile,
                                               unsigned int s, unsigned int t,
                                               unsigned int dsdx, unsigned int dtdy);
#define gSPScisTextureRectangle(pkt, xl, yl, xh, yh, tile, s, t, dsdx, dtdy) \
    do {                                                                     \
        void *_pm3ds_sr0 = (pkt);                                            \
        void *_pm3ds_sr1 = (pkt);                                            \
        void *_pm3ds_sr2 = (pkt);                                            \
        pm3ds_gSPScisTextureRectangle_impl(_pm3ds_sr0, _pm3ds_sr1, _pm3ds_sr2, \
                                       (unsigned int)(xl), (unsigned int)(yl), \
                                       (unsigned int)(xh), (unsigned int)(yh), \
                                       (unsigned int)(tile),                 \
                                       (unsigned int)(s), (unsigned int)(t), \
                                       (unsigned int)(dsdx), (unsigned int)(dtdy)); \
    } while (0)
extern void pm3ds_gDPFillRectangle_impl(void *pkt,
                                        unsigned int ulx, unsigned int uly,
                                        unsigned int lrx, unsigned int lry);
#define gDPFillRectangle(pkt, ulx, uly, lrx, lry) \
    pm3ds_gDPFillRectangle_impl((pkt), (unsigned int)(ulx), (unsigned int)(uly), \
                                       (unsigned int)(lrx), (unsigned int)(lry))
#define gSPSetGeometryMode(pkt, word) \
    pm3ds_gSPSetGeometryMode_impl((pkt), (unsigned int)(word))
#define gSPClearGeometryMode(pkt, word) \
    pm3ds_gSPClearGeometryMode_impl((pkt), (unsigned int)(word))
// gSPSetOtherMode 5-arg version (with cmd) is defined further up — the
// previous 4-arg form here was wrong (decomp callers pass 5 args).
#define gSPSetLights(pkt, n)

/* M8.8: real expansions. The translator's texture cache decodes lazily
 * from the SETTIMG address (handle_loadblock/loadtile only mark state
 * dirty), so the TMEM-load mechanics (LOADSYNC/LOADTILE/LOADBLOCK) are
 * skipped — we emit just SETTIMG + SETTILE(render tile) + SETTILESIZE,
 * each evaluating (pkt) once so gMainGfxPos advances in lockstep.
 * line = row stride in 64-bit TMEM words: bytes-per-row = w << siz >> 1
 * (siz: 0=4b half-byte, 1=8b, 2=16b, 3=32b). */
#define PM3DS_GBI_TILE_LINE(texels, siz) \
    (((((unsigned int)(texels) << (siz)) >> 1) + 7U) >> 3)
#define gDPLoadTextureBlock(pkt, timg, fmt, siz, width, height, pal, cms, cmt, masks, maskt, shifts, shiftt) \
    do {                                                                          \
        gDPSetTextureImage(pkt, fmt, siz, width, timg);                           \
        gDPSetTile(pkt, fmt, siz, PM3DS_GBI_TILE_LINE(width, siz), 0,             \
                   0 /*G_TX_RENDERTILE*/, pal, cmt, maskt, shiftt, cms, masks, shifts); \
        gDPSetTileSize(pkt, 0, 0, 0,                                              \
                       ((unsigned int)(width)  - 1U) << 2,                        \
                       ((unsigned int)(height) - 1U) << 2);                       \
    } while (0)
#define gDPLoadTextureBlock_4b(pkt, timg, fmt, width, height, pal, cms, cmt, masks, maskt, shifts, shiftt) \
    gDPLoadTextureBlock(pkt, timg, fmt, 0 /*G_IM_SIZ_4b*/, width, height, pal, cms, cmt, masks, maskt, shifts, shiftt)
#define gDPLoadTextureTile(pkt, timg, fmt, siz, width, height, uls, ult, lrs, lrt, pal, cms, cmt, masks, maskt, shifts, shiftt) \
    do {                                                                          \
        gDPSetTextureImage(pkt, fmt, siz, width, timg);                           \
        gDPSetTile(pkt, fmt, siz,                                                 \
                   PM3DS_GBI_TILE_LINE((unsigned int)(lrs) - (unsigned int)(uls) + 1U, siz), 0, \
                   0 /*G_TX_RENDERTILE*/, pal, cmt, maskt, shiftt, cms, masks, shifts); \
        gDPSetTileSize(pkt, 0,                                                    \
                       (unsigned int)(uls) << 2, (unsigned int)(ult) << 2,        \
                       (unsigned int)(lrs) << 2, (unsigned int)(lrt) << 2);       \
    } while (0)
#define gDPLoadTextureTile_4b(pkt, timg, fmt, width, height, uls, ult, lrs, lrt, pal, cms, cmt, masks, maskt, shifts, shiftt) \
    gDPLoadTextureTile(pkt, timg, fmt, 0 /*G_IM_SIZ_4b*/, width, height, uls, ult, lrs, lrt, pal, cms, cmt, masks, maskt, shifts, shiftt)
#define gDPLoadTLUT_pal(pkt, pal, dram, count)
#define gDPLoadTLUT_pal16(pkt, pal, dram)
#define gDPLoadTLUT_pal256(pkt, dram)

extern void pm3ds_gDPSetTileSize_impl(void *pkt, unsigned int tile,
                                      unsigned int uls, unsigned int ult,
                                      unsigned int lrs, unsigned int lrt);
#define gDPSetTileSize(pkt, t, uls, ult, lrs, lrt) \
    pm3ds_gDPSetTileSize_impl((pkt), (unsigned int)(t), \
                                      (unsigned int)(uls), (unsigned int)(ult), \
                                      (unsigned int)(lrs), (unsigned int)(lrt))

// ---------------------------------------------------------------------------
// Image format constants
// ---------------------------------------------------------------------------
#define G_IM_FMT_RGBA   0
#define G_IM_FMT_YUV    1
#define G_IM_FMT_CI     2
#define G_IM_FMT_IA     3
#define G_IM_FMT_I      4

#define G_IM_SIZ_4b     0
#define G_IM_SIZ_8b     1
#define G_IM_SIZ_16b    2
#define G_IM_SIZ_32b    3

/* M8.4d: needed once gDPSetTile etc. evaluate their arguments. */
#define G_IM_SIZ_4b_BYTES        0
#define G_IM_SIZ_4b_TILE_BYTES   G_IM_SIZ_4b_BYTES
#define G_IM_SIZ_4b_LINE_BYTES   G_IM_SIZ_4b_BYTES
#define G_IM_SIZ_4b_LOAD_BLOCK   G_IM_SIZ_16b
#define G_IM_SIZ_4b_SHIFT        2
#define G_IM_SIZ_4b_INCR         3

#define G_IM_SIZ_8b_BYTES        1
#define G_IM_SIZ_8b_TILE_BYTES   G_IM_SIZ_8b_BYTES
#define G_IM_SIZ_8b_LINE_BYTES   G_IM_SIZ_8b_BYTES
#define G_IM_SIZ_8b_LOAD_BLOCK   G_IM_SIZ_16b
#define G_IM_SIZ_8b_SHIFT        1
#define G_IM_SIZ_8b_INCR         1

#define G_IM_SIZ_16b_BYTES       2
#define G_IM_SIZ_16b_TILE_BYTES  G_IM_SIZ_16b_BYTES
#define G_IM_SIZ_16b_LINE_BYTES  G_IM_SIZ_16b_BYTES
#define G_IM_SIZ_16b_LOAD_BLOCK  G_IM_SIZ_16b
#define G_IM_SIZ_16b_SHIFT       0
#define G_IM_SIZ_16b_INCR        0

#define G_IM_SIZ_32b_BYTES       4
#define G_IM_SIZ_32b_TILE_BYTES  2
#define G_IM_SIZ_32b_LINE_BYTES  2
#define G_IM_SIZ_32b_LOAD_BLOCK  G_IM_SIZ_32b
#define G_IM_SIZ_32b_SHIFT       0
#define G_IM_SIZ_32b_INCR        0
#define G_IM_SIZ_DD     5

#define G_TX_NOMIRROR   0
#define G_TX_WRAP       1
#define G_TX_CLAMP      2
#define G_TX_MIRROR     3
#define G_TX_NOMASK     0
#define G_TX_NOLOD      0
#define G_TX_RENDERTILE 0
#define G_TX_LOADTILE   7

// ---------------------------------------------------------------------------
// Cycle types
// ---------------------------------------------------------------------------
#define G_CYC_1CYCLE    0
#define G_CYC_2CYCLE    1
#define G_CYC_COPY      2
#define G_CYC_FILL      3

// ---------------------------------------------------------------------------
// Render modes (combinations are pre-defined)
// ---------------------------------------------------------------------------
#define G_RM_OPA_SURF           0
#define G_RM_OPA_SURF2          0
#define G_RM_AA_OPA_SURF        0
#define G_RM_AA_OPA_SURF2       0
#define G_RM_XLU_SURF           0
#define G_RM_XLU_SURF2          0
#define G_RM_AA_XLU_SURF        0
#define G_RM_AA_XLU_SURF2       0
#define G_RM_ZB_OPA_SURF        0
#define G_RM_ZB_OPA_SURF2       0
#define G_RM_AA_ZB_OPA_SURF     0
#define G_RM_AA_ZB_OPA_SURF2    0
#define G_RM_ZB_XLU_SURF        0
#define G_RM_ZB_XLU_SURF2       0
#define G_RM_AA_ZB_XLU_SURF     0
#define G_RM_AA_ZB_XLU_SURF2    0
#define G_RM_TEX_EDGE           0
#define G_RM_TEX_EDGE2          0
#define G_RM_AA_TEX_EDGE        0
#define G_RM_AA_TEX_EDGE2       0
#define G_RM_AA_ZB_TEX_EDGE     0
#define G_RM_AA_ZB_TEX_EDGE2    0
#define G_RM_CLD_SURF           0
#define G_RM_CLD_SURF2          0
#define G_RM_ZB_CLD_SURF        0
#define G_RM_ZB_CLD_SURF2       0

// ---------------------------------------------------------------------------
// Geometry mode bits
// ---------------------------------------------------------------------------
#define G_ZBUFFER           0x00000001
#define G_SHADE             0x00000004
#define G_SHADING_SMOOTH    0x00000200
#define G_CULL_FRONT        0x00002000
#define G_CULL_BACK         0x00001000
#define G_CULL_BOTH         0x00003000
#define G_FOG               0x00010000
#define G_LIGHTING          0x00020000
#define G_TEXTURE_GEN       0x00040000
#define G_TEXTURE_GEN_LINEAR 0x00080000
#define G_LOD               0x00100000
#define G_CLIPPING          0x00800000   /* F3DEX2 */

// ---------------------------------------------------------------------------
// Texture parameters
// ---------------------------------------------------------------------------
#define G_ON                1
#define G_OFF               0
#define G_TP_NONE           0
#define G_TP_PERSP          1
#define G_TT_NONE           0
#define G_TT_RGBA16         2
#define G_TT_IA16           3
#define G_TF_POINT          0
#define G_TF_BILERP         2
#define G_TF_AVERAGE        3
#define G_AC_THRESHOLD      0
#define G_AC_DITHER         1
#define G_CD_MAGICSQ        0
#define G_CD_BAYER          1
#define G_CD_NOISE          2
#define G_CD_DISABLE        3
#define G_AD_PATTERN        0
#define G_AD_NOTPATTERN     1
#define G_AD_NOISE          2
#define G_AD_DISABLE        3
#define G_DS_ON             1
#define G_DS_OFF            0

// Extra mode-shift constants referenced by src/43F0.c and gbi_custom macros
#define G_MDSFT_TEXTDETAIL  12
#define G_MDSFT_TEXTLOD     15
#define G_MDSFT_TEXTCONV    17

// Missing TD/TL/TC constants
#define G_TD_CLAMP          (0 << G_MDSFT_TEXTDETAIL)
#define G_TD_SHARPEN        (1 << G_MDSFT_TEXTDETAIL)
#define G_TD_DETAIL         (2 << G_MDSFT_TEXTDETAIL)
#define G_TL_TILE           (0 << G_MDSFT_TEXTLOD)
#define G_TL_LOD            (1 << G_MDSFT_TEXTLOD)
#define G_TC_FILT           (6 << G_MDSFT_TEXTCONV)
#define G_TC_FILTCONV       (5 << G_MDSFT_TEXTCONV)

// ---------------------------------------------------------------------------
// Matrix parameters
// ---------------------------------------------------------------------------
#define G_MTX_MODELVIEW     0x00
#define G_MTX_PROJECTION    0x01
#define G_MTX_MUL           0x00
#define G_MTX_LOAD          0x02
#define G_MTX_NOPUSH        0x00
#define G_MTX_PUSH          0x04

// ---------------------------------------------------------------------------
// Scissor modes
// ---------------------------------------------------------------------------
#define G_SC_NON_INTERLACE  0
#define G_SC_EVEN_INTERLACE 2
#define G_SC_ODD_INTERLACE  3

// ---------------------------------------------------------------------------
// Combine constants (used by game code)
// These are the standard N64 GBI operand names. They must NOT redefine
// literal 0/1 because game macros like PM_CC_01 use numbers directly.
// ---------------------------------------------------------------------------
#define COMBINED        0
#define TEXEL0          1
#define TEXEL1          2
#define PRIMITIVE       3
#define SHADE           4
#define ENVIRONMENT     5
#define CENTER          6
#define SCALE           7
#define PRIMITIVE_ALPHA 8
#define TEXEL0_ALPHA    9
#define TEXEL1_ALPHA    10
#define SHADE_ALPHA     11
#define ENV_ALPHA       12
#define LOD_FRACTION    13
#define PRIM_LOD_FRAC   14
#define NOISE           15
#define K4              7
#define K5              15

#define G_CC_MODULATEIA           0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0
#define G_CC_MODULATEIDECALA    0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0
#define G_CC_MODULATERGB        0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0
#define G_CC_MODULATERGBDECALA  0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0
#define G_CC_MODULATERGBA       0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0
#define G_CC_MODULATEI          0, 0, 0, TEXEL0, 0, 0, 0, SHADE
#define G_CC_MODULATERGBA_PRIM  0, 0, 0, TEXEL0, 0, 0, 0, PRIMITIVE
#define G_CC_DECALRGB           0, 0, 0, TEXEL0, 0, 0, 0, SHADE
#define G_CC_DECALRGBA          0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0
#define G_CC_BLENDPE            0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0
#define G_CC_BLENDPEDECALA      0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0
#define G_CC_TRILERP            0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0
#define G_CC_INTERFERENCE       0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0
#define G_CC_PASS               0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0
#define G_CC_SHADE              0, 0, 0, SHADE, 0, 0, 0, SHADE

// ---------------------------------------------------------------------------
// GBI utility macros
// ---------------------------------------------------------------------------
#define GPACK_RGBA5551(r, g, b, a) 0
#define GPACK_ZDZ(z, dz) 0
#define G_TEXTURE_IMAGE_FRAC 2
#define CALC_DXT_4b(w) (((w)>>1)+7)>>3
#define CALC_DXT_8b(w) ((w)+7)>>3
#define CALC_DXT_16b(w) ((w)+3)>>2
#define CALC_DXT_32b(w) ((w)+1)>>1
#define G_DXT_FRAC 11

// M4 wave 8: constants referenced by src/battle/*.c
#define G_CK_NONE       0
#define G_CK_KEY        1
#define G_PM_NPRIMITIVE 0
#define G_PM_1PRIMITIVE 1

// ---------------------------------------------------------------------------
// Vertex structures
// ---------------------------------------------------------------------------
// Decomp layout: Vtx is a union of Vtx_t (colored) and Vtx_tn (normal-lit),
// with member access like `vtx.v.ob[0]` (Vtx_t flavor) and `vtx.n.n[0]`
// (Vtx_tn flavor). Wave 6 (src/effects/water_block.c et al.) requires this
// layout; previously Vtx was a flat struct and Vtx_t/Vtx_tn were typedef
// aliases, which broke `.v.ob` access.
typedef struct {
    s16 ob[3];
    s16 flag;
    s16 tc[2];
    u8  cn[4];
} Vtx_t;

typedef struct {
    s16 ob[3];
    s16 flag;
    s16 tc[2];
    s8  n[3];
    u8  a;
} Vtx_tn;

typedef union {
    Vtx_t v;
    Vtx_tn n;
    long long int force_structure_alignment;
} Vtx;

// ---------------------------------------------------------------------------
// Viewport structure
// ---------------------------------------------------------------------------
// Decomp Vp is a union of Vp_t (the actual data) and an alignment marker;
// callers use `vp.vp.vscale[i]`. Match the layout.
typedef struct {
    s16 vscale[4];
    s16 vtrans[4];
} Vp_t;

typedef union {
    Vp_t vp;
    long long int force_structure_alignment;
} Vp;

// ---------------------------------------------------------------------------
// Lights
// ---------------------------------------------------------------------------
// Decomp Light/Ambient are unions of their _t data struct + alignment marker;
// callers use `light.l.col` (src/imgfx.c). Match the layout.
typedef struct {
    u8 col[3];
    u8 pad1;
    u8 colc[3];
    u8 pad2;
    s8 dir[3];
    u8 pad3;
} Light_t;

typedef union {
    Light_t l;
    long long int force_structure_alignment;
} Light;

typedef struct {
    u8 col[3];
    u8 pad1;
    u8 colc[3];
    u8 pad2;
} Ambient_t;

typedef union {
    Ambient_t l;
    long long int force_structure_alignment;
} Ambient;

typedef struct {
    Ambient a;
    Light   l[1];
} Lights1;

typedef struct {
    Ambient a;
    Light   l[2];
} Lights2;

typedef struct {
    Ambient a;
    Light   l[7];
} Lightsn;

// ---------------------------------------------------------------------------
// Other structures
// ---------------------------------------------------------------------------
typedef struct {
    u32 type;
    u32 flags;
    u64* ucode;
    u64* ucode_data;
    u32 ucode_size;
    u32 ucode_data_size;
    void* dram_stack;
    u32 dram_stack_size;
    void* output_buff;
    u32 output_buff_size;
    void* data_ptr;
    u32 data_size;
    void* yield_data_ptr;
    u32 yield_data_size;
} OSTask;

// ---------------------------------------------------------------------------
// gs2dex compatibility (minimal)
// ---------------------------------------------------------------------------
#define G_OBJ_RECTANGLE         0x01
#define G_OBJ_SPRITE            0x02
#define G_OBJ_MOVINGSPRITE      0x03
#define G_OBJ_LDTX_RECT         0x04
#define G_OBJ_LDTX_SPRITE       0x05
#define G_OBJ_LDTX_MOVINGSPRITE 0x06

// Vtx_t / Vtx_tn are now real structs (defined above) — typedef aliases
// retired with the wave-6 union restructure.

#endif
