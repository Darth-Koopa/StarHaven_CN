#ifndef PM3DS_ADDR_H
#define PM3DS_ADDR_H
/*
 * pm3ds_addr.h — one rule for "is this a host pointer or a cart ROM offset?"
 *
 * dma_copy() and pm3ds_pi_read() receive both. The 3DSX image now spans
 * 0x00100000..__end__ (~0x00DD0000 since M11 links the whole world), which
 * overlaps real cart offsets, so a fixed window no longer works:
 *   - PM3DS_ROM(off) tags a port-supplied cart offset with the N64 cart PI
 *     domain base 0x10000000; tagged values are always ROM.
 *   - Untagged values inside the loaded image are host pointers.
 *   - Untagged values below 0x04000000 outside the image are legacy ROM
 *     offsets. Do not add new ones: the two-version program
 *     (tools/universal.py) spans 0x00100000..~0x02780000, so most offsets
 *     fall inside it. src/world/world.c tags the asset table for that reason.
 *     Tag in C, never via an absolute symbol: a 0x1xxxxxxx absolute symbol
 *     makes the .3dsx unloadable.
 *   - Everything else (heap 0x08000000+, linear 0x14000000+/0x30000000+)
 *     is a host pointer.
 */
#include <stdint.h>
#include <stddef.h>

#define PM3DS_ROM_TAG       0x10000000u
#define PM3DS_ROM(off)      ((uint32_t)(off) | PM3DS_ROM_TAG)

#ifndef PM3DS_HOST_TEST
extern char __end__[];
#define PM3DS_IMAGE_END     ((uintptr_t)__end__)
#else
#define PM3DS_IMAGE_END     ((uintptr_t)0x00500000u)
#endif

static inline int pm3ds_addr_is_rom_tagged(uintptr_t a) {
    return a >= PM3DS_ROM_TAG && a < PM3DS_ROM_TAG + 0x04000000u;
}

static inline int pm3ds_addr_in_image(uintptr_t a, size_t n) {
    return a >= 0x00100000u && a + n <= PM3DS_IMAGE_END;
}

/* Returns the cart offset if `a` names ROM, or (uint32_t)-1 for host memory. */
static inline uint32_t pm3ds_addr_rom_offset(uintptr_t a, size_t n) {
    if (pm3ds_addr_is_rom_tagged(a)) return (uint32_t)(a - PM3DS_ROM_TAG);
    if (pm3ds_addr_in_image(a, n)) return (uint32_t)-1;
    if (a < 0x04000000u) return (uint32_t)a;
    if (a >= 0x80000000u) return (uint32_t)(a & 0x0FFFFFFFu);  /* KSEG-style */
    return (uint32_t)-1;
}

#endif
