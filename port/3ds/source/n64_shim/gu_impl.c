/*
 * gu_impl.c — the libultra gu* routines upstream ships as MIPS asm
 * (src/os/guMtxF2L.s, guMtxIdent.s, guTranslate.s, guScale.s, guMtxCatF.s,
 * guMtxL2F.s). The C ones (guRotate.c, perspective.c, lookathil.c, ...) are
 * compiled straight from src/os/ via world_files.mk.
 *
 * Mtx LAYOUT IS PORT-SPECIFIC: the translator (gfx_geom_s15p16_to_float)
 * reads an Mtx as 16 row-major int32 s15.16 values, NOT the N64 split
 * integer-halves/fraction-halves layout. Every Mtx producer and consumer in
 * this file uses that layout, so guMtxF2L/guMtxL2F round-trip exactly.
 */
#include "common.h"

void guMtxF2L(float mf[4][4], Mtx* m) {
    s32* out = (s32*)m;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            out[i * 4 + j] = (s32)(mf[i][j] * 65536.0f);
        }
    }
}

void guMtxL2F(float mf[4][4], Mtx* m) {
    const s32* in = (const s32*)m;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            mf[i][j] = (float)in[i * 4 + j] * (1.0f / 65536.0f);
        }
    }
}

void guMtxIdent(Mtx* m) {
    float mf[4][4];
    guMtxIdentF(mf);
    guMtxF2L(mf, m);
}

// r = m * n with N64's row-vector convention; safe when r aliases m or n.
void guMtxCatF(float m[4][4], float n[4][4], float r[4][4]) {
    float tmp[4][4];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            tmp[i][j] = m[i][0] * n[0][j] + m[i][1] * n[1][j] + m[i][2] * n[2][j] + m[i][3] * n[3][j];
        }
    }
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            r[i][j] = tmp[i][j];
        }
    }
}

void guTranslateF(float mf[4][4], float x, float y, float z) {
    guMtxIdentF(mf);
    mf[3][0] = x;
    mf[3][1] = y;
    mf[3][2] = z;
}

void guTranslate(Mtx* m, float x, float y, float z) {
    float mf[4][4];
    guTranslateF(mf, x, y, z);
    guMtxF2L(mf, m);
}

void guScaleF(float mf[4][4], float x, float y, float z) {
    guMtxIdentF(mf);
    mf[0][0] = x;
    mf[1][1] = y;
    mf[2][2] = z;
}

void guScale(Mtx* m, float x, float y, float z) {
    float mf[4][4];
    guScaleF(mf, x, y, z);
    guMtxF2L(mf, m);
}

/* N64-layout Mtx -> port layout, in place: words 0-7 hold the integer halves
 * and words 8-15 the fraction halves of the 16 elements, two per word. */
static void mtx_n64_to_port(Mtx* m) {
    u32* w = (u32*)m;
    s32 out[16];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            int k = i * 2 + j / 2;
            u32 whole = (j & 1) ? (w[k] & 0xFFFF) : (w[k] >> 16);
            u32 frac = (j & 1) ? (w[8 + k] & 0xFFFF) : (w[8 + k] >> 16);
            out[i * 4 + j] = (s32)((whole << 16) | frac);
        }
    }
    memcpy(m, out, sizeof(out));
}

/* Breakable-block fragment matrices are raw N64-layout data (hex initializers
 * in src/entity/model/); convert them once before anything reads them. */
extern Mtx Entity_BombableRock_FragmentMatrices[];
extern Mtx Entity_BoardedFloor_FragmentMatrices[];
extern Mtx Entity_ShatteringHammer1Block_FragmentsMatrices[];
extern Mtx Entity_ShatteringHammer2Block_FragmentsMatrices[];
extern Mtx Entity_ShatteringHammer3Block_FragmentsMatrices[];
extern Mtx Entity_ShatteringBrickBlock_FragmentsMatrices[];
extern Mtx Entity_WoodenCrate_FragmentsMatrices[];

void pm3ds_convert_n64_mtx_arrays(void) {
    static const struct { Mtx* m; int n; } arrays[] = {
        { Entity_BombableRock_FragmentMatrices, 5 },
        { Entity_BoardedFloor_FragmentMatrices, 12 },
        { Entity_ShatteringHammer1Block_FragmentsMatrices, 26 },
        { Entity_ShatteringHammer2Block_FragmentsMatrices, 26 },
        { Entity_ShatteringHammer3Block_FragmentsMatrices, 26 },
        { Entity_ShatteringBrickBlock_FragmentsMatrices, 26 },
        { Entity_WoodenCrate_FragmentsMatrices, 46 },
    };
    for (u32 a = 0; a < ARRAY_COUNT(arrays); a++) {
        for (int i = 0; i < arrays[a].n; i++) {
            mtx_n64_to_port(&arrays[a].m[i]);
        }
    }
}
