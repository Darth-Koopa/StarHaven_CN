/*
 * audio_alist.c — executes Paper Mario's audio command lists (M14).
 *
 * On the N64 the synth driver (src/audio/core/syn_driver.c) builds an
 * n_aspMain ("naudio") command list each audio frame and the RSP runs it.
 * This is a software RSP for that microcode: a 4 KB DMEM plus the handful of
 * commands the game emits. Every DMEM operand is relative to NAUDIO_BASE,
 * which is how the decomp's N_AL_* offsets are expressed (main L 0x4E0 ->
 * DMEM 0x9D0, etc.).
 *
 * Endianness: DMEM holds native-endian s16 samples. ADPCM input is a byte
 * stream, so it decodes the same either way. Everything else this code
 * reads from DRAM (codebooks, loop/decoder/resampler/envmixer states,
 * delay lines, the output buffer) is written by the CPU-side engine or by
 * this file, so it is native too; the bank loader swaps codebooks and loop
 * states when the banks are read from ROM.
 *
 * The resampler's 4-tap coefficient table is the microcode's own data.
 */
#include <stdint.h>
#include <string.h>

#include "pm3ds_log.h"

#define NAUDIO_BASE   0x4F0
#define NAUDIO_MAIN2  0x660
#define NAUDIO_COUNT  0x170          /* bytes per channel per pass (184 samples) */
#define NAUDIO_DRY_L  0x9D0
#define NAUDIO_DRY_R  0xB40
#define NAUDIO_WET_L  0xCB0
#define NAUDIO_WET_R  0xE20

#define A_INIT 0x01
#define A_LOOP 0x02
#define A_LEFT 0x02
#define A_VOL  0x04

static uint8_t  s_dmem[0x1000] __attribute__((aligned(16)));
static int16_t  s_table[0x100];      /* ADPCM codebook / pole-filter coefficients */
static uint32_t s_loop;              /* SETLOOP: ADPCM loop state address */
static int16_t  s_vol[2], s_target[2], s_dry, s_wet;
static int32_t  s_rate[2];

