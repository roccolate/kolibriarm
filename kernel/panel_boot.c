#include "kernel/panel_boot.h"

#include <stddef.h>
#include <stdint.h>

#include "kernel/boot_program.h"
#include "kernel/aarch64_state.h"
#include "kernel/layout.h"
#include "kernel/mm/pmm.h"
#include "kernel/mm/mmu.h"
#include "kernel/mm/vmm.h"
#include "kernel/panel_boot_argv.h"
#include "kernel/process.h"
#include "kernel/user_image.h"
#include "kernel/user_vm.h"
#include "kernel/vfs.h"
#include "uart/pl011.h"

/*
 * EL0 boot helpers. The kernel boots its first userland process (the
 * panel taskbar) through panel_boot_run; later apps come up via
 * kolibri_spawn_vfs from sys_spawn / sys_spawn_argv; el0_return_address
 * points at the trampoline the lower-EL exception vector returns to
 * after sys_exit.
 *
 * The file used to be called user_demo.* and hosted an embedded
 * programs/user_demo.S blob. That blob is gone; the loader now owns
 * boot images through kernel/user_image.c and the bootfs registry.
 * The kernel-side helpers here are the EL0 launch path, not a demo.
 */

#define PANEL_BOOT_PID_BASE 1U

#define PANEL_BOOT_APP "panel"

extern uint64_t user_enter_el0(uint64_t entry, uint64_t stack_top,
                               uint64_t pstate);
extern char user_enter_el0_return[];

typedef struct {
    uint64_t image_paddr;
    uint64_t stack_paddr;
} panel_user_storage_t;

static uint64_t g_spawn_memory_base;
static uint64_t g_spawn_memory_size;
static panel_map_mmio_fn_t g_spawn_map_mmio;
static uint32_t g_next_spawn_pid = PANEL_BOOT_PID_BASE + 1U;

uint64_t el0_return_address(void) {
    return (uint64_t)(uintptr_t)user_enter_el0_return;
}

static uint64_t panel_image_vaddr(uint32_t slot) {
    return KERNEL_USER_IMAGE_VA_BASE +
           (uint64_t)slot * KERNEL_USER_IMAGE_VA_STRIDE;
}

static uint64_t panel_stack_vaddr(uint32_t slot) {
    return KERNEL_USER_STACK_VA_BASE +
           (uint64_t)slot * KERNEL_USER_STACK_VA_STRIDE;
}

static void zero_memory(uint64_t paddr, uint64_t size) {
    uint8_t *bytes = (uint8_t *)(uintptr_t)paddr;

    for (uint64_t i = 0; i < size; i++) {
        bytes[i] = 0;
    }
}

static void free_user_storage(panel_user_storage_t *storage) {
    if (storage == 0) {
        return;
    }

    if (storage->image_paddr != 0) {
        for (uint64_t i = 0; i < KERNEL_USER_IMAGE_SLOT_PAGES; i++) {
            pmm_free_page(storage->image_paddr + i * PAGE_SIZE);
        }
    }
    if (storage->stack_paddr != 0) {
        pmm_free_page(storage->stack_paddr);
    }

    storage->image_paddr = 0;
    storage->stack_paddr = 0;
}

static int load_named_image(const char *name, user_image_t *image,
                            uint32_t slot, uint32_t entry_index,
                            panel_user_storage_t *storage) {
    if (storage == 0) {
        return -1;
    }

    storage->image_paddr = pmm_alloc_pages(KERNEL_USER_IMAGE_SLOT_PAGES);
    if (storage->image_paddr == 0) {
        return -1;
    }
    zero_memory(storage->image_paddr, KERNEL_USER_IMAGE_SLOT_SIZE);

    if (user_image_load_bootfs_flat(image, name, name,
                                    storage->image_paddr,
                                    KERNEL_USER_IMAGE_SLOT_SIZE,
                                    entry_index) != 0) {
        return -1;
    }

    image->base = panel_image_vaddr(slot);
    image->size = KERNEL_USER_IMAGE_SLOT_SIZE;
    return 0;
}

