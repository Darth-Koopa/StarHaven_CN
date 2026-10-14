#ifndef PLATFORM_H
#define PLATFORM_H

// Platform detection
#if defined(_3DS) || defined(ARM11) || defined(__3DS__)
    #define PLATFORM_3DS 1
    #define PLATFORM_N64   0
#elif defined(__mips__) || defined(__N64__) || defined(PLATFORM_N64)
    #define PLATFORM_3DS 0
    #define PLATFORM_N64   1
#else
    // Default to 3DS when building port
    #define PLATFORM_3DS 1
    #define PLATFORM_N64   0
#endif

// 3DS screen dimensions
#define N3DS_SCREEN_WIDTH_TOP    400
#define N3DS_SCREEN_HEIGHT_TOP   240
#define N3DS_SCREEN_WIDTH_BOT    320
#define N3DS_SCREEN_HEIGHT_BOT   240

// N64 original resolution (Paper Mario runs at 320x240 interlaced internally)
#define PM_SCREEN_WIDTH   320
#define PM_SCREEN_HEIGHT  240

// Configuration
#define PM3DS_USE_TOP_SCREEN     1
#define PM3DS_DUAL_SCREEN        0  // Experimental: show map/status on bottom
#define PM3DS_ENABLE_AUDIO       1
#define PM3DS_ENABLE_3D          0  // Stereoscopic 3D support

// Boot+HAL milestone constants (see docs/superpowers/specs/2026-05-13-papermario-3ds-boot-hal-design.md).
#define PM3DS_ENDIAN_LITTLE 1

/* Heap arena size. Sized to match Paper Mario's N64 Expansion-Pak
 * RDRAM (8 MB) — that's the maximum the original game ever sees, and
 * linearAlloc() on O3DS / Azahar default settings cannot reliably hand
 * out 32 MB after C3D_Init and gfxInitDefault have taken their slice
 * of the linear pool. The plan originally specified 32 MB; we reduced
 * after observing `FATAL: heap init (33554432 bytes) failed` on hardware. */
#define PM3DS_HEAP_BYTES    (8u * 1024u * 1024u)    /* 8 MB arena */

/* GPU command buffer per frame (C3D_Init); the default 256 KB overflowed in
 * stereo. Overridable for measurements. */
#ifndef PM3DS_CMDBUF_BYTES
#define PM3DS_CMDBUF_BYTES  (1024u * 1024u)
#endif

#define PM3DS_RETRACE_HZ    60
#define PM3DS_N64_CPU_HZ    46875000u

#endif