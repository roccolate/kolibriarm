#ifndef KOLIBRIARM_KERNEL_SCHED_SCHED_H
#define KOLIBRIARM_KERNEL_SCHED_SCHED_H

#include <stdint.h>

void sched_init(uint32_t quantum_ticks);
void sched_on_timer_tick(void);
uint64_t sched_ticks(void);
uint64_t sched_quantums(void);

#endif
