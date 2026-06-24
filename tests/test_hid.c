#include <stdint.h>
#include <string.h>

#include "unity/unity.h"
#include "../drivers/usb/hid.h"

void test_hid_parse_boot_keyboard_decodes_modifiers_and_keys(void) {
    const uint8_t report[8] = {
        0x03,    /* LCtrl + LShift */
        0x00,
        0x04,    /* 'a' */
        0x05,    /* 'b' */
        0x06,    /* 'c' */
        0x00,
        0x00,
        0x00,
    };
    hid_boot_keyboard_report_t decoded;
    TEST_ASSERT_EQUAL_UINT64(0,
                            (uint64_t)hid_parse_boot_keyboard(
                                report, sizeof(report), &decoded));
    TEST_ASSERT_EQUAL_UINT64(0x03U, decoded.modifiers);
    TEST_ASSERT_EQUAL_UINT64(0x04U, decoded.keys[0]);
    TEST_ASSERT_EQUAL_UINT64(0x05U, decoded.keys[1]);
    TEST_ASSERT_EQUAL_UINT64(0x06U, decoded.keys[2]);
    TEST_ASSERT_EQUAL_UINT64(0x00U, decoded.keys[3]);
}

void test_hid_parse_boot_keyboard_rejects_short_buffer(void) {
    const uint8_t report[7] = { 0 };
    hid_boot_keyboard_report_t decoded;
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                            (uint64_t)hid_parse_boot_keyboard(
                                report, sizeof(report), &decoded));
}

void test_hid_parse_boot_keyboard_rejects_null(void) {
    hid_boot_keyboard_report_t decoded;
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                            (uint64_t)hid_parse_boot_keyboard(0, 8, &decoded));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                            (uint64_t)hid_parse_boot_keyboard((uint8_t *)1, 8, 0));
}

void test_hid_parse_boot_mouse_decodes_buttons_and_motion(void) {
    const uint8_t report[3] = { 0x01, 0x05, (uint8_t)(int8_t)-3 };
    hid_boot_mouse_report_t decoded;
    TEST_ASSERT_EQUAL_UINT64(0,
                            (uint64_t)hid_parse_boot_mouse(report, sizeof(report),
                                                          &decoded));
    TEST_ASSERT_EQUAL_UINT64(0x01U, decoded.buttons);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(int8_t)5, (uint64_t)decoded.dx);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(int8_t)-3, (uint64_t)decoded.dy);
}

void test_hid_parse_boot_mouse_sign_extends_negative_motion(void) {
    /* dx = 0xFF should decode to -1, not 255. */
    const uint8_t report[3] = { 0x00, 0xFF, 0x80 };
    hid_boot_mouse_report_t decoded;
    TEST_ASSERT_EQUAL_UINT64(0,
                            (uint64_t)hid_parse_boot_mouse(report, sizeof(report),
                                                          &decoded));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(int8_t)-1, (uint64_t)decoded.dx);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(int8_t)-128, (uint64_t)decoded.dy);
}

void test_hid_parse_boot_mouse_rejects_short_buffer(void) {
    const uint8_t report[2] = { 0 };
    hid_boot_mouse_report_t decoded;
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                            (uint64_t)hid_parse_boot_mouse(report, sizeof(report),
                                                          &decoded));
}