#include "usb/hid.h"

int hid_parse_boot_keyboard(const uint8_t *buf, uint32_t len,
                            hid_boot_keyboard_report_t *out) {
    if (buf == 0 || out == 0 || len < HID_BOOT_KEYBOARD_REPORT_SIZE) {
        return -1;
    }
    out->modifiers = buf[0];
    out->reserved = buf[1];
    out->keys[0] = buf[2];
    out->keys[1] = buf[3];
    out->keys[2] = buf[4];
    out->keys[3] = buf[5];
    out->keys[4] = buf[6];
    out->keys[5] = buf[7];
    return 0;
}

int hid_parse_boot_mouse(const uint8_t *buf, uint32_t len,
                         hid_boot_mouse_report_t *out) {
    if (buf == 0 || out == 0 || len < HID_BOOT_MOUSE_REPORT_SIZE) {
        return -1;
    }
    out->buttons = buf[0];
    out->dx = (int8_t)buf[1];
    out->dy = (int8_t)buf[2];
    return 0;
}

/*
 * USB HID Usage ID -> ASCII translation. The boot protocol report
 * uses the keyboard/keypad page (0x07), so the first 0x7F entries
 * are the printable characters. Function keys, modifiers, and
 * navigation keys return 0.
 */
static const uint8_t kHIDUnshifted[128] = {
    [0x04] = 'a', [0x05] = 'b', [0x06] = 'c', [0x07] = 'd',
    [0x08] = 'e', [0x09] = 'f', [0x0A] = 'g', [0x0B] = 'h',
    [0x0C] = 'i', [0x0D] = 'j', [0x0E] = 'k', [0x0F] = 'l',
    [0x10] = 'm', [0x11] = 'n', [0x12] = 'o', [0x13] = 'p',
    [0x14] = 'q', [0x15] = 'r', [0x16] = 's', [0x17] = 't',
    [0x18] = 'u', [0x19] = 'v', [0x1A] = 'w', [0x1B] = 'x',
    [0x1C] = 'y', [0x1D] = 'z',
    [0x1E] = '1', [0x1F] = '2', [0x20] = '3', [0x21] = '4',
    [0x22] = '5', [0x23] = '6', [0x24] = '7', [0x25] = '8',
    [0x26] = '9', [0x27] = '0',
    [0x28] = '\n', [0x29] = 0x1BU,    /* Escape */
    [0x2A] = '\b', [0x2B] = '\t',
    [0x2C] = ' ',
    [0x2D] = '-', [0x2E] = '=', [0x2F] = '[', [0x30] = ']',
    [0x31] = '\\', [0x33] = ';', [0x34] = '\'', [0x35] = '`',
    [0x36] = ',', [0x37] = '.', [0x38] = '/',
};

static const uint8_t kHIDShifted[128] = {
    [0x04] = 'A', [0x05] = 'B', [0x06] = 'C', [0x07] = 'D',
    [0x08] = 'E', [0x09] = 'F', [0x0A] = 'G', [0x0B] = 'H',
    [0x0C] = 'I', [0x0D] = 'J', [0x0E] = 'K', [0x0F] = 'L',
    [0x10] = 'M', [0x11] = 'N', [0x12] = 'O', [0x13] = 'P',
    [0x14] = 'Q', [0x15] = 'R', [0x16] = 'S', [0x17] = 'T',
    [0x18] = 'U', [0x19] = 'V', [0x1A] = 'W', [0x1B] = 'X',
    [0x1C] = 'Y', [0x1D] = 'Z',
    [0x1E] = '!', [0x1F] = '@', [0x20] = '#', [0x21] = '$',
    [0x22] = '%', [0x23] = '^', [0x24] = '&', [0x25] = '*',
    [0x26] = '(', [0x27] = ')',
    [0x28] = '\n', [0x29] = 0x1BU,
    [0x2A] = '\b', [0x2B] = '\t',
    [0x2C] = ' ',
    [0x2D] = '_', [0x2E] = '+', [0x2F] = '{', [0x30] = '}',
    [0x31] = '|', [0x33] = ':', [0x34] = '"', [0x35] = '~',
    [0x36] = '<', [0x37] = '>', [0x38] = '?',
};

uint8_t hid_usage_to_ascii(uint8_t usage, uint8_t shifted) {
    if (usage >= 0x80U) {
        return 0;
    }
    if (shifted) {
        return kHIDShifted[usage];
    }
    return kHIDUnshifted[usage];
}