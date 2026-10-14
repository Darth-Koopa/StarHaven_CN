/* 3DS port: upstream n_abi.h (naudio / n_aspMain command macros), M14. */
#ifndef PM3DS_PR_N_ABI_WRAPPER_H
#define PM3DS_PR_N_ABI_WRAPPER_H
#include "PR/abi.h"
#ifndef _LANGUAGE_C
#define _LANGUAGE_C 1
#define PM3DS_NABI_UNDEF_LANGUAGE_C
#endif
#include "../../../../include/PR/n_abi.h"
#ifdef PM3DS_NABI_UNDEF_LANGUAGE_C
#undef _LANGUAGE_C
#undef PM3DS_NABI_UNDEF_LANGUAGE_C
#endif
#endif
