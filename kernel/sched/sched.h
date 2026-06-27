#ifndef KOLIBRIARM_KERNEL_SCHED_SCHED_H
#define KOLIBRIARM_KERNEL_SCHED_SCHED_H

#include <stdint.h>

/*
 * Small cooperative scheduler for EL1 helper threads.
 *
 * User processes are scheduled through process_dispatch_next from syscall,
 * fault, and IRQ paths. This API is only for kernel threads that voluntarily
 * yield or exit after being started by sched_start.
 */

typedef void (*sched_thread_fn_t)(void *arg);

void sched_init(uint32_t quantum_ticks);
int sched_create_kernel_thread(sched_thread_fn_t entry, void *arg, const char *name);
void sched_start(void);
void sched_yield(void);
void sched_thread_exit(void);
void sched_on_timer_tick(void);
uint64_t sched_ticks(void);
uint64_t sched_quantums(void);

#endif
