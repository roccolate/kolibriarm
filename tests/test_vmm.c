#include <stdint.h>
#include <stdlib.h>

#include "unity/unity.h"
#include "../kernel/mm/pmm.h"
#include "../kernel/mm/vmm.h"

#define TEST_PAGES 32U

static void init_test_memory(void **mem) {
    int rc = posix_memalign(mem, PAGE_SIZE, TEST_PAGES * PAGE_SIZE);
    if (rc != 0) {
        TEST_FAIL("posix_memalign failed");
    }

    TEST_ASSERT_NOT_NULL(*mem);
    pmm_init((uint64_t)(uintptr_t)*mem, TEST_PAGES * PAGE_SIZE);
}

void test_vmm_map_translate_unmap_page(void) {
    void *mem = NULL;
    init_test_memory(&mem);

    uint64_t *pgd = vmm_new_table();
    uint64_t page = pmm_alloc_page();
    uint64_t vaddr = 0x400000ULL;

    TEST_ASSERT_NOT_NULL(pgd);
    TEST_ASSERT_TRUE((page) != 0);
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vmm_map_page(pgd, vaddr, page,
                                                       VMM_FLAG_READ |
                                                           VMM_FLAG_WRITE));
    TEST_ASSERT_EQUAL_UINT64(page + 0x123ULL,
                             vmm_virt_to_phys(pgd, vaddr + 0x123ULL));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vmm_map_page(pgd, vaddr, page,
                                                    VMM_FLAG_READ |
                                                        VMM_FLAG_WRITE));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vmm_unmap_page(pgd, vaddr));
    TEST_ASSERT_EQUAL_UINT64(0, vmm_virt_to_phys(pgd, vaddr));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1, (uint64_t)vmm_unmap_page(pgd, vaddr));

    free(mem);
}

void test_vmm_map_range_and_unmap_range(void) {
    void *mem = NULL;
    init_test_memory(&mem);

    uint64_t *pgd = vmm_new_table();
    uint64_t pages = pmm_alloc_pages(2);
    uint64_t vaddr = 0x800000ULL;

    TEST_ASSERT_NOT_NULL(pgd);
    TEST_ASSERT_TRUE((pages) != 0);
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vmm_map_range(pgd, vaddr, pages,
                                                        PAGE_SIZE * 2,
                                                        VMM_FLAG_READ |
                                                            VMM_FLAG_WRITE));
    TEST_ASSERT_EQUAL_UINT64(pages, vmm_virt_to_phys(pgd, vaddr));
    TEST_ASSERT_EQUAL_UINT64(pages + PAGE_SIZE,
                             vmm_virt_to_phys(pgd, vaddr + PAGE_SIZE));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vmm_unmap_range(pgd, vaddr,
                                                          PAGE_SIZE * 2));
    TEST_ASSERT_EQUAL_UINT64(0, vmm_virt_to_phys(pgd, vaddr));
    TEST_ASSERT_EQUAL_UINT64(0, vmm_virt_to_phys(pgd, vaddr + PAGE_SIZE));

    free(mem);
}

void test_vmm_map_range_rejects_overflowing_span(void) {
    void *mem = NULL;
    init_test_memory(&mem);

    uint64_t *pgd = vmm_new_table();
    uint64_t page = pmm_alloc_page();

    TEST_ASSERT_NOT_NULL(pgd);
    TEST_ASSERT_TRUE(page != 0);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vmm_map_range(
                                 pgd, UINT64_MAX - 0xffULL,
                                 page + PAGE_SIZE - 0x100ULL,
                                 0x200ULL,
                                 VMM_FLAG_READ | VMM_FLAG_WRITE));

    free(mem);
}

void test_vmm_map_range_rolls_back_partial_failure(void) {
    void *mem = NULL;
    init_test_memory(&mem);

    uint64_t *pgd = vmm_new_table();
    uint64_t first_page = pmm_alloc_page();
    uint64_t conflict_page = pmm_alloc_page();
    uint64_t new_pages = pmm_alloc_pages(2);
    uint64_t vaddr = 0xa00000ULL;

    TEST_ASSERT_NOT_NULL(pgd);
    TEST_ASSERT_TRUE(first_page != 0);
    TEST_ASSERT_TRUE(conflict_page != 0);
    TEST_ASSERT_TRUE(new_pages != 0);

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vmm_map_page(
                                    pgd, vaddr + PAGE_SIZE, conflict_page,
                                    VMM_FLAG_READ | VMM_FLAG_WRITE));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vmm_map_range(
                                 pgd, vaddr, new_pages, PAGE_SIZE * 2,
                                 VMM_FLAG_READ | VMM_FLAG_WRITE));

    TEST_ASSERT_EQUAL_UINT64(0, vmm_virt_to_phys(pgd, vaddr));
    TEST_ASSERT_EQUAL_UINT64(conflict_page,
                             vmm_virt_to_phys(pgd, vaddr + PAGE_SIZE));

    (void)first_page;
    free(mem);
}

