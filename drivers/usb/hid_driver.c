#include "usb/hid_driver.h"

#include <stdint.h>

#include "input/input.h"
#include "usb/hid.h"
#include "usb/xhci.h"

uint8_t usb_hid_init(usb_hid_state_t *state, const usb_config_walk_t *walk) {
    if (state == 0 || walk == 0) {
        return 0;
    }
    state->count = 0;
    return usb_hid_add_device(state, walk, 0);
}

static uint8_t boot_report_size(uint8_t protocol) {
    if (protocol == 0x01U) {
        return HID_BOOT_KEYBOARD_REPORT_SIZE;
    }
    if (protocol == 0x02U) {
        return HID_BOOT_MOUSE_REPORT_SIZE;
    }
    return 0;
}

uint8_t usb_hid_add_device(usb_hid_state_t *state,
                           const usb_config_walk_t *walk,
                           const usb_device_t *usb_device) {
    if (state == 0 || walk == 0) {
        return 0;
    }
    uint8_t added = 0;
    for (uint8_t i = 0; i < walk->interface_count; i++) {
        const usb_interface_ref_t *iface = &walk->interfaces[i];
        if (iface->desc->bInterfaceClass != USB_CLASS_HID) {
            continue;
        }
        uint8_t proto = iface->desc->bInterfaceProtocol;
        if (proto != 0x01U && proto != 0x02U) {
            /* Only boot-protocol keyboard (0x01) and mouse (0x02). */
            continue;
        }
        const usb_endpoint_ref_t *ep =
            usb_find_endpoint_in(iface, boot_report_size(proto));
        if (ep == 0) {
            continue;
        }
        if (state->count >= USB_HID_MAX_DEVICES) {
            break;
        }
        usb_hid_device_t *dev = &state->devices[state->count++];
        dev->device_address = 0;
        if (usb_device != 0) {
            dev->usb_device = *usb_device;
            dev->device_address = usb_device->address;
        } else {
            dev->usb_device.xhci.ctrl = 0;
            dev->usb_device.address = 0;
        }
        dev->protocol = proto;
        dev->interface_number = iface->desc->bInterfaceNumber;
        dev->endpoint_in = (uint8_t)(ep->address & 0x0FU);
        dev->interval = ep->desc->bInterval;
        dev->max_packet = ep->max_packet;
        for (uint8_t k = 0; k < 6U; k++) {
            dev->prev_keys[k] = HID_KEY_NONE;
            dev->release_pending[k] = 0;
        }
        dev->prev_buttons = 0;
        added++;
    }
    return added;
}

static uint8_t key_in_report(const hid_boot_keyboard_report_t *report,
                             uint8_t key) {
    for (uint8_t i = 0; i < 6U; i++) {
        if (report->keys[i] == key) {
            return 1;
        }
    }
    return 0;
}

static int8_t key_slot(const usb_hid_device_t *dev, uint8_t key) {
    for (uint8_t i = 0; i < 6U; i++) {
        if (dev->prev_keys[i] == key) {
            return (int8_t)i;
        }
    }
    return -1;
}

static int8_t empty_key_slot(const usb_hid_device_t *dev) {
    for (uint8_t i = 0; i < 6U; i++) {
        if (dev->prev_keys[i] == HID_KEY_NONE) {
            return (int8_t)i;
        }
    }
    return -1;
}

uint8_t usb_hid_keyboard_report(usb_hid_device_t *dev,
                                const hid_boot_keyboard_report_t *report,
                                input_event_t *out, uint8_t out_len) {
    if (dev == 0 || report == 0 || out == 0 || out_len == 0) {
        return 0;
    }
    uint8_t produced = 0;
    /* Check if either Shift key is held via the modifier byte. */
    uint8_t shifted = (report->modifiers & (HID_MOD_LSHIFT | HID_MOD_RSHIFT))
                          ? 1U
                          : 0U;
    /*
     * Releases are debounced by one empty report. QEMU's usb-kbd can present
     * very short empty gaps while the host is still repeating a held key; if we
     * clear prev_keys immediately, the next identical report becomes another
     * KEY_PRESS and userland sees "aaaa..." from a single long press.
     */
    for (uint8_t i = 0; i < 6U; i++) {
        uint8_t prev = dev->prev_keys[i];
        if (prev == HID_KEY_NONE) {
            continue;
        }
        if (key_in_report(report, prev)) {
            dev->release_pending[i] = 0;
            continue;
        }
        if (dev->release_pending[i] == 0) {
            dev->release_pending[i] = 1;
            continue;
        }

        out[produced].type = INPUT_EVENT_KEY_RELEASE;
        out[produced].data.key.key = hid_usage_to_ascii(prev, shifted);
        produced++;
        dev->prev_keys[i] = HID_KEY_NONE;
        dev->release_pending[i] = 0;
        if (produced >= out_len) {
            return produced;
        }
    }
    /* Presses: any code in the new report not seen in prev_keys. */
    for (uint8_t i = 0; i < 6U; i++) {
        uint8_t cur = report->keys[i];
        if (cur == HID_KEY_NONE) {
            continue;
        }
        int8_t seen_slot = key_slot(dev, cur);
        if (seen_slot >= 0) {
            dev->release_pending[(uint8_t)seen_slot] = 0;
            continue;
        }

        int8_t slot = empty_key_slot(dev);
        if (slot < 0) {
            continue;
        }
        dev->prev_keys[(uint8_t)slot] = cur;
        dev->release_pending[(uint8_t)slot] = 0;
        out[produced].type = INPUT_EVENT_KEY_PRESS;
        out[produced].data.key.key = hid_usage_to_ascii(cur, shifted);
        produced++;
        if (produced >= out_len) {
            return produced;
        }
    }
    return produced;
}

