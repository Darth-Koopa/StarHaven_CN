#include "ultra64.h"
#include "common_structs.h"
#include "n64/heap_shim.h"
#include "pm3ds_log.h"

#include <stdlib.h>
#include <string.h>
#include <3ds.h>

/* M6.1f: one-shot checkpoint log per function. Lets us see where
 * boot_main wedges without flooding the console. */
#define PM3DS_HEAP_TRACE(tag) do {                                           \
    static int _pm3ds_once_##__LINE__ = 0;                                   \
    if (!_pm3ds_once_##__LINE__) {                                           \
        _pm3ds_once_##__LINE__ = 1;                                          \
        pm3ds_log(PM3DS_LOG_INFO, "heap-trace: " tag);                       \
    }                                                                        \
} while (0)

// ============================================================================
// M11: the game's own heaps.
//
// Until M11 every _heap_* call went to libc malloc and ignored the HeapNode,
// so *_heap_create() on each map change leaked everything allocated before
// it. The port now uses upstream's allocator (src/43F0.c _heap_*) and
// src/heap.c over the arenas in src/heaps2.c/heaps3.c, exactly as on N64
// (sizes enlarged in include/macros.h under _3DS).
//
// heap_battleHead is placed by the N64 linker script rather than declared in
// C, so the port provides its storage here.
// ============================================================================

// nocommon: under -fcommon a tentative array became a COMMON symbol and
// landed 8-aligned; _heap_create 16-aligns the node while _heap_malloc reads
// the head as given, so every battle allocation failed.
u8 heap_battleHead[BATTLE_HEAP_SIZE] __attribute__((aligned(16), nocommon));

// 3DS linear memory allocation (for GPU DMA)
void* pm3ds_heap_malloc_linear(u32 size) {
    return linearAlloc(size);
}

void pm3ds_heap_free_linear(void* ptr) {
    linearFree(ptr);
}
