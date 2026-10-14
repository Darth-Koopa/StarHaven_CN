#ifndef _N64_HEAP_SHIM_H_
#define _N64_HEAP_SHIM_H_

#include "os_types.h"

// ============================================================================
// N64 Heap Shim
// ============================================================================
// Paper Mario uses custom heap implementations for general and battle memory.
// On 3DS, we wrap standard malloc/free with optional debug tracking.
// ============================================================================

// NOTE: The HeapNode struct is defined in the project's common_structs.h
// (via common.h). We do NOT redeclare it here to avoid redefinition conflicts.
// If you are writing a shim implementation file, include common.h or
// common_structs.h before this header.

// N64 heap functions
struct HeapNode;
struct HeapNode* _heap_create(struct HeapNode* addr, u32 size);
void* _heap_malloc(struct HeapNode* head, u32 size);
void* _heap_malloc_tail(struct HeapNode* head, u32 size);
u32 _heap_free(struct HeapNode* heapNodeList, void* addrToFree);
void* _heap_realloc(struct HeapNode* heapNodeList, void* addr, u32 newSize);

// High-level wrappers
void* heap_malloc(s32 size);
s32 heap_free(void* ptr);
void* heap_realloc(void* ptr, u32 newSize);

// 3DS-specific: allocate from linear pool (for GPU DMA)
void* pm3ds_heap_malloc_linear(u32 size);
void pm3ds_heap_free_linear(void* ptr);

#endif
