#include <stdint.h>
#include <stdlib.h>

#include "unity/unity.h"
#include "../kernel/mm/pmm.h"
#include "../kernel/mm/vmm.h"
#include "../kernel/process.h"
#include "../kernel/user_vm.h"

#define TEST_PAGES 96U

static void init_test_memory(void **mem) {
    int rc = posix_memalign(mem, PAGE_SIZE, TEST_PAGES * PAGE_SIZE);
    if (rc != 0) {
        TEST_FAIL("posix_memalign failed");
    }

    TEST_ASSERT_NOT_NULL(*mem);
    pmm_init((uint64_t)(uintptr_t)*mem, TEST_PAGES * PAGE_SIZE);
}

void test_user_vm_map_anonymous_installs_user_pte(void) {
    void *mem = NULL;
    process_t process;
    process_user_region_t region;

    init_test_memory(&mem);

    uint64_t *pgd = vmm_new_table();
    process_init(&process, 21, "vm-map");
    process_set_page_table(&process, pgd);

    int64_t addr = user_vm_map_anonymous(&process, 0, 1, 0);

    TEST_ASSERT_EQUAL_UINT64(PROCESS_USER_MMAP_BASE, (uint64_t)addr);
    TEST_ASSERT_EQUAL_UINT64(1, process.user_region_count);
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)process_find_user_region(
                                 &process, (uint64_t)addr, PAGE_SIZE,
                                 &region));
    TEST_ASSERT_TRUE(region.paddr != 0);
    TEST_ASSERT_EQUAL_UINT64(PROCESS_USER_REGION_OWNED_PAGES, region.flags);
    TEST_ASSERT_EQUAL_UINT64(region.paddr,
                             vmm_virt_to_phys(pgd, (uint64_t)addr));

    uint64_t entry = vmm_leaf_entry(pgd, (uint64_t)addr);
    TEST_ASSERT_TRUE((entry & (1ULL << 6)) != 0);
    TEST_ASSERT_TRUE((entry & (1ULL << 54)) != 0);
    TEST_ASSERT_TRUE((entry & (1ULL << 53)) != 0);

    free(mem);
}

void test_user_vm_unmap_anonymous_removes_pte_and_frees_pages(void) {
    void *mem = NULL;
    process_t process;

    init_test_memory(&mem);

    uint64_t *pgd = vmm_new_table();
    process_init(&process, 22, "vm-unmap");
    process_set_page_table(&process, pgd);

    int64_t addr = user_vm_map_anonymous(&process, 0, PAGE_SIZE * 2, 0);
    uint64_t free_after_map = pmm_free_count();

    TEST_ASSERT_EQUAL_UINT64(PROCESS_USER_MMAP_BASE, (uint64_t)addr);
    TEST_ASSERT_TRUE(vmm_virt_to_phys(pgd, (uint64_t)addr) != 0);
    TEST_ASSERT_TRUE(vmm_virt_to_phys(pgd, (uint64_t)addr + PAGE_SIZE) != 0);

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)user_vm_unmap_anonymous(
                                 &process, (uint64_t)addr, PAGE_SIZE * 2));
    TEST_ASSERT_EQUAL_UINT64(0, process.user_region_count);
    TEST_ASSERT_EQUAL_UINT64(0, vmm_virt_to_phys(pgd, (uint64_t)addr));
    TEST_ASSERT_EQUAL_UINT64(0,
                             vmm_virt_to_phys(pgd,
                                              (uint64_t)addr + PAGE_SIZE));
    TEST_ASSERT_EQUAL_UINT64(free_after_map + 2, pmm_free_count());

    free(mem);
}

void test_user_vm_rejects_invalid_inputs_without_regions(void) {
    void *mem = NULL;
    process_t process;

    init_test_memory(&mem);

    uint64_t *pgd = vmm_new_table();
    process_init(&process, 23, "vm-invalid");
    process_set_page_table(&process, pgd);

    TEST_ASSERT_EQUAL_UINT64((uint64_t)USER_VM_ERR_INVAL,
                             (uint64_t)user_vm_map_anonymous(
                                 &process, 0x1000ULL, PAGE_SIZE, 0));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)USER_VM_ERR_INVAL,
                             (uint64_t)user_vm_map_anonymous(
                                 &process, 0, PAGE_SIZE, 0x20ULL));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)USER_VM_ERR_INVAL,
                             (uint64_t)user_vm_map_anonymous(
                                 &process, 0, 0, 0));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)USER_VM_ERR_INVAL,
                             (uint64_t)user_vm_map_anonymous(
                                 &process, 0, UINT64_MAX, 0));
    TEST_ASSERT_EQUAL_UINT64(0, process.user_region_count);

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)process_add_user_region(
                                    &process, 0x4000ULL, PAGE_SIZE));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)USER_VM_ERR_INVAL,
                             (uint64_t)user_vm_unmap_anonymous(
                                 &process, 0x4000ULL, PAGE_SIZE));
    TEST_ASSERT_EQUAL_UINT64(1, process.user_region_count);

    free(mem);
}

