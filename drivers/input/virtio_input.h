#ifndef KOLIBRIARM_DRIVERS_INPUT_VIRTIO_INPUT_H
#define KOLIBRIARM_DRIVERS_INPUT_VIRTIO_INPUT_H

#include <stdint.h>

#include "input.h"

typedef struct {
    uint64_t base;
    uint32_t queue_size;
    uint16_t last_used_idx;
    uint8_t ready;
} virtio_input_device_t;

#define VIRTIO_INPUT_CFG_UNSET     0
#define VIRTIO_INPUT_CFG_ID_NAME  1
#define VIRTIO_INPUT_CFG_ID_SERIAL 2
#define VIRTIO_INPUT_CFG_ID_DEVS   3
#define VIRTIO_INPUT_CFG_INPUT_BITMAP  4
#define VIRTIO_INPUT_CFG_INPUT_EV_GEN  5

#define VIRTIO_INPUT_EV_SYN        0x00
#define VIRTIO_INPUT_EV_KEY        0x01
#define VIRTIO_INPUT_EV_REL        0x02
#define VIRTIO_INPUT_EV_ABS        0x03

#define VIRTIO_INPUT_KEY_RESERVED      0

/* Mouse buttons (Linux input-event-codes). Codes 0x110..0x112 are the three
 * primary buttons; anything above 0x10f is treated as a mouse button by the
 * kernel input layer, so the producer in virtio_input.c detects them here. */
#define VIRTIO_INPUT_BTN_LEFT          0x110U
#define VIRTIO_INPUT_BTN_RIGHT         0x111U
#define VIRTIO_INPUT_BTN_MIDDLE        0x112U

