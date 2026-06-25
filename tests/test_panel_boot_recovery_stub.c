/*
 * Host-side stubs for the panel_boot recovery unit tests.
 *
 * The real panel_boot_run in kernel/panel_boot.c sets up a page
 * table, switches TTBR0_EL1, and enters EL0, all of which require
 * the AArch64 toolchain and the real kernel memory map. The
 * recovery unit tests only need to exercise the
 * panel_boot_recovery_decide policy and verify that
 * panel_boot_run_with_recovery loops up to PANEL_BOOT_RECOVERY_MAX_ATTEMPTS
 * before stopping; they never need a real panel run.
 *
 * We provide a counter-based stub: panel_boot_run returns a
 * different exit code each call so a test can detect that the
 * wrapper invoked it multiple times. print_hex64 is silenced so
 * the host test output stays readable.
 */

#include <stdint.h>

#include "../kernel/panel_boot.h"

static uint64_t g_stub_call_count;
static uint64_t g_stub_next_exit = 0xCAFEBABEULL;

void test_panel_boot_recovery_reset_stub(uint64_t next_exit) {
    g_stub_call_count = 0;
    g_stub_next_exit = next_exit;
}

uint32_t test_panel_boot_recovery_call_count(void) {
    return (uint32_t)g_stub_call_count;
}

uint64_t panel_boot_run(uint64_t memory_base, uint64_t memory_size,
                        panel_map_mmio_fn_t map_mmio) {
    (void)memory_base;
    (void)memory_size;
    (void)map_mmio;

    g_stub_call_count++;
    return g_stub_next_exit;
}

void print_hex64(uint64_t value) {
    (void)value;
}