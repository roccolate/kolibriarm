#ifndef KOLIBRIARM_KERNEL_TIMER_TIMER_H
#define KOLIBRIARM_KERNEL_TIMER_TIMER_H

#include <stdint.h>

#define TIMER_IRQ 30U

void timer_init(uint32_t hz);
void timer_handle_irq(void);
uint64_t timer_ticks(void);

#endif
