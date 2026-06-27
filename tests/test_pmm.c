#include <stdlib.h>
#include <stdint.h>

#include "unity/unity.h"
#include "../kernel/mm/pmm.h"

void test_pmm_init_alloc_free_count(void) {
    size_t pages = 16;
    size_t size = pages * PAGE_SIZE;
    void *mem = NULL;
    int rc = posix_memalign(&mem, PAGE_SIZE, size);
    if (rc != 0) TEST_FAIL("posix_memalign failed");
    TEST_ASSERT_NOT_NULL(mem);

    pmm_init((uint64_t)(uintptr_t)mem, size);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)pages, pmm_free_count());

    uint64_t a = pmm_alloc_page();
    TEST_ASSERT_NOT_NULL((void*)(uintptr_t)a);
    if (pmm_free_count() != (uint64_t)(pages - 1)) {
        TEST_FAIL("pmm_free_count() after alloc != expected");
    }

    pmm_free_page(a);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)pages, pmm_free_count());

    free(mem);
}

void test_pmm_reserve_range(void) {
    size_t pages = 8;
    size_t size = pages * PAGE_SIZE;
    void *mem = NULL;
    int rc = posix_memalign(&mem, PAGE_SIZE, size);
    if (rc != 0) TEST_FAIL("posix_memalign failed");
    TEST_ASSERT_NOT_NULL(mem);

    pmm_init((uint64_t)(uintptr_t)mem, size);
    pmm_reserve_range((uint64_t)(uintptr_t)mem, PAGE_SIZE);
    TEST_ASSERT_EQUAL_UINT64(pages - 1, pmm_free_count());

    free(mem);
}

void test_pmm_reserve_range_clamps_overlap_before_base(void) {
    size_t pages = 8;
    size_t size = pages * PAGE_SIZE;
    void *mem = NULL;
    int rc = posix_memalign(&mem, PAGE_SIZE, size);
    if (rc != 0) TEST_FAIL("posix_memalign failed");
    TEST_ASSERT_NOT_NULL(mem);

    uint64_t base = (uint64_t)(uintptr_t)mem;

    pmm_init(base, size);
    pmm_reserve_range(base - 128U, PAGE_SIZE);

    TEST_ASSERT_EQUAL_UINT64(pages - 1U, pmm_free_count());

    free(mem);
}

void test_pmm_reserve_range_handles_overflowing_end(void) {
    size_t pages = 4;
    size_t size = pages * PAGE_SIZE;
    void *mem = NULL;
    int rc = posix_memalign(&mem, PAGE_SIZE, size);
    if (rc != 0) TEST_FAIL("posix_memalign failed");
    TEST_ASSERT_NOT_NULL(mem);

    uint64_t base = (uint64_t)(uintptr_t)mem;
    uint64_t start = base + PAGE_SIZE;
    uint64_t overflowing_size = UINT64_MAX - start + 1ULL;

    pmm_init(base, size);
    pmm_reserve_range(start, overflowing_size);

    TEST_ASSERT_EQUAL_UINT64(1, pmm_free_count());

    free(mem);
}

void test_pmm_alloc_pages_requires_contiguous_run(void) {
    size_t pages = 4;
    size_t size = pages * PAGE_SIZE;
    void *mem = NULL;
    int rc = posix_memalign(&mem, PAGE_SIZE, size);
    if (rc != 0) TEST_FAIL("posix_memalign failed");
    TEST_ASSERT_NOT_NULL(mem);

    uint64_t base = (uint64_t)(uintptr_t)mem;

    pmm_init(base, size);
    pmm_reserve_range(base + PAGE_SIZE, PAGE_SIZE);

    TEST_ASSERT_EQUAL_UINT64(base + PAGE_SIZE * 2ULL, pmm_alloc_pages(2));
    TEST_ASSERT_EQUAL_UINT64(1, pmm_free_count());

    free(mem);
}

/* main moved to tests/main.c */
