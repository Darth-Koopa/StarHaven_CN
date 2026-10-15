/*
 * rom_stream.c — the cart ROM, read from the player's file on demand.
 *
 * The ROM used to sit in RAM whole (48 MB for Europe). Now pm3ds_rom.c checks
 * the file once at boot and hands it over here, and every cart read goes
 * through a small block cache, converted to .z64 byte order per block.
 *
 * Two kinds of data are pinned in RAM instead:
 *   - the audio bank (PM3DS_ROM_AUDIO..PM3DS_ROM_AUDIO_END): the sound engine
 *     reads instrument samples through pointers, from the audio thread, about
 *     800 times a second; an SD card wait there would crackle;
 *   - the map asset table, which os_pi.c byte-swaps in place.
 * Pinned bytes win over the file wherever a read overlaps them.
 *
 * Measured before the change (attract demo, 140 s): ~110k audio reads in the
 * bank, ~40k map asset reads, ~2k sprite reads, nearly nothing elsewhere.
 */
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pm3ds_log.h"
#include "pm3ds_rom_stream.h"

#define BLOCK_SHIFT 15
#define BLOCK_SIZE  (1u << BLOCK_SHIFT)   /* 32 KB */
/* 2 MB of cache; 12 MB on the New 3DS, where a map left for a battle is
 * still cached when the battle ends (each 32 KB miss is ~6 ms on the SD card) */
#define NBLOCKS_OLD 64
#define NBLOCKS_NEW 384
#define MAX_PINS    4
#define NO_TAG      0xFFFFFFFFu

typedef struct { uint32_t off, len; uint8_t *data; } pin_t;

static pin_t s_pins[MAX_PINS];
static int s_npins;
static FILE *s_file;
static int s_order;
static uint32_t s_size;
static uint8_t *s_blocks;
static int s_nblocks;
static uint32_t *s_tag, *s_age, s_clock;
static uint16_t *s_slot;   /* per ROM block: its cache slot + 1, or 0 */
static int s_last = -1;
static LightLock s_lock;
static uint32_t s_hits, s_misses;
static uint64_t s_miss_ticks;

/* .v64: 16-bit halves swapped; .n64: 32-bit words reversed (n multiple of 4) */
static void to_z64(uint8_t *p, size_t n, int order) {
    uint32_t *w = (uint32_t *)p;
    if (order == PM3DS_ORDER_V64) {
        for (size_t i = 0; i < n / 4; i++) w[i] = ((w[i] & 0x00FF00FFu) << 8) | ((w[i] >> 8) & 0x00FF00FFu);
    } else if (order == PM3DS_ORDER_N64) {
        for (size_t i = 0; i < n / 4; i++) w[i] = __builtin_bswap32(w[i]);
    }
}

/* off and len multiples of 4 (block and pin bounds are) */
static bool file_read(uint32_t off, uint8_t *dst, uint32_t len) {
    if (fseek(s_file, (long)off, SEEK_SET) != 0 || fread(dst, 1, len, s_file) != len) {
        memset(dst, 0, len);
        pm3ds_log(PM3DS_LOG_ERROR, "romstream: read of 0x%lX+%lu failed", (unsigned long)off, (unsigned long)len);
        return false;
    }
    to_z64(dst, len, s_order);
    return true;
}

bool pm3ds_rom_stream_open(const char *path, int order, uint32_t size) {
    if (s_file) return true;
    LightLock_Init(&s_lock);
    bool n3ds = false;
    APT_CheckNew3DS(&n3ds);
    s_nblocks = n3ds ? NBLOCKS_NEW : NBLOCKS_OLD;
    s_blocks = (uint8_t *)malloc((size_t)s_nblocks * BLOCK_SIZE);
    if (!s_blocks && s_nblocks > NBLOCKS_OLD) {
        s_nblocks = NBLOCKS_OLD;
        s_blocks = (uint8_t *)malloc((size_t)s_nblocks * BLOCK_SIZE);
    }
    s_tag = (uint32_t *)malloc(s_nblocks * sizeof *s_tag);
    s_age = (uint32_t *)calloc(s_nblocks, sizeof *s_age);
    s_slot = (uint16_t *)calloc((size >> BLOCK_SHIFT) + 1, sizeof *s_slot);
    s_file = s_blocks && s_tag && s_age && s_slot ? fopen(path, "rb") : NULL;
    if (!s_file) {
        free(s_blocks);
        free(s_tag);
        free(s_age);
        free(s_slot);
        s_blocks = NULL;
        pm3ds_log(PM3DS_LOG_ERROR, "romstream: cannot open %s", path);
        return false;
    }
    setvbuf(s_file, NULL, _IONBF, 0);   /* whole blocks: no stdio copy */
    s_order = order;
    s_size = size & ~3u;
    for (int i = 0; i < s_nblocks; i++) s_tag[i] = NO_TAG;
    pm3ds_log(PM3DS_LOG_INFO, "romstream: %s, %lu KB cache", path, (unsigned long)((size_t)s_nblocks * BLOCK_SIZE >> 10));
    return true;
}