void test_user_vm_map_physical_maps_registered_region(void) {
    void *mem = NULL;
    process_t process;
    process_user_region_t region;

    init_test_memory(&mem);

    uint64_t *pgd = vmm_new_table();
    uint64_t paddr = pmm_alloc_page();
    uint64_t vaddr = 0x400000ULL;

    process_init(&process, 24, "vm-physical");
    process_set_page_table(&process, pgd);
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)process_add_user_region(
                                    &process, vaddr, PAGE_SIZE));

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)user_vm_map_physical(
                                 &process, vaddr, paddr, PAGE_SIZE,
                                 USER_VM_PROT_READ | USER_VM_PROT_EXEC));

    TEST_ASSERT_EQUAL_UINT64(paddr, vmm_virt_to_phys(pgd, vaddr));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)process_find_user_region(
                                 &process, vaddr, PAGE_SIZE, &region));
    TEST_ASSERT_EQUAL_UINT64(paddr, region.paddr);
    TEST_ASSERT_EQUAL_UINT64(0, region.flags);

    uint64_t entry = vmm_leaf_entry(pgd, vaddr);
    TEST_ASSERT_TRUE((entry & (1ULL << 6)) != 0);
    TEST_ASSERT_TRUE((entry & (1ULL << 54)) == 0);
    TEST_ASSERT_TRUE((entry & (1ULL << 53)) == 0);

    free(mem);
}

void test_user_vm_map_physical_rejects_unregistered_region(void) {
    void *mem = NULL;
    process_t process;

    init_test_memory(&mem);

    uint64_t *pgd = vmm_new_table();
    uint64_t paddr = pmm_alloc_page();
    uint64_t vaddr = 0x500000ULL;

    process_init(&process, 25, "vm-physical-bad");
    process_set_page_table(&process, pgd);

    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)user_vm_map_physical(
                                 &process, vaddr, paddr, PAGE_SIZE,
                                 USER_VM_PROT_READ));
    TEST_ASSERT_EQUAL_UINT64(0, vmm_virt_to_phys(pgd, vaddr));
    TEST_ASSERT_EQUAL_UINT64(0, process.user_region_count);

    free(mem);
}

void test_user_vm_physical_mappings_stay_per_process(void) {
    void *mem = NULL;
    process_t first;
    process_t second;

    init_test_memory(&mem);

    uint64_t *first_pgd = vmm_new_table();
    uint64_t *second_pgd = vmm_new_table();
    uint64_t first_paddr = pmm_alloc_page();
    uint64_t second_paddr = pmm_alloc_page();
    uint64_t first_vaddr = 0x400000ULL;
    uint64_t second_vaddr = 0x410000ULL;

    process_init(&first, 26, "vm-first");
    process_set_page_table(&first, first_pgd);
    process_init(&second, 27, "vm-second");
    process_set_page_table(&second, second_pgd);

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)process_add_user_region(
                                    &first, first_vaddr, PAGE_SIZE));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)process_add_user_region(
                                    &second, second_vaddr, PAGE_SIZE));

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)user_vm_map_physical(
                                 &first, first_vaddr, first_paddr, PAGE_SIZE,
                                 USER_VM_PROT_READ | USER_VM_PROT_WRITE));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)user_vm_map_physical(
                                 &second, second_vaddr, second_paddr, PAGE_SIZE,
                                 USER_VM_PROT_READ | USER_VM_PROT_WRITE));

    TEST_ASSERT_EQUAL_UINT64(first_paddr,
                             vmm_virt_to_phys(first_pgd, first_vaddr));
    TEST_ASSERT_EQUAL_UINT64(0, vmm_virt_to_phys(first_pgd, second_vaddr));
    TEST_ASSERT_EQUAL_UINT64(second_paddr,
                             vmm_virt_to_phys(second_pgd, second_vaddr));
    TEST_ASSERT_EQUAL_UINT64(0, vmm_virt_to_phys(second_pgd, first_vaddr));

    free(mem);
}
