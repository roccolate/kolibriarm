#include <stdint.h>

#include "kernel/gui.h"

void gui_drag_start(gui_desktop_t *desktop, uint32_t window_id,
                    int32_t off_x, int32_t off_y) {
    gui_window_t *window;

    if (desktop == 0 || window_id >= GUI_MAX_WINDOWS ||
        desktop->windows[window_id].used == 0) {
        return;
    }

    window = &desktop->windows[window_id];

    /* Dock/panel-style windows have no kernel title bar and should not
     * behave like draggable application windows. Titled windows drag only
     * from their title bar, matching normal desktop window-manager policy. */
    if (window->title_h == 0U || off_y < 0 ||
        (uint32_t)off_y >= window->title_h) {
        return;
    }

    desktop->drag_window_id = window_id;
    desktop->drag_off_x = off_x;
    desktop->drag_off_y = off_y;
}
