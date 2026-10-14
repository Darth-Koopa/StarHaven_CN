#ifndef _N64_GFX_SHIM_H_
#define _N64_GFX_SHIM_H_

#include "os_types.h"
#include <3ds.h>

// ============================================================================
// N64 Graphics Microcode / Display List Shim
// This translates N64 F3DEX2 display lists into Citro3D/PICA200 commands.
// ============================================================================

// N64 display list command format — defined as a union in PR/gbi.h
// (Gwords w0/w1, accessed via `.words.w0/w1`). Include PR/gbi.h here so
// translation units that pull in gfx_shim.h alone (e.g. via
// nu/nusys.h → audio.h → bgm_player.c) still see Gfx without having to
// also include ultra64.h.
#include "../PR/gbi.h"

// N64 graphics task state
typedef struct {
    u32     state;
    u32     flags;
} NUScTask;

// N64 microcode identifiers
#define NU_GFX_UCODE_F3DEX     0
#define NU_GFX_UCODE_F3DEX2    1
#define NU_GFX_UCODE_S2DEX     2
#define NU_GFX_UCODE_S2DEX2    3
#define NU_GFX_UCODE_L3DEX     4
#define NU_GFX_UCODE_L3DEX2    5

// N64 graphics task flags
#define NU_SC_TASK_LODABLE     0x01
#define NU_SC_SWAPBUFFER       0x02
#define NU_SC_NOSWAPBUFFER     0x04
#define NU_SC_TASK_NUM         2
#define NU_GFX_TASK_NUM        NU_SC_TASK_NUM

// M12: every G_* constant (matrix params, image formats, othermodes, cycle
// types, geometry mode bits) now comes from the real F3DEX2 PR/gbi.h included
// above. The stand-ins that used to live here were F3D-era or simply wrong
// (G_MTX_PUSH 0x04 / G_MTX_PROJECTION 0x01 swapped, cycle types and texture
// filter/TLUT modes unshifted, G_RM_OPA_SURF 0, cull bits 0x1000/0x2000) and,
// being defined after gbi.h, they silently won.

// N64 ucode structure
typedef struct {
    u32     ucode;
    u32     ucode_size;
    u32     ucode_data;
    u32     ucode_data_size;
} NUUcode;

// Graphics swap callback
typedef void (*NUGfxFunc)(s32);
typedef void (*NUGfxPreNMIFunc)(void);
typedef void (*NUGfxTaskEndFunc)(void);
typedef void (*NUGfxSwapCfbFunc)(void**);

// N64 graphics globals (shimmed to 3DS)
extern OSMesgQueue  nuGfxMesgQ;
extern NUUcode*     nuGfxUcode;
extern u16**        nuGfxCfb;
extern u16*         nuGfxCfb_ptr;
extern u32          nuGfxCfbNum;
extern u16*         nuGfxZBuffer;
extern volatile u32 nuGfxTaskSpool;
extern u32          nuGfxDisplay;
extern u32          nuGfxCfbCounter;
extern OSThread     nuGfxThread;
extern s32          nuGfxUcodeFifoSize;
extern u64*         nuGfxUcodeFifoPtr;
extern NUGfxSwapCfbFunc nuGfxSwapCfbFunc;
extern NUGfxFunc    nuGfxFunc;
extern NUGfxPreNMIFunc nuGfxPreNMIFunc;
extern NUGfxTaskEndFunc nuGfxTaskEndFunc;

// N64 graphics functions
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

// Screen dimensions from original
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

// Matrix type (Matrix4f is used by game code; Mtx is defined in PR/gbi.h)
typedef f32 Matrix4f[4][4];

#endif
