#ifndef _N64_AUDIO_SHIM_H_
#define _N64_AUDIO_SHIM_H_

#include "os_types.h"
// M4.1: ALHeap (for nuAuHeap decl) lives in PR/libaudio.h.
#include "../PR/libaudio.h"
#include <3ds.h>

// ============================================================================
// N64 Audio Shim -> 3DS CSND / DSP (Simplified)
// Paper Mario uses a custom MSEQ sequence format with the N64 sound driver.
// ============================================================================

// Acmd is defined in PR/abi.h (included via ultra64.h)
#include "PR/abi.h"

// N64 audio globals (shimmed)
extern u32 nuAuMgrThreadPri;
extern u32 nuAuSndThreadPri;
extern u32 nuAuBgmThreadPri;
extern u32 nuAuTaskPtr;
// M4.1: nuAuHeap is an ALHeap struct upstream (and src/bss declares it
// as `BSS ALHeap nuAuHeap;`). The previous `s32` decl conflicted once
// src/bss was added to SOURCES.
extern ALHeap nuAuHeap;
extern s32 nuAuSndId;
extern s32 nuAuBgmId;
extern u8  nuAuPreNMI;

// Audio initialization
void nuAuInit(void);
void nuAuPreNMIFuncSet(void (*func)(void));

// Audio task (stubbed for 3DS). Decomp's src/audio/core/system.c defines this
// as `void nuAuMgr(void* arg)` (osCreateThread entry point) — signature must
// match to avoid a conflicting-types error when wave 11 brings src/audio in.
void nuAuMgr(void* arg);

// N64 sound player types (stubbed)
typedef struct NALSndPlayer {
    s32 dummy;
} NALSndPlayer;

typedef struct NALSndPtr {
    s32 dummy;
} NALSndPtr;

// Sound player functions (stubbed)
void n_alSndpNew(NALSndPlayer* sndp, u32 maxSounds);
void n_alSndpSetSound(NALSndPlayer* sndp, NALSndPtr snd);
void n_alSndpPlay(NALSndPlayer* sndp);
void n_alSndpStop(NALSndPlayer* sndp);
void n_alSndpSetVol(NALSndPlayer* sndp, s32 vol);
void n_alSndpSetPitch(NALSndPlayer* sndp, f32 pitch);
void n_alSndpSetPan(NALSndPlayer* sndp, u8 pan);
void n_alSndpSetPriority(NALSndPlayer* sndp, s16 priority);
s32  n_alSndpGetState(NALSndPlayer* sndp);
s32  n_alSndpAllocate(NALSndPlayer* sndp, u32 bank, u32 sound);
void n_alSndpDeallocate(NALSndPlayer* sndp, s32 snd);

// 3DS audio constants
#define N3DS_AUDIO_SAMPLE_RATE      32728  // CSND native rate
#define N3DS_AUDIO_CHANNELS         24     // CSND channels
#define N3DS_AUDIO_BUFFERS          2

#endif