#include "kernel/sched/sched.h"

#include <stddef.h>
#include <stdint.h>

#include "kernel/irq.h"
#include "kernel/mm/pmm.h"
#include "uart/pl011.h"

#define SCHED_MAX_THREADS 4U
#define SCHED_STACK_PAGES 4ULL

/*
 * Cooperative kernel-thread scheduler.
 *
 * EL0 process preemption is handled by process_dispatch_next through the IRQ
 * trap frame. This scheduler is only for EL1 helper threads such as the serial
 * console poller. switch.S saves exactly sched_context_t, so keep the layout
 * asserts below in sync with that assembly boundary.
 */

typedef struct {
    uint64_t x19;
    uint64_t x20;
    uint64_t x21;
    uint64_t x22;
    uint64_t x23;
    uint64_t x24;
    uint64_t x25;
    uint64_t x26;
    uint64_t x27;
    uint64_t x28;
    uint64_t x29;
    uint64_t sp;
    uint64_t lr;
} sched_context_t;

_Static_assert(offsetof(sched_context_t, x19) == 0U,
               "switch.S x19 offset");
_Static_assert(offsetof(sched_context_t, x21) == 16U,
               "switch.S x21 offset");
_Static_assert(offsetof(sched_context_t, x23) == 32U,
               "switch.S x23 offset");
_Static_assert(offsetof(sched_context_t, x25) == 48U,
               "switch.S x25 offset");
_Static_assert(offsetof(sched_context_t, x27) == 64U,
               "switch.S x27 offset");
_Static_assert(offsetof(sched_context_t, x29) == 80U,
               "switch.S x29 offset");
_Static_assert(offsetof(sched_context_t, sp) == 88U,
               "switch.S sp offset");
_Static_assert(offsetof(sched_context_t, lr) == 96U,
               "switch.S lr offset");
_Static_assert(sizeof(sched_context_t) == 104U,
               "switch.S context size");

typedef enum {
    THREAD_UNUSED = 0,
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_ZOMBIE,
} thread_state_t;

typedef struct {
    uint32_t pid;
    thread_state_t state;
    sched_context_t context;
    uint64_t stack_base;
    const char *name;
} kernel_thread_t;

static uint64_t g_sched_ticks;
static uint64_t g_sched_quantums;
static uint32_t g_quantum_ticks = 1;
static uint32_t g_ticks_left = 1;
static kernel_thread_t g_threads[SCHED_MAX_THREADS];
static kernel_thread_t *g_current_thread;
static uint32_t g_thread_count;
static uint32_t g_next_pid = 1;

void switch_context(sched_context_t *old_context, sched_context_t *new_context);
void sched_thread_trampoline(void);

static void sched_wait_for_event(void) {
#ifndef ARMONIOS_TEST
    __asm__ volatile("wfe");
#endif
}

void sched_init(uint32_t quantum_ticks) {
    if (quantum_ticks == 0) {
        quantum_ticks = 1;
    }

    g_sched_ticks = 0;
    g_sched_quantums = 0;
    g_quantum_ticks = quantum_ticks;
    g_ticks_left = quantum_ticks;
    g_current_thread = 0;
    g_thread_count = 0;
    g_next_pid = 1;

    for (uint32_t i = 0; i < SCHED_MAX_THREADS; i++) {
        g_threads[i].pid = 0;
        g_threads[i].state = THREAD_UNUSED;
        g_threads[i].stack_base = 0;
        g_threads[i].name = 0;
    }
}

static kernel_thread_t *next_runnable_thread(void) {
    uint32_t start = 0;

    if (g_current_thread != 0) {
        start = (uint32_t)(g_current_thread - g_threads) + 1U;
    }

    for (uint32_t offset = 0; offset < SCHED_MAX_THREADS; offset++) {
        uint32_t index = (start + offset) % SCHED_MAX_THREADS;

        if (g_threads[index].state == THREAD_READY) {
            return &g_threads[index];
        }
    }

    return 0;
}

