#include "input.h"

#include "uart/pl011.h"

static input_event_t g_event_queue[INPUT_EVENT_QUEUE_SIZE];
static uint32_t g_event_head = 0;
static uint32_t g_event_tail = 0;
static uint32_t g_event_count = 0;

/*
 * ANSI escape-sequence parser. The kernel's only input channel today
 * is the UART (QEMU `-serial stdio`), and QEMU forwards real terminal
 * arrow keys as ESC [ A / B / C / D. We accept those byte sequences
 * here and turn them into the synthetic INPUT_KEY_UP/DOWN/LEFT/RIGHT
 * events so EL0 apps can implement command history or line editing.
 *
 * States:
 *   0 = idle: a normal byte is pushed as-is.
 *   1 = got ESC (0x1B): expect '['.
 *   2 = got ESC[: expect the direction letter.
 * Anything unexpected drops back to state 0 (the bytes are
 * discarded, but a stray byte is never queued as a key event).
 */
enum {
    ESC_STATE_IDLE = 0,
    ESC_STATE_GOT_ESC = 1,
    ESC_STATE_GOT_BRACKET = 2,
};

static uint8_t g_esc_state = ESC_STATE_IDLE;
static uint8_t g_esc_discard;

static void push_key_event(uint32_t key) {
    input_event_t event = {
        .type = INPUT_EVENT_KEY_PRESS,
        .timestamp = 0,
        .data.key.key = key,
    };
    (void)input_queue_push(&event);
}

static void push_escape_key(uint8_t direction) {
    uint32_t key;
    switch (direction) {
    case 'A': key = INPUT_KEY_UP; break;
    case 'B': key = INPUT_KEY_DOWN; break;
    case 'C': key = INPUT_KEY_RIGHT; break;
    case 'D': key = INPUT_KEY_LEFT; break;
    default: return;
    }
    push_key_event(key);
}

void input_queue_init(void) {
    g_event_head = 0;
    g_event_tail = 0;
    g_event_count = 0;
}

int input_queue_push(const input_event_t *event) {
    if (g_event_count >= INPUT_EVENT_QUEUE_SIZE) {
        return -1;
    }

    g_event_queue[g_event_tail] = *event;
    g_event_tail = (g_event_tail + 1) % INPUT_EVENT_QUEUE_SIZE;
    g_event_count++;

    return 0;
}

int input_queue_poll(input_event_t *event) {
    if (g_event_count == 0) {
        return -1;
    }

    *event = g_event_queue[g_event_head];
    g_event_head = (g_event_head + 1) % INPUT_EVENT_QUEUE_SIZE;
    g_event_count--;

    return 0;
}

int input_queue_peek(input_event_t *event) {
    if (g_event_count == 0) {
        return -1;
    }

    *event = g_event_queue[g_event_head];
    return 0;
}

int input_queue_poll_char(void) {
    input_event_t event;

    if (input_queue_poll(&event) != 0) {
        return -1;
    }

    if (event.type != INPUT_EVENT_KEY_PRESS) {
        return -1;
    }

    return (int)(event.data.key.key & 0xff);
}

int input_queue_available(void) {
    return (int)g_event_count;
}

int input_uart_poll(void) {
    int c = uart_getc_nonblock();
    if (c < 0) {
        return -1;
    }
    return input_inject_byte(c);
}

int input_inject_byte(int c) {
    /*
     * Drive the ANSI arrow-key state machine. Bare ESC is queued as a
     * key event so the shell can react to the ESC key itself; only
     * the '[' and direction letters of a real arrow-key sequence are
     * swallowed.
     */
    if (g_esc_state == ESC_STATE_IDLE) {
        if (c == 0x1B) {
            g_esc_state = ESC_STATE_GOT_ESC;
            push_key_event((uint32_t)c);
            return 0;
        }
        push_key_event((uint32_t)c);
        return 0;
    }

    if (g_esc_state == ESC_STATE_GOT_ESC) {
        if (c == '[') {
            g_esc_state = ESC_STATE_GOT_BRACKET;
            return 0;
        }
        g_esc_state = ESC_STATE_IDLE;
        if (c == 0x1B) {
            g_esc_state = ESC_STATE_GOT_ESC;
            push_key_event((uint32_t)c);
            return 0;
        }
        push_key_event((uint32_t)c);
        return 0;
    }

    /* ESC_STATE_GOT_BRACKET: c is the direction letter. */
    g_esc_state = ESC_STATE_IDLE;
    push_escape_key((uint8_t)c);
    (void)g_esc_discard;
    return 0;
}
