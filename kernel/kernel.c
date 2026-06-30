/*
 * Kernel bootstrap and runtime pump.
 *
 * This file intentionally stays at the orchestration layer: board probing,
 * core subsystem initialization, runtime input/redraw polling, and the final
 * handoff into the scheduler. Device details live in drivers/ or the board
 * layer; EL0 image loading and recovery policy live in panel_boot*.
 */

#include <stdint.h>

#include "board.h"
#include "gpu/virtio_gpu.h"
#include "kernel/bootfs.h"
#include "kernel/console.h"
#include "kernel/dtb.h"
#include "kernel/exceptions.h"
#include "kernel/fat32.h"
#include "kernel/gui.h"
#include "kernel/init_status.h"
#include "kernel/ipc.h"
#include "kernel/irq.h"
#include "kernel/kernel_compiler.h"
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

static int desktop_has_keyboard_focus(void) {
    gui_desktop_t *desktop = gui_desktop();

    return desktop != 0 && desktop->focused_window_id != GUI_NO_WINDOW;
}

/*
 * Serial console input runs as a kernel thread after the scheduler starts.
 * It shares the input queue with the GUI path, so it only consumes key
 * presses while no desktop window has keyboard focus. Once a user app is
 * focused, keyboard events stay queued for the GUI.
 */
