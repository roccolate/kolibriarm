#include "unity/unity.h"
#include "usb/usb_core.h"
#include "usb/hid.h"

#include <stdint.h>
#include <string.h>

/*
 * Tests for the USB descriptor walker. The walker is pure C: given a
 * buffer that looks like a real configuration descriptor, it has to
 * find the configuration header, each interface, each endpoint, and
 * any class descriptors. We hand-build the bytes here so the tests
 * do not depend on any hardware or driver state.
 */

static void put8(uint8_t *buf, uint32_t *cursor, uint8_t v) {
    buf[(*cursor)++] = v;
}

static void put16(uint8_t *buf, uint32_t *cursor, uint16_t v) {
    buf[(*cursor)++] = (uint8_t)(v & 0xFFU);
    buf[(*cursor)++] = (uint8_t)(v >> 8);
}

static uint32_t make_keyboard_config(uint8_t *buf) {
    uint32_t c = 0;
    /* Configuration descriptor (9 bytes). */
    put8(buf, &c, 9);
    put8(buf, &c, USB_DESC_CONFIGURATION);
    put16(buf, &c, 9 + 9 + 9 + 7); /* wTotalLength */
    put8(buf, &c, 1);  /* bNumInterfaces */
    put8(buf, &c, 1);  /* bConfigurationValue */
    put8(buf, &c, 0);
    put8(buf, &c, 0x80U); /* bmAttributes: bus-powered */
    put8(buf, &c, 50);    /* bMaxPower */
    /* Interface descriptor (9 bytes): HID keyboard. */
    put8(buf, &c, 9);
    put8(buf, &c, USB_DESC_INTERFACE);
    put8(buf, &c, 0); /* bInterfaceNumber */
    put8(buf, &c, 0); /* bAlternateSetting */
    put8(buf, &c, 1); /* bNumEndpoints */
    put8(buf, &c, USB_CLASS_HID);
    put8(buf, &c, 0x01U); /* boot interface subclass */
    put8(buf, &c, 0x01U); /* keyboard protocol */
    put8(buf, &c, 0);
    /* HID class descriptor (9 bytes). */
    put8(buf, &c, 9);
    put8(buf, &c, USB_DESC_HID);
    put16(buf, &c, 0x0111U); /* bcdHID */
    put8(buf, &c, 0);
    put8(buf, &c, 1);     /* bNumDescriptors */
    put8(buf, &c, USB_DESC_HID_REPORT);
    put16(buf, &c, 64);   /* wReportDescriptorLength */
    /* Endpoint descriptor (7 bytes). */
    put8(buf, &c, 7);
    put8(buf, &c, USB_DESC_ENDPOINT);
    put8(buf, &c, 0x81U); /* address 1, IN */
    put8(buf, &c, USB_EP_TRANSFER_INTERRUPT);
    put16(buf, &c, 8);    /* wMaxPacketSize */
    put8(buf, &c, 10);
    return c;
}

void test_usb_walk_configuration_finds_interface(void) {
    uint8_t buf[64];
    uint32_t len = make_keyboard_config(buf);
    usb_config_walk_t walk;
    TEST_ASSERT_EQUAL_UINT64(0,
                            (uint64_t)usb_walk_configuration(buf,
                                                            (uint16_t)len,
                                                            &walk));
    TEST_ASSERT_EQUAL_UINT64(1, walk.interface_count);
    TEST_ASSERT_EQUAL_UINT64(USB_CLASS_HID,
                            walk.interfaces[0].desc->bInterfaceClass);
    TEST_ASSERT_EQUAL_UINT64(0x01U,
                            walk.interfaces[0].desc->bInterfaceProtocol);
}

void test_usb_walk_configuration_attaches_endpoint(void) {
    uint8_t buf[64];
    uint32_t len = make_keyboard_config(buf);
    usb_config_walk_t walk;
    usb_walk_configuration(buf, (uint16_t)len, &walk);
    TEST_ASSERT_EQUAL_UINT64(1, walk.interfaces[0].endpoint_count);
    TEST_ASSERT_EQUAL_UINT64(0x81U,
                            walk.interfaces[0].endpoints[0].address);
    TEST_ASSERT_EQUAL_UINT64(USB_EP_TRANSFER_INTERRUPT,
                            walk.interfaces[0].endpoints[0].transfer_type);
    TEST_ASSERT_EQUAL_UINT64(8,
                            walk.interfaces[0].endpoints[0].max_packet);
}

void test_usb_walk_configuration_attaches_hid_class_descriptor(void) {
    uint8_t buf[64];
    uint32_t len = make_keyboard_config(buf);
    usb_config_walk_t walk;
    usb_walk_configuration(buf, (uint16_t)len, &walk);
    TEST_ASSERT_NOT_NULL(walk.interfaces[0].hid);
    TEST_ASSERT_EQUAL_UINT64(0x0111U, walk.interfaces[0].hid->bcdHID);
    TEST_ASSERT_EQUAL_UINT64(64, walk.interfaces[0].hid->wReportDescriptorLength);
}

void test_usb_walk_configuration_rejects_short_buffer(void) {
    uint8_t buf[2] = { 0 };
    usb_config_walk_t walk;
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                            (uint64_t)usb_walk_configuration(buf, 2, &walk));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                            (uint64_t)usb_walk_configuration(0, 64, &walk));
}

void test_usb_walk_configuration_rejects_wrong_descriptor_type(void) {
    uint8_t buf[9] = { 9, USB_DESC_DEVICE, 0, 1, 0, 0, 0, 0, 0 };
    usb_config_walk_t walk;
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                            (uint64_t)usb_walk_configuration(buf, 9, &walk));
}

void test_usb_find_interface_returns_null_when_no_match(void) {
    uint8_t buf[64];
    uint32_t len = make_keyboard_config(buf);
    usb_config_walk_t walk;
    usb_walk_configuration(buf, (uint16_t)len, &walk);
    /* No mass-storage interface in this descriptor. */
    const usb_interface_ref_t *iface =
        usb_find_interface(&walk, 0x08U, 0x06U, 0x50U);
    TEST_ASSERT_NULL(iface);
}

void test_usb_find_interface_matches_exact_class_subclass_protocol(void) {
    uint8_t buf[64];
    uint32_t len = make_keyboard_config(buf);
    usb_config_walk_t walk;
    usb_walk_configuration(buf, (uint16_t)len, &walk);
    const usb_interface_ref_t *iface =
        usb_find_interface(&walk, USB_CLASS_HID, 0x01U, 0x01U);
    TEST_ASSERT_NOT_NULL(iface);
    TEST_ASSERT_EQUAL_UINT64(0x01U, iface->desc->bInterfaceProtocol);
}

void test_usb_find_endpoint_in_returns_interrupt_endpoint(void) {
    uint8_t buf[64];
    uint32_t len = make_keyboard_config(buf);
    usb_config_walk_t walk;
    usb_walk_configuration(buf, (uint16_t)len, &walk);
    const usb_endpoint_ref_t *ep =
        usb_find_endpoint_in(&walk.interfaces[0], 8U);
    TEST_ASSERT_NOT_NULL(ep);
    TEST_ASSERT_EQUAL_UINT64(0x81U, ep->address);
}

void test_usb_find_endpoint_in_returns_null_when_too_small(void) {
    uint8_t buf[64];
    uint32_t len = make_keyboard_config(buf);
    usb_config_walk_t walk;
    usb_walk_configuration(buf, (uint16_t)len, &walk);
    const usb_endpoint_ref_t *ep =
        usb_find_endpoint_in(&walk.interfaces[0], 16U);
    TEST_ASSERT_NULL(ep);
}