void test_vmm_unmap_range_rejects_overflowing_span(void) {
    void *mem = NULL;
    init_test_memory(&mem);

    uint64_t *pgd = vmm_new_table();

    TEST_ASSERT_NOT_NULL(pgd);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)vmm_unmap_range(
                                 pgd, UINT64_MAX - 0xffULL, 0x200ULL));

    free(mem);
}

void test_vmm_user_exec_mapping_flags(void) {
    void *mem = NULL;
    init_test_memory(&mem);

    uint64_t *pgd = vmm_new_table();
    uint64_t page = pmm_alloc_page();
    uint64_t vaddr = 0x100000ULL;
    uint64_t entry;

    TEST_ASSERT_NOT_NULL(pgd);
    TEST_ASSERT_TRUE((page) != 0);
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vmm_map_page(pgd, vaddr, page,
                                                       VMM_FLAG_READ |
                                                           VMM_FLAG_EXEC |
                                                           VMM_FLAG_USER));

    entry = vmm_leaf_entry(pgd, vaddr);
    TEST_ASSERT_TRUE((entry & (1ULL << 6)) != 0);
    TEST_ASSERT_TRUE((entry & (1ULL << 54)) == 0);
    TEST_ASSERT_TRUE((entry & (1ULL << 53)) == 0);

    free(mem);
}

void test_vmm_kernel_mapping_can_be_replaced_with_user_mapping(void) {
    void *mem = NULL;
    init_test_memory(&mem);

    uint64_t *pgd = vmm_new_table();
    uint64_t page = pmm_alloc_page();
    uint64_t vaddr = 0x200000ULL;
    uint64_t entry;

    TEST_ASSERT_NOT_NULL(pgd);
    TEST_ASSERT_TRUE(page != 0);
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vmm_map_page(pgd, vaddr, page,
                                                       VMM_FLAG_READ |
                                                           VMM_FLAG_WRITE |
                                                           VMM_FLAG_EXEC));

    entry = vmm_leaf_entry(pgd, vaddr);
    TEST_ASSERT_TRUE((entry & (1ULL << 6)) == 0);

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vmm_unmap_range(pgd, vaddr,
                                                          PAGE_SIZE));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vmm_map_range(pgd, vaddr, page,
                                                        PAGE_SIZE,
                                                        VMM_FLAG_READ |
                                                            VMM_FLAG_EXEC |
                                                            VMM_FLAG_USER));

    entry = vmm_leaf_entry(pgd, vaddr);
    TEST_ASSERT_TRUE((entry & (1ULL << 6)) != 0);
    TEST_ASSERT_TRUE((entry & (1ULL << 54)) == 0);
    TEST_ASSERT_TRUE((entry & (1ULL << 53)) == 0);
    TEST_ASSERT_EQUAL_UINT64(page, vmm_virt_to_phys(pgd, vaddr));

    free(mem);
}

void test_vmm_free_table_releases_tables_not_leaf_pages(void) {
    void *mem = NULL;
    init_test_memory(&mem);

    uint64_t free_before = pmm_free_count();
    uint64_t *pgd = vmm_new_table();
    uint64_t page = pmm_alloc_page();
    uint64_t vaddr = 0x300000ULL;

    TEST_ASSERT_NOT_NULL(pgd);
    TEST_ASSERT_TRUE(page != 0);
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)vmm_map_page(pgd, vaddr, page,
                                                       VMM_FLAG_READ |
                                                           VMM_FLAG_WRITE));
    TEST_ASSERT_TRUE(pmm_free_count() < free_before - 1U);

    vmm_free_table(pgd);

    /*
     * The mapped leaf page is still caller-owned; only the page-table
     * hierarchy should have returned to PMM here.
     */
    TEST_ASSERT_EQUAL_UINT64(free_before - 1U, pmm_free_count());

    pmm_free_page(page);
    TEST_ASSERT_EQUAL_UINT64(free_before, pmm_free_count());

    free(mem);
}
