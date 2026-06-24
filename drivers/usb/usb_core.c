#include "usb/usb_core.h"

#include <stdint.h>

#include "usb/hid.h"
#include "usb/uhci.h"

static usb_control_xfer_fn g_xfer_fn;
static void *g_xfer_ctx;

void usb_install_controller(usb_control_xfer_fn fn, void *ctx) {
    g_xfer_fn = fn;
    g_xfer_ctx = ctx;
}

static int xfer(const usb_setup_t *setup, void *data, uint16_t len) {
    if (g_xfer_fn == 0) {
        return -1;
    }
    return g_xfer_fn(g_xfer_ctx, setup, data, len);
}

int usb_set_address(uint8_t address) {
    usb_setup_t setup;
    setup.bmRequestType = 0x00U;
    setup.bRequest = USB_REQ_SET_ADDRESS;
    setup.wValue = (uint16_t)address;
    setup.wIndex = 0;
    setup.wLength = 0;
    return xfer(&setup, 0, 0);
}

int usb_get_device_descriptor(usb_device_descriptor_t *out) {
    usb_setup_t setup;
    setup.bmRequestType = 0x80U;
    setup.bRequest = USB_REQ_GET_DESCRIPTOR;
    setup.wValue = (uint16_t)((USB_DESC_DEVICE << 8) | 0);
    setup.wIndex = 0;
    setup.wLength = (uint16_t)sizeof(usb_device_descriptor_t);
    return xfer(&setup, out, (uint16_t)sizeof(usb_device_descriptor_t));
}

int usb_set_configuration(uint8_t value) {
    usb_setup_t setup;
    setup.bmRequestType = 0x00U;
    setup.bRequest = USB_REQ_SET_CONFIGURATION;
    setup.wValue = (uint16_t)value;
    setup.wIndex = 0;
    setup.wLength = 0;
    return xfer(&setup, 0, 0);
}

int usb_walk_configuration(const void *buffer, uint16_t buffer_len,
                           usb_config_walk_t *out) {
    if (buffer == 0 || out == 0 || buffer_len < 4U) {
        return -1;
    }
    const uint8_t *bytes = (const uint8_t *)buffer;
    const usb_config_descriptor_t *config = (const usb_config_descriptor_t *)
        bytes;
    if (config->bLength < 4U || config->bDescriptorType != USB_DESC_CONFIGURATION) {
        return -1;
    }
    out->config = config;
    out->interface_count = 0;
    out->buffer_end = bytes + buffer_len;
    uint16_t offset = config->bLength;
    const uint8_t *cursor = bytes + offset;
    usb_interface_ref_t *cur_iface = 0;
    while ((uintptr_t)cursor < (uintptr_t)out->buffer_end) {
        uint8_t len = cursor[0];
        uint8_t type = cursor[1];
        if (len < 2U) {
            return -1;
        }
        if (type == USB_DESC_INTERFACE) {
            if (out->interface_count >= USB_MAX_INTERFACES) {
                return -1;
            }
            usb_interface_ref_t *iface = &out->interfaces[out->interface_count++];
            iface->desc = (const usb_interface_descriptor_t *)cursor;
            iface->endpoint_count = 0;
            iface->hid = 0;
            cur_iface = iface;
        } else if (type == USB_DESC_ENDPOINT) {
            if (cur_iface == 0) {
                return -1;
            }
            if (cur_iface->endpoint_count >= USB_MAX_ENDPOINTS) {
                return -1;
            }
            usb_endpoint_ref_t *ep =
                &cur_iface->endpoints[cur_iface->endpoint_count++];
            ep->desc = (const usb_endpoint_descriptor_t *)cursor;
            ep->address = ep->desc->bEndpointAddress;
            ep->transfer_type = (uint8_t)(ep->desc->bmAttributes & 0x03U);
            ep->max_packet = ep->desc->wMaxPacketSize;
        } else if (type == USB_DESC_HID) {
            if (cur_iface == 0) {
                return -1;
            }
            cur_iface->hid = (const usb_hid_descriptor_t *)cursor;
        }
        cursor += len;
    }
    return 0;
}

int usb_get_config_descriptor(uint8_t index, void *buffer,
                              uint16_t buffer_len, usb_config_walk_t *out) {
    usb_setup_t setup;
    setup.bmRequestType = 0x80U;
    setup.bRequest = USB_REQ_GET_DESCRIPTOR;
    setup.wValue = (uint16_t)((USB_DESC_CONFIGURATION << 8) | index);
    setup.wIndex = 0;
    setup.wLength = buffer_len;
    int rc = xfer(&setup, buffer, buffer_len);
    if (rc < 0) {
        return rc;
    }
    return usb_walk_configuration(buffer, buffer_len, out);
}

const usb_interface_ref_t *usb_find_interface(const usb_config_walk_t *walk,
                                              uint8_t class_code,
                                              uint8_t subclass,
                                              uint8_t protocol) {
    if (walk == 0) {
        return 0;
    }
    for (uint8_t i = 0; i < walk->interface_count; i++) {
        const usb_interface_descriptor_t *d = walk->interfaces[i].desc;
        if (d->bInterfaceClass == class_code &&
            d->bInterfaceSubClass == subclass &&
            d->bInterfaceProtocol == protocol) {
            return &walk->interfaces[i];
        }
    }
    return 0;
}

const usb_endpoint_ref_t *usb_find_endpoint_in(
    const usb_interface_ref_t *iface, uint16_t max_packet) {
    if (iface == 0) {
        return 0;
    }
    for (uint8_t i = 0; i < iface->endpoint_count; i++) {
        const usb_endpoint_ref_t *ep = &iface->endpoints[i];
        if ((ep->address & 0x80U) == 0x80U &&
            ep->transfer_type == USB_EP_TRANSFER_INTERRUPT &&
            ep->max_packet >= max_packet) {
            return ep;
        }
    }
    return 0;
}

/*
 * Bridge from the UHCI driver to usb_core. The UHCI driver exposes
 * a controller handle per port; usb_core calls into it through this
 * trampoline when controllers are installed. Today the trampoline
 * returns -1 because the UHCI transfer engine is not yet wired up
 * to descriptors; once uhci_control_transfer is real this becomes
 * the only path the enumeration flow needs.
 */
static int uhci_xfer_trampoline(void *ctx, const usb_setup_t *setup,
                                void *data, uint16_t data_len) {
    (void)ctx;
    (void)setup;
    (void)data;
    (void)data_len;
    return -1;
}

__attribute__((constructor))
static void usb_core_register_default(void) {
    usb_install_controller(uhci_xfer_trampoline, 0);
}