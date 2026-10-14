#ifndef _NUSYS_H_
#define _NUSYS_H_

// ============================================================================
// NuSystem Shim for 3DS Paper Mario Port
// ============================================================================
// NuSystem is Nintendo's higher-level library on top of libultra.
// It provides graphics, controller, and audio task management.
// This 3DS shim replaces all NuSystem APIs with libctru equivalents.
// ============================================================================

#include "n64/os_types.h"
#include "n64/os_shim.h"
#include "n64/cont_shim.h"
#include "n64/gfx_shim.h"
#include "n64/audio_shim.h"
#include "platform.h"

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// NuSystem types
// ---------------------------------------------------------------------------

// NuSystem callback list (already declared in cont_shim.h)
#include "n64/cont_shim.h"

// Extended controller data (with analog as floats)
typedef struct {
    u16     button;
    s8      stick_x;
    s8      stick_y;
    u8      errno;
    f32     stick_x_f;
    f32     stick_y_f;
} NUContData;

// Graphics yield buffer size
#define NU_GFX_YIELD_BUF_SIZE   0x1000

// M4.1: nusys constants + scheduler type for src/bss BSS allocations.
// Sizes match upstream so the BSS layout reserves the correct amount
// of memory; runtime use of these subsystems is M5+.
#ifndef NU_CONT_MAXCONTROLLERS
#define NU_CONT_MAXCONTROLLERS 4
#endif
#ifndef NU_GFX_RDP_OUTPUTBUFF_SIZE
#define NU_GFX_RDP_OUTPUTBUFF_SIZE 0x10000
#endif
#ifndef NU_SC_STACK_SIZE
#define NU_SC_STACK_SIZE 0x2000
#endif

// nusys scheduler — opaque stub.
typedef struct NUSched {
    u8 storage[0x100];
} NUSched;

// ---------------------------------------------------------------------------
// NuSystem globals (declared in original code, defined in shim impl files)
// ---------------------------------------------------------------------------

extern OSMesgQueue      nuGfxMesgQ;
extern NUUcode*         nuGfxUcode;
extern u16**            nuGfxCfb;
extern u16*             nuGfxCfb_ptr;
extern u32              nuGfxCfbNum;
extern u16*             nuGfxZBuffer;
extern volatile u32     nuGfxTaskSpool;
extern u32              nuGfxDisplay;
extern u32              nuGfxCfbCounter;
extern OSThread         nuGfxThread;
extern s32              nuGfxUcodeFifoSize;
extern u64*             nuGfxUcodeFifoPtr;
extern NUGfxSwapCfbFunc nuGfxSwapCfbFunc;
extern NUGfxFunc        nuGfxFunc;
extern NUGfxPreNMIFunc  nuGfxPreNMIFunc;
extern NUGfxTaskEndFunc nuGfxTaskEndFunc;

extern OSContStatus     nuContStatus[4];
extern OSContPad        nuContData[4];
extern u32              nuContNum;
extern u32              nuContDataLockKey;
extern OSMesgQueue      nuContWaitMesgQ;
extern OSPfs            nuContPfs[4];
extern NUCallBackList   nuContCallBack;
extern u16              nuContPakCompanyCode;
extern u32              nuContPakGameCode;
extern NUCallBackList   nuContPakCallBack;
extern NUContRmbCtl     nuContRmbCtl[4];
extern u32              nuContRmbSearchTime;
extern NUCallBackList   nuContRmbCallBack;
extern NUCallBackList   nuContGBPakCallBack;

extern NUScTask         nuGfxTask[NU_GFX_TASK_NUM];

// ---------------------------------------------------------------------------
// NuSystem function declarations (implemented in shims)
// ---------------------------------------------------------------------------

