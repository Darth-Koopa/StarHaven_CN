#ifndef _SPTASK_H_
#define _SPTASK_H_

// 3DS stub for N64 sptask.h
#include "PR/ultratypes.h"

typedef u32 OSYieldResult;

typedef struct {
    u32     type;
    u32     flags;
    u64*    ucode;
    u64*    ucode_data;
    u32     ucode_size;
    u32     ucode_data_size;
    void*   dram_stack;
    u32     dram_stack_size;
    void*   output_buff;
    u32     output_buff_size;
    void*   data_ptr;
    u32     data_size;
    void*   yield_data_ptr;
    u32     yield_data_size;
} OSTask;

// Task flags
#define OS_TASK_YIELDED      0x0001
#define OS_TASK_DP_WAIT      0x0002
#define OS_TASK_LOADABLE     0x0004
#define OS_TASK_SP_ONLY      0x0008

#endif
