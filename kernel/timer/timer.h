#ifndef KOLIBRIARM_KERNEL_TIMER_TIMER_H
#define KOLIBRIARM_KERNEL_TIMER_TIMER_H

#include <stdint.h>

#define TIMER_IRQ 30U

/*
 * Kernel physical timer interface.
 *
 * TIMER_IRQ is the board timer interrupt number used by kernel.c during IRQ
 * setup. timer_handle_irq is registered as the IRQ callback and owns the
 * per-tick scheduler/input handoff.
 */

void timer_init(uint32_t hz);
void timer_handle_irq(void *context);
uint64_t timer_ticks(void);

#endif
