#ifndef PM3DS_HEAP_H
#define PM3DS_HEAP_H

#include <stddef.h>
#include <stdint.h>

/*
 * Single-arena bump+free allocator that backs all decomp heap allocations.
 * Initialised once at boot with a fixed-size region (allocated via
 * linearAlloc on 3DS, malloc on host tests).
 *
 * Semantics:
 *   - pm3ds_heap_alloc returns 8-byte-aligned blocks, NULL on OOM.
 *   - pm3ds_heap_free releases a previously-allocated block.
 *   - No coalescing: this is not a general-purpose allocator. The decomp's
 *     own heap manager sits on top of one big block we hand out here.
 *
 * Thread safety: caller serialises. The shim layer holds a single lock
 * around all heap operations during boot; once the decomp's heap manager
 * owns its block, it manages its own concurrency.
 */

void  *pm3ds_heap_init(size_t bytes);   /* returns base of arena or NULL */
void   pm3ds_heap_shutdown(void);
void  *pm3ds_heap_alloc(size_t bytes);
void   pm3ds_heap_free(void *p);
size_t pm3ds_heap_used(void);
size_t pm3ds_heap_capacity(void);

#endif /* PM3DS_HEAP_H */
