#include "kernel/user_demo.h"

#include <stddef.h>
#include <stdint.h>

#include "kernel/boot_program.h"
#include "kernel/mm/mmu.h"
#include "kernel/mm/vmm.h"
#include "kernel/process.h"
#include "kernel/user_image.h"
#include "kernel/user_vm.h"
#include "kernel/vfs.h"
#include "uart/pl011.h"

#define USER_STACK_SIZE 4096ULL
#define USER_IMAGE_SLOT_SIZE 8192ULL
#define USER_DEMO_PID_BASE 1U
#define USER_DEMO_PSTATE 0x340ULL
#define USER_DEMO_IMAGE_VA_BASE 0x0000000000400000ULL
#define USER_DEMO_IMAGE_VA_STRIDE 0x0000000000010000ULL
#define USER_DEMO_STACK_VA_BASE 0x0000000000800000ULL
#define USER_DEMO_STACK_VA_STRIDE 0x0000000000010000ULL

#define USER_DEMO_BOOT_APP "panel"

extern uint64_t user_enter_el0(uint64_t entry, uint64_t stack_top, uint64_t pstate);
extern char user_enter_el0_return[];

static uint8_t g_user_stacks[PROCESS_MAX_PROCESSES][USER_STACK_SIZE]
    __attribute__((aligned(4096)));
static uint8_t g_user_image_slots[PROCESS_MAX_PROCESSES][USER_IMAGE_SLOT_SIZE]
    __attribute__((aligned(4096)));
static uint64_t g_spawn_memory_base;
static uint64_t g_spawn_memory_size;
static user_demo_map_mmio_fn_t g_spawn_map_mmio;
static uint32_t g_next_spawn_pid = USER_DEMO_PID_BASE + 1U;

uint64_t user_demo_return_address(void) {
    return (uint64_t)(uintptr_t)user_enter_el0_return;
}

static uint64_t user_demo_image_vaddr(uint32_t slot) {
    return USER_DEMO_IMAGE_VA_BASE + slot * USER_DEMO_IMAGE_VA_STRIDE;
}

static uint64_t user_demo_stack_vaddr(uint32_t slot) {
    return USER_DEMO_STACK_VA_BASE + slot * USER_DEMO_STACK_VA_STRIDE;
}

static int load_named_image(const char *name, user_image_t *image,
                            uint32_t slot, uint32_t entry_index) {
    if (user_image_load_bootfs_flat(image, name, name,
                                    (uint64_t)(uintptr_t)g_user_image_slots[slot],
                                    USER_IMAGE_SLOT_SIZE, entry_index) != 0) {
        return -1;
    }

    image->base = user_demo_image_vaddr(slot);
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
                                  uint32_t slot, uint64_t memory_base,
                                  uint64_t memory_size,
                                  user_demo_map_mmio_fn_t map_mmio) {
    uint64_t image_paddr;
    uint64_t stack_paddr;
    uint64_t stack_vaddr;

    if (process == 0 || slot >= PROCESS_MAX_PROCESSES) {
        return -1;
    }

    image_paddr = (uint64_t)(uintptr_t)g_user_image_slots[slot];
    stack_paddr = (uint64_t)(uintptr_t)g_user_stacks[slot];
    stack_vaddr = user_demo_stack_vaddr(slot);

    if (user_image_prepare_process(process, image, stack_vaddr,
                                   USER_STACK_SIZE, USER_DEMO_PSTATE) != 0) {
        return -1;
    }

    return create_user_demo_page_table(process, image, image_paddr,
                                       stack_vaddr, stack_paddr,
                                       USER_STACK_SIZE, memory_base,
                                       memory_size, map_mmio);
}

int user_demo_spawn_vfs(const char *path, uint32_t entry_index) {
    process_t *process;
    user_image_t image;
    uint32_t slot;
    const char *app_name;
    size_t name_len;

    if (path == 0 || g_spawn_memory_size == 0) {
        return -1;
    }

    if (path[0] != '/' || path[1] != 'k' || path[2] != 'o' ||
        path[3] != 'l' || path[4] != 'i' || path[5] != 'b' ||
        path[6] != 'r' || path[7] != 'i' || path[8] != '/' ||
        path[9] == '\0') {
        return -1;
    }

    app_name = path + 9;
    name_len = 0;
    while (app_name[name_len] != '\0') {
        name_len++;
    }
    if (name_len == 0 || name_len >= 32) {
        return -1;
    }

    (void)process_reclaim_zombies();
    process = process_alloc(g_next_spawn_pid++, app_name);
    if (process == 0) {
        return -1;
    }

    if (process_index(process, &slot) != 0 || slot >= PROCESS_MAX_PROCESSES) {
        process_release(process);
        return -1;
    }

    if (load_named_image(app_name, &image, slot, entry_index) != 0) {
        process_release(process);
        return -1;
    }

    if (init_user_demo_process(process, &image, slot, g_spawn_memory_base,
                               g_spawn_memory_size, g_spawn_map_mmio) != 0) {
        process_release(process);
        return -1;
    }

    process->state = PROCESS_READY;
    return (int)process->pid;
}

uint64_t user_demo_run(uint64_t memory_base, uint64_t memory_size,
                       user_demo_map_mmio_fn_t map_mmio) {
    uint64_t *kernel_page_table =
        (uint64_t *)(uintptr_t)mmu_read_ttbr0_el1();
    uint64_t exit_code;
    process_t *shell;
    user_image_t shell_image;
    uint32_t slot;

    (void)process_reclaim_zombies();
    shell = process_alloc(USER_DEMO_PID_BASE, USER_DEMO_BOOT_APP);
    if (shell == 0) {
        uart_puts("USER demo: process alloc failed\n");
        return 1;
    }

    if (process_index(shell, &slot) != 0) {
        process_release(shell);
        uart_puts("USER demo: process slot failed\n");
        return 1;
    }

    if (load_named_image(USER_DEMO_BOOT_APP, &shell_image, slot, 0) != 0) {
        process_release(shell);
        uart_puts("USER demo: image load failed\n");
        return 1;
    }

    if (init_user_demo_process(shell, &shell_image, slot, memory_base,
                               memory_size, map_mmio) != 0) {
        process_release(shell);
        uart_puts("USER demo: process setup failed\n");
        return 1;
    }

    g_spawn_memory_base = memory_base;
    g_spawn_memory_size = memory_size;
    g_spawn_map_mmio = map_mmio;

    shell->state = PROCESS_RUNNING;
    process_set_current(shell);

    uart_puts("USER demo: entering EL0\n");
    if (shell->page_table != 0) {
        mmu_set_ttbr0(shell->page_table);
    }
    exit_code = user_enter_el0(shell->pc, shell->sp, shell->pstate);
    if (kernel_page_table != 0) {
        mmu_set_ttbr0(kernel_page_table);
    }
    uart_puts("USER demo: returned to EL1\n");

    (void)process_reclaim_zombies();
    process_release(shell);

    return exit_code;
}
