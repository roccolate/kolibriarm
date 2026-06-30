#include "kernel/timer/timer.h"

#include <stdint.h>

#include "kernel/sched/sched.h"
#include "uart/pl011.h"

void kernel_on_timer_tick(void);

/*
 * AArch64 physical timer driver.
 *
 * timer_init programs CNTP_TVAL/CTL using CNTFRQ_EL0. Each interrupt pumps
 * UART input, lets the kernel poll input/rendering work, and then advances the
 * scheduler tick. Keep policy out of the register helpers below.
 */

static uint64_t g_ticks;
static uint64_t g_interval_ticks;
static uint64_t g_next_cval;

static uint64_t read_cntfrq(void) {
    uint64_t value;

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(value));
    return value;
}

static uint64_t read_cntpct(void) {
    uint64_t value;

    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(value));
    return value;
}

static void write_cntp_cval(uint64_t value) {
    __asm__ volatile("msr cntp_cval_el0, %0" :: "r"(value));
}

static void write_cntp_ctl(uint64_t value) {
    __asm__ volatile("msr cntp_ctl_el0, %0" :: "r"(value));
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

    g_next_cval = read_cntpct() + g_interval_ticks;
    write_cntp_cval(g_next_cval);
    write_cntp_ctl(1);
}

void timer_handle_irq(void *context) {
    (void)context;
    g_ticks++;

    /* Advance CVAL by the fixed interval so ticks are anchored to the
     * previous expiry, not the moment we service the IRQ.  This eliminates
     * cumulative drift that TVAL reloads cause. */
    g_next_cval += g_interval_ticks;
    write_cntp_cval(g_next_cval);

    uart_pump_input();
    kernel_on_timer_tick();
    sched_on_timer_tick();
}

uint64_t timer_ticks(void) {
    return g_ticks;
}
