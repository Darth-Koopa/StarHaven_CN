/*
 * os_pi.c — Peripheral Interface (PI) / DMA shim.
 * Fidelity tier: MINIMAL — synchronous reads through rom_stream.c (the
 *                player's ROM file behind a block cache); the PI manager
 *                thread is collapsed away. Cart addresses the game passes
 *                (0xB0000000+offset) are read at their offset.
 */

#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include "pm3ds_log.h"
#include "pm3ds_addr.h"
#include "pm3ds_rom_layout.h"
#include "pm3ds_rom_stream.h"

/* mapfs_ROM_START/END: absolute symbols equal to the cart offsets, for the
 * version being built (see segment_storage.s for why). Kept untagged: an
 * absolute symbol at 0x1xxxxxxx makes the .3dsx unloadable (Azahar: "Failed
 * to load ROM (Error 1)"); world.c tags the asset table address in C. */
#define PM3DS_STR2(x) #x
#define PM3DS_STR(x) PM3DS_STR2(x)
__asm__(".global mapfs_ROM_START\n.global mapfs_ROM_END\n"
        ".set mapfs_ROM_START, " PM3DS_STR(PM3DS_ROM_MAPFS) "\n"
        ".set mapfs_ROM_END, " PM3DS_STR(PM3DS_ROM_MAPFS_END) "\n");

typedef void *OSMesg;
typedef struct OSMesgQueue OSMesgQueue;
int osSendMesg(OSMesgQueue *, OSMesg, int);

/* M8.6: populate the real BSS storage for ROM segments the port needs.
 * segment_storage.s provides logos_ROM_START / _END as 108 KiB of
 * .bss; we memcpy the corresponding bytes from the loaded baserom so
 * the decomp's `dma_copy(logos_ROM_START, logos_ROM_END, dest)` (now
 * also a strong override that's just memcpy) ends up putting real
 * logo image data at the destination. Extend this list as more
 * segments come online. */
extern unsigned char logos_ROM_START[];
extern unsigned char logos_ROM_END[];

static void pm3ds_populate_segments(void) {
    size_t brom_bytes = pm3ds_rom_stream_size();
    struct { const char *name; unsigned char *dst; unsigned char *dst_end;
             size_t rom_offset, rom_size; } segs[] = {
        { "logos", logos_ROM_START, logos_ROM_END, PM3DS_ROM_LOGOS, 0x1B000 },
    };
    for (unsigned i = 0; i < sizeof(segs)/sizeof(segs[0]); i++) {
        size_t end = segs[i].rom_offset + segs[i].rom_size;
        size_t avail = (size_t)(segs[i].dst_end - segs[i].dst);
        if (end > brom_bytes || avail < segs[i].rom_size) {
            pm3ds_log(PM3DS_LOG_WARN,
                      "segment '%s': baserom too small or dst short (rom=%zu/%zu, dst=%zu/%zu)",
                      segs[i].name, end, brom_bytes, avail, segs[i].rom_size);
            continue;
        }
        pm3ds_rom_read((uint32_t)segs[i].rom_offset, segs[i].dst, (uint32_t)segs[i].rom_size);
        pm3ds_log(PM3DS_LOG_INFO,
                  "segment '%s' loaded (%zu bytes from rom 0x%zx)",
                  segs[i].name, segs[i].rom_size, segs[i].rom_offset);
    }
}

/* M8.9: byte-swap the mapfs asset-table u32 fields in place.
 *
 * The mapfs asset table (PAL ROM 0x2600000) is a list of 0x1C-byte
 * AssetHeader entries: char name[16]; u32 offset; u32 compressedLength;
 * u32 decompressedLength — all u32s BIG-ENDIAN (N64 ROM order). Both
 * load_asset_by_name and get_asset_offset read these natively on
 * little-endian ARM, so without a swap the table size and per-asset
 * offsets come out byte-reversed (0x7118 -> 0x18710000), heap_malloc
 * blows up, and the unbounded name walk floods memory with ~1M reads.
 *
 * --wrap can't fix this: load_map_by_IDs calls load_asset_by_name from
 * inside world.c (same TU), an intra-module call the linker can't
 * redirect. So we fix the DATA instead — swap the three u32 fields of
 * every entry once, in a pinned RAM copy of the table (rom_stream.c serves
 * the pinned bytes to every read that overlaps them), leaving the name bytes
 * untouched (strcmp is endian-agnostic). All callers then read correct
 * little-endian values with zero per-call cost.
 *
 * The asset DATA the offsets point at stays big-endian and is handled
 * downstream (yay0.c decompresses big-endian; decoded structs are a
 * later milestone). */
#define PM3DS_MAPFS_ROM_OFFSET   ((uint32_t)PM3DS_ROM_MAPFS)
#define PM3DS_ASSET_HDR_SIZE     0x1Cu
#define PM3DS_ASSET_TABLE_FIRST  (PM3DS_MAPFS_ROM_OFFSET + 0x20u)

