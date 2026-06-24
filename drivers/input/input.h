#ifndef KOLIBRIARM_DRIVERS_INPUT_INPUT_H
#define KOLIBRIARM_DRIVERS_INPUT_INPUT_H

#include <stdint.h>

#define INPUT_EVENT_QUEUE_SIZE 64

/*
 * Special (non-ASCII) key codes delivered through
 * input_event_t::data.key.key. They live above 0xFF so they never
 * collide with the 7-bit ASCII range that the UART and most
 * keyboards emit. The shell uses UP/DOWN for command history; LEFT/
 * RIGHT are reserved for future line editing (cursor movement, etc.)
 * and are forwarded the same way.
 */
#define INPUT_KEY_UP    0x101U
#define INPUT_KEY_DOWN  0x102U
#define INPUT_KEY_LEFT  0x103U
#define INPUT_KEY_RIGHT 0x104U

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
/* Peek at the next event without removing it. Returns 0 and fills
 * `event` on success, or -1 if the queue is empty. The caller can
 * decide whether to pop the event with input_queue_poll. The peek
 * and the pop are not atomic; if another consumer drains the
 * queue in between, input_queue_poll will return -1. */
int input_queue_peek(input_event_t *event);
int input_queue_poll_char(void);
int input_queue_available(void);

int input_uart_poll(void);
/*
 * Inject a single byte into the input queue through the same ANSI
 * escape-sequence parser used for UART bytes. Useful for tests and
 * for keyboard drivers that already produced a single byte.
 */
int input_inject_byte(int c);

#endif
