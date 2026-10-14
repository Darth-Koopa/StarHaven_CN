#ifndef _NU_NUALSGI_H_
#define _NU_NUALSGI_H_

// ============================================================================
// NuSystem Audio AL SGI Shim
// ============================================================================
// This replaces the N64 nualsgi audio library headers.
// ============================================================================

#include "n64/os_types.h"
// M4.1: ALHeap (for nuAuHeap decl) lives in PR/libaudio.h.
#include "../PR/libaudio.h"
#include "n64/audio_shim.h"

// Audio globals
extern u32 nuAuMgrThreadPri;
extern u32 nuAuSndThreadPri;
extern u32 nuAuBgmThreadPri;
extern u32 nuAuTaskPtr;
// M4.1: ALHeap struct upstream (src/bss declares BSS ALHeap nuAuHeap;).
extern ALHeap nuAuHeap;
extern s32 nuAuSndId;
extern s32 nuAuBgmId;
extern u8  nuAuPreNMI;

#endif
