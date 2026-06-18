#ifndef KOLIBRIARM_DRIVERS_INPUT_INPUT_H
#define KOLIBRIARM_DRIVERS_INPUT_INPUT_H

#include <stdint.h>

#define INPUT_EVENT_QUEUE_SIZE 64

typedef enum {
    INPUT_EVENT_KEY_PRESS,
    INPUT_EVENT_KEY_RELEASE,
    INPUT_EVENT_MOUSE_MOVE,
    INPUT_EVENT_MOUSE_BUTTON,
} input_event_type_t;

typedef struct {
    input_event_type_t type;
    uint64_t timestamp;
    union {
        struct {
            uint32_t key;
        } key;
        struct {
            int32_t dx;
            int32_t dy;
        } mouse_move;
        struct {
            uint32_t button;
            uint32_t pressed;
        } mouse_button;
    } data;
} input_event_t;

void input_queue_init(void);
int input_queue_push(const input_event_t *event);
int input_queue_poll(input_event_t *event);
int input_queue_poll_char(void);
int input_queue_available(void);

int input_uart_poll(void);

#endif