static void console_input_thread(void *arg) {
    (void)arg;

    for (;;) {
        input_uart_poll();
        board_virtio_input_poll();
        usb_hid_poll_all();

        input_event_t event;
        while (!desktop_has_keyboard_focus() &&
               input_queue_peek(&event) == 0) {
            if (event.type == INPUT_EVENT_KEY_PRESS) {
                (void)input_queue_poll(&event);
                char c = (char)event.data.key.key;
                console_poll_char(c);
            } else {
                /* Consume and discard non-key events so they don't
                 * starve KEY_PRESS events queued behind them. */
                (void)input_queue_poll(&event);
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
    /* Keep the loaded kernel image and DTB out of the free page pool. */
    pmm_reserve_range(memory->base,
                      (uint64_t)((uintptr_t)__kernel_end - memory->base));
    pmm_reserve_range(dtb_addr, dtb_total_size(dtb_addr));
}

/*
 * Mount the always-available bootfs and a small tmpfs smoke file. bootfs is
 * required because it is the fallback app source; tmpfs is useful but not
 * fatal, so failures there downgrade the phase to WARN.
 */
static init_status_t init_vfs(void) {
    static const uint8_t note[] = "ArmoniOS tmpfs\n";
    uint8_t magic[4];
    uint64_t bytes_read = 0;
    uint64_t bytes_written = 0;
    int bootfs_ok = 0;
    int tmpfs_ok = 0;
    int bootfs_read_ok = 0;
    int vfs_read_ok = 0;

    vfs_reset();
    tmpfs_reset();

    if (bootfs_mount_vfs() == 0) {
        uart_puts("VFS bootfs: mounted\n");
        bootfs_ok = 1;
    } else {
        uart_puts("VFS bootfs: failed\n");
    }

    if (tmpfs_create("note") == 0 &&
        tmpfs_write("note", 0, note, sizeof(note) - 1U, &bytes_written) == 0 &&
        tmpfs_mount_vfs("/tmp/note", "note") == 0) {
        uart_puts("VFS tmpfs: mounted\n");
        tmpfs_ok = 1;
    } else {
        uart_puts("VFS tmpfs: failed\n");
    }

    if (bootfs_read("shell", 0, magic, sizeof(magic),
                    &bytes_read) == 0 && bytes_read == sizeof(magic)) {
        uart_puts("bootfs read: ok\n");
        bootfs_read_ok = 1;
    } else {
        uart_puts("bootfs read: failed\n");
    }

    bytes_read = 0;
    if (vfs_read("/armonios/shell", 0, magic, sizeof(magic),
                 &bytes_read) == 0 && bytes_read == sizeof(magic)) {
        uart_puts("VFS read: ok\n");
        vfs_read_ok = 1;
    } else {
        uart_puts("VFS read: failed\n");
    }

    if (bootfs_ok == 0 || bootfs_read_ok == 0 || vfs_read_ok == 0) {
        return INIT_STATUS_FAIL;
    }
    if (tmpfs_ok == 0) {
        return INIT_STATUS_WARN;
    }
    return INIT_STATUS_OK;
}

typedef struct {
    uint64_t memory_base;
    uint64_t memory_size;
    panel_map_mmio_fn_t map_mmio;
} panel_boot_context_t;

/* Adapter used by panel_boot_recovery_run so retry policy stays testable. */
static uint64_t run_panel_boot_once(void *ctx) {
    panel_boot_context_t *boot = (panel_boot_context_t *)ctx;

    if (boot == 0) {
        return 1;
    }
    return panel_boot_run(boot->memory_base, boot->memory_size,
                          boot->map_mmio);
}

static void panel_boot_log_line(const char *line) {
    uart_puts(line);
}

/*
 * Start the panel under the recovery wrapper. A non-zero EL0 exit code is
 * recorded as WARN rather than FAIL because the kernel can still keep the
 * serial console and scheduler alive for debugging.
 */
static init_status_t start_panel_boot(const dtb_memory_t *memory) {
    panel_boot_context_t boot = {
        .memory_base = memory->base,
        .memory_size = memory->size,
        .map_mmio = board_map_mmio,
    };
    uint64_t user_exit_code = panel_boot_recovery_run(
        run_panel_boot_once, &boot, panel_boot_log_line);

    uart_puts("USER exit code: ");
    print_hex64(user_exit_code);
    uart_puts("\n");

    return user_exit_code == 0 ? INIT_STATUS_OK : INIT_STATUS_WARN;
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

    if (fat32_mount_vfs_file(&g_fat32_fs, "/fat/shell.bin",
                             "SHELL.BIN") != 0 ||
        vfs_stat("/fat/shell.bin", &stat) != 0) {
        uart_puts("FAT32 shell: absent\n");
        return -1;
    }

    uart_puts("FAT32 shell bytes: ");
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

/*
 * Storage is optional at this stage. A valid FAT32 image lets apps load from
 * /fat, but bootfs remains the fallback when the block device or filesystem is
 * absent.
 */
static init_status_t probe_storage(void) {
    uint8_t sector[512] KERNEL_ALIGNED(8);
    int read_status;

    if (board_storage_init() != 0) {
        uart_puts("storage: init failed\n");
        return INIT_STATUS_WARN;
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
        return probe_fat32() == 0 ? INIT_STATUS_OK : INIT_STATUS_WARN;
    } else {
        uart_puts("storage read err: ");
        print_hex64((uint64_t)(uint32_t)read_status);
        uart_puts("\n");
    }

    return INIT_STATUS_WARN;
}

static uint64_t g_gpu_base;
static uint8_t g_gpu_ready;

/*
 * Display is optional for serial-only boots. When a virtio GPU is present,
 * gui_init_for_framebuffer owns desktop setup and later redraws reuse the
 * saved MMIO base.
 */
static init_status_t init_display(void) {
    uint64_t gpu_base;

    g_gpu_base = 0;
    g_gpu_ready = 0;

    if (virtio_gpu_probe_range(board_virtio_mmio_base(),
                               board_virtio_mmio_size(),
                               board_virtio_mmio_stride(), &gpu_base) != 0) {
        return INIT_STATUS_WARN;
    }

    g_gpu_base = gpu_base;

    if (virtio_gpu_draw(gpu_base, gui_init_for_framebuffer, 0) == 0) {
        uart_puts("VIRTIO gpu: windows\n");
        g_gpu_ready = 1;
        return INIT_STATUS_OK;
    } else {
        uart_puts("VIRTIO gpu: failed\n");
        return INIT_STATUS_WARN;
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

/* Timer IRQ work that must happen even when no userspace thread is running. */
void kernel_on_timer_tick(void) {
    board_virtio_input_poll();
    usb_hid_poll_all();
    poll_input_events();
    net_poll();
}

static int enable_identity_mmu(const dtb_memory_t *memory, uint64_t dtb_addr) {
    uint64_t *kernel_pgd = vmm_new_table();
    int vmm_ok = 0;

    (void)dtb_addr;

    /*
     * The early kernel still runs with identity addresses. Map all reported
     * RAM and board MMIO before enabling the MMU so existing pointers remain
     * valid while later EL0 page tables get their own address spaces.
     */
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
        uart_puts("VMM map: failed\n");
        return -1;
    }

    mmu_enable_identity(kernel_pgd);

    uart_puts("MMU identity map: active\n");

    return 0;
}

/* Bring up IRQ delivery, timer ticks, UART RX interrupts, and scheduling. */
static void init_timer_irq(void) {
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

static init_status_t init_network(void) {
    if (net_init() == 0) {
        uart_puts("network: initialized\n");
        return INIT_STATUS_OK;
    } else {
        uart_puts("network: failed\n");
        return INIT_STATUS_WARN;
    }
}

/*
 * Initialize every input source the board can expose. Missing devices are not
 * fatal: serial input alone is enough to keep the kernel console usable.
 */
static init_status_t init_input(void) {
    input_queue_init();
    if (board_virtio_input_init() == 0) {
        uart_puts("input: virtio-input initialized\n");
    }
    pci_device_t pci_devs[16];
    uint32_t pci_n = pci_enumerate(pci_devs, 16);
    {
        uint32_t assigned = pci_assign_bars(pci_devs, pci_n,
                                            0x20000000U, 0x1000U);

        uart_puts("PCI: ");
        print_dec64(pci_n);
        uart_puts(" devices, ");
        print_dec64(assigned);
        uart_puts(" BARs assigned\n");
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

    return INIT_STATUS_OK;
}

/*
 * The scheduler does not return in normal operation. The return value exists
 * for the thread-creation failure path before sched_start() takes over.
 */
static init_status_t start_scheduler(void) {
    if (sched_create_kernel_thread(console_input_thread, 0, "kconsole") != 0) {
        return INIT_STATUS_FAIL;
    }

    init_status_set(INIT_PHASE_SCHED, INIT_STATUS_OK);
    sched_start();
    return INIT_STATUS_OK;
}

void kernel_main(uint64_t dtb_addr) {
    dtb_memory_t memory;
    init_status_t storage_status;

    init_status_reset();
    board_early_init();
    init_status_set(INIT_PHASE_BOARD, INIT_STATUS_OK);

    uart_puts("\nArmoniOS ");
    uart_puts(board_name());
    uart_puts("\n");

    /*
     * Boot is staged so each phase can report a stable status through
     * `k> status`. Required phases mark FAIL and stop advancing; optional
     * device phases use WARN and let bootfs/serial fallback keep working.
     */
    if (dtb_get_memory(dtb_addr, &memory) == 0) {
        init_status_set(INIT_PHASE_DTB, INIT_STATUS_OK);
        init_memory_manager(&memory, dtb_addr);
        init_status_set(INIT_PHASE_PMM, INIT_STATUS_OK);
        process_table_init();
        ipc_init();
        console_init(&memory);
        init_status_set(INIT_PHASE_CONSOLE, INIT_STATUS_OK);
        kheap_init();
        init_status_set(INIT_PHASE_KHEAP, INIT_STATUS_OK);

        if (enable_identity_mmu(&memory, dtb_addr) == 0) {
            init_status_set(INIT_PHASE_VMM, INIT_STATUS_OK);
            init_status_set(INIT_PHASE_VFS, init_vfs());
            init_timer_irq();
            init_status_set(INIT_PHASE_IRQ_TIMER, INIT_STATUS_OK);
            storage_status = probe_storage();
            init_status_set(INIT_PHASE_STORAGE, storage_status);
            if (storage_status == INIT_STATUS_OK) {
                uart_puts("storage app image: FAT32\n");
            } else {
                uart_puts("storage app image: bootfs fallback\n");
            }
            init_status_set(INIT_PHASE_DISPLAY, init_display());
            init_status_set(INIT_PHASE_NETWORK, init_network());
            init_status_set(INIT_PHASE_INPUT, init_input());
            init_status_set(INIT_PHASE_PANEL, start_panel_boot(&memory));
            console_start_interactive();
            if (start_scheduler() != INIT_STATUS_OK) {
                init_status_set(INIT_PHASE_SCHED, INIT_STATUS_FAIL);
            }
        } else {
            init_status_set(INIT_PHASE_VMM, INIT_STATUS_FAIL);
        }
    } else {
        init_status_set(INIT_PHASE_DTB, INIT_STATUS_FAIL);
        uart_puts("RAM map: unavailable\n");
    }

    for (;;) {
        __asm__ volatile("wfe");
    }
}
