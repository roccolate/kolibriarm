#include "kernel/mm/pmm.h"

#include <stdint.h>

/*
 * Physical page allocator.
 *
 * The PMM owns one fixed bitmap for the first PMM_MAX_MEMORY bytes reported by
 * the board RAM map. A set bit means "used/reserved"; a clear bit means "free".
 * Callers pass physical addresses, and allocation always returns PAGE_SIZE
 * aligned pages from the initialized range.
 */

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

static uint64_t add_saturated(uint64_t a, uint64_t b) {
    if (a > UINT64_MAX - b) {
        return UINT64_MAX;
    }
    return a + b;
}

static uint64_t align_up_saturated(uint64_t value) {
    if ((value & (PAGE_SIZE - 1ULL)) == 0) {
        return value;
    }
    if (value > UINT64_MAX - (PAGE_SIZE - 1ULL)) {
        return UINT64_MAX;
    }
    return align_up(value);
}

static uint64_t managed_end(void) {
    return add_saturated(g_mem_base, g_total_pages * PAGE_SIZE);
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

    /* Reserve page 0 so that pmm_alloc_page can use 0 as an error
     * sentinel without accidentally handing out a valid page. */
    if (g_mem_base == 0 && g_total_pages > 0) {
        set_used(0);
    }
}

void pmm_reserve_range(uint64_t start, uint64_t size) {
    uint64_t end;
    uint64_t limit;
    uint64_t page_start;
    uint64_t page_end;

    if (size == 0 || g_total_pages == 0) {
        return;
    }

    end = add_saturated(start, size);
    limit = managed_end();

    if (end <= g_mem_base || start >= limit) {
        return;
    }

    if (start < g_mem_base) {
        page_start = 0;
    } else {
        page_start = (align_down(start) - g_mem_base) / PAGE_SIZE;
    }

    if (end >= limit) {
        page_end = g_total_pages;
    } else {
        page_end = (align_up_saturated(end) - g_mem_base) / PAGE_SIZE;
        if (page_end > g_total_pages) {
            page_end = g_total_pages;
        }
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

uint64_t pmm_alloc_pages(uint64_t count) {
    uint64_t run_start = 0;
    uint64_t run_count = 0;

    if (count == 0 || count > g_total_pages) {
        return 0;
    }

    for (uint64_t page = 0; page < g_total_pages; page++) {
        uint64_t word = page / BITS_PER_WORD;
        uint64_t bit = page % BITS_PER_WORD;

        if ((g_bitmap[word] & (1ULL << bit)) == 0) {
            if (run_count == 0) {
                run_start = page;
            }

            run_count++;
            if (run_count == count) {
                for (uint64_t used = run_start; used < run_start + count; used++) {
                    set_used(used);
                }

                return g_mem_base + run_start * PAGE_SIZE;
            }
        } else {
            run_count = 0;
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

uint64_t pmm_total_count(void) {
    return g_total_pages;
}
