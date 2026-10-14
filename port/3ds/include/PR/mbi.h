#ifndef _MBI_H_
#define _MBI_H_
// On N64, mbi.h includes gbi.h and abi.h and provides additional sprite microcode definitions.
// On 3DS, we use the direct GBI stub and pull in the real ABI header for audio state types.
#include "PR/gbi.h"
#include <PR/abi.h>
#endif

