#ifndef PM3DS_GFX_F3DEX2_H
#define PM3DS_GFX_F3DEX2_H

#include <stdint.h>

/* F3DEX2 Gfx command: 64-bit little-or-big-endian-depending-on-host word.
 * On the N64 these are big-endian; we'll byteswap at submit time so the
 * translator sees host-endian 32-bit halves throughout. */
typedef struct {
    uint32_t w0;
    uint32_t w1;
} Gfx;

/* Top 8 bits of w0 are the opcode. */
#define GFX_OPCODE(g) (((g).w0 >> 24) & 0xFF)

/* The subset Paper Mario actually emits, taken from the decomp's gbi.h.
 * Numerical values match libultra; do not renumber. */
#define G_NOOP              0x00
#define G_RDPHALF_2         0xF1
#define G_SETOTHERMODE_H    0xE3
#define G_SETOTHERMODE_L    0xE2
#define G_RDPHALF_1         0xE1
#define G_SETSCISSOR        0xED
#define G_SETPRIMDEPTH      0xEE
#define G_RDPSETOTHERMODE   0xEF
#define G_LOADTLUT          0xF0
#define G_SETTILESIZE       0xF2
#define G_LOADBLOCK         0xF3
#define G_LOADTILE          0xF4
#define G_SETTILE           0xF5
#define G_FILLRECT          0xF6
#define G_SETFILLCOLOR      0xF7
#define G_SETFOGCOLOR       0xF8
#define G_SETBLENDCOLOR     0xF9
#define G_SETPRIMCOLOR      0xFA
#define G_SETENVCOLOR       0xFB
#define G_SETCOMBINE        0xFC
#define G_SETTIMG           0xFD
#define G_SETZIMG           0xFE
#define G_SETCIMG           0xFF
#define G_TEXRECT           0xE4
#define G_TEXRECTFLIP       0xE5

#define G_VTX               0x01
#define G_TRI1              0x05
#define G_TRI2              0x06
#define G_QUAD              0x07
#define G_BRANCH_Z          0x04
#define G_DL                0xDE
#define G_ENDDL             0xDF
#define G_SETGEOMETRYMODE   0xD9
#define G_CLEARGEOMETRYMODE 0xD9   /* same opcode, sign bit differentiates */
#define G_SP_MATRIX         0xDA
#define G_POPMTX            0xD8
#define G_VIEWPORT          0xDC
#define G_LIGHT             0xDC
#define G_NUMLIGHTS         0xDC
#define G_LOOKAT            0xDC
#define G_MOVEWORD          0xDB

/* SETOTHERMODE_L bits we care about (cycle-type, alpha-compare, render-mode
 * lo half) — names match libultra. */
#define G_MDSFT_RENDERMODE  3
#define G_MDSFT_ALPHACOMPARE 0
#define G_MDSFT_ZSRCSEL     2

/* SETOTHERMODE_H bits (cycle-type hi, texture filter, perspective, lod). */
#define G_MDSFT_CYCLETYPE   20
#define G_MDSFT_TEXTFILT    12
#define G_MDSFT_PERSPECTIVE 19
#define G_MDSFT_TEXTLOD     16

/* Geometry mode bits. */
#define G_ZBUFFER           0x00000001
#define G_SHADE             0x00000004
#define G_CULL_FRONT        0x00000200
#define G_CULL_BACK         0x00000400
#define G_FOG               0x00010000
#define G_LIGHTING          0x00020000
#define G_TEXTURE_GEN       0x00040000
#define G_SHADING_SMOOTH    0x00200000

/* Pull a bitfield from a 32-bit word. */
static inline uint32_t gfx_bf(uint32_t w, int shift, int width) {
    return (w >> shift) & ((1u << width) - 1u);
}

#endif /* PM3DS_GFX_F3DEX2_H */
