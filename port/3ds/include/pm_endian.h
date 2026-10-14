#ifndef PM3DS_PM_ENDIAN_H
#define PM3DS_PM_ENDIAN_H

#include <stdint.h>

/*
 * Endian conversion helpers. Paper Mario baked binary data is big-endian
 * (the N64's native order). The 3DS ARM11 is little-endian. These helpers
 * convert from BE source bytes to native host integers. On a hypothetical
 * BE host they collapse to no-ops at the compiler level.
 *
 * Usage rule (enforced by tools/check_endian_access.py): any code that
 * dereferences a baked .bin.inc.c blob must read through these helpers,
 * not via a direct cast to wide-integer pointer.
 */

#define BE16(x) ((uint16_t)__builtin_bswap16((uint16_t)(x)))
#define BE32(x) ((uint32_t)__builtin_bswap32((uint32_t)(x)))
#define BE64(x) ((uint64_t)__builtin_bswap64((uint64_t)(x)))

/* Unaligned-safe big-endian reads — useful for byte-stream parsers. */
static inline uint16_t LE_READ_BE_U16(const void *p) {
    const uint8_t *b = (const uint8_t *)p;
    return ((uint16_t)b[0] << 8) | (uint16_t)b[1];
}

static inline uint32_t LE_READ_BE_U32(const void *p) {
    const uint8_t *b = (const uint8_t *)p;
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8)  |  (uint32_t)b[3];
}

static inline uint64_t LE_READ_BE_U64(const void *p) {
    const uint8_t *b = (const uint8_t *)p;
    return ((uint64_t)b[0] << 56) | ((uint64_t)b[1] << 48) |
           ((uint64_t)b[2] << 40) | ((uint64_t)b[3] << 32) |
           ((uint64_t)b[4] << 24) | ((uint64_t)b[5] << 16) |
           ((uint64_t)b[6] << 8)  |  (uint64_t)b[7];
}

#endif /* PM3DS_PM_ENDIAN_H */