int sched_create_kernel_thread(sched_thread_fn_t entry, void *arg, const char *name) {
    kernel_thread_t *thread = 0;

    if (entry == 0) {
        return -1;
    }

    for (uint32_t i = 0; i < SCHED_MAX_THREADS; i++) {
        if (g_threads[i].state == THREAD_UNUSED) {
            thread = &g_threads[i];
            break;
        }
    }

    if (thread == 0) {
        return -1;
    }

    uint64_t stack = pmm_alloc_pages(SCHED_STACK_PAGES);
    if (stack == 0) {
        return -1;
    }

    thread->pid = g_next_pid++;
    thread->state = THREAD_READY;
    thread->stack_base = stack;
    thread->name = name;

    thread->context.x19 = (uint64_t)(uintptr_t)entry;
    thread->context.x20 = (uint64_t)(uintptr_t)arg;
    thread->context.x21 = 0;
    thread->context.x22 = 0;
    thread->context.x23 = 0;
    thread->context.x24 = 0;
    thread->context.x25 = 0;
    thread->context.x26 = 0;
    thread->context.x27 = 0;
    thread->context.x28 = 0;
    thread->context.x29 = 0;
    thread->context.sp = stack + SCHED_STACK_PAGES * PAGE_SIZE;
    thread->context.lr = (uint64_t)(uintptr_t)&sched_thread_trampoline;
    g_thread_count++;

    return 0;
}

void sched_start(void) {
    sched_context_t boot_context;
    kernel_thread_t *next = next_runnable_thread();

    if (next == 0) {
        return;
    }

    g_current_thread = next;
    next->state = THREAD_RUNNING;
    switch_context(&boot_context, &next->context);
}

void sched_yield(void) {
    kernel_thread_t *old_thread;
    kernel_thread_t *next_thread;

    old_thread = g_current_thread;
    if (old_thread == 0) {
        return;
    }

    irq_disable();

    next_thread = next_runnable_thread();

    if (next_thread == 0 || next_thread == old_thread) {
        irq_enable();
        return;
    }

    old_thread->state = THREAD_READY;
    next_thread->state = THREAD_RUNNING;
    g_current_thread = next_thread;

    switch_context(&old_thread->context, &next_thread->context);
    irq_enable();
}

void sched_thread_exit(void) {
    kernel_thread_t *old_thread;
    kernel_thread_t *next_thread;

    irq_disable();

    old_thread = g_current_thread;
    if (old_thread == 0) {
        uart_puts("SCHED idle: no current thread\n");
        irq_enable();
        for (;;) {
            sched_wait_for_event();
        }
    }

    old_thread->state = THREAD_ZOMBIE;
    if (g_thread_count > 0) {
        g_thread_count--;
    }

    next_thread = next_runnable_thread();
    if (next_thread == 0) {
        /* Free stack before idling — this thread will never run again. */
        if (old_thread->stack_base != 0) {
            for (uint64_t i = 0; i < SCHED_STACK_PAGES; i++) {
                pmm_free_page(old_thread->stack_base + i * PAGE_SIZE);
            }
            old_thread->stack_base = 0;
        }
        uart_puts("SCHED idle: no runnable threads\n");
        irq_enable();
        for (;;) {
            sched_wait_for_event();
        }
    }

    next_thread->state = THREAD_RUNNING;
    g_current_thread = next_thread;

    /*
     * Free the exiting thread's stack pages.  IRQs are disabled and
     * switch_context will atomically swap SP to next_thread's stack,
     * so the few instructions between here and the SP swap are safe.
     */
    if (old_thread->stack_base != 0) {
        for (uint64_t i = 0; i < SCHED_STACK_PAGES; i++) {
            pmm_free_page(old_thread->stack_base + i * PAGE_SIZE);
        }
        old_thread->stack_base = 0;
    }

    switch_context(&old_thread->context, &next_thread->context);

    for (;;) {
        sched_wait_for_event();
    }
}

void sched_on_timer_tick(void) {
    g_sched_ticks++;

    if (g_ticks_left > 0) {
        g_ticks_left--;
    }

    if (g_ticks_left == 0) {
        g_sched_quantums++;
        g_ticks_left = g_quantum_ticks;
    }
}

uint64_t sched_ticks(void) {
    return g_sched_ticks;
}

uint64_t sched_quantums(void) {
    return g_sched_quantums;
}
