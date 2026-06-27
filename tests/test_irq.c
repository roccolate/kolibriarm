#include <stdint.h>

#include "unity/unity.h"
#include "../kernel/irq.h"

#define TEST_IRQ_SLOTS 64U
#define TEST_IRQ_SPURIOUS_NONE 0xffffffffU

static uint32_t g_test_irq_ack;
static uint32_t g_test_irq_spurious;
static uint32_t g_test_irq_end_count;
static uint32_t g_test_irq_end_last;
static uint32_t g_test_irq_handler_calls;
static void *g_test_irq_handler_context;

uint32_t board_irq_ack(void) {
    return g_test_irq_ack;
}

void board_irq_end(uint32_t irq) {
    g_test_irq_end_count++;
    g_test_irq_end_last = irq;
}

int board_irq_is_spurious(uint32_t irq) {
    return irq == g_test_irq_spurious;
}

static void test_irq_handler(void *context) {
    g_test_irq_handler_calls++;
    g_test_irq_handler_context = context;
}

static void reset_irq_test_state(void) {
    for (uint32_t irq = 0; irq < TEST_IRQ_SLOTS; irq++) {
        irq_unregister_handler(irq);
    }

    g_test_irq_ack = 0;
    g_test_irq_spurious = TEST_IRQ_SPURIOUS_NONE;
    g_test_irq_end_count = 0;
    g_test_irq_end_last = 0;
    g_test_irq_handler_calls = 0;
    g_test_irq_handler_context = 0;
}

void test_irq_register_rejects_invalid_inputs(void) {
    reset_irq_test_state();

    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)irq_register_handler(TEST_IRQ_SLOTS,
                                                            test_irq_handler,
                                                            0));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)irq_register_handler(1, 0, 0));
}

void test_irq_dispatches_registered_handler_and_sends_eoi(void) {
    int context = 7;

    reset_irq_test_state();
    g_test_irq_ack = 5;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)irq_register_handler(5,
                                                               test_irq_handler,
                                                               &context));
    irq_handler();

    TEST_ASSERT_EQUAL_UINT64(1, g_test_irq_handler_calls);
    TEST_ASSERT_TRUE(g_test_irq_handler_context == &context);
    TEST_ASSERT_EQUAL_UINT64(1, g_test_irq_end_count);
    TEST_ASSERT_EQUAL_UINT64(5, g_test_irq_end_last);
}

void test_irq_unregister_prevents_dispatch_but_still_sends_eoi(void) {
    int context = 11;

    reset_irq_test_state();
    g_test_irq_ack = 6;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)irq_register_handler(6,
                                                               test_irq_handler,
                                                               &context));
    irq_unregister_handler(6);
    irq_handler();

    TEST_ASSERT_EQUAL_UINT64(0, g_test_irq_handler_calls);
    TEST_ASSERT_EQUAL_UINT64(1, g_test_irq_end_count);
    TEST_ASSERT_EQUAL_UINT64(6, g_test_irq_end_last);
}

void test_irq_spurious_skips_handler_and_eoi(void) {
    int context = 13;

    reset_irq_test_state();
    g_test_irq_ack = 7;
    g_test_irq_spurious = 7;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)irq_register_handler(7,
                                                               test_irq_handler,
                                                               &context));
    irq_handler();

    TEST_ASSERT_EQUAL_UINT64(0, g_test_irq_handler_calls);
    TEST_ASSERT_EQUAL_UINT64(0, g_test_irq_end_count);
}
