#ifndef _ULTRATYPES_H_
#define _ULTRATYPES_H_

// 3DS redirect for N64 ultratypes.h
#include "n64/os_types.h"

// N64 compatibility: ensure NULL/TRUE/FALSE are defined
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef NULL
#define NULL 0
#endif

#endif
