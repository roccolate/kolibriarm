#include "usb/hid.h"
#include "usb/hid_driver.h"
#include "unity/unity.h"

#include <stdint.h>

/*
 * Tests for the HID usage ID -> ASCII translation table.
 *
 * The boot protocol report carries raw USB HID Usage IDs, not ASCII.
 * The kernel's input layer expects ASCII bytes (or the special 0x101..
 * 0x104 navigation codes). hid_usage_to_ascii performs the lookup.
 */

void test_hid_usage_to_ascii_lowercase_letters(void) {
    TEST_ASSERT_EQUAL_UINT64('a', hid_usage_to_ascii(0x04U, 0));
    TEST_ASSERT_EQUAL_UINT64('b', hid_usage_to_ascii(0x05U, 0));
    TEST_ASSERT_EQUAL_UINT64('z', hid_usage_to_ascii(0x1DU, 0));
}

void test_hid_usage_to_ascii_uppercase_letters_with_shift(void) {
    TEST_ASSERT_EQUAL_UINT64('A', hid_usage_to_ascii(0x04U, 1));
    TEST_ASSERT_EQUAL_UINT64('Z', hid_usage_to_ascii(0x1DU, 1));
    /* Without shift, the same key returns lowercase. */
    TEST_ASSERT_EQUAL_UINT64('a', hid_usage_to_ascii(0x04U, 0));
}

void test_hid_usage_to_ascii_digits_and_punctuation(void) {
    TEST_ASSERT_EQUAL_UINT64('1', hid_usage_to_ascii(0x1EU, 0));
    TEST_ASSERT_EQUAL_UINT64('0', hid_usage_to_ascii(0x27U, 0));
    TEST_ASSERT_EQUAL_UINT64(' ', hid_usage_to_ascii(0x2CU, 0));
    TEST_ASSERT_EQUAL_UINT64('\n', hid_usage_to_ascii(0x28U, 0));
    TEST_ASSERT_EQUAL_UINT64('\b', hid_usage_to_ascii(0x2AU, 0));
    TEST_ASSERT_EQUAL_UINT64('/', hid_usage_to_ascii(0x38U, 0));
}

void test_hid_usage_to_ascii_shifted_punctuation(void) {
    TEST_ASSERT_EQUAL_UINT64('!', hid_usage_to_ascii(0x1EU, 1));
    TEST_ASSERT_EQUAL_UINT64(')', hid_usage_to_ascii(0x27U, 1));
    TEST_ASSERT_EQUAL_UINT64('?', hid_usage_to_ascii(0x38U, 1));
}

void test_hid_usage_to_ascii_returns_zero_for_modifiers(void) {
    /* LCtrl = 0xE0, LShift = 0xE1, LAlt = 0xE2, LGUI = 0xE3. */
    TEST_ASSERT_EQUAL_UINT64(0, hid_usage_to_ascii(0xE0U, 0));
    TEST_ASSERT_EQUAL_UINT64(0, hid_usage_to_ascii(0xE1U, 0));
    TEST_ASSERT_EQUAL_UINT64(0, hid_usage_to_ascii(0xE2U, 0));
    TEST_ASSERT_EQUAL_UINT64(0, hid_usage_to_ascii(0xE3U, 0));
}

void test_hid_usage_to_ascii_returns_zero_for_function_keys(void) {
    /* F1 = 0x3A, F12 = 0x45. */
    TEST_ASSERT_EQUAL_UINT64(0, hid_usage_to_ascii(0x3AU, 0));
    TEST_ASSERT_EQUAL_UINT64(0, hid_usage_to_ascii(0x45U, 0));
}

void test_hid_usage_to_ascii_high_usage_ids_return_zero(void) {
    /* Usage IDs above 0x7F are reserved and must not be returned. */
    TEST_ASSERT_EQUAL_UINT64(0, hid_usage_to_ascii(0x80U, 0));
    TEST_ASSERT_EQUAL_UINT64(0, hid_usage_to_ascii(0xFFU, 0));
}

/*
 * Tests for the boot report -> input_event_t conversion.
 *
 * The driver tracks the previous report and emits a release for any
 * keycode that disappears plus a press for any new keycode. The mouse
 * driver emits a single MOUSE_MOVE event plus one MOUSE_BUTTON per
 * edge.
 */

void test_hid_keyboard_report_emits_press_for_new_key(void) {
    usb_hid_device_t dev = { 0 };
    hid_boot_keyboard_report_t r = { 0 };
    r.keys[0] = 0x04U; /* 'a' */
    input_event_t events[8];
    uint8_t n = usb_hid_keyboard_report(&dev, &r, events, 8);
    TEST_ASSERT_EQUAL_UINT64(1, n);
    TEST_ASSERT_EQUAL_UINT64(INPUT_EVENT_KEY_PRESS, events[0].type);
    TEST_ASSERT_EQUAL_UINT64('a', events[0].data.key.key);
}