static const int16_t RESAMPLE_LUT[64 * 4] = {
    (int16_t)0x0c39, (int16_t)0x66ad, (int16_t)0x0d46, (int16_t)0xffdf, (int16_t)0x0b39, (int16_t)0x6696, (int16_t)0x0e5f, (int16_t)0xffd8,
    (int16_t)0x0a44, (int16_t)0x6669, (int16_t)0x0f83, (int16_t)0xffd0, (int16_t)0x095a, (int16_t)0x6626, (int16_t)0x10b4, (int16_t)0xffc8,
    (int16_t)0x087d, (int16_t)0x65cd, (int16_t)0x11f0, (int16_t)0xffbf, (int16_t)0x07ab, (int16_t)0x655e, (int16_t)0x1338, (int16_t)0xffb6,
    (int16_t)0x06e4, (int16_t)0x64d9, (int16_t)0x148c, (int16_t)0xffac, (int16_t)0x0628, (int16_t)0x643f, (int16_t)0x15eb, (int16_t)0xffa1,
    (int16_t)0x0577, (int16_t)0x638f, (int16_t)0x1756, (int16_t)0xff96, (int16_t)0x04d1, (int16_t)0x62cb, (int16_t)0x18cb, (int16_t)0xff8a,
    (int16_t)0x0435, (int16_t)0x61f3, (int16_t)0x1a4c, (int16_t)0xff7e, (int16_t)0x03a4, (int16_t)0x6106, (int16_t)0x1bd7, (int16_t)0xff71,
    (int16_t)0x031c, (int16_t)0x6007, (int16_t)0x1d6c, (int16_t)0xff64, (int16_t)0x029f, (int16_t)0x5ef5, (int16_t)0x1f0b, (int16_t)0xff56,
    (int16_t)0x022a, (int16_t)0x5dd0, (int16_t)0x20b3, (int16_t)0xff48, (int16_t)0x01be, (int16_t)0x5c9a, (int16_t)0x2264, (int16_t)0xff3a,
    (int16_t)0x015b, (int16_t)0x5b53, (int16_t)0x241e, (int16_t)0xff2c, (int16_t)0x0101, (int16_t)0x59fc, (int16_t)0x25e0, (int16_t)0xff1e,
    (int16_t)0x00ae, (int16_t)0x5896, (int16_t)0x27a9, (int16_t)0xff10, (int16_t)0x0063, (int16_t)0x5720, (int16_t)0x297a, (int16_t)0xff02,
    (int16_t)0x001f, (int16_t)0x559d, (int16_t)0x2b50, (int16_t)0xfef4, (int16_t)0xffe2, (int16_t)0x540d, (int16_t)0x2d2c, (int16_t)0xfee8,
    (int16_t)0xffac, (int16_t)0x5270, (int16_t)0x2f0d, (int16_t)0xfedb, (int16_t)0xff7c, (int16_t)0x50c7, (int16_t)0x30f3, (int16_t)0xfed0,
    (int16_t)0xff53, (int16_t)0x4f14, (int16_t)0x32dc, (int16_t)0xfec6, (int16_t)0xff2e, (int16_t)0x4d57, (int16_t)0x34c8, (int16_t)0xfebd,
    (int16_t)0xff0f, (int16_t)0x4b91, (int16_t)0x36b6, (int16_t)0xfeb6, (int16_t)0xfef5, (int16_t)0x49c2, (int16_t)0x38a5, (int16_t)0xfeb0,
    (int16_t)0xfedf, (int16_t)0x47ed, (int16_t)0x3a95, (int16_t)0xfeac, (int16_t)0xfece, (int16_t)0x4611, (int16_t)0x3c85, (int16_t)0xfeab,
    (int16_t)0xfec0, (int16_t)0x4430, (int16_t)0x3e74, (int16_t)0xfeac, (int16_t)0xfeb6, (int16_t)0x424a, (int16_t)0x4060, (int16_t)0xfeaf,
    (int16_t)0xfeaf, (int16_t)0x4060, (int16_t)0x424a, (int16_t)0xfeb6, (int16_t)0xfeac, (int16_t)0x3e74, (int16_t)0x4430, (int16_t)0xfec0,
    (int16_t)0xfeab, (int16_t)0x3c85, (int16_t)0x4611, (int16_t)0xfece, (int16_t)0xfeac, (int16_t)0x3a95, (int16_t)0x47ed, (int16_t)0xfedf,
    (int16_t)0xfeb0, (int16_t)0x38a5, (int16_t)0x49c2, (int16_t)0xfef5, (int16_t)0xfeb6, (int16_t)0x36b6, (int16_t)0x4b91, (int16_t)0xff0f,
    (int16_t)0xfebd, (int16_t)0x34c8, (int16_t)0x4d57, (int16_t)0xff2e, (int16_t)0xfec6, (int16_t)0x32dc, (int16_t)0x4f14, (int16_t)0xff53,
    (int16_t)0xfed0, (int16_t)0x30f3, (int16_t)0x50c7, (int16_t)0xff7c, (int16_t)0xfedb, (int16_t)0x2f0d, (int16_t)0x5270, (int16_t)0xffac,
    (int16_t)0xfee8, (int16_t)0x2d2c, (int16_t)0x540d, (int16_t)0xffe2, (int16_t)0xfef4, (int16_t)0x2b50, (int16_t)0x559d, (int16_t)0x001f,
    (int16_t)0xff02, (int16_t)0x297a, (int16_t)0x5720, (int16_t)0x0063, (int16_t)0xff10, (int16_t)0x27a9, (int16_t)0x5896, (int16_t)0x00ae,
    (int16_t)0xff1e, (int16_t)0x25e0, (int16_t)0x59fc, (int16_t)0x0101, (int16_t)0xff2c, (int16_t)0x241e, (int16_t)0x5b53, (int16_t)0x015b,
    (int16_t)0xff3a, (int16_t)0x2264, (int16_t)0x5c9a, (int16_t)0x01be, (int16_t)0xff48, (int16_t)0x20b3, (int16_t)0x5dd0, (int16_t)0x022a,
    (int16_t)0xff56, (int16_t)0x1f0b, (int16_t)0x5ef5, (int16_t)0x029f, (int16_t)0xff64, (int16_t)0x1d6c, (int16_t)0x6007, (int16_t)0x031c,
    (int16_t)0xff71, (int16_t)0x1bd7, (int16_t)0x6106, (int16_t)0x03a4, (int16_t)0xff7e, (int16_t)0x1a4c, (int16_t)0x61f3, (int16_t)0x0435,
    (int16_t)0xff8a, (int16_t)0x18cb, (int16_t)0x62cb, (int16_t)0x04d1, (int16_t)0xff96, (int16_t)0x1756, (int16_t)0x638f, (int16_t)0x0577,
    (int16_t)0xffa1, (int16_t)0x15eb, (int16_t)0x643f, (int16_t)0x0628, (int16_t)0xffac, (int16_t)0x148c, (int16_t)0x64d9, (int16_t)0x06e4,
    (int16_t)0xffb6, (int16_t)0x1338, (int16_t)0x655e, (int16_t)0x07ab, (int16_t)0xffbf, (int16_t)0x11f0, (int16_t)0x65cd, (int16_t)0x087d,
    (int16_t)0xffc8, (int16_t)0x10b4, (int16_t)0x6626, (int16_t)0x095a, (int16_t)0xffd0, (int16_t)0x0f83, (int16_t)0x6669, (int16_t)0x0a44,
    (int16_t)0xffd8, (int16_t)0x0e5f, (int16_t)0x6696, (int16_t)0x0b39, (int16_t)0xffdf, (int16_t)0x0d46, (int16_t)0x66ad, (int16_t)0x0c39,
};

