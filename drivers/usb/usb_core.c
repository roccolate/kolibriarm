#include "usb/usb_core.h"

#include <stdint.h>

#include "usb/hid.h"
#include "usb/xhci.h"

static usb_control_xfer_fn g_xfer_fn;
static void *g_xfer_ctx;
static xhci_controller_t *g_active_ctrl;

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

int usb_installed_xfer(const usb_setup_t *setup, void *data,
                       uint16_t data_len) {
    return xfer(setup, data, data_len);
}

int usb_device_xfer(usb_device_t *dev, const usb_setup_t *setup,
                    void *data, uint16_t data_len) {
    if (dev == 0 || setup == 0) {
        return -1;
    }
    return xhci_control_transfer_device(&dev->xhci, setup,
                                        (uint8_t)sizeof(usb_setup_t),
                                        data, data_len);
}

static int usb_device_get_device_descriptor(usb_device_t *dev,
                                            usb_device_descriptor_t *out) {
    usb_setup_t setup;
    setup.bmRequestType = 0x80U;
    setup.bRequest = USB_REQ_GET_DESCRIPTOR;
    setup.wValue = (uint16_t)((USB_DESC_DEVICE << 8) | 0);
    setup.wIndex = 0;
    setup.wLength = (uint16_t)sizeof(usb_device_descriptor_t);
    return usb_device_xfer(dev, &setup, out,
                           (uint16_t)sizeof(usb_device_descriptor_t));
}

static int usb_device_set_configuration(usb_device_t *dev, uint8_t value) {
    usb_setup_t setup;
    setup.bmRequestType = 0x00U;
    setup.bRequest = USB_REQ_SET_CONFIGURATION;
    setup.wValue = (uint16_t)value;
    setup.wIndex = 0;
    setup.wLength = 0;
    return usb_device_xfer(dev, &setup, 0, 0);
}

static int usb_device_get_config_descriptor(usb_device_t *dev,
                                            uint8_t index, void *buffer,
                                            uint16_t buffer_len,
                                            usb_config_walk_t *out) {
    if (buffer == 0 || out == 0 ||
        buffer_len < sizeof(usb_config_descriptor_t)) {
        return -1;
    }

    usb_setup_t setup;
    setup.bmRequestType = 0x80U;
    setup.bRequest = USB_REQ_GET_DESCRIPTOR;
    setup.wValue = (uint16_t)((USB_DESC_CONFIGURATION << 8) | index);
    setup.wIndex = 0;
    setup.wLength = (uint16_t)sizeof(usb_config_descriptor_t);
    int rc = usb_device_xfer(dev, &setup, buffer,
                             (uint16_t)sizeof(usb_config_descriptor_t));
    if (rc < 0) {
        return rc;
    }

    const usb_config_descriptor_t *config =
        (const usb_config_descriptor_t *)buffer;
    if (config->bLength < sizeof(usb_config_descriptor_t) ||
        config->bDescriptorType != USB_DESC_CONFIGURATION ||
        config->wTotalLength < config->bLength) {
        return -1;
    }

    uint16_t total = config->wTotalLength;
    if (total > buffer_len) {
        total = buffer_len;
    }

    setup.wLength = total;
    rc = usb_device_xfer(dev, &setup, buffer, total);
    if (rc < 0) {
        return rc;
    }
    return usb_walk_configuration(buffer, total, out);
}

int usb_enumerate_default_device(uint8_t address, uint8_t config_value,
                                 void *buffer, uint16_t buffer_len,
                                 usb_config_walk_t *out) {
    if (buffer == 0 || out == 0) {
        return -1;
    }
    /* Step 1: SET_ADDRESS on the default-address pipe. */
    if (usb_set_address(address) < 0) {
        return -1;
    }
    /* Step 2: GET_DESCRIPTOR (device, 8-byte minimum). */
    usb_device_descriptor_t dev;
    if (usb_get_device_descriptor(&dev) < 0) {
        return -1;
    }
    /* Step 3: GET_DESCRIPTOR (configuration, full blob). */
    if (usb_get_config_descriptor(0, buffer, buffer_len, out) < 0) {
        return -1;
    }
    /* Step 4: SET_CONFIGURATION. */
    if (usb_set_configuration(config_value) < 0) {
        return -1;
    }
    return 0;
}

