#ifndef KOLIBRIARM_DRIVERS_USB_UHCI_H
#define KOLIBRIARM_DRIVERS_USB_UHCI_H

#include <stdint.h>

#include "pci/pci.h"

/*
 * UHCI (Universal Host Controller Interface) driver.
 *
 * UHCI is the USB 1.1 host controller Intel defined for the i440FX /
 * i82078 / i8409 chipsets. QEMU exposes it on the `piix3-usb-uhci`
 * (and friends) PCI devices. Each controller has two downstream
 * ports and walks a 1024-entry frame list once per millisecond.
 *
 * The driver is poll mode and supports the boot protocol flow
 * needed to talk to an attached USB keyboard or mouse:
 *
 *   - uhci_pci_probe: walk PCI for class 0x0C03 devices.
 *   - uhci_init:      reset + configure + run the controller.
 *   - uhci_port_reset: drive port-reset and read port status.
 *   - uhci_control_transfer: SETUP / DATA / STATUS TD chain on
 *     the default-address pipe (endpoint 0). Used for SET_ADDRESS,
 *     GET_DESCRIPTOR, SET_CONFIGURATION, and HID class requests.
 *   - uhci_interrupt_in: IN TD on the HID endpoint, polls until
 *     the active bit clears and copies the report out.
 *
 * Each controller gets a private frame list (4 KB, 1024 entries)
 * and a transfer-descriptor pool (16 bytes per TD). All buffers
 * the controller DMAs must be in physical memory; we hand it our
 * own static pool which is already at a known identity-mapped
 * virtual address.
 */

/* UHCI register offsets relative to the MMIO base. */
#define UHCI_REG_CMD          0x00U
#define UHCI_REG_STS          0x02U
#define UHCI_REG_INTR         0x04U
#define UHCI_REG_FRNUM        0x06U
#define UHCI_REG_FLBASE       0x08U
#define UHCI_REG_PORTSC0      0x10U
#define UHCI_REG_PORTSC1      0x12U

/* USBCMD bits. */
#define UHCI_CMD_RS           (1U << 0)
#define UHCI_CMD_HCRESET      (1U << 1)
#define UHCI_CMD_GRESET       (1U << 2)
#define UHCI_CMD_EGSM         (1U << 3)
#define UHCI_CMD_FGR          (1U << 4)
#define UHCI_CMD_SWDBG        (1U << 5)
#define UHCI_CMD_CF           (1U << 6)
#define UHCI_CMD_MAXP         (1U << 7)

/* USBSTS bits. */
#define UHCI_STS_USBINT       (1U << 0)
#define UHCI_STS_ERROR        (1U << 1)
#define UHCI_STS_RESUMEDETECT (1U << 2)
#define UHCI_STS_HCPE         (1U << 3)
#define UHCI_STS_HCHALTED     (1U << 5)

/* PORTSC bits. */
#define UHCI_PORT_CCS         (1U << 0)
#define UHCI_PORT_CSC         (1U << 1)
#define UHCI_PORT_PE          (1U << 2)
#define UHCI_PORT_PEC         (1U << 3)
#define UHCI_PORT_RD          (1U << 6)
#define UHCI_PORT_RESET       (1U << 9)
#define UHCI_PORT_SUSP        (1U << 12)
#define UHCI_PORT_RWC         (UHCI_PORT_CSC | UHCI_PORT_PEC)

/* TD status field bits. */
#define UHCI_TD_ACTIVE        (1U << 23)
#define UHCI_TD_BITSTUFF      (1U << 17)
#define UHCI_TD_CRC           (1U << 18)
#define UHCI_TD_NAK           (1U << 19)
#define UHCI_TD_BABBLE        (1U << 20)
#define UHCI_TD_DBUFFER       (1U << 21)
#define UHCI_TD_STALLED       (1U << 22)
#define UHCI_TD_ACTIVE_BIT    (1U << 23)
#define UHCI_TD_IOC           (1U << 24)
#define UHCI_TD_ISO           (1U << 25)
#define UHCI_TD_LOW_SPEED     (1U << 26)
#define UHCI_TD_ERR_COUNT_MASK (3U << 27)

/* TD PIDs. */
#define UHCI_PID_SETUP        0x2DU
#define UHCI_PID_IN           0x69U
#define UHCI_PID_OUT          0xE1U

/* Frame list link types. */
#define UHCI_PTR_TERM         0x00000001U

/* One UHCI controller. */
typedef struct {
    uint64_t mmio_base;
    uint8_t device_addr;     /* Assigned during enumeration. */
    uint8_t endpoint_in;     /* HID interrupt-in endpoint number. */
    uint8_t max_packet;      /* Endpoint 0 max packet size. */
    uint8_t priv_index;      /* Index into the driver-private pool. */
} uhci_controller_t;

uint32_t uhci_pci_probe(uhci_controller_t *out, uint32_t max_count);
int uhci_init(uhci_controller_t *ctrl);
int uhci_port_reset(uhci_controller_t *ctrl, uint8_t port_index);
int uhci_control_transfer(uhci_controller_t *ctrl, const void *setup,
                          uint8_t setup_size, void *data,
                          uint16_t data_size);
int uhci_interrupt_in(uhci_controller_t *ctrl, uint8_t endpoint,
                      void *buf, uint16_t buf_size);

#endif