static inline int16_t clamp16(int32_t v) {
    return v < -32768 ? -32768 : v > 32767 ? 32767 : (int16_t)v;
}
static inline int16_t *dm16(uint32_t off) { return (int16_t *)(s_dmem + (off & 0xFFE)); }
static inline uint8_t *dm8(uint32_t off) { return s_dmem + (off & 0xFFF); }
static inline void *host(uint32_t addr) { return (void *)(uintptr_t)addr; }

/* ADPCM / RESAMPLE / POLEF pack their state pointer into 24 bits (RDRAM was
 * 8 MB). Those states all live in the engine heap (AuHeapBase, see
 * audio_system.c), so the missing top byte is that heap's. */
extern unsigned char AuHeapBase[];
uint32_t pm3ds_alist_fix_addr(uint32_t a24) {
    return a24 | ((uint32_t)(uintptr_t)AuHeapBase & 0xFF000000u);
}

static void dmem_copy_in(uint32_t dmem, uint32_t addr, uint32_t count) {
    if (dmem >= 0x1000 || addr == 0) return;
    if (dmem + count > 0x1000) count = 0x1000 - dmem;
    memcpy(s_dmem + dmem, host(addr), count);
}
static void dmem_copy_out(uint32_t addr, uint32_t dmem, uint32_t count) {
    if (dmem >= 0x1000 || addr == 0) return;
    if (dmem + count > 0x1000) count = 0x1000 - dmem;
    memcpy(host(addr), s_dmem + dmem, count);
}

/* (sum over j < n of x[j] * y[n-1-j]) — the in-frame prediction term */
static int32_t rdot(int n, const int16_t *x, const int16_t *y) {
    int32_t acc = 0;
    for (int j = 0; j < n; j++) acc += x[j] * y[n - 1 - j];
    return acc;
}