static uint32_t pm3ds_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}
static void pm3ds_wr32le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void pm3ds_byteswap_mapfs_table(void) {
    size_t brom_bytes = pm3ds_rom_stream_size();
    if (PM3DS_ASSET_TABLE_FIRST + PM3DS_ASSET_HDR_SIZE > brom_bytes) return;

    /* Entry 0's offset field (name[16] -> +0x10) holds the table size. */
    uint8_t first[PM3DS_ASSET_HDR_SIZE];
    pm3ds_rom_read(PM3DS_ASSET_TABLE_FIRST, first, sizeof first);
    uint32_t table_size = pm3ds_be32(first + 0x10);
    if (table_size == 0 || table_size > (16u * 1024u * 1024u) ||
        PM3DS_ASSET_TABLE_FIRST + table_size > brom_bytes) {
        pm3ds_log(PM3DS_LOG_WARN,
                  "mapfs: implausible table size 0x%x, not byte-swapping",
                  (unsigned)table_size);
        return;
    }
    uint8_t *table = pm3ds_rom_pin(PM3DS_ASSET_TABLE_FIRST, table_size);
    if (!table) return;

    uint32_t entries = table_size / PM3DS_ASSET_HDR_SIZE;
    for (uint32_t i = 0; i < entries; i++) {
        uint8_t *e = table + (size_t)i * PM3DS_ASSET_HDR_SIZE;
        pm3ds_wr32le(e + 0x10, pm3ds_be32(e + 0x10)); /* offset */
        pm3ds_wr32le(e + 0x14, pm3ds_be32(e + 0x14)); /* compressedLength */
        pm3ds_wr32le(e + 0x18, pm3ds_be32(e + 0x18)); /* decompressedLength */
    }
    pm3ds_log(PM3DS_LOG_INFO,
              "mapfs: byte-swapped %u asset-table entries (size 0x%x)",
              (unsigned)entries, (unsigned)table_size);
}

/* The ROM is open (pm3ds_rom.c): pin what must stay in RAM and fill the
 * segments the port keeps in .bss. */
void pm3ds_pi_rom_ready(void) {
    /* the audio bank: the sound engine reads samples by pointer from the
     * audio thread (audio_system.c), where an SD card wait would crackle */
    if (!pm3ds_rom_pin(PM3DS_ROM_AUDIO, PM3DS_ROM_AUDIO_END - PM3DS_ROM_AUDIO))
        pm3ds_log(PM3DS_LOG_WARN, "audio bank not pinned: samples are copied from the SD card");
    pm3ds_byteswap_mapfs_table();
    pm3ds_populate_segments();
}

size_t pm3ds_baserom_size(void) { return pm3ds_rom_stream_size(); }

typedef struct OSIoMesg {
    OSMesgQueue *mq;
    OSMesg       msg;
    void        *dramAddr;
    uint32_t     devAddr;
    uint32_t     size;
    int          direction; /* OS_READ = 0, OS_WRITE = 1 */
} OSIoMesg;

#define OS_READ  0
#define OS_WRITE 1

typedef struct OSPiHandle OSPiHandle;
struct OSPiHandle {
    int reserved;
};

static OSPiHandle g_rom_handle;

OSPiHandle *osCartRomInit(void) { return &g_rom_handle; }

int osPiStartDma(OSIoMesg *iom, int priority, int direction,
                 uint32_t devAddr, void *dramAddr, uint32_t size,
                 OSMesgQueue *mq) {
    (void)priority;
    /* Map N64 cart bus address (0xB0000000+ when virtual; treat low bits
     * as ROM offset). */
    uint32_t rom_off = devAddr & 0x0FFFFFFF;

    if (direction == OS_READ) {
        pm3ds_rom_read(rom_off, dramAddr, size);
    } else {
        /* writes to ROM are no-ops */
    }

    if (mq) osSendMesg(mq, iom ? iom->msg : NULL, /*NOBLOCK*/ 0);
    return 0;
}

int osEPiStartDma(OSPiHandle *h, OSIoMesg *iom, int direction) {
    (void)h;
    return osPiStartDma(iom, 0, direction,
                        iom->devAddr, iom->dramAddr, iom->size, iom->mq);
}

int osEPiReadIo (OSPiHandle *h, uint32_t devAddr, uint32_t *data) {
    (void)h;
    uint32_t off = devAddr & 0x0FFFFFFF;
    pm3ds_rom_read(off, data, 4);
    return 0;
}

int osEPiWriteIo(OSPiHandle *h, uint32_t devAddr, uint32_t data) {
    (void)h; (void)devAddr; (void)data;
    return 0;
}

/* On 3DS with linearAlloc-backed RDRAM, virtual == physical for GPU
 * purposes — the decomp's TLB-shaped helpers collapse to identity. */
void *osVirtualToPhysical(void *vaddr)    { return vaddr; }
void *osPhysicalToVirtual(uint32_t paddr) { return (void *)(uintptr_t)paddr; }

/* Synchronous cart read used by the texture pipeline: host pointers are
 * copied, ROM addresses go to rom_stream.c. Zeroes past the game's data. */
int pm3ds_pi_read(uint32_t addr, void *dst, uint32_t size) {
    /* M11: shared host-vs-ROM rule (pm3ds_addr.h); replaces the M8.8
     * >=0x04000000 fast path and the M10.2 fixed image window. */
    uint32_t rom = pm3ds_addr_rom_offset((uintptr_t)addr, size);
    if (rom == (uint32_t)-1) {
        memcpy(dst, (const void *)(uintptr_t)addr, size);
        return 0;
    }
    addr = rom;

    uint32_t rom_off = addr & 0x0FFFFFFF;

    pm3ds_rom_read(rom_off, dst, size);
    return 0;
}
