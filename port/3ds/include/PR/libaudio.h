/*
 * 3DS port: upstream libaudio.h (M14) plus the nusys audio-DMA stand-ins the
 * port keeps (src/audio/core/system.c is replaced by
 * source/n64_shim/audio_system.c, so nusys' scheduler types are not needed).
 */
#ifndef PM3DS_PR_LIBAUDIO_WRAPPER_H
#define PM3DS_PR_LIBAUDIO_WRAPPER_H
#include "PR/ultratypes.h"
#include "PR/abi.h"
#ifndef _LANGUAGE_C
#define _LANGUAGE_C 1
#define PM3DS_LIBAUDIO_UNDEF_LANGUAGE_C
#endif
#include "../../../../include/PR/libaudio.h"
#ifdef PM3DS_LIBAUDIO_UNDEF_LANGUAGE_C
#undef _LANGUAGE_C
#undef PM3DS_LIBAUDIO_UNDEF_LANGUAGE_C
#endif

typedef struct NUDMABuffer {
    ALLink node;
    s32 startAddr;
    s32 frameCnt;
    char* ptr;
} NUDMABuffer;

typedef struct {
    u8 initialized;
    NUDMABuffer* firstUsed;
    NUDMABuffer* firstFree;
} NUDMAState;

#define NU_AU_TASK_RUN    0
#define NU_AU_TASK_STOP   1
#define NU_AU_STACK_SIZE  0x2000
#endif
