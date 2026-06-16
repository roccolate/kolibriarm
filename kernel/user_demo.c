#include "kernel/user_demo.h"

#include <stdint.h>

#include "kernel/mm/mmu.h"
#include "kernel/mm/vmm.h"
#include "kernel/process.h"
#include "kernel/user_image.h"
#include "kernel/user_vm.h"
#include "uart/pl011.h"

#define USER_STACK_SIZE 4096ULL
#define USER_IMAGE_SLOT_SIZE 4096ULL
#define USER_DEMO_PROCESS_COUNT 3U
#define USER_DEMO_PID_BASE 1U
#define USER_DEMO_PSTATE 0x340ULL
#define USER_DEMO_IMAGE_VA_BASE 0x0000000000400000ULL
#define USER_DEMO_IMAGE_VA_STRIDE 0x0000000000010000ULL
#define USER_DEMO_STACK_VA_BASE 0x0000000000800000ULL
#define USER_DEMO_STACK_VA_STRIDE 0x0000000000010000ULL

extern uint64_t user_enter_el0(uint64_t entry, uint64_t stack_top, uint64_t pstate);
extern char user_enter_el0_return[];

static uint8_t g_user_stacks[USER_DEMO_PROCESS_COUNT][USER_STACK_SIZE]
    __attribute__((aligned(4096)));
static uint8_t g_user_image_slots[USER_DEMO_PROCESS_COUNT][USER_IMAGE_SLOT_SIZE]
    __attribute__((aligned(4096)));
static user_image_t g_user_demo_images[USER_DEMO_PROCESS_COUNT];

static uint64_t user_demo_image_vaddr(uint32_t index) {
    return USER_DEMO_IMAGE_VA_BASE + index * USER_DEMO_IMAGE_VA_STRIDE;
}

static uint64_t user_demo_stack_vaddr(uint32_t index) {
    return USER_DEMO_STACK_VA_BASE + index * USER_DEMO_STACK_VA_STRIDE;
}

uint64_t user_demo_return_address(void) {
    return (uint64_t)(uintptr_t)user_enter_el0_return;
}

int user_demo_prepare_images(void) {
    if (user_image_load_bootfs_flat(&g_user_demo_images[0], "user-demo-a",
                                    "user_demo",
                                    (uint64_t)(uintptr_t)g_user_image_slots[0],
                                    USER_IMAGE_SLOT_SIZE, 0) != 0) {
        return -1;
    }
    g_user_demo_images[0].base = user_demo_image_vaddr(0);

    if (user_image_load_bootfs_flat(&g_user_demo_images[1], "user-demo-b",
                                    "user_demo",
                                    (uint64_t)(uintptr_t)g_user_image_slots[1],
                                    USER_IMAGE_SLOT_SIZE, 1) != 0) {
        return -1;
    }
    g_user_demo_images[1].base = user_demo_image_vaddr(1);

    if (user_image_load_bootfs_flat(&g_user_demo_images[2], "user-demo-fault",
                                    "user_demo",
                                    (uint64_t)(uintptr_t)g_user_image_slots[2],
                                    USER_IMAGE_SLOT_SIZE, 2) != 0) {
        return -1;
    }
    g_user_demo_images[2].base = user_demo_image_vaddr(2);

    return 0;
}

static int map_kernel_identity(uint64_t *pgd, uint64_t memory_base,
                               uint64_t memory_size,
                               user_demo_map_mmio_fn_t map_mmio) {
    int status;

    if (pgd == 0 || memory_size == 0) {
        return -1;
    }

    status = vmm_map_range(pgd, memory_base, memory_base, memory_size,
                           VMM_FLAG_READ | VMM_FLAG_WRITE | VMM_FLAG_EXEC);
    if (status == 0 && map_mmio != 0) {
        status = map_mmio(pgd);
    }

    return status;
}

