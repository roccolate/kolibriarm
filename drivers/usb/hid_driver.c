#include "usb/hid_driver.h"

#include <stdint.h>

#include "usb/hid.h"

uint8_t usb_hid_init(usb_hid_state_t *state, const usb_config_walk_t *walk) {
    if (state == 0) {
        return 0;
    }
    state->count = 0;
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
        const usb_endpoint_ref_t *ep = usb_find_endpoint_in(iface, 8U);
        if (ep == 0) {
            continue;
        }
        if (state->count >= USB_HID_MAX_DEVICES) {
            break;
        }
        usb_hid_device_t *dev = &state->devices[state->count++];
        dev->device_address = 0;
        dev->protocol = proto;
        dev->endpoint_in = (uint8_t)(ep->address & 0x0FU);
        dev->max_packet = ep->max_packet;
        for (uint8_t k = 0; k < 6U; k++) {
            dev->prev_keys[k] = HID_KEY_NONE;
        }
        dev->prev_buttons = 0;
    }
    return state->count;
}

uint8_t usb_hid_keyboard_report(usb_hid_device_t *dev,
                                const hid_boot_keyboard_report_t *report,
                                input_event_t *out, uint8_t out_len) {
    if (dev == 0 || report == 0 || out == 0 || out_len == 0) {
        return 0;
    }
    uint8_t produced = 0;
    /* Releases: any code in prev_keys that's missing from the new
     * report is released. */
    for (uint8_t i = 0; i < 6U; i++) {
        uint8_t prev = dev->prev_keys[i];
        if (prev == HID_KEY_NONE) {
            continue;
        }
        uint8_t present = 0;
        for (uint8_t j = 0; j < 6U; j++) {
            if (report->keys[j] == prev) {
                present = 1;
                break;
            }
        }
        if (!present) {
            out[produced].type = INPUT_EVENT_KEY_RELEASE;
            out[produced].data.key.key = hid_usage_to_ascii(prev, 0);
            produced++;
            dev->prev_keys[i] = HID_KEY_NONE;
            if (produced >= out_len) {
                return produced;
            }
        }
    }
    /* Presses: any code in the new report not seen in prev_keys. */
    for (uint8_t i = 0; i < 6U; i++) {
        uint8_t cur = report->keys[i];
        if (cur == HID_KEY_NONE) {
            continue;
        }
        uint8_t seen = 0;
        for (uint8_t j = 0; j < 6U; j++) {
            if (dev->prev_keys[j] == cur) {
                seen = 1;
                break;
            }
        }
        if (!seen) {
            out[produced].type = INPUT_EVENT_KEY_PRESS;
            out[produced].data.key.key = hid_usage_to_ascii(cur, 0);
            produced++;
            if (produced >= out_len) {
                return produced;
            }
        }
    }
    /* Update prev_keys to the new report. */
    for (uint8_t i = 0; i < 6U; i++) {
        dev->prev_keys[i] = report->keys[i];
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
        out[produced].data.mouse_button.button = mask;
        out[produced].data.mouse_button.pressed =
            (report->buttons & mask) != 0U ? 1U : 0U;
        produced++;
    }
    dev->prev_buttons = report->buttons;
    return produced;
}