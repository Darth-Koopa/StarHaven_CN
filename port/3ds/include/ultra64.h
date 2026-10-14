#ifndef _PM3DS_ULTRA64_H_
#define _PM3DS_ULTRA64_H_

// 3DS Unified Ultra64 Replacement Header
// This replaces the N64 SDK headers when building for 3DS.

#include "n64/os_types.h"
#include "pm_types.h"
#include "PR/gbi.h"
#include "PR/gu.h"
#include "PR/abi.h"
#include "PR/libaudio.h"
#include "n64/os_shim.h"
#include "n64/cont_shim.h"
#include "n64/gfx_shim.h"
#include "n64/audio_shim.h"
#include "n64/math_shim.h"
#include "n64/heap_shim.h"
// M4.1: PR/os.h carries OSViMode + osViSetMode/osViSwapBuffer/etc. that
// src/main.c needs for VI setup. Include AFTER n64/os_shim.h so the real
// OSMesgQueue struct is visible before PR/os.h forward-decls it.
#include "PR/os.h"
#include "platform.h"

// Include 3DS system headers
#include <3ds.h>
#include <citro3d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

// Forward declarations for game functions that lack headers
extern s16 sins(u16 angle);
extern s16 coss(u16 angle);

// Alignment macros matching N64 style
#ifndef ALIGNED
    #define ALIGNED(x) __attribute__((aligned(x)))
#endif

// N64 memory segment macros (3DS linear allocation)
#define MAIN_SEGMENT     0
#define ENGINE_SEGMENT   1
#define BATTLE_SEGMENT   2
#define AREA_SEGMENT     3
#define EFFECTS_SEGMENT  4
#define OVERLAY_SEGMENT  5

// N64 RSP/RDP register stubs (not needed on 3DS, but referenced in code)
#define SP_DMEM_START    0x04000000
#define SP_IMEM_START    0x04001000
#define SP_MEM_ADDR_REG  0x04040000
#define SP_DRAM_ADDR_REG 0x04040004

// N64 PI (Peripheral Interface) register stubs
#define PI_BASE_REG      0x04600000
#define PI_STATUS_REG    (PI_BASE_REG+0x10)
#define PI_BSD_DOM1_LAT_REG  (PI_BASE_REG+0x14)
#define PI_BSD_DOM1_PWD_REG  (PI_BASE_REG+0x18)
#define PI_BSD_DOM1_PGS_REG  (PI_BASE_REG+0x1C)
#define PI_BSD_DOM1_RLS_REG  (PI_BASE_REG+0x20)

// N64 SI (Serial Interface) register stubs
#define SI_BASE_REG      0x04800000
#define SI_STATUS_REG    (SI_BASE_REG+0x18)

// N64 VI (Video Interface) register stubs
#define VI_BASE_REG      0x04400000
#define VI_CTRL_REG      (VI_BASE_REG+0x00)
#define VI_DRAM_ADDR_REG (VI_BASE_REG+0x08)

// N64 AI (Audio Interface) register stubs
#define AI_BASE_REG      0x04500000
#define AI_LEN_REG       (AI_BASE_REG+0x04)

// N64 cache line size
#define OS_CACHE_LINE_SIZE  16

// N64 memory size
#define OS_MEM_SIZE         (0x800000)  // 8MB Expansion Pak

// N64 boot address
#define OS_RAMDUMP_START    0x80000000

// ARRAY_COUNT macro
#define ARRAY_COUNT(arr) (s32)(sizeof(arr) / sizeof(arr[0]))

// Dead code / unused macro (only if not already defined)
#ifndef DEAD
#define DEAD  __attribute__((unused))
#endif
#ifndef STATIC
#define STATIC static
#endif
#ifndef EXTERN
#define EXTERN extern
#endif
#ifndef VOLATILE
#define VOLATILE volatile
#endif

// Version macros — default the unset ones to 0, then default VERSION_US to 1
// only if nothing else won. Decomp uses `#if VERSION_X` ladders so having two
// truthy VERSION_* macros makes the first elif win, breaking version-specific
// blocks (e.g. src/pause/pause_gfx.c VERSION_PAL block defining
// gPauseMsg_PAL_42 vs the VERSION_US block defining gPauseMsg_42 arrays).
#ifndef VERSION_JP
#define VERSION_JP 0
#endif

#ifndef VERSION_PAL
#define VERSION_PAL 0
#endif

#ifndef VERSION_IQUE
#define VERSION_IQUE 0
#endif

#ifndef VERSION_US
#if !VERSION_PAL && !VERSION_JP && !VERSION_IQUE
#define VERSION_US 1
#else
#define VERSION_US 0
#endif
#endif

// Do NOT define SHIFT on 3DS; original code uses #ifdef SHIFT to select paths
// that require generated linker symbols we don't have.
// SHIFT=0 is functionally the same as a non-shifted build.

// Region-specific TV type default
#if VERSION_JP
    #define OS_DEFAULT_TV_TYPE    OS_TV_NTSC
#elif VERSION_PAL
    #define OS_DEFAULT_TV_TYPE    OS_TV_PAL
#else
    #define OS_DEFAULT_TV_TYPE    OS_TV_NTSC
#endif

extern s32 osTvType;

// Legacy GCC compatibility (not needed on 3DS modern toolchain)
#define OLD_GCC 0

// DMA helper function declarations
void pm3ds_dma_copy_segment(const char* segment_name);
void* pm3ds_load_file(const char* path);

// 3DS main entry declaration
int pm3ds_main(void);

#endif