static void cmd_adpcm(uint32_t w0, uint32_t w1) {
    uint32_t state;
    uint32_t flags = w1 >> 28;
    uint32_t count = ((w1 >> 16) & 0xFFF);
    uint32_t in = NAUDIO_BASE + ((w1 >> 12) & 0xF);
    uint32_t out = NAUDIO_BASE + (w1 & 0xFFF);
    int16_t last[16];

    extern uint32_t pm3ds_alist_fix_addr(uint32_t a24);
    state = pm3ds_alist_fix_addr(w0 & 0x00FFFFFF);
    count = (count + 31) & ~31u;

    if (flags & A_INIT) memset(last, 0, sizeof(last));
    else memcpy(last, host((flags & A_LOOP) ? s_loop : state), sizeof(last));

    for (int i = 0; i < 16; i++, out += 2) *dm16(out) = last[i];

    while (count) {
        uint8_t code = *dm8(in++);
        int scale = code >> 4;
        const int16_t *book1 = s_table + ((code & 0xF) << 4);
        const int16_t *book2 = book1 + 8;
        int rshift = scale < 12 ? 12 - scale : 0;
        int16_t frame[16];
        for (int i = 0; i < 8; i++) {
            uint8_t b = *dm8(in++);
            frame[i * 2]     = (int16_t)((int16_t)((uint16_t)(b & 0xF0) << 8) >> rshift);
            frame[i * 2 + 1] = (int16_t)((int16_t)((uint16_t)(b & 0x0F) << 12) >> rshift);
        }
        for (int half = 0; half < 2; half++) {
            const int16_t *src = frame + half * 8;
            int16_t l1 = half ? last[6] : last[14];
            int16_t l2 = half ? last[7] : last[15];
            int16_t res[8];
            for (int i = 0; i < 8; i++) {
                int32_t acc = (int32_t)src[i] << 11;
                acc += book1[i] * l1 + book2[i] * l2 + rdot(i, book2, src);
                res[i] = clamp16(acc >> 11);
            }
            memcpy(last + half * 8, res, sizeof(res));
        }
        for (int i = 0; i < 16; i++, out += 2) *dm16(out) = last[i];
        count -= 32;
    }
    memcpy(host(state), last, sizeof(last));
}

static void cmd_resample(uint32_t w0, uint32_t w1) {
    extern uint32_t pm3ds_alist_fix_addr(uint32_t a24);
    uint32_t state = pm3ds_alist_fix_addr(w0 & 0x00FFFFFF);
    uint32_t flags = w1 >> 30;
    uint32_t pitch = ((w1 >> 14) & 0xFFFF) << 1;
    uint32_t in = NAUDIO_BASE + ((w1 >> 2) & 0xFFF);
    uint32_t out = (w1 & 3) ? NAUDIO_MAIN2 : NAUDIO_BASE;
    int16_t *st = (int16_t *)host(state);
    uint32_t ipos = (in >> 1) - 4, opos = out >> 1;
    uint32_t acc;

    if (flags & A_INIT) {
        for (int k = 0; k < 4; k++) ((int16_t *)s_dmem)[(ipos + k) & 0x7FF] = 0;
        acc = 0;
    } else {
        for (int k = 0; k < 4; k++) ((int16_t *)s_dmem)[(ipos + k) & 0x7FF] = st[k];
        acc = (uint16_t)st[4];
    }
    for (int n = NAUDIO_COUNT >> 1; n; n--) {
        const int16_t *lut = RESAMPLE_LUT + ((acc & 0xFC00) >> 8);
        int16_t *s = (int16_t *)s_dmem;
        int32_t v = s[ipos & 0x7FF] * lut[0] + s[(ipos + 1) & 0x7FF] * lut[1] +
                    s[(ipos + 2) & 0x7FF] * lut[2] + s[(ipos + 3) & 0x7FF] * lut[3];
        s[opos++ & 0x7FF] = clamp16(v >> 15);
        acc += pitch;
        ipos += acc >> 16;
        acc &= 0xFFFF;
    }
    for (int k = 0; k < 4; k++) st[k] = ((int16_t *)s_dmem)[(ipos + k) & 0x7FF];
    st[4] = (int16_t)acc;
}

typedef struct { int64_t value, step, target; } ramp_t;

static int16_t ramp_step(ramp_t *r) {
    r->value += r->step;
    int reached = r->step <= 0 ? r->value <= r->target : r->value >= r->target;
    if (reached) { r->value = r->target; r->step = 0; }
    return (int16_t)(r->value >> 16);
}

static inline void mix_into(int16_t *dst, int16_t src, int16_t gain) {
    *dst = clamp16(*dst + ((src * gain) >> 15));
}

