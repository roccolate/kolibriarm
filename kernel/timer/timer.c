#include "kernel/timer/timer.h"

#include <stdint.h>

#include "kernel/sched/sched.h"
#include "uart/pl011.h"

static uint64_t g_ticks;
static uint64_t g_interval_ticks;

static uint64_t read_cntfrq(void) {
    uint64_t value;

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(value));
    return value;
}

static void write_cntp_tval(uint64_t value) {
    __asm__ volatile("msr cntp_tval_el0, %0" :: "r"(value));
}

static void write_cntp_ctl(uint64_t value) {
    __asm__ volatile("msr cntp_ctl_el0, %0" :: "r"(value));
}

static void print_hex64(uint64_t value) {
    static const char digits[] = "0123456789abcdef";

    uart_puts("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        uart_putc(digits[(value >> shift) & 0xf]);
    }
}

void timer_init(uint32_t hz) {
    uint64_t freq = read_cntfrq();

    if (hz == 0) {
        hz = 1;
    }

    g_interval_ticks = freq / hz;
    if (g_interval_ticks == 0) {
        g_interval_ticks = 1;
    }

    write_cntp_tval(g_interval_ticks);
    write_cntp_ctl(1);
}

void timer_handle_irq(void) {
    g_ticks++;
    write_cntp_tval(g_interval_ticks);
    sched_on_timer_tick();

    if (g_ticks <= 3) {
        uart_puts("TIMER tick: ");
        print_hex64(g_ticks);
        uart_puts("\n");
    }
}

uint64_t timer_ticks(void) {
    return g_ticks;
}
