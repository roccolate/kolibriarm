#ifndef KOLIBRIARM_DRIVERS_USB_HID_H
#define KOLIBRIARM_DRIVERS_USB_HID_H

#include <stdint.h>

/*
 * HID boot protocol report parsers.
 *
 * The HID specification lets a device describe any input format, but
 * every USB keyboard and mouse ships with a "boot protocol" profile
 * (defined in the HID 1.11 spec, Appendix B) that we parse directly.
 * Once a device is in boot protocol mode the report shape is fixed:
 *
 *   Keyboard report (8 bytes):
 *     [0]    modifier byte (bit 0..7: LCtrl, LShift, LAlt, LGUI,
 *                             RCtrl, RShift, RAlt, RGui)
 *     [1]    reserved (0)
 *     [2..7] up to 6 concurrent key codes (USB HID Usage IDs)
 *
 *   Mouse report (3 bytes, no wheel):
 *     [0]    button state (bit 0..2: left, right, middle)
 *     [1]    X displacement (signed)
 *     [2]    Y displacement (signed)
 *
 * The kernel only consumes boot protocol reports; full HID report
 * descriptors (with arbitrary field layouts) are out of scope.
 */

#define HID_BOOT_KEYBOARD_REPORT_SIZE 8U
#define HID_BOOT_MOUSE_REPORT_SIZE    3U

#define HID_KEY_NONE  0x00U

typedef struct {
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keys[6];
} hid_boot_keyboard_report_t;

typedef struct {
    uint8_t buttons;
    int8_t dx;
    int8_t dy;
} hid_boot_mouse_report_t;

#define HID_MOD_LCTRL   (1U << 0)
#define HID_MOD_LSHIFT  (1U << 1)
#define HID_MOD_LALT    (1U << 2)
#define HID_MOD_LGUI    (1U << 3)
#define HID_MOD_RCTRL   (1U << 4)
#define HID_MOD_RSHIFT  (1U << 5)
#define HID_MOD_RALT    (1U << 6)
#define HID_MOD_RGUI    (1U << 7)

#define HID_BTN_LEFT    (1U << 0)
#define HID_BTN_RIGHT   (1U << 1)
#define HID_BTN_MIDDLE  (1U << 2)

/* Parse a boot-protocol keyboard report into the report struct.
 * Returns 0 on success, -1 if the buffer is too small. */
int hid_parse_boot_keyboard(const uint8_t *buf, uint32_t len,
                            hid_boot_keyboard_report_t *out);

/* Parse a boot-protocol mouse report. Returns 0 on success, -1 if the
 * buffer is too small. */
int hid_parse_boot_mouse(const uint8_t *buf, uint32_t len,
                         hid_boot_mouse_report_t *out);

/* Translate a USB HID Usage ID (from a keyboard report) into an ASCII
 * character. The `shifted` flag picks the unshifted or shifted form
 * of the key. Returns 0 for keys that have no ASCII equivalent
 * (modifiers, function keys, navigation keys). */
uint8_t hid_usage_to_ascii(uint8_t usage, uint8_t shifted);

#endif