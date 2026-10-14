#ifndef _REGION_H_
#define _REGION_H_
#include "PR/ultratypes.h"
typedef struct {
    u32     addr;
    u32     size;
} OSRegion;
extern void osCreateRegion(void* addr, u32 size, u32 unitSize, u32 flags);
extern void* osMalloc(OSRegion* rp, u32 size);
extern void osFree(OSRegion* rp, void* addr);
#endif
