#include "kernel/sched/sched.h"

#include <stdint.h>

#include "uart/pl011.h"

static uint64_t g_sched_ticks;
static uint64_t g_sched_quantums;
static uint32_t g_quantum_ticks = 1;
static uint32_t g_ticks_left = 1;

static void print_hex64(uint64_t value) {
    static const char digits[] = "0123456789abcdef";

    uart_puts("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        uart_putc(digits[(value >> shift) & 0xf]);
    }
}

void sched_init(uint32_t quantum_ticks) {
    if (quantum_ticks == 0) {
        quantum_ticks = 1;
    }

    g_sched_ticks = 0;
    g_sched_quantums = 0;
    g_quantum_ticks = quantum_ticks;
    g_ticks_left = quantum_ticks;
}

void sched_on_timer_tick(void) {
    g_sched_ticks++;

    if (g_ticks_left > 0) {
        g_ticks_left--;
    }

    if (g_ticks_left == 0) {
        g_sched_quantums++;
        g_ticks_left = g_quantum_ticks;

        if (g_sched_quantums <= 3) {
            uart_puts("SCHED quantum: ");
            print_hex64(g_sched_quantums);
            uart_puts("\n");
        }
    }
}

uint64_t sched_ticks(void) {
    return g_sched_ticks;
}

uint64_t sched_quantums(void) {
    return g_sched_quantums;
}
