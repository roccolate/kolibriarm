#include "kernel/panel_boot_recovery.h"

#include <stddef.h>

#include "kernel/panel_boot.h"
#include "kernel/print.h"
#include "uart/pl011.h"

panel_boot_recovery_action_t panel_boot_recovery_decide(uint32_t attempts_used,
                                                       uint64_t last_exit_code) {
    (void)last_exit_code;

    if (attempts_used == 0) {
        return PANEL_BOOT_RECOVERY_CONTINUE;
    }
    if (attempts_used >= PANEL_BOOT_RECOVERY_MAX_ATTEMPTS) {
        return PANEL_BOOT_RECOVERY_STOP_EXHAUSTED;
    }
    return PANEL_BOOT_RECOVERY_CONTINUE;
}

uint64_t panel_boot_run_with_recovery(uint64_t memory_base, uint64_t memory_size,
                                      panel_map_mmio_fn_t map_mmio) {
    uint64_t last_exit = 0;
    uint32_t attempts = 0;

    while (attempts < PANEL_BOOT_RECOVERY_MAX_ATTEMPTS) {
        attempts++;

        if (attempts == 1U) {
            uart_puts("panel_boot: launching\n");
        } else {
            uart_puts("panel_boot: relaunching attempt ");
            print_hex64(attempts);
            uart_puts("\n");
        }

        last_exit = panel_boot_run(memory_base, memory_size, map_mmio);

        uart_puts("panel_boot: exited code ");
        print_hex64(last_exit);
        uart_puts(" after ");
        print_hex64(attempts);
        uart_puts(" attempt(s)\n");

        if (panel_boot_recovery_decide(attempts, last_exit) !=
            PANEL_BOOT_RECOVERY_CONTINUE) {
            break;
        }
    }

    if (attempts >= PANEL_BOOT_RECOVERY_MAX_ATTEMPTS) {
        uart_puts("panel_boot: giving up after ");
        print_hex64(attempts);
        uart_puts(" attempts\n");
    }

    return last_exit;
}