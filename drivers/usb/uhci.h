#ifndef KOLIBRIARM_DRIVERS_USB_UHCI_H
#define KOLIBRIARM_DRIVERS_USB_UHCI_H

#include <stdint.h>

#include "pci/pci.h"

/*
 * UHCI (Universal Host Controller Interface) driver.
 *
 * UHCI is the USB 1.1 host controller Intel defined for the i440FX /
 * i82078 / i8409 chipsets. QEMU exposes it on the `piix3-usb-uhci`
 * (and friends) PCI devices and on the `pci-ohci` aliases. Each
 * controller has two downstream ports and walks a 1024-entry frame
 * list once per millisecond.
 *
 * The driver today only does polling and only supports the boot
 * protocol flow needed to talk to a single attached keyboard or
 * mouse. It exposes a small driver API and pushes HID reports into
 * the existing input queue. The full USB 2.0 enumeration flow
 * (configuration descriptors, interface matching, endpoint setup)
 * is implemented in `uhci.c` as a sequence of control transfers
 * driven by descriptor templates.
 */

/* UHCI register offsets relative to BAR4 (the io-space / MMIO base). */
#define UHCI_REG_CMD          0x00U
#define UHCI_REG_STS          0x02U
#define UHCI_REG_INTR         0x04U
#define UHCI_REG_FRNUM        0x06U
#define UHCI_REG_FLBASE       0x08U
#define UHCI_REG_PORTSC0      0x10U
#define UHCI_REG_PORTSC1      0x12U

/* USBCMD bits. */
#define UHCI_CMD_RS           (1U << 0)  /* Run/Stop */
#define UHCI_CMD_HCRESET      (1U << 1)  /* Host Controller Reset */
#define UHCI_CMD_GRESET       (1U << 2)  /* Global Reset */
#define UHCI_CMD_EGSM         (1U << 3)  /* Enter Global Suspend Mode */
#define UHCI_CMD_FGR          (1U << 4)  /* Force Global Resume */
#define UHCI_CMD_SWDBG        (1U << 5)  /* Software Debug */
#define UHCI_CMD_CF           (1U << 6)  /* Configure Flag */
#define UHCI_CMD_MAXP         (1U << 7)  /* Max Packet (64-byte TDs) */

/* USBSTS bits. */
#define UHCI_STS_USBINT       (1U << 0)  /* USB interrupt */
#define UHCI_STS_ERROR        (1U << 1)  /* USB error interrupt */
#define UHCI_STS_RESUMEDETECT (1U << 2)  /* Resume detect */
#define UHCI_STS_HCPE         (1U << 3)  /* Host controller process error */
#define UHCI_STS_HCHALTED     (1U << 5)  /* Host controller halted */

/* PORTSC bits. */
#define UHCI_PORT_CCS          (1U << 0)  /* Current connect status */
#define UHCI_PORT_CSC          (1U << 1)  /* Connect status change */
#define UHCI_PORT_PE           (1U << 2)  /* Port enable */
#define UHCI_PORT_PEC          (1U << 3)  /* Port enable change */
#define UHCI_PORT_LS_MASK      (3U << 4)  /* Line state */
#define UHCI_PORT_RD           (1U << 6)  /* Resume detect */
#define UHCI_PORT_RESET        (1U << 9)  /* Port reset */
#define UHCI_PORT_SUSP         (1U << 12) /* Suspend */
#define UHCI_PORT_RWC          (UHCI_PORT_CSC | UHCI_PORT_PEC)

/* Transfer descriptor bits (element dword 0). */
#define UHCI_TD_ACTIVE        (1U << 23)
#define UHCI_TD_IOC           (1U << 24)  /* Interrupt on completion */
#define UHCI_TD_ISO            (1U << 25) /* Isochronous */
#define UHCI_TD_LOW_SPEED     (1U << 26)  /* Low speed device */
#define UHCI_TD_ERR_COUNT_MASK (3U << 27)
#define UHCI_TD_ERR_COUNT_MAX  (3U << 27)

#define UHCI_TD_PID_IN        (0x69U << 0) /* token type: IN */
#define UHCI_TD_PID_OUT       (0xE1U << 0) /* token type: OUT */
#define UHCI_TD_PID_SETUP     (0x2DU << 0) /* token type: SETUP */

/* Setup packet tokens live in element dword 0..7 of an 8-dword (32 byte)
 * Transfer Descriptor. The frame list entry is one dword: address of
 * the next QH/TD, with bits 0..2 as type (00 = TD, 01 = QH, 10 = depth
 * first QH). */
#define UHCI_PTR_TERM        0x01U
#define UHCI_PTR_QH           0x02U
#define UHCI_PTR_DEPTH        0x04U

/* One UHCI controller, identified by its BAR4 (MMIO) base. */
typedef struct {
    uint64_t mmio_base;
    uint8_t device_addr;   /* USB address assigned during enumeration. */
    uint8_t endpoint_in;   /* HID interrupt-in endpoint address. */
    uint8_t endpoint_out;  /* HID control-out endpoint address. */
    uint8_t max_packet;    /* Endpoint 0 max packet size (8/16/32/64). */
    uint16_t hid_report_size;
} uhci_controller_t;

/* Probe PCI for UHCI class 0x0C0300 devices. Returns the count of
 * controllers discovered; caps at max_count. */
uint32_t uhci_pci_probe(uhci_controller_t *out_controllers,
                        uint32_t max_count);

/* Initialize a controller: bus reset, configure, frame list, run.
 * Returns 0 on success, -1 on any failure. */
int uhci_init(uhci_controller_t *ctrl);

/* Reset a port and wait for connection. Returns 1 if a device is
 * present after the reset, 0 otherwise. */
int uhci_port_reset(uhci_controller_t *ctrl, uint8_t port_index);

/* Send a USB control transfer to the default address. Sets the
 * device address on success. Returns 0 on success, -1 on timeout. */
int uhci_control_transfer(uhci_controller_t *ctrl, const void *setup,
                         uint8_t setup_size, void *data, uint16_t data_size);

/* Read a single interrupt-in report from the HID endpoint. Returns
 * the number of bytes copied into `buf` (0 if no report ready),
 * or -1 on timeout / error. */
int uhci_interrupt_in(uhci_controller_t *ctrl, uint8_t endpoint,
                      void *buf, uint16_t buf_size);

#endif