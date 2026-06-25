#ifndef KOLIBRIARM_KERNEL_PANEL_BOOT_RECOVERY_H
#define KOLIBRIARM_KERNEL_PANEL_BOOT_RECOVERY_H

#include <stdint.h>

#include "kernel/panel_boot.h"

/*
 * Maximum number of times the kernel will relaunch the panel taskbar
 * after it exits (cleanly via sys_exit or via a fault) before giving
 * up. Capped at 3 so a persistent panel bug surfaces instead of
 * hiding behind an infinite restart loop. Anything strictly less
 * than this constant is a CONTINUE; reaching it stops the recovery
 * loop with STOP_EXHAUSTED.
 */
#define PANEL_BOOT_RECOVERY_MAX_ATTEMPTS 3U

typedef enum {
    PANEL_BOOT_RECOVERY_CONTINUE = 0,
    PANEL_BOOT_RECOVERY_STOP_EXHAUSTED = 1,
} panel_boot_recovery_action_t;

uint64_t panel_boot_run_with_recovery(uint64_t memory_base, uint64_t memory_size,
                                      panel_map_mmio_fn_t map_mmio);

/*
 * Pure helper extracted from the recovery loop so the host test
 * suite can exercise it without booting the panel. Given how many
 * attempts have already been used (1-based: 1 = first launch just
 * returned), decide whether to launch the panel again or stop.
 *
 * `last_exit_code` is currently informational only: the kernel
 * cannot tell a "clean exit because the user closed the desktop"
 * from a fault that funnelled through the same return trampoline,
 * so the policy is "always retry until the attempt budget runs
 * out". Once a shutdown syscall exists, this is where the
 * clean-exit branch would go.
 */
panel_boot_recovery_action_t panel_boot_recovery_decide(uint32_t attempts_used,
                                                       uint64_t last_exit_code);

#endif