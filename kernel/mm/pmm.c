#include "kernel/mm/pmm.h"

#include <stdint.h>

#define PMM_MAX_MEMORY (128ULL * 1024ULL * 1024ULL)
#define PMM_MAX_PAGES  (PMM_MAX_MEMORY / PAGE_SIZE)
#define BITS_PER_WORD  64ULL

static uint64_t g_bitmap[PMM_MAX_PAGES / BITS_PER_WORD];
static uint64_t g_mem_base;
static uint64_t g_total_pages;
static uint64_t g_free_pages;

static uint64_t align_down(uint64_t value) {
    return value & ~(PAGE_SIZE - 1ULL);
}

static uint64_t align_up(uint64_t value) {
    return (value + PAGE_SIZE - 1ULL) & ~(PAGE_SIZE - 1ULL);
}

static void set_used(uint64_t page) {
    uint64_t word = page / BITS_PER_WORD;
    uint64_t bit = page % BITS_PER_WORD;
    uint64_t mask = 1ULL << bit;

    if ((g_bitmap[word] & mask) == 0) {
        g_bitmap[word] |= mask;
        g_free_pages--;
    }
}

static void set_free(uint64_t page) {
    uint64_t word = page / BITS_PER_WORD;
    uint64_t bit = page % BITS_PER_WORD;
    uint64_t mask = 1ULL << bit;

    if ((g_bitmap[word] & mask) != 0) {
        g_bitmap[word] &= ~mask;
        g_free_pages++;
    }
}

void pmm_init(uint64_t mem_base, uint64_t mem_size) {
    uint64_t pages = mem_size / PAGE_SIZE;

    g_mem_base = mem_base;
    g_total_pages = pages > PMM_MAX_PAGES ? PMM_MAX_PAGES : pages;
    g_free_pages = 0;

    for (uint64_t i = 0; i < PMM_MAX_PAGES / BITS_PER_WORD; i++) {
        g_bitmap[i] = ~0ULL;
    }

    for (uint64_t i = 0; i < g_total_pages; i++) {
        set_free(i);
    }
}

void pmm_reserve_range(uint64_t start, uint64_t size) {
    uint64_t end;
    uint64_t page_start;
    uint64_t page_end;

    if (size == 0 || start < g_mem_base) {
        return;
    }

    end = align_up(start + size);
    page_start = (align_down(start) - g_mem_base) / PAGE_SIZE;
    page_end = (end - g_mem_base) / PAGE_SIZE;

    if (page_start >= g_total_pages) {
        return;
    }

    if (page_end > g_total_pages) {
        page_end = g_total_pages;
    }

    for (uint64_t page = page_start; page < page_end; page++) {
        set_used(page);
    }
}

uint64_t pmm_alloc_page(void) {
    for (uint64_t page = 0; page < g_total_pages; page++) {
        uint64_t word = page / BITS_PER_WORD;
        uint64_t bit = page % BITS_PER_WORD;

        if ((g_bitmap[word] & (1ULL << bit)) == 0) {
            set_used(page);
            return g_mem_base + page * PAGE_SIZE;
        }
    }

    return 0;
}

void pmm_free_page(uint64_t paddr) {
    uint64_t page;

    if (paddr < g_mem_base || (paddr & (PAGE_SIZE - 1ULL)) != 0) {
        return;
    }

    page = (paddr - g_mem_base) / PAGE_SIZE;
    if (page >= g_total_pages) {
        return;
    }

    set_free(page);
}

uint64_t pmm_free_count(void) {
    return g_free_pages;
}