static void cmd_envmixer(uint32_t w0, uint32_t w1) {
    uint32_t flags = (w0 >> 16) & 0xFF;
    int16_t *save = (int16_t *)host(w1);   /* 80 bytes; full 32-bit field */
    ramp_t r[2];
    int16_t dry = s_dry, wet = s_wet;

    s_vol[1] = (int16_t)w0;
    if (flags & A_INIT) {
        r[0].step = s_rate[0] / 8; r[0].value = (int64_t)s_vol[0] << 16; r[0].target = (int64_t)s_target[0] << 16;
        r[1].step = s_rate[1] / 8; r[1].value = (int64_t)s_vol[1] << 16; r[1].target = (int64_t)s_target[1] << 16;
    } else {
        int32_t v;
        wet = save[0];
        dry = save[1];
        r[0].target = (int64_t)save[2] << 16;
        r[1].target = (int64_t)save[3] << 16;
        memcpy(&v, &save[4], 4);  r[0].step = v;
        memcpy(&v, &save[6], 4);  r[1].step = v;
        memcpy(&v, &save[8], 4);  r[0].value = v;
        memcpy(&v, &save[10], 4); r[1].value = v;
    }
    const int16_t *in = dm16(NAUDIO_BASE);
    int16_t *dl = dm16(NAUDIO_DRY_L), *dr = dm16(NAUDIO_DRY_R);
    int16_t *wl = dm16(NAUDIO_WET_L), *wr = dm16(NAUDIO_WET_R);
    for (int k = 0; k < NAUDIO_COUNT / 2; k++) {
        int16_t lv = ramp_step(&r[0]), rv = ramp_step(&r[1]);
        int16_t s = in[k];
        mix_into(&dl[k], s, clamp16((lv * dry + 0x4000) >> 15));
        mix_into(&dr[k], s, clamp16((rv * dry + 0x4000) >> 15));
        mix_into(&wl[k], s, clamp16((lv * wet + 0x4000) >> 15));
        mix_into(&wr[k], s, clamp16((rv * wet + 0x4000) >> 15));
    }
    int32_t v;
    save[0] = wet;
    save[1] = dry;
    save[2] = (int16_t)(r[0].target >> 16);
    save[3] = (int16_t)(r[1].target >> 16);
    v = (int32_t)r[0].step;  memcpy(&save[4], &v, 4);
    v = (int32_t)r[1].step;  memcpy(&save[6], &v, 4);
    v = (int32_t)r[0].value; memcpy(&save[8], &v, 4);
    v = (int32_t)r[1].value; memcpy(&save[10], &v, 4);
}

static void cmd_polef(uint32_t w0, uint32_t w1) {
    extern uint32_t pm3ds_alist_fix_addr(uint32_t a24);
    uint32_t flags = (w0 >> 16) & 0xFF;
    int16_t gain = (int16_t)(w0 & 0xFFFF);
    uint32_t dmem = (w1 >> 24) ? NAUDIO_MAIN2 : NAUDIO_BASE;
    int16_t *st = (int16_t *)host(pm3ds_alist_fix_addr(w1 & 0x00FFFFFF));
    const int16_t *h1 = s_table;
    int16_t h2[8], h2_before[8];
    int16_t l1, l2;

    for (int i = 0; i < 8; i++) {
        h2_before[i] = s_table[8 + i];
        h2[i] = (int16_t)(((int32_t)s_table[8 + i] * gain) >> 14);
    }
    if (flags & A_INIT) { l1 = 0; l2 = 0; }
    else { l1 = st[2]; l2 = st[3]; }
    int16_t *p = dm16(dmem);
    int16_t last4[4] = { 0, 0, 0, 0 };
    for (int n = NAUDIO_COUNT / 16; n; n--) {
        int16_t frame[8];
        memcpy(frame, p, sizeof(frame));
        for (int i = 0; i < 8; i++) {
            int32_t acc = frame[i] * gain + h1[i] * l1 + h2_before[i] * l2 + rdot(i, h2, frame);
            p[i] = clamp16(acc >> 14);
        }
        l1 = p[6];
        l2 = p[7];
        memcpy(last4, p + 4, sizeof(last4));
        p += 8;
    }
    memcpy(st, last4, sizeof(last4));
}

