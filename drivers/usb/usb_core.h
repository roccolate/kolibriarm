#ifndef KOLIBRIARM_DRIVERS_USB_CORE_H
#define KOLIBRIARM_DRIVERS_USB_CORE_H

#include <stdint.h>

#include "usb/usb.h"

/*
 * USB enumeration core. This module owns the descriptor walking and
 * the small set of standard control requests we use today:
 *
 *   - usb_set_address(addr)              — assign a new device address
 *   - usb_get_device_descriptor(buf, n)  — 18 bytes
 *   - usb_set_configuration(value)       — 0 = address state
 *   - usb_get_config_descriptor(...)     — full configuration blob
 *
 * The actual USB bus transfer is performed by the host controller
 * driver (UHCI today, possibly XHCI later). usb_core.c calls into
 * uhci_control_transfer through a function pointer so the core
 * itself stays bus-agnostic.
 *
 * The descriptor walkers (`usb_find_interface`, `usb_find_endpoint`,
 * `usb_find_hid`) are pure C: they walk a single configuration
 * descriptor buffer and are unit-testable without any hardware.
 */

#define USB_MAX_CONFIG_LEN 256U
#define USB_MAX_INTERFACES 4U
#define USB_MAX_ENDPOINTS  4U

/* One endpoint we have discovered. */
typedef struct {
    const usb_endpoint_descriptor_t *desc;
    uint8_t address;       /* Endpoint number + direction bit. */
    uint8_t transfer_type; /* 0=ctrl, 1=iso, 2=bulk, 3=interrupt. */
    uint16_t max_packet;
} usb_endpoint_ref_t;

/* One interface we have discovered. */
typedef struct {
    const usb_interface_descriptor_t *desc;
    uint8_t endpoint_count;
    usb_endpoint_ref_t endpoints[USB_MAX_ENDPOINTS];
    const usb_hid_descriptor_t *hid; /* Non-null when class is HID. */
} usb_interface_ref_t;

/* Result of `usb_walk_configuration`. */
typedef struct {
    const usb_config_descriptor_t *config;
    uint8_t interface_count;
    usb_interface_ref_t interfaces[USB_MAX_INTERFACES];
    const void *buffer_end;
} usb_config_walk_t;

typedef int (*usb_control_xfer_fn)(void *ctx, const usb_setup_t *setup,
                                   void *data, uint16_t data_len);

/* Install the host controller's control-transfer callback. Called
 * once at boot from usb_init. */
void usb_install_controller(usb_control_xfer_fn fn, void *ctx);

/* Set the USB device address on the default-address pipe. */
int usb_set_address(uint8_t address);

/* Read a device descriptor into `out` (must be 18 bytes or larger). */
int usb_get_device_descriptor(usb_device_descriptor_t *out);

/* Configure a device with configuration value `value`. */
int usb_set_configuration(uint8_t value);

/* Read a full configuration descriptor into `buffer` (up to
 * USB_MAX_CONFIG_LEN bytes). Returns the parsed walk result. */
int usb_get_config_descriptor(uint8_t index, void *buffer,
                              uint16_t buffer_len, usb_config_walk_t *out);

/* Walk a configuration descriptor buffer and populate `out`. */
int usb_walk_configuration(const void *buffer, uint16_t buffer_len,
                           usb_config_walk_t *out);

/* Convenience accessors built on the walk result. */
const usb_interface_ref_t *usb_find_interface(const usb_config_walk_t *walk,
                                              uint8_t class_code,
                                              uint8_t subclass,
                                              uint8_t protocol);

const usb_endpoint_ref_t *usb_find_endpoint_in(
    const usb_interface_ref_t *iface, uint16_t max_packet);

/* Boot the USB stack: probe PCI for UHCI controllers, initialize the
 * first one, reset its ports, and install the control transfer
 * callback so subsequent usb_set_address / usb_get_device_descriptor
 * calls work. Returns the number of controllers found. */
uint32_t usb_init(void);

/* Reset a single port on the most recently initialized controller.
 * Returns 1 if a device is detected after reset, 0 otherwise. */
int usb_port_reset(uint8_t port_index);

#endif