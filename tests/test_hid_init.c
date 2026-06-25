#include "unity/unity.h"
#include "usb/hid.h"
#include "usb/hid_driver.h"
#include "usb/usb_core.h"

#include <stdint.h>
#include <string.h>

/*
 * Tests for the full HID init flow.
 *
 * usb_hid_init walks a config descriptor, finds boot-protocol
 * keyboard and mouse interfaces, and registers them in the state
 * struct. We hand-build a real-looking config blob (config +
 * interface + HID class + endpoint) and verify that the walk
 * + registration produces the expected device entries.
 */

static uint32_t put8(uint8_t *buf, uint32_t cursor, uint8_t v) {
    buf[cursor++] = v;
    return cursor;
}

static uint32_t put16(uint8_t *buf, uint32_t cursor, uint16_t v) {
    buf[cursor++] = (uint8_t)(v & 0xFFU);
    buf[cursor++] = (uint8_t)(v >> 8);
    return cursor;
}

static uint32_t make_mouse_config(uint8_t *buf) {
    uint32_t c = 0;
    c = put8(buf, c, 9);
    c = put8(buf, c, USB_DESC_CONFIGURATION);
    c = put16(buf, c, 9 + 9 + 9 + 7);
    c = put8(buf, c, 1);
    c = put8(buf, c, 1);
    c = put8(buf, c, 0);
    c = put8(buf, c, 0x80);
    c = put8(buf, c, 50);
    c = put8(buf, c, 9);
    c = put8(buf, c, USB_DESC_INTERFACE);
    c = put8(buf, c, 0);
    c = put8(buf, c, 0);
    c = put8(buf, c, 1);
    c = put8(buf, c, USB_CLASS_HID);
    c = put8(buf, c, 0x01U); /* boot subclass */
    c = put8(buf, c, 0x02U); /* mouse protocol */
    c = put8(buf, c, 0);
    c = put8(buf, c, 9);
    c = put8(buf, c, USB_DESC_HID);
    c = put16(buf, c, 0x0111U);
    c = put8(buf, c, 0);
    c = put8(buf, c, 1);
    c = put8(buf, c, USB_DESC_HID_REPORT);
    c = put16(buf, c, 50);
    c = put8(buf, c, 7);
    c = put8(buf, c, USB_DESC_ENDPOINT);
    c = put8(buf, c, 0x81U);
    c = put8(buf, c, USB_EP_TRANSFER_INTERRUPT);
    c = put16(buf, c, 4);
    c = put8(buf, c, 10);
    return c;
}

void test_hid_init_registers_mouse_protocol(void) {
    uint8_t buf[64];
    uint32_t len = make_mouse_config(buf);
    usb_config_walk_t walk;
    TEST_ASSERT_EQUAL_UINT64(0,
                            (uint64_t)usb_walk_configuration(
                                buf, (uint16_t)len, &walk));
    usb_hid_state_t state;
    state.count = 0xFFU; /* poison */
    uint8_t n = usb_hid_init(&state, &walk);
    TEST_ASSERT_EQUAL_UINT64(1, n);
    TEST_ASSERT_EQUAL_UINT64(1, state.count);
    TEST_ASSERT_EQUAL_UINT64(0x02U, state.devices[0].protocol);
    TEST_ASSERT_EQUAL_UINT64(0, state.devices[0].interface_number);
    TEST_ASSERT_EQUAL_UINT64(0x01U, state.devices[0].endpoint_in);
    TEST_ASSERT_EQUAL_UINT64(10, state.devices[0].interval);
    TEST_ASSERT_EQUAL_UINT64(4, state.devices[0].max_packet);
}

void test_hid_init_zeroes_prev_keys_for_new_device(void) {
    uint8_t buf[64];
    uint32_t len = make_mouse_config(buf);
    usb_config_walk_t walk;
    usb_walk_configuration(buf, (uint16_t)len, &walk);
    usb_hid_state_t state;
    state.devices[0].prev_keys[0] = 0x42U;
    state.devices[0].prev_keys[1] = 0x99U;
    state.devices[0].prev_buttons = 0xFFU;
    uint8_t n = usb_hid_init(&state, &walk);
    TEST_ASSERT_EQUAL_UINT64(1, n);
    for (uint8_t k = 0; k < 6; k++) {
        TEST_ASSERT_EQUAL_UINT64(HID_KEY_NONE, state.devices[0].prev_keys[k]);
    }
    TEST_ASSERT_EQUAL_UINT64(0, state.devices[0].prev_buttons);
}

void test_hid_add_device_appends_and_copies_usb_handle(void) {
    uint8_t buf[64];
    uint32_t len = make_mouse_config(buf);
    usb_config_walk_t walk;
    usb_walk_configuration(buf, (uint16_t)len, &walk);

    usb_hid_state_t state;
    uint8_t n = usb_hid_init(&state, &walk);
    TEST_ASSERT_EQUAL_UINT64(1, n);
    TEST_ASSERT_EQUAL_UINT64(1, state.count);

    usb_device_t usb_dev;
    usb_dev.address = 2;
    usb_dev.xhci.ctrl = (xhci_controller_t *)(uintptr_t)0x1000U;
    usb_dev.xhci.index = 1;
    usb_dev.xhci.slot_id = 7;
    n = usb_hid_add_device(&state, &walk, &usb_dev);

    TEST_ASSERT_EQUAL_UINT64(1, n);
    TEST_ASSERT_EQUAL_UINT64(2, state.count);
    TEST_ASSERT_EQUAL_UINT64(2, state.devices[1].device_address);
    TEST_ASSERT_EQUAL_UINT64(7, state.devices[1].usb_device.xhci.slot_id);
}

