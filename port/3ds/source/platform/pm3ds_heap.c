/*
 * pm3ds_heap.c — single-arena allocator that backs the decomp's RDRAM.
 * Fidelity tier: N/A (3DS-side primitive, not an N64 SDK shim)
 *
 * Implementation: simple free-list. Each block carries an 8-byte header
 * with (size, free-flag, prev-pointer). Adjacent free blocks coalesce on
 * free. Allocations are aligned to 8 bytes. This is sufficient for the
 * boot path; the decomp's own general_heap manages fine-grained alloc on
 * top of one big block returned from here.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef PM3DS_HOST_TEST
#include <stdlib.h>
#define PM3DS_HEAP_BACKING_ALLOC(sz) malloc(sz)
#define PM3DS_HEAP_BACKING_FREE(p)   free(p)
#else
#include <3ds.h>
#define PM3DS_HEAP_BACKING_ALLOC(sz) linearAlloc(sz)
#define PM3DS_HEAP_BACKING_FREE(p)   linearFree(p)
#endif

#include "pm3ds_heap.h"

#define HDR_ALIGN 8
#define ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~((size_t)((a) - 1)))

typedef struct block {
    size_t        size;     /* size of user region, NOT including header */
    int           free;
    struct block *next;
    struct block *prev;
} block_t;

#define HDR_SIZE ALIGN_UP(sizeof(block_t), HDR_ALIGN)

static uint8_t *g_arena       = NULL;
static size_t   g_arena_bytes = 0;
static block_t *g_head        = NULL;
static size_t   g_used        = 0;

void *pm3ds_heap_init(size_t bytes) {
    if (g_arena) return NULL; /* already initialised */
    g_arena = PM3DS_HEAP_BACKING_ALLOC(bytes);
    if (!g_arena) return NULL;
    g_arena_bytes = bytes;
    g_used = 0;

    g_head = (block_t *)g_arena;
    g_head->size = bytes - HDR_SIZE;
    g_head->free = 1;
    g_head->next = NULL;
    g_head->prev = NULL;
    return g_arena;
}

void pm3ds_heap_shutdown(void) {
    if (g_arena) PM3DS_HEAP_BACKING_FREE(g_arena);
    g_arena = NULL;
    g_arena_bytes = 0;
    g_head = NULL;
    g_used = 0;
}

void *pm3ds_heap_alloc(size_t bytes) {
    if (!g_arena) return NULL;
    size_t need = ALIGN_UP(bytes, HDR_ALIGN);

    for (block_t *b = g_head; b; b = b->next) {
        if (!b->free || b->size < need) continue;

        /* split if the leftover is big enough to host another block */
        if (b->size >= need + HDR_SIZE + HDR_ALIGN) {
            block_t *split = (block_t *)((uint8_t *)b + HDR_SIZE + need);
            split->size = b->size - need - HDR_SIZE;
            split->free = 1;
            split->next = b->next;
            split->prev = b;
            if (b->next) b->next->prev = split;
            b->next = split;
            b->size = need;
        }

        b->free = 0;
        g_used += b->size + HDR_SIZE;
        return (uint8_t *)b + HDR_SIZE;
    }
    return NULL;
}

void pm3ds_heap_free(void *p) {
    if (!p) return;
    block_t *b = (block_t *)((uint8_t *)p - HDR_SIZE);
    b->free = 1;
    g_used -= b->size + HDR_SIZE;

    /* coalesce forward */
    if (b->next && b->next->free) {
        b->size += HDR_SIZE + b->next->size;
        b->next = b->next->next;
        if (b->next) b->next->prev = b;
    }
    /* coalesce backward */
    if (b->prev && b->prev->free) {
        b->prev->size += HDR_SIZE + b->size;
        b->prev->next = b->next;
        if (b->next) b->next->prev = b->prev;
    }
}

size_t pm3ds_heap_used(void)     { return g_used; }
size_t pm3ds_heap_capacity(void) { return g_arena_bytes; }
