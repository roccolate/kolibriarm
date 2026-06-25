#include <stdint.h>

#include "board.h"
#include "gpu/virtio_gpu.h"
#include "kernel/bootfs.h"
#include "kernel/console.h"
#include "kernel/dtb.h"
#include "kernel/exceptions.h"
#include "kernel/fat32.h"
#include "kernel/gui.h"
#include "kernel/ipc.h"
#include "kernel/irq.h"
#include "kernel/mm/kheap.h"
#include "kernel/mm/mmu.h"
#include "kernel/mm/pmm.h"
#include "kernel/mm/vmm.h"
#include "kernel/print.h"
#include "kernel/process.h"
#include "kernel/sched/sched.h"
#include "kernel/timer/timer.h"
#include "kernel/tmpfs.h"
#include "kernel/panel_boot.h"
#include "kernel/panel_boot_recovery.h"
#include "kernel/vfs.h"
#include "uart/pl011.h"
#include "input/input.h"
#include "kernel/net/dhcp.h"
#include "pci/pci.h"
#include "usb/hid_driver.h"
#include "usb/usb_core.h"

extern char __kernel_end[];

void kernel_main(uint64_t dtb_addr);

static fat32_fs_t g_fat32_fs;

static void console_input_thread(void *arg) {
    (void)arg;

    for (;;) {
        input_uart_poll();
        board_virtio_input_poll();
        usb_hid_poll_all();

        /* The shell only consumes KEY_PRESS. Use peek so we do not
         * pop MOUSE_MOVE / MOUSE_BUTTON events out from under the
         * GUI input thread; those stay in the queue for poll_input_events. */
        input_event_t event;
        while (input_queue_peek(&event) == 0) {
            if (event.type == INPUT_EVENT_KEY_PRESS) {
                (void)input_queue_poll(&event);
                char c = (char)event.data.key.key;
                console_poll_char(c);
            } else {
                break;
            }
        }

        if (input_queue_available() == 0) {
            __asm__ volatile("wfe");
        }

        net_poll();

        sched_yield();
    }
}

static void init_memory_manager(const dtb_memory_t *memory, uint64_t dtb_addr) {
    pmm_init(memory->base, memory->size);
    pmm_reserve_range(memory->base,
                      (uint64_t)((uintptr_t)__kernel_end - memory->base));
    pmm_reserve_range(dtb_addr, dtb_total_size(dtb_addr));
}

static void run_pmm_smoke(void) {
    uint64_t page_a = pmm_alloc_page();
    uint64_t page_b = pmm_alloc_page();

    pmm_free_page(page_a);
    pmm_free_page(page_b);
}

static void run_kheap_smoke(void) {
    kheap_init();

    void *heap_a = kmalloc(64);
    void *heap_b = kmalloc(128);

    kfree(heap_a);
    void *heap_c = kmalloc(32);

    kfree(heap_b);
    kfree(heap_c);
}

static void init_vfs(void) {
    static const uint8_t note[] = "KolibriARM tmpfs\n";
    uint8_t magic[4];
    uint64_t bytes_read = 0;
    uint64_t bytes_written = 0;

    vfs_reset();
    tmpfs_reset();

    if (bootfs_mount_vfs() == 0) {
        uart_puts("VFS bootfs: mounted\n");
    } else {
        uart_puts("VFS bootfs: failed\n");
    }

    if (tmpfs_create("note") == 0 &&
        tmpfs_write("note", 0, note, sizeof(note) - 1U, &bytes_written) == 0 &&
        tmpfs_mount_vfs("/tmp/note", "note") == 0) {
        uart_puts("VFS tmpfs: mounted\n");
    } else {
        uart_puts("VFS tmpfs: failed\n");
    }

    if (bootfs_read("shell", 0, magic, sizeof(magic),
                    &bytes_read) == 0 && bytes_read == sizeof(magic)) {
        uart_puts("bootfs read: ok\n");
    } else {
        uart_puts("bootfs read: failed\n");
    }

    bytes_read = 0;
    if (vfs_read("/kolibri/shell", 0, magic, sizeof(magic),
                 &bytes_read) == 0 && bytes_read == sizeof(magic)) {
        uart_puts("VFS read: ok\n");
    } else {
        uart_puts("VFS read: failed\n");
    }
}

static void run_panel_boot_smoke(const dtb_memory_t *memory) {
    uint64_t user_exit_code = panel_boot_run_with_recovery(
        memory->base, memory->size, board_map_mmio);

    uart_puts("USER demo exit code: ");
    print_hex64(user_exit_code);
    uart_puts("\n");
}

static int fat32_read_storage(void *context, uint32_t lba, uint8_t *buffer) {
    (void)context;
    return board_storage_read(lba, 1, buffer);
}