unsigned pm3ds_alist_cmds, pm3ds_alist_unknown;

void pm3ds_alist_run(const uint32_t *cmds, int nwords) {
    for (int i = 0; i + 1 < nwords; i += 2) {
        uint32_t w0 = cmds[i], w1 = cmds[i + 1];
        pm3ds_alist_cmds++;
        switch (w0 >> 24) {
            case 0: break;                                                  /* SPNOOP */
            case 1: cmd_adpcm(w0, w1); break;                               /* ADPCM */
            case 2: {                                                       /* CLEARBUFF */
                uint32_t d = NAUDIO_BASE + (w0 & 0xFFFF), c = w1 & 0xFFF;
                if (d < 0x1000) memset(s_dmem + d, 0, d + c > 0x1000 ? 0x1000 - d : c);
                break;
            }
            case 3: cmd_envmixer(w0, w1); break;                            /* ENVMIXER */
            case 4:                                                         /* LOADBUFF */
                dmem_copy_in(NAUDIO_BASE + (w0 & 0xFFF), w1, ((w0 >> 12) & 0xFFF));
                break;
            case 5: cmd_resample(w0, w1); break;                            /* RESAMPLE */
            case 6:                                                         /* SAVEBUFF */
                dmem_copy_out(w1, NAUDIO_BASE + (w0 & 0xFFF), ((w0 >> 12) & 0xFFF));
                break;
            case 9: {                                                       /* SETVOL */
                uint32_t f = (w0 >> 16) & 0xFF;
                if (f & A_VOL) {
                    if (f & A_LEFT) { s_vol[0] = (int16_t)w0; s_dry = (int16_t)(w1 >> 16); s_wet = (int16_t)w1; }
                    else { s_target[1] = (int16_t)w0; s_rate[1] = (int32_t)w1; }
                } else { s_target[0] = (int16_t)w0; s_rate[0] = (int32_t)w1; }
                break;
            }
            case 10: {                                                      /* DMEMMOVE */
                uint32_t di = NAUDIO_BASE + (w0 & 0xFFFF);
                uint32_t dO = NAUDIO_BASE + (w1 >> 16);
                uint32_t c = ((w1 & 0xFFFF) + 3) & ~3u;
                if (di < 0x1000 && dO < 0x1000) {
                    if (di + c > 0x1000) c = 0x1000 - di;
                    if (dO + c > 0x1000) c = 0x1000 - dO;
                    memmove(s_dmem + dO, s_dmem + di, c);
                }
                break;
            }
            case 11: {                                                      /* LOADADPCM */
                uint32_t c = w0 & 0xFFFF;
                if (c > sizeof(s_table)) c = sizeof(s_table);
                if (w1) memcpy(s_table, host(w1), c);
                break;
            }
            case 12: {                                                      /* MIXER */
                int16_t gain = (int16_t)(w0 & 0xFFFF);
                int16_t *src = dm16(NAUDIO_BASE + (w1 >> 16));
                int16_t *dst = dm16(NAUDIO_BASE + (w1 & 0xFFFF));
                for (int k = 0; k < NAUDIO_COUNT / 2; k++) mix_into(&dst[k], src[k], gain);
                break;
            }
            case 13: {                                                      /* INTERLEAVE */
                int16_t tmp[NAUDIO_COUNT];
                const int16_t *l = dm16(NAUDIO_DRY_L), *r = dm16(NAUDIO_DRY_R);
                for (int k = 0; k < NAUDIO_COUNT / 2; k++) { tmp[2 * k] = l[k]; tmp[2 * k + 1] = r[k]; }
                memcpy(dm16(NAUDIO_BASE), tmp, sizeof(tmp));
                break;
            }
            case 14: cmd_polef(w0, w1); break;                              /* POLEF */
            case 15: s_loop = w1; break;                                    /* SETLOOP */
            default:
                if (pm3ds_alist_unknown++ < 4)
                    pm3ds_log(PM3DS_LOG_WARN, "alist: unknown command %08X %08X", (unsigned)w0, (unsigned)w1);
                break;
        }
    }
}
