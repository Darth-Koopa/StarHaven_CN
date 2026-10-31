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

#include <malloc.h>

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
static size_t s_nslots;
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
    s_nslots = (size >> BLOCK_SHIFT) + 1;
    s_slot = (uint16_t *)calloc(s_nslots, sizeof *s_slot);
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

static u32 heap_free(void) {
    extern u32 __ctru_heap_size;
    struct mallinfo mi = mallinfo();
    return __ctru_heap_size - mi.arena + mi.fordblks;
}

/* os_pi.c, once the ROM is ready: what a player's log needs to tell a short heap apart */
void pm3ds_rom_log_memory(const char *when) {
    extern u32 __ctru_heap_size, __ctru_linear_heap_size;
    pm3ds_log(PM3DS_LOG_WARN, "memory %s: heap %lu KB, %lu KB free; linear %lu KB, %lu KB free; ROM cache %d KB; "
              "app region %lu KB, memtype %lu, %s", when,
              (unsigned long)(__ctru_heap_size >> 10), (unsigned long)(heap_free() >> 10),
              (unsigned long)(__ctru_linear_heap_size >> 10), (unsigned long)(linearSpaceFree() >> 10),
              s_nblocks * (int)(BLOCK_SIZE >> 10), (unsigned long)(osGetMemRegionSize(MEMREGION_APPLICATION) >> 10),
              (unsigned long)osGetApplicationMemType(), envIsHomebrew() ? "homebrew launcher" : "installed title");
}

/* platform/bottom_screen.c, the title: a New 3DS started the game with about 66 MB instead of
 * about 128 MB (GitHub #70, an installed CIA, a restart gives it back; why is still unknown) */
bool pm3ds_low_memory_start(void) {
    extern u32 __ctru_heap_size, __ctru_linear_heap_size;
    bool n3ds = false;
    APT_CheckNew3DS(&n3ds);
    return n3ds && __ctru_heap_size + __ctru_linear_heap_size < (40u << 20);
}

/* A pin that needs the cache's memory (the 10 MB audio bank): with the cache's 12 MB
 * allocated first it no longer fitted on some consoles, and the samples were then read
 * from the SD card while playing (GitHub #70). Free the cache, pin, then take the cache
 * back as large as still fits. The rest of the game needs heap too (about 1 MB after boot,
 * plus the texture uploads' staging buffers): when the pin would leave less than
 * HEAP_RESERVE beside the Old 3DS cache, it is let go again and the samples come from the
 * SD card, as before. 1.3.7 kept it and left no cache and no heap: textures went missing.
 * Only at startup, before anything reads the ROM from another thread. */
#define HEAP_RESERVE (4u << 20)

static void alloc_cache(int want, u32 keep) {
    u32 fr = heap_free();
    int n = fr > keep ? (int)((fr - keep) >> BLOCK_SHIFT) : 0;
    if (n > want) n = want;
    s_blocks = NULL;
    while (n >= 16 && !(s_blocks = (uint8_t *)malloc((size_t)n * BLOCK_SIZE))) n -= 16;
    s_nblocks = s_blocks ? n : 0;
}

uint8_t *pm3ds_rom_pin_big(uint32_t off, uint32_t len) {
#ifdef PM3DS_TEST_TIGHT_HEAP   /* test: take the heap until only PM3DS_TEST_TIGHT_HEAP MB are left */
    {
        static void *volatile chunk[512];
        int k = 0;
        while (k < 512 && (chunk[k] = malloc(1u << 20))) k++;
        for (int j = 0; j < PM3DS_TEST_TIGHT_HEAP && k > 0; j++) free(chunk[--k]);
        pm3ds_log(PM3DS_LOG_WARN, "test: holding %d MB of heap", k);
    }
#endif
    if (!s_file) return NULL;
    if (heap_free() >= len + HEAP_RESERVE) {
        uint8_t *p = pm3ds_rom_pin(off, len);   /* fits beside the cache */
        if (p) return p;
    }
    int want = s_nblocks;
    LightLock_Lock(&s_lock);
    free(s_blocks);
    s_blocks = NULL;
    s_nblocks = 0;
    LightLock_Unlock(&s_lock);
    uint8_t *p = NULL;
    if (heap_free() >= len + HEAP_RESERVE + NBLOCKS_OLD * BLOCK_SIZE) {
        p = pm3ds_rom_pin(off, len);
        if (p && heap_free() < HEAP_RESERVE + NBLOCKS_OLD * BLOCK_SIZE) {   /* fragmented: let it go */
            free(s_pins[--s_npins].data);
            p = NULL;
        }
    }
    LightLock_Lock(&s_lock);
    alloc_cache(want, HEAP_RESERVE);
    for (int i = 0; i < want; i++) {
        s_tag[i] = NO_TAG;
        s_age[i] = 0;
    }
    memset(s_slot, 0, s_nslots * sizeof *s_slot);
    s_last = -1;
    LightLock_Unlock(&s_lock);
    pm3ds_log(PM3DS_LOG_WARN, "romstream: %s, ROM cache %d KB", p ? "pinned after making the cache smaller" :
              "not enough memory to pin, samples come from the SD card", s_nblocks * (int)(BLOCK_SIZE >> 10));
    return p;
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
    if (!s_blocks) {   /* no cache memory left at all: straight from the file */
        static uint8_t tmp[4096];
        for (uint32_t done = 0; done < len;) {
            uint32_t o = (off + done) & ~3u, in = off + done - o;
            uint32_t n = sizeof tmp - in < len - done ? sizeof tmp - in : len - done;
            uint32_t rd = (in + n + 3) & ~3u;
            if (o + rd > s_size) rd = s_size - o;
            file_read(o, tmp, rd);
            memcpy(out + done, tmp + in, n);
            done += n;
        }
        LightLock_Unlock(&s_lock);
        return;
    }
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

#ifdef PM3DS_TEST_MEMSTAT   /* test: the least free heap seen, every few seconds (call per frame) */
void pm3ds_test_memstat(void) {
    extern u32 __ctru_heap_size;
    static u32 minFree = 0xFFFFFFFFu, n;
    struct mallinfo mi = mallinfo();
    u32 fr = __ctru_heap_size - mi.arena + mi.fordblks;
    if (fr < minFree) minFree = fr;
    if (++n % 300 == 0) pm3ds_log(PM3DS_LOG_WARN, "MEM heap %lu KB free %lu KB min %lu KB", (unsigned long)(__ctru_heap_size >> 10),
                                  (unsigned long)(fr >> 10), (unsigned long)(minFree >> 10));
}
#endif