uint8_t usb_hid_mouse_report(usb_hid_device_t *dev,
                             const hid_boot_mouse_report_t *report,
                             input_event_t *out, uint8_t out_len) {
    if (dev == 0 || report == 0 || out == 0 || out_len == 0) {
        return 0;
    }
    uint8_t produced = 0;
    if (report->dx != 0 || report->dy != 0) {
        if (produced < out_len) {
            out[produced].type = INPUT_EVENT_MOUSE_MOVE;
            out[produced].data.mouse_move.dx = report->dx;
            out[produced].data.mouse_move.dy = report->dy;
            produced++;
        }
    }
    uint8_t changed = (uint8_t)(report->buttons ^ dev->prev_buttons);
    for (uint8_t i = 0; i < 3U; i++) {
        uint8_t mask = (uint8_t)(1U << i);
        if ((changed & mask) == 0U) {
            continue;
        }
        if (produced >= out_len) {
            break;
        }
        out[produced].type = INPUT_EVENT_MOUSE_BUTTON;
        out[produced].data.mouse_button.button = i;
        out[produced].data.mouse_button.pressed =
            (report->buttons & mask) != 0U ? 1U : 0U;
        produced++;
    }
    dev->prev_buttons = report->buttons;
    return produced;
}

int usb_hid_poll_device(usb_hid_device_t *dev) {
    if (dev == 0) {
        return -1;
    }
    if (dev->endpoint_in == 0) {
        return -1;
    }
    xhci_controller_t *ctrl = usb_active_controller();
    if (ctrl == 0) {
        return -1;
    }
    if (dev->protocol == 0x01U) {
        hid_boot_keyboard_report_t report;
        int n;
        if (dev->usb_device.xhci.ctrl != 0) {
            n = xhci_interrupt_in_device(&dev->usb_device.xhci,
                                         dev->endpoint_in,
                                         dev->max_packet, dev->interval,
                                         &report, sizeof(report));
        } else {
            n = xhci_interrupt_in(ctrl, dev->endpoint_in,
                                  dev->max_packet, dev->interval,
                                  &report, sizeof(report));
        }
        if (n <= 0) {
            return n;
        }
        input_event_t events[8];
        uint8_t produced = usb_hid_keyboard_report(dev, &report, events, 8);
        for (uint8_t i = 0; i < produced; i++) {
            input_queue_push(&events[i]);
        }
        return (int)produced;
    } else if (dev->protocol == 0x02U) {
        hid_boot_mouse_report_t report;
        int n;
        if (dev->usb_device.xhci.ctrl != 0) {
            n = xhci_interrupt_in_device(&dev->usb_device.xhci,
                                         dev->endpoint_in,
                                         dev->max_packet, dev->interval,
                                         &report, sizeof(report));
        } else {
            n = xhci_interrupt_in(ctrl, dev->endpoint_in,
                                  dev->max_packet, dev->interval,
                                  &report, sizeof(report));
        }
        if (n <= 0) {
            return n;
        }
        input_event_t events[4];
        uint8_t produced = usb_hid_mouse_report(dev, &report, events, 4);
        for (uint8_t i = 0; i < produced; i++) {
            input_queue_push(&events[i]);
        }
        return (int)produced;
    }
    return -1;
}

usb_hid_state_t g_usb_hid_state;

void usb_hid_state_reset(void) {
    g_usb_hid_state.count = 0;
    for (uint8_t i = 0; i < USB_HID_MAX_DEVICES; i++) {
        for (uint8_t k = 0; k < 6; k++) {
            g_usb_hid_state.devices[i].prev_keys[k] = HID_KEY_NONE;
            g_usb_hid_state.devices[i].release_pending[k] = 0;
        }
        g_usb_hid_state.devices[i].prev_buttons = 0;
        g_usb_hid_state.devices[i].endpoint_in = 0;
        g_usb_hid_state.devices[i].interface_number = 0;
        g_usb_hid_state.devices[i].interval = 0;
        g_usb_hid_state.devices[i].device_address = 0;
        g_usb_hid_state.devices[i].protocol = 0;
        g_usb_hid_state.devices[i].usb_device.xhci.ctrl = 0;
        g_usb_hid_state.devices[i].usb_device.address = 0;
    }
}

int usb_hid_poll_all(void) {
    int total = 0;
    for (uint8_t i = 0; i < g_usb_hid_state.count; i++) {
        int n = usb_hid_poll_device(&g_usb_hid_state.devices[i]);
        if (n > 0) {
            total += n;
        }
    }
    return total;
}

int usb_hid_set_protocol_boot(usb_hid_device_t *dev) {
    if (dev == 0) {
        return -1;
    }
    usb_setup_t setup;
    setup.bmRequestType = 0x21U; /* Class, Interface, Host-to-device */
    setup.bRequest = USB_HID_SET_PROTOCOL;
    setup.wValue = USB_HID_BOOT_PROTOCOL;
    setup.wIndex = dev->interface_number;
    setup.wLength = 0;
    if (dev->usb_device.xhci.ctrl != 0) {
        return usb_device_xfer(&dev->usb_device, &setup, 0, 0);
    }
    return usb_installed_xfer(&setup, 0, 0);
}
