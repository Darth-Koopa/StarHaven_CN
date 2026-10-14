/*
 * 3DS port: the real N64 audio ABI (M14). The old stand-in only carried the
 * state typedefs and a {w0,w1} Acmd, so the decomp's synth driver could not
 * build command lists. The upstream macros pack plain integers and work on
 * ARM32; port/3ds/source/n64_shim/audio_alist.c executes the lists.
 */
#ifndef PM3DS_PR_ABI_WRAPPER_H
#define PM3DS_PR_ABI_WRAPPER_H
#ifndef _LANGUAGE_C
#define _LANGUAGE_C 1
#define PM3DS_ABI_UNDEF_LANGUAGE_C
#endif
#ifndef _SHIFTL
#define _SHIFTL(v, s, w)    ((unsigned int) (((unsigned int)(v) & ((0x01 << (w)) - 1)) << (s)))
#define _SHIFTR(v, s, w)    ((unsigned int)(((unsigned int)(v) >> (s)) & ((0x01 << (w)) - 1)))
#endif
#include "../../../../include/PR/abi.h"
#ifdef PM3DS_ABI_UNDEF_LANGUAGE_C
#undef _LANGUAGE_C
#undef PM3DS_ABI_UNDEF_LANGUAGE_C
#endif
#endif