#define VIRTIO_INPUT_KEY_1             2
#define VIRTIO_INPUT_KEY_ESC           1
#define VIRTIO_INPUT_KEY_BACKSPACE     14
#define VIRTIO_INPUT_KEY_ENTER         28
#define VIRTIO_INPUT_KEY_TAB           15
#define VIRTIO_INPUT_KEY_SPACE         57
#define VIRTIO_INPUT_KEY_LEFTSHIFT     42
#define VIRTIO_INPUT_KEY_RIGHTSHIFT    54
#define VIRTIO_INPUT_KEY_LEFTCTRL      29
#define VIRTIO_INPUT_KEY_RIGHTCTRL     97
#define VIRTIO_INPUT_KEY_A             30
#define VIRTIO_INPUT_KEY_B             48
#define VIRTIO_INPUT_KEY_C             46
#define VIRTIO_INPUT_KEY_D             32
#define VIRTIO_INPUT_KEY_E             18
#define VIRTIO_INPUT_KEY_F             33
#define VIRTIO_INPUT_KEY_G             34
#define VIRTIO_INPUT_KEY_H             35
#define VIRTIO_INPUT_KEY_I             23
#define VIRTIO_INPUT_KEY_J             36
#define VIRTIO_INPUT_KEY_K             37
#define VIRTIO_INPUT_KEY_L             38
#define VIRTIO_INPUT_KEY_M             50
#define VIRTIO_INPUT_KEY_N             49
#define VIRTIO_INPUT_KEY_O             24
#define VIRTIO_INPUT_KEY_P             25
#define VIRTIO_INPUT_KEY_Q             16
#define VIRTIO_INPUT_KEY_R             19
#define VIRTIO_INPUT_KEY_S             31
#define VIRTIO_INPUT_KEY_T             20
#define VIRTIO_INPUT_KEY_U             22
#define VIRTIO_INPUT_KEY_V             47
#define VIRTIO_INPUT_KEY_W             17
#define VIRTIO_INPUT_KEY_X             45
#define VIRTIO_INPUT_KEY_Y             21
#define VIRTIO_INPUT_KEY_Z             44
#define VIRTIO_INPUT_KEY_0             11
#define VIRTIO_INPUT_KEY_2             12
#define VIRTIO_INPUT_KEY_3             13
#define VIRTIO_INPUT_KEY_4             10
#define VIRTIO_INPUT_KEY_5             8
#define VIRTIO_INPUT_KEY_6             9
#define VIRTIO_INPUT_KEY_7             7
#define VIRTIO_INPUT_KEY_8             6
#define VIRTIO_INPUT_KEY_9             5
#define VIRTIO_INPUT_KEY_MINUS         12
#define VIRTIO_INPUT_KEY_EQUAL         13
#define VIRTIO_INPUT_KEY_LEFTBRACKET   26
#define VIRTIO_INPUT_KEY_RIGHTBRACKET  27
#define VIRTIO_INPUT_KEY_SEMICOLON     39
#define VIRTIO_INPUT_KEY_APOSTROPHE    40
#define VIRTIO_INPUT_KEY_GRAVE         41
#define VIRTIO_INPUT_KEY_BACKSLASH     43
#define VIRTIO_INPUT_KEY_COMMA         51
#define VIRTIO_INPUT_KEY_DOT           52
#define VIRTIO_INPUT_KEY_SLASH         53
#define VIRTIO_INPUT_KEY_KP0           82
#define VIRTIO_INPUT_KEY_KP1           79
#define VIRTIO_INPUT_KEY_KP2           80
#define VIRTIO_INPUT_KEY_KP3           81
#define VIRTIO_INPUT_KEY_KP4           75
#define VIRTIO_INPUT_KEY_KP5           76
#define VIRTIO_INPUT_KEY_KP6           77
#define VIRTIO_INPUT_KEY_KP7           71
#define VIRTIO_INPUT_KEY_KP8           72
#define VIRTIO_INPUT_KEY_KP9           73
#define VIRTIO_INPUT_KEY_KPDOT         83
#define VIRTIO_INPUT_KEY_KPENTER       96
#define VIRTIO_INPUT_KEY_KPPLUS        78
#define VIRTIO_INPUT_KEY_KPMINUS       74
#define VIRTIO_INPUT_KEY_KPMULTIPLY    55
#define VIRTIO_INPUT_KEY_KPDIVIDE      98
#define VIRTIO_INPUT_KEY_UP            103
#define VIRTIO_INPUT_KEY_DOWN          108
#define VIRTIO_INPUT_KEY_LEFT          105
#define VIRTIO_INPUT_KEY_RIGHT         106
#define VIRTIO_INPUT_KEY_HOME          102
#define VIRTIO_INPUT_KEY_END           107
#define VIRTIO_INPUT_KEY_PAGEUP        104
#define VIRTIO_INPUT_KEY_PAGEDOWN      109
#define VIRTIO_INPUT_KEY_INSERT        110
#define VIRTIO_INPUT_KEY_DELETE        111
#define VIRTIO_INPUT_KEY_LEFTALT       56
#define VIRTIO_INPUT_KEY_RIGHTALT      100
#define VIRTIO_INPUT_KEY_CAPSLOCK     58
#define VIRTIO_INPUT_KEY_SCROLLLOCK   70
#define VIRTIO_INPUT_KEY_NUMLOCK       69
#define VIRTIO_INPUT_KEY_F1            59
#define VIRTIO_INPUT_KEY_F2            60
#define VIRTIO_INPUT_KEY_F3            61
#define VIRTIO_INPUT_KEY_F4            62
#define VIRTIO_INPUT_KEY_F5            63
#define VIRTIO_INPUT_KEY_F6            64
#define VIRTIO_INPUT_KEY_F7            65
#define VIRTIO_INPUT_KEY_F8            66
#define VIRTIO_INPUT_KEY_F9            67
#define VIRTIO_INPUT_KEY_F10           68
#define VIRTIO_INPUT_KEY_F11           87
#define VIRTIO_INPUT_KEY_F12           88

int virtio_input_probe(uint64_t base);
int virtio_input_probe_range(uint64_t base, uint64_t size, uint64_t stride,
                             uint64_t *found_base);
int virtio_input_init(virtio_input_device_t *device, uint64_t base);
int virtio_input_poll(virtio_input_device_t *device);
int virtio_input_has_events(virtio_input_device_t *device);

#endif
