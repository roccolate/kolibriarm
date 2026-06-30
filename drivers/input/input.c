#include "input.h"

#include "kernel/irq.h"
#include "uart/pl011.h"

static input_event_t g_event_queue[INPUT_EVENT_QUEUE_SIZE];
static uint32_t g_event_head = 0;
static uint32_t g_event_tail = 0;
static uint32_t g_event_count = 0;

/*
 * ANSI escape-sequence parser. The kernel's only input channel today
 * is the UART (QEMU `-serial stdio`), and QEMU forwards real terminal
 * arrow keys as ESC [ A / B / C / D and Page Up / Page Down as
 * ESC [ 5~ / ESC [ 6~. We accept those byte sequences here and turn
 * them into the synthetic INPUT_KEY_UP/DOWN/LEFT/RIGHT/PGUP/PGDN
 * events so EL0 apps can implement command history, line editing,
 * and log scrollback.
 *
 * States:
 *   0 = idle: a normal byte is pushed as-is.
 *   1 = got ESC (0x1B): expect '['.
 *   2 = got ESC[: expect the direction letter (A/B/C/D) or a digit.
 *   3 = got ESC [ <digit>: expect '~' to close the sequence.
 * Anything unexpected drops back to state 0 (the bytes are
 * discarded, but a stray byte is never queued as a key event).
 */
enum {
    ESC_STATE_IDLE = 0,
    ESC_STATE_GOT_ESC = 1,
    ESC_STATE_GOT_BRACKET = 2,
    ESC_STATE_GOT_BRACKET_DIGIT = 3,
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

/* Handle the digit that follows ESC [. The xterm convention is
 * `ESC [ <digit> ~`, used for Page Up / Page Down (5 and 6) and for
 * F1..F4. We only recognise Page Up and Page Down today; any other
 * digit is silently dropped. */
static void push_escape_tilde(uint8_t digit) {
    uint32_t key;
    switch (digit) {
    case '5': key = INPUT_KEY_PGUP; break;
    case '6': key = INPUT_KEY_PGDN; break;
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
    irq_disable();

    if (g_event_count >= INPUT_EVENT_QUEUE_SIZE) {
        irq_enable();
        return -1;
    }

    g_event_queue[g_event_tail] = *event;
    g_event_tail = (g_event_tail + 1) % INPUT_EVENT_QUEUE_SIZE;
    g_event_count++;

    irq_enable();
    return 0;
}

int input_queue_poll(input_event_t *event) {
    irq_disable();

    if (g_event_count == 0) {
        irq_enable();
        return -1;
    }

    *event = g_event_queue[g_event_head];
    g_event_head = (g_event_head + 1) % INPUT_EVENT_QUEUE_SIZE;
    g_event_count--;

    irq_enable();
    return 0;
}

int input_queue_peek(input_event_t *event) {
    irq_disable();

    if (g_event_count == 0) {
        irq_enable();
        return -1;
    }

    *event = g_event_queue[g_event_head];

    irq_enable();
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
    irq_disable();
    int count = (int)g_event_count;
    irq_enable();
    return count;
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

    if (g_esc_state == ESC_STATE_GOT_BRACKET) {
        /* Page Up / Page Down arrive as `ESC [ <digit> ~`. A digit
         * takes us to ESC_STATE_GOT_BRACKET_DIGIT; anything else is
         * the single-letter arrow key (A/B/C/D). */
        if (c >= '0' && c <= '9') {
            g_esc_state = ESC_STATE_GOT_BRACKET_DIGIT;
            /* Stash the digit on g_esc_discard so the closing state
             * knows which key to emit. The variable is unused except
             * as a 1-byte scratchpad. */
            g_esc_discard = (uint8_t)c;
            return 0;
        }
        g_esc_state = ESC_STATE_IDLE;
        push_escape_key((uint8_t)c);
        return 0;
    }

    /* ESC_STATE_GOT_BRACKET_DIGIT: c should be '~'. Anything else
     * (including another digit, which the xterm spec does not allow)
     * drops the sequence silently. */
    g_esc_state = ESC_STATE_IDLE;
    if (c == '~') {
        push_escape_tilde(g_esc_discard);
        return 0;
    }
    return 0;
}
