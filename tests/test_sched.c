#include <stdint.h>

#include "unity/unity.h"
#include "../kernel/sched/sched.h"

static uint32_t g_sched_switch_count;
static uint32_t g_sched_irq_enable_count;
static uint32_t g_sched_irq_disable_count;

void switch_context(void *old_context, void *new_context) {
    (void)old_context;
    (void)new_context;
    g_sched_switch_count++;
}

void sched_thread_trampoline(void) {
}

void irq_enable(void) {
    g_sched_irq_enable_count++;
}

void irq_disable(void) {
    g_sched_irq_disable_count++;
}

static void reset_sched_stub_counts(void) {
    g_sched_switch_count = 0;
    g_sched_irq_enable_count = 0;
    g_sched_irq_disable_count = 0;
}

void test_sched_timer_quantum_counts_elapsed_ticks(void) {
    sched_init(3);

    TEST_ASSERT_EQUAL_UINT64(0, sched_ticks());
    TEST_ASSERT_EQUAL_UINT64(0, sched_quantums());

    sched_on_timer_tick();
    sched_on_timer_tick();
    TEST_ASSERT_EQUAL_UINT64(2, sched_ticks());
    TEST_ASSERT_EQUAL_UINT64(0, sched_quantums());

    sched_on_timer_tick();
    TEST_ASSERT_EQUAL_UINT64(3, sched_ticks());
    TEST_ASSERT_EQUAL_UINT64(1, sched_quantums());
}

void test_sched_zero_quantum_defaults_to_one_tick(void) {
    sched_init(0);

    sched_on_timer_tick();
    TEST_ASSERT_EQUAL_UINT64(1, sched_ticks());
    TEST_ASSERT_EQUAL_UINT64(1, sched_quantums());
}

void test_sched_start_without_threads_does_not_switch(void) {
    sched_init(1);
    reset_sched_stub_counts();

    sched_start();

    TEST_ASSERT_EQUAL_UINT64(0, g_sched_switch_count);
    TEST_ASSERT_EQUAL_UINT64(0, g_sched_irq_enable_count);
    TEST_ASSERT_EQUAL_UINT64(0, g_sched_irq_disable_count);
}

void test_sched_yield_without_current_thread_is_noop(void) {
    sched_init(1);
    reset_sched_stub_counts();

    sched_yield();

    TEST_ASSERT_EQUAL_UINT64(0, g_sched_switch_count);
    TEST_ASSERT_EQUAL_UINT64(0, g_sched_irq_enable_count);
    TEST_ASSERT_EQUAL_UINT64(0, g_sched_irq_disable_count);
}
