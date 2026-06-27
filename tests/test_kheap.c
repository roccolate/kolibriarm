#include <stdint.h>
#include <stddef.h>

#include "unity/unity.h"
#include "../kernel/mm/pmm.h"
#include "../kernel/mm/kheap.h"

#define KHEAP_TEST_PAGES 256U
#define PANEL_BACKING_BYTES (640U * 56U * sizeof(uint32_t))

static uint8_t g_kheap_test_mem[KHEAP_TEST_PAGES * PAGE_SIZE]
    __attribute__((aligned(PAGE_SIZE)));

static void reset_test_heap(void) {
    kheap_reset_for_tests();
    pmm_init((uint64_t)(uintptr_t)g_kheap_test_mem, sizeof(g_kheap_test_mem));
    kheap_init();
}

static void assert_kheap_basic_alloc_free(void) {
    reset_test_heap();

    uint64_t total = kheap_total_bytes();
    TEST_ASSERT_TRUE(total > 0);

    void *p = kmalloc(32);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT64(0, ((uint64_t)(uintptr_t)p) & 0xfULL);

    uint64_t free_before = kheap_free_bytes();
    kfree(p);
    uint64_t free_after = kheap_free_bytes();
    TEST_ASSERT_TRUE(free_after >= free_before);
}

static void assert_kheap_rejects_empty_and_impossible_allocations(void) {
    reset_test_heap();

    uint64_t free_before = kheap_free_bytes();

    TEST_ASSERT_NULL(kmalloc(0));
    TEST_ASSERT_NULL(kmalloc((size_t)-1));
    kfree(NULL);

    TEST_ASSERT_EQUAL_UINT64(free_before, kheap_free_bytes());
}

static void assert_kheap_allocates_larger_than_one_page(void) {
    reset_test_heap();

    void *p = kmalloc(PAGE_SIZE * 2U + 123U);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT64(0, ((uint64_t)(uintptr_t)p) & 0xfULL);

    uint8_t *bytes = (uint8_t *)p;
    bytes[0] = 0x11U;
    bytes[PAGE_SIZE] = 0x22U;
    bytes[PAGE_SIZE * 2U + 122U] = 0x33U;

    TEST_ASSERT_EQUAL_UINT64(0x11U, bytes[0]);
    TEST_ASSERT_EQUAL_UINT64(0x22U, bytes[PAGE_SIZE]);
    TEST_ASSERT_EQUAL_UINT64(0x33U, bytes[PAGE_SIZE * 2U + 122U]);

    kfree(p);
}

static void assert_kheap_allocates_panel_sized_backing_buffer(void) {
    reset_test_heap();

    void *p = kmalloc(PANEL_BACKING_BYTES);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT64(0, ((uint64_t)(uintptr_t)p) & 0xfULL);

    uint32_t *pixels = (uint32_t *)p;
    pixels[0] = 0xff202428U;
    pixels[(PANEL_BACKING_BYTES / sizeof(uint32_t)) - 1U] = 0xffe0e8f0U;

    TEST_ASSERT_EQUAL_UINT64(0xff202428U, pixels[0]);
    TEST_ASSERT_EQUAL_UINT64(0xffe0e8f0U,
                             pixels[(PANEL_BACKING_BYTES / sizeof(uint32_t)) - 1U]);

    kfree(p);
}

static void assert_kheap_reuses_large_freed_block_for_small_allocations(void) {
    reset_test_heap();

    void *large = kmalloc(PANEL_BACKING_BYTES);
    TEST_ASSERT_NOT_NULL(large);
    kfree(large);

    void *small_a = kmalloc(64);
    void *small_b = kmalloc(128);
    TEST_ASSERT_NOT_NULL(small_a);
    TEST_ASSERT_NOT_NULL(small_b);
    TEST_ASSERT_TRUE(small_a != small_b);

    kfree(small_a);
    kfree(small_b);
}

static void assert_kheap_large_alloc_free_cycles_remain_usable(void) {
    reset_test_heap();

    for (uint32_t i = 0; i < 8U; i++) {
        void *large = kmalloc(PANEL_BACKING_BYTES);
        TEST_ASSERT_NOT_NULL(large);
        kfree(large);
    }

    void *final = kmalloc(PANEL_BACKING_BYTES);
    TEST_ASSERT_NOT_NULL(final);
    kfree(final);
}

void test_kheap_basic_alloc_free(void) {
    assert_kheap_basic_alloc_free();
    assert_kheap_rejects_empty_and_impossible_allocations();
    assert_kheap_allocates_larger_than_one_page();
    assert_kheap_allocates_panel_sized_backing_buffer();
    assert_kheap_reuses_large_freed_block_for_small_allocations();
    assert_kheap_large_alloc_free_cycles_remain_usable();
}

void test_kheap_rejects_empty_and_impossible_allocations(void) {
    assert_kheap_rejects_empty_and_impossible_allocations();
}

void test_kheap_allocates_larger_than_one_page(void) {
    assert_kheap_allocates_larger_than_one_page();
}

void test_kheap_allocates_panel_sized_backing_buffer(void) {
    assert_kheap_allocates_panel_sized_backing_buffer();
}

void test_kheap_reuses_large_freed_block_for_small_allocations(void) {
    assert_kheap_reuses_large_freed_block_for_small_allocations();
}

void test_kheap_large_alloc_free_cycles_remain_usable(void) {
    assert_kheap_large_alloc_free_cycles_remain_usable();
}

/* main moved to tests/main.c */
