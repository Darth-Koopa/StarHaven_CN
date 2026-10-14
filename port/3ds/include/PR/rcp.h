#ifndef _RCP_H_
#define _RCP_H_

// 3DS stub for N64 RCP (Reality Co-Processor) register definitions
#include "PR/ultratypes.h"

// RCP memory map (stubs)
#define SP_DMEM_START    0x04000000
#define SP_IMEM_START    0x04001000
#define SP_MEM_ADDR_REG  0x04040000
#define SP_DRAM_ADDR_REG 0x04040004
#define PI_BASE_REG      0x04600000
#define PI_STATUS_REG    (PI_BASE_REG+0x10)
#define SI_BASE_REG      0x04800000
#define SI_STATUS_REG    (SI_BASE_REG+0x18)
#define VI_BASE_REG      0x04400000
#define VI_CTRL_REG      (VI_BASE_REG+0x00)
#define VI_DRAM_ADDR_REG (VI_BASE_REG+0x08)
#define AI_BASE_REG      0x04500000
#define AI_LEN_REG       (AI_BASE_REG+0x04)

// DMA constants
#define OS_MESG_PRI_NORMAL  0
#define OS_MESG_PRI_HIGH    1

#endif