static int fat32_write_storage(void *context, uint32_t lba,
                               const uint8_t *buffer) {
    (void)context;
    return board_storage_write(lba, 1, buffer);
}

static int probe_fat32(void) {
    vfs_stat_t stat;

    fat32_vfs_reset();
    if (fat32_mount(&g_fat32_fs, fat32_read_storage, NULL) != 0) {
        uart_puts("FAT32: absent\n");
        return -1;
    }
    fat32_set_write_sector(&g_fat32_fs, fat32_write_storage);

    uart_puts("FAT32: mounted\n");
    if (fat32_mount_vfs_root(&g_fat32_fs, "/fat") == 0) {
        uart_puts("FAT32 root: mounted\n");
    } else {
        uart_puts("FAT32 root: absent\n");
    }

    if (fat32_mount_vfs_file(&g_fat32_fs, "/fat/user_demo.bin",
                             "USERDEMO.BIN") != 0 ||
        vfs_stat("/fat/user_demo.bin", &stat) != 0) {
        uart_puts("FAT32 file: absent\n");
        return -1;
    }

    uart_puts("FAT32 file bytes: ");
    print_hex64(stat.size);
    uart_puts("\n");

    if (fat32_mount_vfs_file(&g_fat32_fs, "/fat/edit.txt",
                             "EDIT.TXT") == 0) {
        uart_puts("FAT32 edit file: mounted\n");
    } else {
        uart_puts("FAT32 edit file: absent\n");
    }
    return 0;
}

static int probe_storage(void) {
    uint8_t sector[512] __attribute__((aligned(8)));
    int read_status;

    if (board_storage_init() != 0) {
        uart_puts("storage: init failed\n");
        return -1;
    }

    uart_puts("storage: initialized\n");

    read_status = board_storage_read(0, 1, sector);
    if (read_status == 0) {
        uint32_t word = (uint32_t)sector[0] |
                        ((uint32_t)sector[1] << 8) |
                        ((uint32_t)sector[2] << 16) |
                        ((uint32_t)sector[3] << 24);

        uart_puts("storage sector0: ");
        print_hex64(word);
        uart_puts("\n");
        return probe_fat32();
    } else {
        uart_puts("storage read err: ");
        print_hex64((uint64_t)(uint32_t)read_status);
        uart_puts("\n");
    }

    return -1;
}

static uint64_t g_gpu_base;
static uint8_t g_gpu_ready;

static void init_display(void) {
    uint64_t gpu_base;

    g_gpu_base = 0;
    g_gpu_ready = 0;

    if (virtio_gpu_probe_range(board_virtio_mmio_base(),
                               board_virtio_mmio_size(),
                               board_virtio_mmio_stride(), &gpu_base) != 0) {
        console_set_framebuffer_ready(0);
        return;
    }

    g_gpu_base = gpu_base;

    if (virtio_gpu_draw(gpu_base, gui_init_for_framebuffer, 0) == 0) {
        uart_puts("VIRTIO gpu: windows\n");
        console_set_framebuffer_ready(1);
        g_gpu_ready = 1;
    } else {
        uart_puts("VIRTIO gpu: failed\n");
        console_set_framebuffer_ready(0);
    }
}

static void flush_dirty_redraw(void) {
    if (g_gpu_ready && gui_is_dirty()) {
        (void)virtio_gpu_draw(g_gpu_base, gui_render, 0);
        gui_clear_dirty();
    }
}

static void poll_input_events(void) {
    input_event_t event;

    while (input_queue_poll(&event) == 0) {
        if (event.type == INPUT_EVENT_MOUSE_MOVE ||
            event.type == INPUT_EVENT_MOUSE_BUTTON ||
            event.type == INPUT_EVENT_KEY_PRESS ||
            event.type == INPUT_EVENT_KEY_RELEASE) {
            (void)gui_handle_input(&event);
        }
    }
    /* Flush any pending redraw regardless of whether events arrived. An
     * EL0 app that called SYS_WINDOW_REDRAW without producing input must
     * still see its drawing on screen by the next timer tick. */
    flush_dirty_redraw();
}

void kernel_on_timer_tick(void) {
    board_virtio_input_poll();
    poll_input_events();
}

static int enable_identity_mmu(const dtb_memory_t *memory, uint64_t dtb_addr) {
    uint64_t *kernel_pgd = vmm_new_table();
    int vmm_ok = 0;

    (void)dtb_addr;

    if (kernel_pgd != 0) {
        vmm_ok = vmm_map_range(kernel_pgd, memory->base, memory->base,
                               memory->size,
                               VMM_FLAG_READ | VMM_FLAG_WRITE | VMM_FLAG_EXEC);
    } else {
        vmm_ok = -1;
    }

    if (vmm_ok == 0) {
        vmm_ok = board_map_mmio(kernel_pgd);
    }

    if (vmm_ok != 0) {
        uart_puts("VMM smoke: failed\n");
        return -1;
    }

    mmu_enable_identity(kernel_pgd);

    uart_puts("MMU identity map: active\n");

    return 0;
}