static int map_kernel_identity(uint64_t *pgd, uint64_t memory_base,
                               uint64_t memory_size,
                               panel_map_mmio_fn_t map_mmio) {
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

static int create_panel_page_table(process_t *process,
                                   const user_image_t *image,
                                   uint64_t image_paddr,
                                   uint64_t stack_vaddr,
                                   uint64_t stack_paddr,
                                   uint64_t stack_size,
                                   uint64_t memory_base,
                                   uint64_t memory_size,
                                   panel_map_mmio_fn_t map_mmio) {
    uint64_t *pgd;

    if (process == 0 || image == 0) {
        return -1;
    }

    pgd = vmm_new_table();
    if (pgd == 0) {
        return -1;
    }

    if (map_kernel_identity(pgd, memory_base, memory_size, map_mmio) != 0) {
        vmm_free_table(pgd);
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

static int init_panel_process(process_t *process, const user_image_t *image,
                              panel_user_storage_t *storage,
                              uint32_t slot, uint64_t memory_base,
                              uint64_t memory_size,
                              panel_map_mmio_fn_t map_mmio) {
    uint64_t stack_vaddr;

    if (process == 0 || image == 0 || storage == 0 ||
        slot >= PROCESS_MAX_PROCESSES || storage->image_paddr == 0) {
        return -1;
    }

    storage->stack_paddr = pmm_alloc_pages(KERNEL_USER_STACK_PAGES);
    if (storage->stack_paddr == 0) {
        return -1;
    }
    zero_memory(storage->stack_paddr, KERNEL_USER_STACK_SIZE);

    stack_vaddr = panel_stack_vaddr(slot);

    if (user_image_prepare_process(process, image, stack_vaddr,
                                   KERNEL_USER_STACK_SIZE,
                                   AARCH64_SPSR_EL0T_DAF_MASKED) != 0) {
        return -1;
    }

    if (create_panel_page_table(process, image, storage->image_paddr,
                                stack_vaddr, storage->stack_paddr,
                                KERNEL_USER_STACK_SIZE, memory_base,
                                memory_size, map_mmio) != 0) {
        return -1;
    }

    if (process_set_user_region_mapping(
            process, image->base, image->size, storage->image_paddr,
            PROCESS_USER_REGION_OWNED_PAGES) != 0) {
        return -1;
    }
    storage->image_paddr = 0;
    /*
     * Keep stack_paddr in storage after ownership transfer: sys_spawn_argv
     * still needs the kernel-accessible stack backing to pack argv before the
     * new process first runs. If argv packing fails, process_release owns the
     * cleanup path through PROCESS_USER_REGION_OWNED_PAGES.
     */
    if (process_set_user_region_mapping(
            process, stack_vaddr, KERNEL_USER_STACK_SIZE, storage->stack_paddr,
            PROCESS_USER_REGION_OWNED_PAGES) != 0) {
        return -1;
    }
    return 0;
}

static int place_argv_on_stack(uint64_t stack_paddr, uint32_t slot,
                               const uint64_t *argv_ptr,
                               uint32_t argc, uint64_t *out_argv_vaddr) {
    if (stack_paddr == 0 || slot >= PROCESS_MAX_PROCESSES) {
        return -1;
    }

    return panel_boot_place_argv_on_stack((uint8_t *)(uintptr_t)stack_paddr,
                                          panel_stack_vaddr(slot),
                                          KERNEL_USER_STACK_SIZE,
                                          argv_ptr, argc, out_argv_vaddr);
}

int kolibri_spawn_vfs(const char *path, uint32_t entry_index,
                      const uint64_t *argv_ptr, uint32_t argc) {
    process_t *process;
    user_image_t image;
    panel_user_storage_t storage = {0};
    uint32_t slot;
    const char *app_name;
    size_t name_len;
    uint64_t argv_vaddr = 0;

    if (path == 0 || g_spawn_memory_size == 0) {
        return -1;
    }

    app_name = vfs_strip_prefix(path, "/kolibri/");
    if (app_name == 0) {
        return -1;
    }

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

    if (load_named_image(app_name, &image, slot, entry_index, &storage) != 0) {
        process_release(process);
        free_user_storage(&storage);
        return -1;
    }

    if (init_panel_process(process, &image, &storage, slot, g_spawn_memory_base,
                           g_spawn_memory_size, g_spawn_map_mmio) != 0) {
        process_release(process);
        free_user_storage(&storage);
        return -1;
    }

    /* Always run place_argv_on_stack: it returns argv_vaddr=0 for
     * argc==0, otherwise copies the strings and argv array into the
     * new stack and stores the initial sp. This consolidates the
     * argc==0 and argc>0 paths and keeps the inlined code small. */
    process->regs[0] = 0;
    process->regs[1] = 0;
    if (place_argv_on_stack(storage.stack_paddr, slot, argv_ptr, argc,
                            &argv_vaddr) != 0) {
        process_release(process);
        return -1;
    }
    if (argv_vaddr != 0) {
        process->sp = argv_vaddr;
        process->regs[0] = argc;
        process->regs[1] = argv_vaddr;
    }

    process->state = PROCESS_READY;
    return (int)process->pid;
}

uint64_t panel_boot_run(uint64_t memory_base, uint64_t memory_size,
                        panel_map_mmio_fn_t map_mmio) {
    uint64_t *kernel_page_table =
        (uint64_t *)(uintptr_t)mmu_read_ttbr0_el1();
    uint64_t exit_code;
    process_t *panel;
    user_image_t panel_image;
    panel_user_storage_t storage = {0};
    uint32_t slot;

    (void)process_reclaim_zombies();
    panel = process_alloc(PANEL_BOOT_PID_BASE, PANEL_BOOT_APP);
    if (panel == 0) {
        uart_puts("panel_boot: process alloc failed\n");
        return 1;
    }

    if (process_index(panel, &slot) != 0) {
        process_release(panel);
        uart_puts("panel_boot: process slot failed\n");
        return 1;
    }

    if (load_named_image(PANEL_BOOT_APP, &panel_image, slot, 0,
                         &storage) != 0) {
        process_release(panel);
        free_user_storage(&storage);
        uart_puts("panel_boot: image load failed\n");
        return 1;
    }

    if (init_panel_process(panel, &panel_image, &storage, slot, memory_base,
                           memory_size, map_mmio) != 0) {
        process_release(panel);
        free_user_storage(&storage);
        uart_puts("panel_boot: process setup failed\n");
        return 1;
    }

    g_spawn_memory_base = memory_base;
    g_spawn_memory_size = memory_size;
    g_spawn_map_mmio = map_mmio;

    panel->state = PROCESS_RUNNING;
    process_set_current(panel);

    uart_puts("panel_boot: entering EL0\n");
    if (panel->page_table != 0) {
        mmu_set_ttbr0(panel->page_table);
    }
    exit_code = user_enter_el0(panel->pc, panel->sp, panel->pstate);
    if (kernel_page_table != 0) {
        mmu_set_ttbr0(kernel_page_table);
    }
    uart_puts("panel_boot: returned to EL1\n");

    (void)process_reclaim_zombies();
    process_release(panel);

    return exit_code;
}
