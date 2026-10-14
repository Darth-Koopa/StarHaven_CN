#ifndef __N_ABI__
#define __N_ABI__

// 3DS stub for N64 n_abi.h — include the standard ABI state types
#include "PR/abi.h"

// N-specific audio command macros (minimal stubs)
#define n_aADPCMdec(pkt, s, f, c, a, d)
#define n_aPoleFilter(pkt, f, g, t, s)
#define n_aEnvMixer(pkt, f, t, s)
#define n_aInterleave(pkt)
#define n_aLoadBuffer(pkt, c, d, s)
#define n_aResample(pkt, s, f, p, i, o)
#define n_aSaveBuffer(pkt, c, d, s)
#define n_aSetVolume(pkt, f, v, t, r)
#define n_aLoadADPCM(pkt, c, d)

#endif
