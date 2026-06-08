#include "kernel/mm/kheap.h"

#include <stddef.h>
#include <stdint.h>

#include "kernel/mm/pmm.h"

#define KHEAP_ALIGN       16ULL
#define KHEAP_MIN_SPLIT   32ULL

typedef struct heap_block {
    uint64_t size;
    uint64_t free;
    struct heap_block *next;
    struct heap_block *prev;
} heap_block_t;

static heap_block_t *g_heap_head;
static heap_block_t *g_heap_tail;
static uint64_t g_heap_total_bytes;

static uint64_t align_up(uint64_t value) {
    return (value + KHEAP_ALIGN - 1ULL) & ~(KHEAP_ALIGN - 1ULL);
}

static uint64_t block_payload_capacity(void) {
    return PAGE_SIZE - sizeof(heap_block_t);
}

static int blocks_are_adjacent(heap_block_t *left, heap_block_t *right) {
    uintptr_t left_end = (uintptr_t)left + sizeof(heap_block_t) + left->size;

    return left_end == (uintptr_t)right;
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

static heap_block_t *extend_heap(void) {
    uint64_t page = pmm_alloc_page();
    heap_block_t *block;

    if (page == 0) {
        return NULL;
    }

    block = (heap_block_t *)(uintptr_t)page;
    block->size = block_payload_capacity();
    block->free = 1;
    append_block(block);
    g_heap_total_bytes += block->size;

    return block;
}

static void split_block(heap_block_t *block, uint64_t size) {
    heap_block_t *next;

    if (block->size < size + sizeof(heap_block_t) + KHEAP_MIN_SPLIT) {
        return;
    }

    next = (heap_block_t *)((uintptr_t)block + sizeof(heap_block_t) + size);
    next->size = block->size - size - sizeof(heap_block_t);
    next->free = 1;
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

    if (next == NULL || next->free == 0 || !blocks_are_adjacent(block, next)) {
        return;
    }

    block->size += sizeof(heap_block_t) + next->size;
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

    (void)extend_heap();
}

void *kmalloc(size_t size) {
    uint64_t aligned_size;
    heap_block_t *block;

    if (size == 0) {
        return NULL;
    }

    aligned_size = align_up((uint64_t)size);
    if (aligned_size > block_payload_capacity()) {
        return NULL;
    }

    if (g_heap_head == NULL) {
        kheap_init();
    }

    block = g_heap_head;
    while (block != NULL) {
        if (block->free != 0 && block->size >= aligned_size) {
            split_block(block, aligned_size);
            block->free = 0;
            return (void *)((uintptr_t)block + sizeof(heap_block_t));
        }

        block = block->next;
    }

    block = extend_heap();
    if (block == NULL) {
        return NULL;
    }

    split_block(block, aligned_size);
    block->free = 0;

    return (void *)((uintptr_t)block + sizeof(heap_block_t));
}

void kfree(void *ptr) {
    heap_block_t *block;

    if (ptr == NULL) {
        return;
    }

    block = (heap_block_t *)((uintptr_t)ptr - sizeof(heap_block_t));
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
