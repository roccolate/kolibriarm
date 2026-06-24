#include <stdint.h>

#include "unity/unity.h"
#include "../drivers/input/input.h"

/*
 * Stub UART that never has a byte ready. Lets us test
 * input_inject_byte directly without going through MMIO.
 */
int uart_getc_nonblock(void) { return -1; }

static uint32_t pop_key(void) {
    input_event_t event;
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)input_queue_poll(&event));
    return event.data.key.key;
}

static void flush_all(void) {
    input_event_t event;
    while (input_queue_poll(&event) == 0) {
    }
}

void test_input_injects_ascii_byte_directly(void) {
    flush_all();
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)input_inject_byte('A'));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)input_inject_byte('b'));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)input_inject_byte('\n'));

    TEST_ASSERT_EQUAL_UINT64('A', pop_key());
    TEST_ASSERT_EQUAL_UINT64('b', pop_key());
    TEST_ASSERT_EQUAL_UINT64('\n', pop_key());
}

void test_input_translates_esc_up_to_key_up(void) {
    flush_all();
    /*
     * ESC [ A -> INPUT_KEY_UP. The ESC byte is also queued as a
     * standalone key event (real terminals send it that way); the
     * '[' is consumed by the state machine; the direction letter
     * triggers the synthetic key.
     */
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)input_inject_byte(0x1B));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)input_inject_byte('['));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)input_inject_byte('A'));
    TEST_ASSERT_EQUAL_UINT64(0x1B, pop_key());
    TEST_ASSERT_EQUAL_UINT64(INPUT_KEY_UP, pop_key());
    /* No leftover events. */
    input_event_t ev;
    TEST_ASSERT_TRUE(input_queue_poll(&ev) != 0);
}

void test_input_translates_esc_down_to_key_down(void) {
    flush_all();
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)input_inject_byte(0x1B));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)input_inject_byte('['));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)input_inject_byte('B'));
    TEST_ASSERT_EQUAL_UINT64(0x1B, pop_key());
    TEST_ASSERT_EQUAL_UINT64(INPUT_KEY_DOWN, pop_key());
    input_event_t ev;
    TEST_ASSERT_TRUE(input_queue_poll(&ev) != 0);
}

void test_input_translates_esc_left_and_right(void) {
    flush_all();
    input_inject_byte(0x1B);
    input_inject_byte('[');
    input_inject_byte('C');
    TEST_ASSERT_EQUAL_UINT64(0x1B, pop_key());
    TEST_ASSERT_EQUAL_UINT64(INPUT_KEY_RIGHT, pop_key());

    input_inject_byte(0x1B);
    input_inject_byte('[');
    input_inject_byte('D');
    TEST_ASSERT_EQUAL_UINT64(0x1B, pop_key());
    TEST_ASSERT_EQUAL_UINT64(INPUT_KEY_LEFT, pop_key());
}

void test_input_keeps_esc_for_unrelated_followup(void) {
    flush_all();
    /*
     * Bare ESC followed by a non-'[' byte should not consume the
     * follow-up byte: ESC + 'X' yields one ESC followed by 'X'.
     * Real terminals reset the state machine so a stray ESC isn't
     * absorbed.
     */
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)input_inject_byte(0x1B));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)input_inject_byte('X'));
    TEST_ASSERT_EQUAL_UINT64(0x1B, pop_key());
    TEST_ASSERT_EQUAL_UINT64('X', pop_key());
}

void test_input_supports_consecutive_esc_sequences(void) {
    flush_all();
    input_inject_byte(0x1B);
    input_inject_byte('[');
    input_inject_byte('A');
    input_inject_byte(0x1B);
    input_inject_byte('[');
    input_inject_byte('B');
    TEST_ASSERT_EQUAL_UINT64(0x1B, pop_key());
    TEST_ASSERT_EQUAL_UINT64(INPUT_KEY_UP, pop_key());
    TEST_ASSERT_EQUAL_UINT64(0x1B, pop_key());
    TEST_ASSERT_EQUAL_UINT64(INPUT_KEY_DOWN, pop_key());
}

void test_input_poll_char_mask_strips_high_bits(void) {
    flush_all();
    input_inject_byte(0x1B);
    input_inject_byte('[');
    input_inject_byte('A');
    /*
     * input_queue_poll_char masks with 0xFF so ASCII stdin reads
     * return the raw byte; arrow keys (>= 0x100) get their high
     * byte stripped. KEY_UP = 0x101, so poll_char returns 0x01.
     */
    TEST_ASSERT_EQUAL_UINT64(0x1B, (uint64_t)input_queue_poll_char());
    TEST_ASSERT_EQUAL_UINT64(0x01, (uint64_t)input_queue_poll_char());
}