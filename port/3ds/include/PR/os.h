#ifndef _OS_H_
#define _OS_H_

// 3DS stub for N64 PR/os.h
#include "PR/ultratypes.h"

// Thread / message / event types used by N64 game code.
// M4.1: removed the broken `typedef OSMesgQueue OSMesgQueue;` self-reference
// (was harmless when PR/os.h wasn't in the include chain). port/3ds/include/
// n64/os_shim.h provides the real struct; ultra64.h now includes os_shim.h
// before this header so callers see the struct.
struct OSMesgQueue;
typedef struct OSMesgQueue OSMesgQueue;

// Minimal OSViMode stub
typedef struct {
    u32 dummy[20];
} OSViMode;

// Framebuffer stub
typedef struct {
    void* framebuffer;
    u32 width;
    u32 height;
    u32 stride;
} OSViFieldRegs;

// Time
typedef u64 OSTime;

// Counter
typedef u32 OSCounter;

// Event
typedef u32 OSEvent;

// M4.1: OSPri thread priority — used by src/bss via PR/osint.h. Upstream
// defines it in PR/os_thread.h; port's PR/os.h is included instead and
// didn't carry the typedef. Match upstream's `typedef s32 OSPri`.
typedef s32 OSPri;

// N64 VI mode presets (referenced by symbol, never used on 3DS)
extern OSViMode osViModeNtscLan1;
extern OSViMode osViModeMpalLan1;
extern OSViMode osViModePalLan1;
extern OSViMode osViModeFpalLpn1;

// VI functions (stub declarations)
extern void osViSetMode(OSViMode* mode);
extern void osViSetYScale(f32 scale);
extern void osViSetSpecialFeatures(u32 features);
extern void osViSwapBuffer(void* frameBuf);
extern void* osViGetCurrentFramebuffer(void);
extern void osViBlack(u32 active);

// VI special features (never used)
#define OS_VI_GAMMA_OFF           0x0001
#define OS_VI_GAMMA_DITHER_OFF    0x0002
#define OS_VI_DIVOT_ON            0x0004
#define OS_VI_DITHER_FILTER_ON    0x0008
#define OS_VI_GAMMA_ON            0x0010

// Message functions
extern void osCreateMesgQueue(OSMesgQueue* mq, OSMesg* msg, s32 count);
extern s32 osSendMesg(OSMesgQueue* mq, OSMesg msg, s32 flags);
extern s32 osRecvMesg(OSMesgQueue* mq, OSMesg* msg, s32 flags);
extern s32 osJamMesg(OSMesgQueue* mq, OSMesg msg, s32 flags);

// Message flags
#define OS_MESG_NOBLOCK     0
#define OS_MESG_BLOCK       1

// Cache
extern void osInvalDCache(void* vaddr, s32 nbytes);
extern void osInvalICache(void* vaddr, s32 nbytes);
extern void osWritebackDCache(void* vaddr, s32 nbytes);
extern void osWritebackDCacheAll(void);

// Interrupts
extern void osSetIntMask(u32 im);
extern u32 osGetIntMask(void);

// Thread (types are in os_shim.h / os_types.h, but declare here too for PR compatibility)
typedef struct OSThread OSThread;
extern void osCreateThread(OSThread* t, s32 id, void (*entry)(void*), void* arg, void* sp, s32 pri);
extern void osStartThread(OSThread* t);
extern void osStopThread(OSThread* t);
extern void osYieldThread(void);
extern OSThread* osGetThreadId(void);
extern void osSetThreadPri(OSThread* t, s32 pri);
extern s32 osGetThreadPri(OSThread* t);

// EEPROM (also defined in os_shim.h, but keep compatible)
extern s32 osEepromLongRead(OSMesgQueue* mq, u8 address, u8* buffer, s32 nbytes);
extern s32 osEepromLongWrite(OSMesgQueue* mq, u8 address, u8* buffer, s32 nbytes);
extern s32 osEepromRead(OSMesgQueue* mq, u8 address, u8* buffer);
extern s32 osEepromWrite(OSMesgQueue* mq, u8 address, u8* buffer);
extern s32 osEepromProbe(OSMesgQueue* mq);

// Time / counter
extern void osSetTime(OSTime time);
extern OSTime osGetTime(void);
extern u32 osGetCount(void);

// Panic
extern void osPanic(const char* file, s32 line, const char* msg, ...);

// Misc
extern void osSyncPrintf(const char* fmt, ...);

#endif
