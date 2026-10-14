#ifndef _N64_CONT_SHIM_H_
#define _N64_CONT_SHIM_H_

#include "os_types.h"

// ============================================================================
// N64 Controller -> 3DS Button Mapping
// ============================================================================

// NOTE: Button macros (BUTTON_*) are defined by the project's enums.h when
// common.h is included. Do NOT add colliding #define macros here.

// libultra trigger aliases — decomp src/ uses Z_TRIG / R_TRIG / L_TRIG
// (defined in upstream include/PR/os_cont.h, which our port doesn't ship
// because PR/os_cont.h isn't on our INCLUDES). Values match enums.h
// BUTTON_Z/BUTTON_R/BUTTON_L exactly.
#define R_TRIG 0x00000010
#define L_TRIG 0x00000020
#define Z_TRIG 0x00002000
#define U_CBUTTONS 0x00000008
#define L_CBUTTONS 0x00000002
#define R_CBUTTONS 0x00000001
#define D_CBUTTONS 0x00000004

// N64 controller data format
typedef struct {
    u16     button;       // button mask
    s8      stick_x;      // analog X
    s8      stick_y;      // analog Y
    u8      errno;        // error code
} OSContPad;

// N64 controller status
typedef struct {
    u16     type;
    u8      status;
    u8      errno;
} OSContStatus;

// NuSystem callback list (needed by controller globals)
typedef struct NUCallBackList {
    struct NUCallBackList* next;
    s32 (*func)(void);
} NUCallBackList;

// Rumble control
typedef struct {
    u16     freq;
    u16     frame;
    u8      state;
    u8      mode;
} NUContRmbCtl;

// Controller functions
u8   nuContInit(void);   // returns controller bit pattern
void nuContDataGet(OSContPad* contdata, u32 padno);
void nuContDataGetAll(OSContPad* contdata);
void nuContDataRead(OSContPad* pad);
void nuContDataReadWait(void);
void nuContDataReadStart(void);
void nuContDataLock(void);
void nuContDataUnLock(void);

// Rumble (mapped to 3DS vibration if available)
void nuContRmbForceStop(void);
void nuContRmbStart(u32 contNo, u16 freq, u16 frame);
void nuContRmbStop(u32 contNo);
s32  nuContRmbCheck(u32 contNo);
void nuContRmbModeSet(u32 contNo, u8 mode);
void nuContRmbForceStopEnd(void);
void nuContRmbSearchTimeSet(u32 frame);

// GBA Pak (not supported on 3DS, stubbed)
typedef struct OSGbpakId {
    u32 dummy;
} OSGbpakId;

#endif
