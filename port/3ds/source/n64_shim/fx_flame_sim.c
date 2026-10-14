/*
 * fx_flame_sim.c — the flame effect's fire texture, computed on the CPU.
 *
 * Every frame the N64 flame effect renders a 32x64 I8 fire texture with the
 * RDP (src/effects/gfx/flame.c, D_090009E0): pass 1 blends two noise images
 * (one scrolling) into D_800B3AE0, pass 2 runs three diffusion steps that mix
 * it into last frame's D_800B32E0, which the flame quads then sample. The port
 * has no render-to-texture, so these passes were dropped and the texture
 * stayed empty: every torch and fire breath drew nothing. This replays the
 * same combiner/blender math per texel (RDP 9-bit combiner with its clamp,
 * the 2-cycle TEXEL0/TEXEL1 swap, tile shift/wrap/mirror/clamp).
 */
#include <stdint.h>
#include <string.h>

extern unsigned char D_800B32E0[0x800];   /* fire texture (sampled by the quads) */
extern unsigned char D_800B3AE0[0x800];   /* pass-1 noise mix */
/* the effect's 32x64 I4 noise images (two 32x32 halves each, linked in order) */
extern unsigned char D_09000000_353BB0[];
extern unsigned char D_09000400_353FB0[];
void gfx_tex_invalidate_range(const void *addr, size_t len);

#define W 32
#define H 64

/* (a - b) * c + d, then the RDP's 9-bit clamp: 256..383 saturate to 255,
 * 384..511 are negative and become 0 */
static int cc(int a, int b, int c, int d) {
    int v = (((a - b) * c + (d << 8) + 0x80) >> 8) & 0x1FF;
    if (v & 0x100) return (v & 0x80) ? 0 : 255;
    return v;
}

static int i4(const uint8_t *img, int x, int y) {
    uint8_t b = img[y * (W / 2) + (x >> 1)];
    int v = (x & 1) ? (b & 0xF) : (b >> 4);
    return (v << 4) | v;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* tile 1 of pass 1: shifts = 1, wrap S at 32, mirror T at 64, bilinear */
static int noise_scroll(const uint8_t *img, float s, float t) {
    int s0 = (int)(s < 0 ? s - 1.0f : s), t0 = (int)(t < 0 ? t - 1.0f : t);
    float fs = s - (float)s0, ft = t - (float)t0;
    int v[2][2];
    for (int j = 0; j < 2; j++) {
        int ty = (t0 + j) & 127;
        if (ty & 64) ty = 127 - ty;
        for (int i = 0; i < 2; i++) v[j][i] = i4(img, (s0 + i) & 31, ty);
    }
    float top = v[0][0] + (v[0][1] - v[0][0]) * fs;
    float bot = v[1][0] + (v[1][1] - v[1][0]) * fs;
    return (int)(top + (bot - top) * ft + 0.5f);
}

void pm3ds_flame_simulate(int uls, int ult) {
    const uint8_t *noise_scrolling = D_09000000_353BB0;
    const uint8_t *noise_static = D_09000400_353FB0;
    uint8_t *mix = D_800B3AE0, *fire = D_800B32E0;
    static uint8_t prev[W * H];

    /* pass 1: clear, then PM_CC_36/37 blended (CLD_SURF2) over the cleared
     * buffer: color = (t0 - t1) * t0 + t1, alpha = (a1 - t0') * t0' + t0'
     * where a1 = clamp((1 - t0) * t1 + 1) and t0' is the next pixel's texel 0 */
    float su = uls / 4.0f, tv = ult / 4.0f;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int t0 = i4(noise_static, x, y);
            int t1 = noise_scroll(noise_scrolling, x * 0.5f - su, y - tv);
            int c1 = cc(t0, t1, t0, t1);
            int a1 = cc(255, t0, t1, 255);
            int tn = i4(noise_static, clampi(x + 1, 0, W - 1), y);
            int a2 = cc(a1, tn, tn, tn);
            mix[y * W + x] = (uint8_t)((c1 * a2 + 127) / 255);
        }
    }

    /* pass 2: three OPA texrects into the fire texture, each reading the
     * previous result shifted by one texel (tile 1: wrap S, clamp T):
     * c1 = (t1 - K4) * primA + t0, a1 = (t0 - t1) * envA + envA,
     * out = (t0' - K4) * a1 + c1 with t0' the next pixel's texel 0 */
    static const int du[3] = { 1, -1, 0 }, dv[3] = { 0, 0, 1 };
    const int k4 = 92, prim_a = 0x78, env_a = 0xA4;
    for (int p = 0; p < 3; p++) {
        memcpy(prev, fire, sizeof(prev));
        for (int y = 0; y < H; y++) {
            int sy = clampi(y - dv[p], 0, H - 1 - dv[p]);
            for (int x = 0; x < W; x++) {
                int t0 = mix[y * W + x];
                int t1 = prev[sy * W + ((x - du[p]) & (W - 1))];
                int c1 = cc(t1, k4, prim_a, t0);
                int a1 = cc(t0, t1, env_a, env_a);
                int tn = mix[y * W + clampi(x + 1, 0, W - 1)];
                fire[y * W + x] = (uint8_t)cc(tn, k4, a1, c1);
            }
        }
    }
    /* the quads must not reuse last frame's upload */
    gfx_tex_invalidate_range(fire, sizeof(prev));
    gfx_tex_invalidate_range(mix, sizeof(prev));
}