static void init_timer_irq_demo(void) {
    board_irq_init();
    (void)irq_register_handler(TIMER_IRQ, timer_handle_irq, 0);
    (void)irq_register_handler(board_uart0_irq(), uart_irq_handler, 0);
    board_irq_enable(TIMER_IRQ);
    board_irq_enable(board_uart0_irq());
    uart_enable_rx_irq();
    sched_init(5);
    timer_init(100);
    irq_enable();
}

static void init_network(void) {
    if (net_init() == 0) {
        uart_puts("network: initialized\n");
    } else {
        uart_puts("network: failed\n");
    }
}

static void init_input(void) {
    input_queue_init();
    if (board_virtio_input_init() == 0) {
        uart_puts("input: virtio-input initialized\n");
    }
    pci_device_t pci_devs[16];
    uint32_t pci_n = pci_enumerate(pci_devs, 16);
    {
        uint32_t assigned = pci_assign_bars(pci_devs, pci_n,
                                            0x20000000U, 0x1000U);
        char buf[24];
        int p = 0;
        const char *prefix = "PCI: ";
        for (int i = 0; prefix[i] != 0; i++) buf[p++] = prefix[i];
        buf[p++] = '0' + (char)(pci_n / 10);
        buf[p++] = '0' + (char)(pci_n % 10);
        const char *mid = " devices, ";
        for (int i = 0; mid[i] != 0; i++) buf[p++] = mid[i];
        buf[p++] = '0' + (char)(assigned / 10);
        buf[p++] = '0' + (char)(assigned % 10);
        const char *suffix = " BARs assigned\n";
        for (int i = 0; suffix[i] != 0; i++) buf[p++] = suffix[i];
        buf[p] = 0;
        uart_puts(buf);
    }
    if (usb_init() > 0) {
        uart_puts("USB: controller initialized\n");
        usb_hid_state_reset();
        uint8_t usb_ports = usb_port_count();
        uint8_t usb_address = 1;
        for (uint8_t port = 0; port < usb_ports; port++) {
            if (usb_port_reset(port) > 0) {
                uart_puts("USB: device on port ");
                print_dec64(port);
                uart_puts("\n");

                uint8_t config_buf[256];
                usb_config_walk_t walk;
                usb_device_t dev;
                if (usb_enumerate_port(port, usb_address, 1, config_buf,
                                       sizeof(config_buf), &dev, &walk) == 0) {
                    uart_puts("USB: enumeration ok\n");
                    uint8_t before = g_usb_hid_state.count;
                    (void)usb_hid_add_device(&g_usb_hid_state, &walk, &dev);
                    for (uint8_t i = before; i < g_usb_hid_state.count; i++) {
                        usb_hid_set_protocol_boot(&g_usb_hid_state.devices[i]);
                    }
                    usb_address++;
                } else {
                    uart_puts("USB: enumeration skipped\n");
                }
            }
        }
        if (g_usb_hid_state.count > 0) {
            uart_puts("USB HID: ");
            uart_putc('0' + (char)g_usb_hid_state.count);
            uart_puts(" devices\n");
        } else {
            uart_puts("USB HID: no boot-protocol devices\n");
        }
    }
}

static void start_scheduler_demo(void) {
    (void)sched_create_kernel_thread(console_input_thread, 0, "kconsole");
    sched_start();
}

void kernel_main(uint64_t dtb_addr) {
    dtb_memory_t memory;

    board_early_init();

    uart_puts("\nKolibriARM ");
    uart_puts(board_name());
    uart_puts("\n");

    if (dtb_get_memory(dtb_addr, &memory) == 0) {
        init_memory_manager(&memory, dtb_addr);
        process_table_init();
        ipc_init();
        console_init(&memory);
        run_pmm_smoke();
        run_kheap_smoke();

        if (enable_identity_mmu(&memory, dtb_addr) == 0) {
            init_vfs();
            init_timer_irq_demo();
            if (probe_storage() == 0) {
                console_set_storage_ready(1);
                uart_puts("USER image source: FAT32\n");
            } else {
                console_set_storage_ready(0);
                uart_puts("USER image source: bootfs\n");
            }
            init_display();
            init_network();
            init_input();
            run_panel_boot_smoke(&memory);
            console_start_interactive();
            start_scheduler_demo();
        }
    } else {
        uart_puts("RAM map: unavailable\n");
    }

    for (;;) {
        __asm__ volatile("wfe");
    }
}
