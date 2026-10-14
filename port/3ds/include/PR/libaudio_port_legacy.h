#ifndef _LIBAUDIO_H_
#define _LIBAUDIO_H_

// 3DS stub for N64 libaudio.h
// Provides enough types to allow src/audio.h to compile.
#include "PR/ultratypes.h"
#include "PR/abi.h"

typedef s32 ALMicroTime;
typedef u8      ALPan;

typedef struct {
    u8          *base;
    u8          *cur;
    s32         len;
    s32         count;
} ALHeap;

extern void alHeapInit(ALHeap* hp, u8* base, s32 len);
extern s32 alHeapCheck(ALHeap* hp);

// Minimal stub for types referenced by src/audio.h
typedef struct {
    u32 start;
    u32 end;
    u32 count;
} ALRawLoop;

typedef struct NUDMABuffer {
    struct NUDMABuffer* next;
    s32 startAddr;
    s32 frameCnt;
    char* ptr;
} NUDMABuffer;

typedef struct {
    u8 initialized;
    NUDMABuffer* firstUsed;
    NUDMABuffer* firstFree;
} NUDMAState;


// Nusys audio defines
#define NU_AU_TASK_RUN    0
#define NU_AU_TASK_STOP   1
#define NU_AU_STACK_SIZE  0x2000

#endif
