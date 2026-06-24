#ifndef KOLIBRIARM_DRIVERS_USB_HID_DRIVER_H
#define KOLIBRIARM_DRIVERS_USB_HID_DRIVER_H

#include <stdint.h>

#include "input/input.h"
#include "usb/hid.h"
#include "usb/usb_core.h"

/*
 * HID class driver.
 *
 * Backs the boot protocol only: keyboards expose an 8-byte report,
 * mice a 3-byte report. Each report is converted into the kernel's
 * internal `input_event_t` representation and pushed onto the same
 * input queue virtio-input and UART input use. The driver exposes:
 *
 *   - `usb_hid_init(ctrl)`: walk a configuration descriptor blob and
 *      configure a UHCI controller for the keyboard or mouse found.
 *   - `usb_hid_poll(ctrl)`: drain pending boot reports and produce
 *      events. Returns the number of events pushed (0 if none).
 *
 * A single controller at a time is the practical limit on QEMU's
 * virt machine; the array below fits up to four devices so we can
 * grow into a hub later without breaking the API.
 */

#define USB_HID_MAX_DEVICES 4U

typedef struct {
    uint8_t device_address;
    uint8_t protocol;     /* 0x01 = keyboard, 0x02 = mouse */
    uint8_t endpoint_in;  /* Endpoint number (without direction bit) */
    uint16_t max_packet;
    uint8_t prev_keys[6]; /* Last keyboard report's keycodes */
    uint8_t prev_buttons; /* Last mouse report's button byte */
} usb_hid_device_t;

typedef struct {
    usb_hid_device_t devices[USB_HID_MAX_DEVICES];
    uint8_t count;
} usb_hid_state_t;

/* Initialize from a configuration walk. The walk result is the one
 * returned by `usb_get_config_descriptor` / `usb_walk_configuration`.
 * Returns the number of HID devices registered. */
uint8_t usb_hid_init(usb_hid_state_t *state, const usb_config_walk_t *walk);

/* Convert a single boot keyboard report into a sequence of key events
 * (max 6 per report). Existing keys not present in the new report
 * are emitted as released. Returns the number of events written. */
uint8_t usb_hid_keyboard_report(usb_hid_device_t *dev,
                                const hid_boot_keyboard_report_t *report,
                                input_event_t *out, uint8_t out_len);

/* Convert a single boot mouse report into a delta event. */
uint8_t usb_hid_mouse_report(usb_hid_device_t *dev,
                             const hid_boot_mouse_report_t *report,
                             input_event_t *out, uint8_t out_len);

/* Read the next pending report from a HID device's interrupt-in
 * endpoint. Parses the report and pushes the resulting events
 * into the global input queue. Returns the number of events
 * pushed, 0 if no report was pending, or -1 on error. */
int usb_hid_poll_device(usb_hid_device_t *dev);

/* Kernel-wide HID state: holds the registered devices after
 * enumeration. Exposed so the input thread can poll everything
 * in a single call. */
extern usb_hid_state_t g_usb_hid_state;

/* Poll every registered HID device. Pushes the resulting events
 * into the global input queue. Returns the total number of events
 * pushed. */
int usb_hid_poll_all(void);

/* Reset the kernel-wide HID state. */
void usb_hid_state_reset(void);

/* Send HID SET_PROTOCOL=0 (boot) to a HID device. The endpoint
 * parameter is the device's interrupt-in endpoint number (the
 * function uses it as wIndex in the class request). */
int usb_hid_set_protocol_boot(uint8_t endpoint_in);

#endif