bool pm3ds_rom_stream_active(void) { return s_file != NULL; }
uint32_t pm3ds_rom_stream_size(void) { return s_size; }

uint8_t *pm3ds_rom_pin(uint32_t off, uint32_t len) {
    uint32_t a = off & ~3u, b = (off + len + 3) & ~3u;
    if (!s_file || s_npins == MAX_PINS || b > s_size || a >= b) return NULL;
    uint8_t *data = (uint8_t *)malloc(b - a);
    if (!data) {
        pm3ds_log(PM3DS_LOG_ERROR, "romstream: no memory to pin 0x%lX+%lu", (unsigned long)a, (unsigned long)(b - a));
        return NULL;
    }
    LightLock_Lock(&s_lock);
    file_read(a, data, b - a);
    s_pins[s_npins++] = (pin_t){ a, b - a, data };
    LightLock_Unlock(&s_lock);
    pm3ds_log(PM3DS_LOG_INFO, "romstream: pinned 0x%lX..0x%lX (%lu KB)", (unsigned long)a, (unsigned long)b,
              (unsigned long)((b - a) >> 10));
    return data + (off - a);
}

const uint8_t *pm3ds_rom_ptr(uint32_t off, uint32_t len) {
    for (int i = 0; i < s_npins; i++)
        if (off >= s_pins[i].off && off - s_pins[i].off + len <= s_pins[i].len)
            return s_pins[i].data + (off - s_pins[i].off);
    return NULL;
}

/* the cache slot holding block blk, loading it on a miss (lock held) */
static const uint8_t *block(uint32_t blk) {
    if (s_last >= 0 && s_tag[s_last] == blk) {
        s_age[s_last] = ++s_clock;
        s_hits++;
        return s_blocks + (size_t)s_last * BLOCK_SIZE;
    }
    int i = (int)s_slot[blk] - 1;
    if (i >= 0) {
        s_age[i] = ++s_clock;
        s_last = i;
        s_hits++;
        return s_blocks + (size_t)i * BLOCK_SIZE;
    }
    int victim = 0;
    for (i = 1; i < s_nblocks; i++)
        if (s_age[i] < s_age[victim]) victim = i;
    if (s_tag[victim] != NO_TAG) s_slot[s_tag[victim]] = 0;
    uint64_t t0 = svcGetSystemTick();
    uint32_t start = blk << BLOCK_SHIFT;
    uint32_t n = s_size - start < BLOCK_SIZE ? s_size - start : BLOCK_SIZE;
    uint8_t *dst = s_blocks + (size_t)victim * BLOCK_SIZE;
    file_read(start, dst, n);
    s_tag[victim] = blk;
    s_slot[blk] = (uint16_t)(victim + 1);
    s_age[victim] = ++s_clock;
    s_last = victim;
    s_misses++;
    s_miss_ticks += svcGetSystemTick() - t0;
    return dst;
}

void pm3ds_rom_read(uint32_t off, void *dst, uint32_t len) {
    uint8_t *out = (uint8_t *)dst;
    if (!s_file || off >= s_size) {
        memset(out, 0, len);
        return;
    }
    if (len > s_size - off) {   /* past the game's data: zeros, as before */
        memset(out + (s_size - off), 0, len - (s_size - off));
        len = s_size - off;
    }
    const uint8_t *p = pm3ds_rom_ptr(off, len);
    if (p) {
        memcpy(out, p, len);
        return;
    }
    LightLock_Lock(&s_lock);
    for (uint32_t done = 0; done < len;) {
        uint32_t o = off + done;
        uint32_t in = o & (BLOCK_SIZE - 1);
        uint32_t n = BLOCK_SIZE - in < len - done ? BLOCK_SIZE - in : len - done;
        memcpy(out + done, block(o >> BLOCK_SHIFT) + in, n);
        done += n;
    }
    LightLock_Unlock(&s_lock);
    for (int i = 0; i < s_npins; i++) {   /* pinned bytes win (the swapped map table) */
        uint32_t a = s_pins[i].off > off ? s_pins[i].off : off;
        uint32_t b = s_pins[i].off + s_pins[i].len < off + len ? s_pins[i].off + s_pins[i].len : off + len;
        if (a < b) memcpy(out + (a - off), s_pins[i].data + (a - s_pins[i].off), b - a);
    }
}

void pm3ds_rom_stream_stats(uint32_t *hits, uint32_t *misses, uint32_t *miss_ms) {
    *hits = s_hits;
    *misses = s_misses;
    *miss_ms = (uint32_t)(s_miss_ticks / (SYSCLOCK_ARM11 / 1000));
}
