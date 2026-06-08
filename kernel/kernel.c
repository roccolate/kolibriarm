#include <stdint.h>

#include "irq/gicv2.h"
#include "kernel/dtb.h"
#include "kernel/exceptions.h"
#include "kernel/irq.h"
#include "kernel/mm/kheap.h"
#include "kernel/mm/mmu.h"
#include "kernel/mm/pmm.h"
#include "kernel/mm/vmm.h"
#include "kernel/sched/sched.h"
#include "kernel/timer/timer.h"
#include "uart/pl011.h"

extern char __kernel_end[];

static void print_hex64(uint64_t value) {
    static const char digits[] = "0123456789abcdef";

    uart_puts("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        uart_putc(digits[(value >> shift) & 0xf]);
    }
}

void kernel_main(uint64_t dtb_addr) {
    dtb_memory_t memory;

    uart_init();

    uart_puts("\nKolibriARM Phase 1\n");
    uart_puts("Boot OK: EL1 memory bring-up is alive.\n");
    uart_puts("DTB address: ");
    print_hex64(dtb_addr);
    uart_puts("\nVBAR_EL1: ");
    print_hex64(exception_vector_base());
    uart_puts("\n");

    if (dtb_get_memory(dtb_addr, &memory) == 0) {
        uart_puts("RAM base: ");
        print_hex64(memory.base);
        uart_puts("\nRAM size: ");
        print_hex64(memory.size);
        uart_puts("\n");

        pmm_init(memory.base, memory.size);
        pmm_reserve_range(memory.base,
                          (uint64_t)((uintptr_t)__kernel_end - memory.base));
        pmm_reserve_range(dtb_addr, dtb_total_size(dtb_addr));

        uart_puts("PMM free pages: ");
        print_hex64(pmm_free_count());
        uart_puts("\n");

        uint64_t page_a = pmm_alloc_page();
        uint64_t page_b = pmm_alloc_page();

        uart_puts("PMM alloc A: ");
        print_hex64(page_a);
        uart_puts("\nPMM alloc B: ");
        print_hex64(page_b);
        uart_puts("\n");

        pmm_free_page(page_a);

        uart_puts("PMM free after one free: ");
        print_hex64(pmm_free_count());
        uart_puts("\n");

        pmm_free_page(page_b);

        kheap_init();
        uart_puts("KHEAP total bytes: ");
        print_hex64(kheap_total_bytes());
        uart_puts("\nKHEAP free bytes: ");
        print_hex64(kheap_free_bytes());
        uart_puts("\n");

        void *heap_a = kmalloc(64);
        void *heap_b = kmalloc(128);

        uart_puts("KHEAP alloc A: ");
        print_hex64((uint64_t)(uintptr_t)heap_a);
        uart_puts("\nKHEAP alloc B: ");
        print_hex64((uint64_t)(uintptr_t)heap_b);
        uart_puts("\nKHEAP free after alloc: ");
        print_hex64(kheap_free_bytes());
        uart_puts("\n");

        kfree(heap_a);
        void *heap_c = kmalloc(32);

        uart_puts("KHEAP alloc C: ");
        print_hex64((uint64_t)(uintptr_t)heap_c);
        uart_puts("\nKHEAP free final: ");
        print_hex64(kheap_free_bytes());
        uart_puts("\n");

        kfree(heap_b);
        kfree(heap_c);

        uint64_t *kernel_pgd = vmm_new_table();
        int vmm_ok = 0;

        if (kernel_pgd != 0) {
            vmm_ok = vmm_map_range(kernel_pgd, memory.base, memory.base, memory.size,
                                   VMM_FLAG_READ | VMM_FLAG_WRITE | VMM_FLAG_EXEC);
        } else {
            vmm_ok = -1;
        }

        if (vmm_ok == 0) {
            vmm_ok = vmm_map_page(kernel_pgd, 0x09000000ULL, 0x09000000ULL,
                                  VMM_FLAG_READ | VMM_FLAG_WRITE | VMM_FLAG_DEVICE);
        }

        if (vmm_ok == 0) {
            vmm_ok = vmm_map_range(kernel_pgd, 0x08000000ULL, 0x08000000ULL, 0x20000ULL,
                                   VMM_FLAG_READ | VMM_FLAG_WRITE | VMM_FLAG_DEVICE);
        }

        if (vmm_ok == 0) {
            uint64_t kernel_phys =
                vmm_virt_to_phys(kernel_pgd, (uint64_t)(uintptr_t)&kernel_main);
            uint64_t dtb_phys = vmm_virt_to_phys(kernel_pgd, dtb_addr);
            uint64_t uart_phys = vmm_virt_to_phys(kernel_pgd, 0x09000000ULL);
            uint64_t gic_phys = vmm_virt_to_phys(kernel_pgd, 0x08000000ULL);

            uart_puts("VMM PGD: ");
            print_hex64((uint64_t)(uintptr_t)kernel_pgd);
            uart_puts("\nVMM kernel phys: ");
            print_hex64(kernel_phys);
            uart_puts("\nVMM DTB phys: ");
            print_hex64(dtb_phys);
            uart_puts("\nVMM UART phys: ");
            print_hex64(uart_phys);
            uart_puts("\nVMM GIC phys: ");
            print_hex64(gic_phys);
            uart_puts("\nVMM UART PTE: ");
            print_hex64(vmm_leaf_entry(kernel_pgd, 0x09000000ULL));
            uart_puts("\nPMM free after VMM: ");
            print_hex64(pmm_free_count());
            uart_puts("\n");

            mmu_enable_identity(kernel_pgd);

            uart_puts("MMU SCTLR_EL1: ");
            print_hex64(mmu_read_sctlr_el1());
            uart_puts("\nMMU identity map: active\n");

            gicv2_init();
            gicv2_enable_irq(TIMER_IRQ);
            sched_init(5);
            timer_init(10);
            irq_enable();

            uart_puts("IRQ timer: armed\n");
            uart_puts("SCHED quantum ticks: 0x0000000000000005\n");
        } else {
            uart_puts("VMM smoke: failed\n");
        }
    } else {
        uart_puts("RAM map: unavailable\n");
    }

    for (;;) {
        __asm__ volatile("wfe");
    }
}
