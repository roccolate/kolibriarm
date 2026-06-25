#ifndef KOLIBRIARM_DRIVERS_USB_XHCI_H
#define KOLIBRIARM_DRIVERS_USB_XHCI_H

#include <stdint.h>

#include "pci/pci.h"

/*
 * xHCI (USB 3.x Extensible Host Controller Interface) driver.
 *
 * This is a poll-mode host controller backend for the early HID path.
 * It supports the pieces needed for directly attached boot keyboards
 * and mice on QEMU's qemu-xhci device and on PCIe xHCI controllers
 * such as the Raspberry Pi 4 VL805 once the board PCIe host bridge is
 * mapped:
 *
 *   - PCI discovery by USB class + xHCI programming interface.
 *   - Controller reset, DCBAA, command ring, event ring, and CONFIG.
 *   - Port reset and speed capture.
 *   - Enable Slot + Address Device for endpoint zero.
 *   - Synchronous control transfers on endpoint zero.
 *   - Lazy interrupt-in endpoint configuration and polled HID reports.
 */

#define XHCI_MAX_CONTROLLERS 2U
#define XHCI_MAX_DEVICES     4U
#define XHCI_MAX_SLOTS       32U
#define XHCI_MAX_PORTS       16U
#define XHCI_MAX_ENDPOINTS   32U

typedef struct {
    uint64_t mmio_base;
    uint64_t op_base;
    uint64_t rt_base;
    uint64_t db_base;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t max_slots;
    uint8_t max_ports;
    uint8_t context_size;
    uint8_t priv_index;
    uint8_t slot_id;
    uint8_t port_id;     /* 1-based root-hub port selected for enumeration. */
    uint8_t port_speed;  /* xHCI speed ID read from PORTSC. */
    uint8_t device_addr;
    uint16_t ep0_max_packet;
} xhci_controller_t;

typedef struct {
    xhci_controller_t *ctrl;
    uint8_t index;       /* Driver-private device index. */
    uint8_t slot_id;     /* xHCI slot assigned by Enable Slot. */
    uint8_t port_id;     /* 1-based root-hub port. */
    uint8_t port_speed;  /* xHCI speed ID read from PORTSC. */
    uint8_t device_addr; /* Software-visible USB address. */
    uint16_t ep0_max_packet;
} xhci_device_t;

uint32_t xhci_pci_probe(xhci_controller_t *out, uint32_t max_count);
int xhci_init(xhci_controller_t *ctrl);
int xhci_port_reset(xhci_controller_t *ctrl, uint8_t port_index);
int xhci_address_device(xhci_controller_t *ctrl, uint8_t port_index,
                        uint8_t address, xhci_device_t *out);
int xhci_control_transfer(xhci_controller_t *ctrl,
                          const void *setup, uint8_t setup_size,
                          void *data, uint16_t data_size);
int xhci_control_transfer_device(xhci_device_t *dev,
                                 const void *setup, uint8_t setup_size,
                                 void *data, uint16_t data_size);
int xhci_interrupt_in(xhci_controller_t *ctrl, uint8_t endpoint,
                      uint16_t max_packet, uint8_t interval,
                      void *buf, uint16_t buf_size);
int xhci_interrupt_in_device(xhci_device_t *dev, uint8_t endpoint,
                             uint16_t max_packet, uint8_t interval,
                             void *buf, uint16_t buf_size);

#endif
