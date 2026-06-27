#include "kernel/panel_boot_recovery.h"

#include <stddef.h>

/*
 * Panel recovery policy.
 *
 * The recovery wrapper is intentionally callback-based: kernel_main owns board
 * context and logging, while this file owns only the retry budget. Today every
 * panel exit is retried until the fixed budget is exhausted because the ABI has
 * no explicit "desktop shutdown requested" signal yet.
 */

panel_boot_recovery_action_t panel_boot_recovery_decide(uint32_t attempts_used,
                                                        uint64_t last_exit_code) {
    (void)last_exit_code;

    if (attempts_used >= PANEL_BOOT_RECOVERY_MAX_ATTEMPTS) {
        return PANEL_BOOT_RECOVERY_STOP_EXHAUSTED;
    }
    return PANEL_BOOT_RECOVERY_CONTINUE;
}

static void panel_boot_recovery_log(panel_boot_recovery_log_fn_t log,
                                    const char *line) {
    if (log != 0) {
        log(line);
    }
}

uint64_t panel_boot_recovery_run(panel_boot_recovery_run_fn_t run, void *ctx,
                                 panel_boot_recovery_log_fn_t log) {
    uint64_t last_exit = 0;
    uint32_t attempts = 0;

    if (run == 0) {
        panel_boot_recovery_log(log, "panel_boot: no run callback\n");
        return 0;
    }

    while (attempts < PANEL_BOOT_RECOVERY_MAX_ATTEMPTS) {
        attempts++;

        if (attempts == 1U) {
            panel_boot_recovery_log(log, "panel_boot: launching\n");
        } else {
            panel_boot_recovery_log(log, "panel_boot: relaunching\n");
        }

        last_exit = run(ctx);

        panel_boot_recovery_log(log, "panel_boot: exited\n");

        if (panel_boot_recovery_decide(attempts, last_exit) !=
            PANEL_BOOT_RECOVERY_CONTINUE) {
            break;
        }
    }

    if (attempts >= PANEL_BOOT_RECOVERY_MAX_ATTEMPTS) {
        panel_boot_recovery_log(log, "panel_boot: giving up\n");
    }

    return last_exit;
}