void test_hid_init_returns_zero_when_no_boot_interfaces(void) {
    /* Build a config with a non-HID interface (mass storage). */
    uint8_t buf[64];
    uint32_t c = 0;
    c = put8(buf, c, 9);
    c = put8(buf, c, USB_DESC_CONFIGURATION);
    c = put16(buf, c, 9 + 9);
    c = put8(buf, c, 1);
    c = put8(buf, c, 1);
    c = put8(buf, c, 0);
    c = put8(buf, c, 0x80);
    c = put8(buf, c, 50);
    c = put8(buf, c, 9);
    c = put8(buf, c, USB_DESC_INTERFACE);
    c = put8(buf, c, 0);
    c = put8(buf, c, 0);
    c = put8(buf, c, 0);
    c = put8(buf, c, 0x08U); /* mass storage */
    c = put8(buf, c, 0x06U);
    c = put8(buf, c, 0x50U);
    c = put8(buf, c, 0);
    usb_config_walk_t walk;
    usb_walk_configuration(buf, (uint16_t)c, &walk);
    usb_hid_state_t state;
    uint8_t n = usb_hid_init(&state, &walk);
    TEST_ASSERT_EQUAL_UINT64(0, n);
    TEST_ASSERT_EQUAL_UINT64(0, state.count);
}

void test_hid_init_returns_zero_for_null_state(void) {
    uint8_t buf[64];
    uint32_t len = make_mouse_config(buf);
    usb_config_walk_t walk;
    usb_walk_configuration(buf, (uint16_t)len, &walk);
    uint8_t n = usb_hid_init(0, &walk);
    TEST_ASSERT_EQUAL_UINT64(0, n);
}

void test_hid_init_keeps_max_four_devices(void) {
    /* Build a config with 5 HID interfaces; we only keep 4. */
    uint8_t buf[256];
    uint32_t c = 0;
    c = put8(buf, c, 9);
    c = put8(buf, c, USB_DESC_CONFIGURATION);
    /* total length will be filled at the end */
    uint32_t total_off = c;
    c = put8(buf, c, 0);
    c = put8(buf, c, 0);
    c = put8(buf, c, 5);
    c = put8(buf, c, 1);
    c = put8(buf, c, 0);
    c = put8(buf, c, 0x80);
    c = put8(buf, c, 50);
    for (uint8_t i = 0; i < 5; i++) {
        c = put8(buf, c, 9);
        c = put8(buf, c, USB_DESC_INTERFACE);
        c = put8(buf, c, i);
        c = put8(buf, c, 0);
        c = put8(buf, c, 1);  /* bNumEndpoints = 1 */
        c = put8(buf, c, USB_CLASS_HID);
        c = put8(buf, c, 0x01U);
        c = put8(buf, c, (i & 1) ? 0x02U : 0x01U);
        c = put8(buf, c, 0);
        c = put8(buf, c, 9);
        c = put8(buf, c, USB_DESC_HID);
        c = put16(buf, c, 0x0111U);
        c = put8(buf, c, 0);
        c = put8(buf, c, 1);
        c = put8(buf, c, USB_DESC_HID_REPORT);
        c = put16(buf, c, 50);
        c = put8(buf, c, 7);
        c = put8(buf, c, USB_DESC_ENDPOINT);
        c = put8(buf, c, (uint8_t)(0x80U | (i + 1U)));
        c = put8(buf, c, USB_EP_TRANSFER_INTERRUPT);
        c = put16(buf, c, 8);
        c = put8(buf, c, 10);
    }
    /* Fill total length: config (9) + 5 * (interface (9) + hid (9) + ep (7)). */
    uint16_t total = (uint16_t)(9 + 5 * (9 + 9 + 7));
    buf[total_off] = (uint8_t)(total & 0xFFU);
    buf[total_off + 1] = (uint8_t)((total >> 8) & 0xFFU);
    /* The walker caps at USB_MAX_INTERFACES = 4, so we only get 4
     * interfaces in the walk. usb_hid_init also caps at 4. The
     * 5th interface is silently dropped. */
    usb_config_walk_t walk;
    int rc = usb_walk_configuration(buf, (uint16_t)c, &walk);
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)rc);
    TEST_ASSERT_EQUAL_UINT64(USB_MAX_INTERFACES, walk.interface_count);
    usb_hid_state_t state;
    uint8_t n = usb_hid_init(&state, &walk);
    TEST_ASSERT_EQUAL_UINT64(USB_HID_MAX_DEVICES, n);
    TEST_ASSERT_EQUAL_UINT64(USB_HID_MAX_DEVICES, state.count);
}