int usb_enumerate_port(uint8_t port_index, uint8_t address,
                       uint8_t config_value, void *buffer,
                       uint16_t buffer_len, usb_device_t *dev,
                       usb_config_walk_t *out) {
    if (g_active_ctrl == 0 || buffer == 0 || dev == 0 || out == 0) {
        return -1;
    }

    if (xhci_address_device(g_active_ctrl, port_index, address,
                            &dev->xhci) != 0) {
        return -1;
    }
    dev->address = address;

    usb_device_descriptor_t desc;
    if (usb_device_get_device_descriptor(dev, &desc) < 0) {
        return -1;
    }
    if (usb_device_get_config_descriptor(dev, 0, buffer,
                                         buffer_len, out) < 0) {
        return -1;
    }
    if (usb_device_set_configuration(dev, config_value) < 0) {
        return -1;
    }
    return 0;
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
        /* Reject if the descriptor would read past buffer_end. */
        if ((uintptr_t)cursor + (uintptr_t)len > (uintptr_t)out->buffer_end) {
            return -1;
        }
        if (type == USB_DESC_INTERFACE) {
            /* If we have already filled the interface array, stop
             * tracking the current interface (endpoints and HID
             * descriptors after this point are ignored). */
            if (out->interface_count < USB_MAX_INTERFACES) {
                usb_interface_ref_t *iface =
                    &out->interfaces[out->interface_count++];
                iface->desc = (const usb_interface_descriptor_t *)cursor;
                iface->endpoint_count = 0;
                iface->hid = 0;
                cur_iface = iface;
            } else {
                cur_iface = 0;
            }
        } else if (type == USB_DESC_ENDPOINT) {
            if (cur_iface == 0) {
                /* Endpoint appeared before any interface or
                 * after we ran out of interface slots; ignore. */
                ;
            } else if (cur_iface->endpoint_count >= USB_MAX_ENDPOINTS) {
                /* Silently drop endpoints beyond the cap. */
                ;
            } else {
                usb_endpoint_ref_t *ep =
                    &cur_iface->endpoints[cur_iface->endpoint_count++];
                ep->desc = (const usb_endpoint_descriptor_t *)cursor;
                ep->address = ep->desc->bEndpointAddress;
                ep->transfer_type = (uint8_t)(ep->desc->bmAttributes & 0x03U);
                ep->max_packet = ep->desc->wMaxPacketSize;
            }
        } else if (type == USB_DESC_HID) {
            if (cur_iface != 0) {
                cur_iface->hid = (const usb_hid_descriptor_t *)cursor;
            }
        }
        cursor += len;
    }
    return 0;
}

int usb_get_config_descriptor(uint8_t index, void *buffer,
                              uint16_t buffer_len, usb_config_walk_t *out) {
    if (buffer == 0 || out == 0 ||
        buffer_len < sizeof(usb_config_descriptor_t)) {
        return -1;
    }

    usb_setup_t setup;
    setup.bmRequestType = 0x80U;
    setup.bRequest = USB_REQ_GET_DESCRIPTOR;
    setup.wValue = (uint16_t)((USB_DESC_CONFIGURATION << 8) | index);
    setup.wIndex = 0;
    setup.wLength = (uint16_t)sizeof(usb_config_descriptor_t);
    int rc = xfer(&setup, buffer,
                  (uint16_t)sizeof(usb_config_descriptor_t));
    if (rc < 0) {
        return rc;
    }

    const usb_config_descriptor_t *config =
        (const usb_config_descriptor_t *)buffer;
    if (config->bLength < sizeof(usb_config_descriptor_t) ||
        config->bDescriptorType != USB_DESC_CONFIGURATION ||
        config->wTotalLength < config->bLength) {
        return -1;
    }

    uint16_t total = config->wTotalLength;
    if (total > buffer_len) {
        total = buffer_len;
    }

    setup.wLength = total;
    rc = xfer(&setup, buffer, total);
    if (rc < 0) {
        return rc;
    }
    return usb_walk_configuration(buffer, total, out);
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

xhci_controller_t *usb_active_controller(void) {
    return g_active_ctrl;
}

static int xhci_xfer_trampoline(void *ctx, const usb_setup_t *setup,
                                void *data, uint16_t data_len) {
    (void)ctx;
    if (g_active_ctrl == 0) {
        return -1;
    }
    return xhci_control_transfer(g_active_ctrl, setup,
                                 (uint8_t)sizeof(usb_setup_t),
                                 data, data_len);
}

uint32_t usb_init(void) {
    static xhci_controller_t controllers[XHCI_MAX_CONTROLLERS];
    uint32_t n = xhci_pci_probe(controllers, XHCI_MAX_CONTROLLERS);
    if (n == 0) {
        return 0;
    }
    if (xhci_init(&controllers[0]) != 0) {
        return 0;
    }
    g_active_ctrl = &controllers[0];
    usb_install_controller(xhci_xfer_trampoline, 0);
    return 1;
}

int usb_port_reset(uint8_t port_index) {
    if (g_active_ctrl == 0) {
        return 0;
    }
    return xhci_port_reset(g_active_ctrl, port_index);
}

uint8_t usb_port_count(void) {
    if (g_active_ctrl == 0) {
        return 0;
    }
    return g_active_ctrl->max_ports;
}