void test_hid_keyboard_report_emits_release_when_key_dropped(void) {
    usb_hid_device_t dev = { 0 };
    hid_boot_keyboard_report_t a = { 0 };
    a.keys[0] = 0x04U; /* 'a' */
    input_event_t scratch[8];
    usb_hid_keyboard_report(&dev, &a, scratch, 8);

    hid_boot_keyboard_report_t empty = { 0 };
    input_event_t events[8];
    uint8_t n = usb_hid_keyboard_report(&dev, &empty, events, 8);
    TEST_ASSERT_EQUAL_UINT64(0, n);

    n = usb_hid_keyboard_report(&dev, &empty, events, 8);
    TEST_ASSERT_EQUAL_UINT64(1, n);
    TEST_ASSERT_EQUAL_UINT64(INPUT_EVENT_KEY_RELEASE, events[0].type);
    TEST_ASSERT_EQUAL_UINT64('a', events[0].data.key.key);
}

void test_hid_keyboard_report_filters_single_empty_repeat_gap(void) {
    usb_hid_device_t dev = { 0 };
    hid_boot_keyboard_report_t a = { 0 };
    a.keys[0] = 0x04U; /* 'a' */
    hid_boot_keyboard_report_t empty = { 0 };
    input_event_t events[8];

    uint8_t n = usb_hid_keyboard_report(&dev, &a, events, 8);
    TEST_ASSERT_EQUAL_UINT64(1, n);

    n = usb_hid_keyboard_report(&dev, &empty, events, 8);
    TEST_ASSERT_EQUAL_UINT64(0, n);

    n = usb_hid_keyboard_report(&dev, &a, events, 8);
    TEST_ASSERT_EQUAL_UINT64(0, n);
}

void test_hid_keyboard_report_handles_two_key_rollover(void) {
    usb_hid_device_t dev = { 0 };
    hid_boot_keyboard_report_t r = { 0 };
    r.keys[0] = 0x04U; /* 'a' */
    r.keys[1] = 0x05U; /* 'b' */
    input_event_t events[8];
    uint8_t n = usb_hid_keyboard_report(&dev, &r, events, 8);
    TEST_ASSERT_EQUAL_UINT64(2, n);
    TEST_ASSERT_EQUAL_UINT64(INPUT_EVENT_KEY_PRESS, events[0].type);
    TEST_ASSERT_EQUAL_UINT64('a', events[0].data.key.key);
    TEST_ASSERT_EQUAL_UINT64(INPUT_EVENT_KEY_PRESS, events[1].type);
    TEST_ASSERT_EQUAL_UINT64('b', events[1].data.key.key);
}

void test_hid_mouse_report_emits_move_when_deltas_nonzero(void) {
    usb_hid_device_t dev = { 0 };
    hid_boot_mouse_report_t r = { 0, 3, -2 };
    input_event_t events[4];
    uint8_t n = usb_hid_mouse_report(&dev, &r, events, 4);
    TEST_ASSERT_EQUAL_UINT64(1, n);
    TEST_ASSERT_EQUAL_UINT64(INPUT_EVENT_MOUSE_MOVE, events[0].type);
    TEST_ASSERT_EQUAL_UINT64(3, (uint64_t)events[0].data.mouse_move.dx);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(int32_t)-2,
                            (uint64_t)events[0].data.mouse_move.dy);
}

void test_hid_mouse_report_emits_button_press_and_release(void) {
    usb_hid_device_t dev = { 0 };
    hid_boot_mouse_report_t press = { HID_BTN_LEFT, 0, 0 };
    input_event_t events[4];
    uint8_t n = usb_hid_mouse_report(&dev, &press, events, 4);
    TEST_ASSERT_EQUAL_UINT64(1, n);
    TEST_ASSERT_EQUAL_UINT64(INPUT_EVENT_MOUSE_BUTTON, events[0].type);
    TEST_ASSERT_EQUAL_UINT64(0, events[0].data.mouse_button.button);
    TEST_ASSERT_EQUAL_UINT64(1, events[0].data.mouse_button.pressed);

    hid_boot_mouse_report_t release = { 0, 0, 0 };
    n = usb_hid_mouse_report(&dev, &release, events, 4);
    TEST_ASSERT_EQUAL_UINT64(1, n);
    TEST_ASSERT_EQUAL_UINT64(INPUT_EVENT_MOUSE_BUTTON, events[0].type);
    TEST_ASSERT_EQUAL_UINT64(0, events[0].data.mouse_button.pressed);
}

void test_hid_mouse_report_emits_no_events_when_idle(void) {
    usb_hid_device_t dev = { 0 };
    hid_boot_mouse_report_t r = { 0, 0, 0 };
    input_event_t events[4];
    uint8_t n = usb_hid_mouse_report(&dev, &r, events, 4);
    TEST_ASSERT_EQUAL_UINT64(0, n);
}

void test_hid_mouse_report_rejects_null_arguments(void) {
    usb_hid_device_t dev = { 0 };
    hid_boot_mouse_report_t r = { 0, 0, 0 };
    input_event_t events[4];
    TEST_ASSERT_EQUAL_UINT64(0, usb_hid_mouse_report(0, &r, events, 4));
    TEST_ASSERT_EQUAL_UINT64(0, usb_hid_mouse_report(&dev, 0, events, 4));
    TEST_ASSERT_EQUAL_UINT64(0, usb_hid_mouse_report(&dev, &r, 0, 4));
}