static int create_user_demo_page_table(process_t *process,
                                       const user_image_t *image,
                                       uint64_t image_paddr,
                                       uint64_t stack_vaddr,
                                       uint64_t stack_paddr,
                                       uint64_t stack_size,
                                       uint64_t memory_base,
                                       uint64_t memory_size,
                                       user_demo_map_mmio_fn_t map_mmio) {
    uint64_t *pgd = vmm_new_table();

    if (process == 0 || image == 0 || pgd == 0) {
        return -1;
    }

    if (map_kernel_identity(pgd, memory_base, memory_size, map_mmio) != 0) {
        return -1;
    }

    process_set_page_table(process, pgd);

    if (user_vm_map_physical(process, image->base, image_paddr, image->size,
                             USER_VM_PROT_READ | USER_VM_PROT_EXEC) != 0) {
        return -1;
    }

    if (user_vm_map_physical(process, stack_vaddr, stack_paddr, stack_size,
                             USER_VM_PROT_READ | USER_VM_PROT_WRITE) != 0) {
        return -1;
    }

    return 0;
}

static int init_user_demo_process(process_t *process, const user_image_t *image,
                                  uint32_t stack_index, uint64_t memory_base,
                                  uint64_t memory_size,
                                  user_demo_map_mmio_fn_t map_mmio) {
    uint64_t image_paddr;
    uint64_t stack_paddr;
    uint64_t stack_vaddr;

    if (process == 0 || stack_index >= USER_DEMO_PROCESS_COUNT) {
        return -1;
    }

    image_paddr = (uint64_t)(uintptr_t)g_user_image_slots[stack_index];
    stack_paddr = (uint64_t)(uintptr_t)g_user_stacks[stack_index];
    stack_vaddr = user_demo_stack_vaddr(stack_index);

    if (user_image_prepare_process(process, image, stack_vaddr,
                                   USER_STACK_SIZE, USER_DEMO_PSTATE) != 0) {
        return -1;
    }

    return create_user_demo_page_table(process, image, image_paddr,
                                       stack_vaddr, stack_paddr,
                                       USER_STACK_SIZE, memory_base,
                                       memory_size, map_mmio);
}

uint64_t user_demo_run(uint64_t memory_base, uint64_t memory_size,
                       user_demo_map_mmio_fn_t map_mmio) {
    uint64_t *kernel_page_table =
        (uint64_t *)(uintptr_t)mmu_read_ttbr0_el1();
    uint64_t exit_code;
    process_t *first = process_alloc(USER_DEMO_PID_BASE,
                                     g_user_demo_images[0].name);
    process_t *second = process_alloc(USER_DEMO_PID_BASE + 1U,
                                      g_user_demo_images[1].name);
    process_t *faulting = process_alloc(USER_DEMO_PID_BASE + 2U,
                                        g_user_demo_images[2].name);

    if (init_user_demo_process(first, &g_user_demo_images[0], 0, memory_base,
                               memory_size, map_mmio) != 0 ||
        init_user_demo_process(second, &g_user_demo_images[1], 1, memory_base,
                               memory_size, map_mmio) != 0 ||
        init_user_demo_process(faulting, &g_user_demo_images[2], 2, memory_base,
                               memory_size, map_mmio) != 0) {
        uart_puts("USER demo: process setup failed\n");
        process_release(first);
        process_release(second);
        process_release(faulting);
        return 1;
    }

    first->state = PROCESS_RUNNING;
    second->state = PROCESS_READY;
    faulting->state = PROCESS_READY;
    process_set_current(first);

    uart_puts("USER demo: entering EL0\n");
    if (first->page_table != 0) {
        mmu_set_ttbr0(first->page_table);
    }
    exit_code = user_enter_el0(first->pc, first->sp, first->pstate);
    if (kernel_page_table != 0) {
        mmu_set_ttbr0(kernel_page_table);
    }
    uart_puts("USER demo: returned to EL1\n");

    if (process_reclaim_zombies() == 0) {
        process_release(first);
        process_release(second);
        process_release(faulting);
    }

    return exit_code;
}
