#include "kernel/mm/kheap.h"

#include <stddef.h>
#include <stdint.h>

#include "kernel/mm/pmm.h"

/*
 * Simple PMM-backed kernel heap.
 *
 * The heap grows by whole physical-page arenas and keeps a doubly linked list
 * of blocks inside those arenas. Blocks coalesce only when they are adjacent in
 * the same arena; the heap does not return arenas to the PMM yet, because the
 * current kernel workload benefits more from predictable reuse than trimming.
 */

#define KHEAP_ALIGN       16ULL
#define KHEAP_MIN_SPLIT   32ULL
#define KHEAP_MAGIC       0x4B484541504F4B21ULL  /* "KHEAPOK!" */

typedef struct heap_block {
    uint64_t magic;
    uint64_t size;
    uint64_t free;
    struct heap_block *next;
    struct heap_block *prev;
    uint64_t arena_id;
} heap_block_t;

static heap_block_t *g_heap_head;
static heap_block_t *g_heap_tail;
static uint64_t g_heap_total_bytes;
static uint64_t g_next_arena_id = 1;

static uint64_t align_up(uint64_t value) {
    return (value + KHEAP_ALIGN - 1ULL) & ~(KHEAP_ALIGN - 1ULL);
}

static uint64_t header_size(void) {
    return align_up((uint64_t)sizeof(heap_block_t));
}

static uint64_t page_count_for_payload(uint64_t payload_size) {
    uint64_t hdr = header_size();
    uint64_t needed;

    if (payload_size > UINT64_MAX - hdr) {
        return 0;
    }

    needed = hdr + payload_size;
    if (needed > UINT64_MAX - (PAGE_SIZE - 1ULL)) {
        return 0;
    }

    return (needed + PAGE_SIZE - 1ULL) / PAGE_SIZE;
}

static uint64_t block_payload_capacity(uint64_t page_count) {
    return page_count * PAGE_SIZE - header_size();
}

static int blocks_are_adjacent(heap_block_t *left, heap_block_t *right) {
    uintptr_t left_end = (uintptr_t)left + header_size() + left->size;

    return left_end == (uintptr_t)right;
}

static int blocks_can_coalesce(heap_block_t *left, heap_block_t *right) {
    return left != NULL && right != NULL && right->free != 0 &&
           left->arena_id == right->arena_id &&
           blocks_are_adjacent(left, right);
}

static void append_block(heap_block_t *block) {
    block->next = NULL;
    block->prev = g_heap_tail;

    if (g_heap_tail != NULL) {
        g_heap_tail->next = block;
    } else {
        g_heap_head = block;
    }

    g_heap_tail = block;
}

static heap_block_t *extend_heap(uint64_t min_payload_size) {
    uint64_t pages = page_count_for_payload(min_payload_size);
    uint64_t addr;
    heap_block_t *block;

    if (pages == 0) {
        return NULL;
    }

    addr = pmm_alloc_pages(pages);
    if (addr == 0) {
        return NULL;
    }

    block = (heap_block_t *)(uintptr_t)addr;
    block->magic = KHEAP_MAGIC;
    block->size = block_payload_capacity(pages);
    block->free = 1;
    block->arena_id = g_next_arena_id++;
    append_block(block);
    g_heap_total_bytes += block->size;

    return block;
}

static void split_block(heap_block_t *block, uint64_t size) {
    heap_block_t *next;
    uint64_t hdr = header_size();

    if (block->size < size + hdr + KHEAP_MIN_SPLIT) {
        return;
    }

    next = (heap_block_t *)((uintptr_t)block + hdr + size);
    next->magic = KHEAP_MAGIC;
    next->size = block->size - size - hdr;
    next->free = 1;
    next->arena_id = block->arena_id;
    next->prev = block;
    next->next = block->next;

    if (block->next != NULL) {
        block->next->prev = next;
    } else {
        g_heap_tail = next;
    }

    block->next = next;
    block->size = size;
}

static void coalesce_next(heap_block_t *block) {
    heap_block_t *next = block->next;

    if (!blocks_can_coalesce(block, next)) {
        return;
    }

    block->size += header_size() + next->size;
    block->next = next->next;

    if (next->next != NULL) {
        next->next->prev = block;
    } else {
        g_heap_tail = block;
    }
}

void kheap_init(void) {
    if (g_heap_head != NULL) {
        return;
    }

    (void)extend_heap(1);
}

void *kmalloc(size_t size) {
    uint64_t aligned_size;
    heap_block_t *block;

    if (size == 0 || (uint64_t)size > UINT64_MAX - (KHEAP_ALIGN - 1ULL)) {
        return NULL;
    }

    aligned_size = align_up((uint64_t)size);

    if (g_heap_head == NULL) {
        kheap_init();
    }

    block = g_heap_head;
    while (block != NULL) {
        if (block->free != 0 && block->size >= aligned_size) {
            split_block(block, aligned_size);
            block->free = 0;
            return (void *)((uintptr_t)block + header_size());
        }

        block = block->next;
    }

    block = extend_heap(aligned_size);
    if (block == NULL) {
        return NULL;
    }

    split_block(block, aligned_size);
    block->free = 0;

    return (void *)((uintptr_t)block + header_size());
}

void kfree(void *ptr) {
    heap_block_t *block;

    if (ptr == NULL) {
        return;
    }

    block = (heap_block_t *)((uintptr_t)ptr - header_size());
    if (block->magic != KHEAP_MAGIC) {
        return;
    }
    if (block->free != 0) {
        return;
    }

    block->free = 1;
    coalesce_next(block);

    if (block->prev != NULL && block->prev->free != 0) {
        coalesce_next(block->prev);
    }
}

uint64_t kheap_total_bytes(void) {
    return g_heap_total_bytes;
}

uint64_t kheap_free_bytes(void) {
    uint64_t free_bytes = 0;
    heap_block_t *block = g_heap_head;

    while (block != NULL) {
        if (block->free != 0) {
            free_bytes += block->size;
        }

        block = block->next;
    }

    return free_bytes;
}

#ifdef ARMONIOS_TEST
void kheap_reset_for_tests(void) {
    g_heap_head = NULL;
    g_heap_tail = NULL;
    g_heap_total_bytes = 0;
    g_next_arena_id = 1;
}
#endif
