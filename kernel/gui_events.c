#include "kernel/gui_events.h"
#include "kernel/irq.h"

int gui_window_push_event(gui_window_t *window, uint32_t type,
                          int32_t data1, int32_t data2) {
    int result;

    if (window == 0 || window->used == 0) {
        return -1;
    }

    irq_disable();

    if (type == GUI_EVENT_MOUSE_MOVE && window->event_count > 0U) {
        uint32_t prev = window->event_tail == 0U
                            ? GUI_EVENT_QUEUE_SIZE - 1U
                            : window->event_tail - 1U;
        if (window->events[prev].type == GUI_EVENT_MOUSE_MOVE) {
            window->events[prev].data1 = data1;
            window->events[prev].data2 = data2;
            irq_enable();
            return 0;
        }
    }

    if (window->event_count >= GUI_EVENT_QUEUE_SIZE) {
        if (type == GUI_EVENT_MOUSE_MOVE) {
            irq_enable();
            return 0;
        }
        window->event_head = (window->event_head + 1U) % GUI_EVENT_QUEUE_SIZE;
        window->event_count--;
    }

    window->events[window->event_tail].type = type;
    window->events[window->event_tail].data1 = data1;
    window->events[window->event_tail].data2 = data2;
    window->event_tail = (window->event_tail + 1U) % GUI_EVENT_QUEUE_SIZE;
    window->event_count++;
    result = 0;

    irq_enable();
    return result;
}

int gui_window_pop_event(gui_window_t *window, gui_event_t *out) {
    if (window == 0 || window->used == 0 || out == 0) {
        return -1;
    }

    irq_disable();

    if (window->event_count == 0U) {
        irq_enable();
        return -1;
    }

    *out = window->events[window->event_head];
    window->event_head = (window->event_head + 1U) % GUI_EVENT_QUEUE_SIZE;
    window->event_count--;

    irq_enable();
    return 0;
}