// Graphics
void nuGfxInit(void);
void nuGfxInitEX2(void);
void nuGfxThreadStart(void);
void nuGfxTaskMgrInit(void);
void nuGfxTaskStart(Gfx *gfxList_ptr, u32 gfxListSize, u32 ucode, u32 flag);
void nuGfxFuncSet(NUGfxFunc func);
void nuGfxPreNMIFuncSet(NUGfxPreNMIFunc func);
void nuGfxSwapCfbFuncSet(NUGfxSwapCfbFunc func);
void nuGfxSetCfb(u16** framebuf, u32 framebufnum);
void nuGfxSwapCfb(void* framebuffer);
void nuGfxSetUcodeFifo(void* fifoBufPtr, s32 size);
void nuGfxTaskEndFuncSet(NUGfxTaskEndFunc func);
void nuGfxTaskAllEndWait(void);
void nuGfxRetraceWait(u32 retrace_num);
void nuGfxDisplayOff(void);
void nuGfxDisplayOn(void);

// Controller
u8   nuContInit(void);
s32  nuContMgr(void* mesg);
u8   nuContMgrInit(void);
void nuContMgrRemove(void);
void nuContDataLock(void);
void nuContDataUnLock(void);
void nuContDataRead(OSContPad* pad);
void nuContDataReadWait(void);
void nuContDataReadStart(void);
void nuContDataGet(OSContPad* contdata, u32 padno);
void nuContDataGetAll(OSContPad* contdata);
void nuContDataGetEx(NUContData *contdata, u32 padno);
void nuContDataGetExAll(NUContData *contdata);
void nuContReadFuncSet(void (*func)(void));
void nuContQueryRead(void);
void nuContDataClose(void);
void nuContDataOpen(void);

// Controller Pak
s32  nuContPakMgr(void* mesg_type);
void nuContPakMgrInit(void);
void nuContPakMgrRemove(void);
void nuContPakJisToNis(u8* src, u8* dest, u32 len);
void nuContPakNisToJis(u8* src, u8* dest, u32 len);
s32  nuContPakOpen(void* file, u32 contNo);
s32  nuContPakGetFree(void* file);
void nuContPakCodeSet(u8* companyCode, u8* gameCode);
s32  nuContPakRepairId(void* file);
s32  nuContPakFileOpen(void* file, u8* noteName, u8* extName, u32 size);
s32  nuContPakFileOpenJis(void* file, u8* noteName, u8* extName, u32 size);
s32  nuContPakFileReadWrite(void* file, u32 flag, u32 offset, u8* buffer, u32 size);
s32  nuContPakFileDelete(void* file, u8* noteName, u8* extName);
s32  nuContPakFileState(void* file, void* state);
s32  nuContPakFileNum(void* file, s32* maxFiles, s32* filesUsed);
s32 nuContPakFileFread(void* file, s32 offset, s32 size, u8* buf);

// Rumble
void nuContRmbMgrInit(void);
s32  nuContRmbMgr(void* mesg);
void nuContRmbMgrRemove(void);
void nuContRmbStart(u32 contNo, u16 freq, u16 frame);
void nuContRmbStop(u32 contNo);
s32  nuContRmbCheck(u32 contNo);
void nuContRmbModeSet(u32 contNo, u8 mode);
void nuContRmbForceStop(void);
void nuContRmbForceStopEnd(void);
void nuContRmbSearchTimeSet(u32 frame);

// GBA Pak
s32 nuContGBPakMgr(void* mesg);
void nuContGBPakMgrInit(void);
void nuContGBPakMgrRemove(void);
s32 nuContGBPakOpen(void* handle, s32 contNo);
s32 nuContGBPakGetStatus(void* handle, u8* status);
s32 nuContGBPakReadID(void* handle, void* id, u8* status);

// PI ROM
void nuPiReadRom(u32 rom_addr, void* buf_ptr, u32 size);

// M4 wave 12: PI overlay segment descriptor used by src/state_file_select.c
// (and other state_*.c files that load overlay code). Match upstream's
// include/nu/nusys.h NUPiOverlaySegment.
typedef struct st_PiOverlaySegment {
    u8* romStart;
    u8* romEnd;
    u8* ramStart;
    u8* textStart;
    u8* textEnd;
    u8* dataStart;
    u8* dataEnd;
    u8* bssStart;
    u8* bssEnd;
} NUPiOverlaySegment;

void nuPiReadRomOverlay(NUPiOverlaySegment* segment);

// Audio
void nuAuInit(void);
void nuAuPreNMIFuncSet(void (*func)(void));
// Signature matches decomp's src/audio/core/system.c (thread entry).
void nuAuMgr(void* arg);

#endif
