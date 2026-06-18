#include "kernel/gui.h"

#include <stdint.h>

#include "kernel/font.h"
#include "uart/pl011.h"

/*
 * 12x12 arrow cursor.
 * 0 = transparent, 1 = black outline, 2 = white fill.
 */
static const uint8_t g_cursor[GUI_CURSOR_H][GUI_CURSOR_W] = {
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0},
    {1, 2, 2, 2, 1, 1, 1, 1, 1, 0, 0, 0},
    {1, 2, 1, 0, 1, 1, 1, 0, 0, 0, 0, 0},
    {0, 1, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0},
};

static void gui_draw_cursor(fb_t *fb, int32_t x, int32_t y) {
    for (uint32_t row = 0; row < GUI_CURSOR_H; row++) {
        for (uint32_t col = 0; col < GUI_CURSOR_W; col++) {
            uint8_t v = g_cursor[row][col];
            if (v == 0) {
                continue;
            }
            uint32_t color = (v == 1) ? 0xff101010U : 0xffe0e0e0U;
            fb_putpixel(fb, (uint32_t)(x + (int32_t)col),
                        (uint32_t)(y + (int32_t)row), color);
        }
    }
}

int gui_window_contains(gui_window_t *window, int32_t x, int32_t y) {
    if (window == 0 || window->used == 0) {
        return 0;
    }
    return x >= (int32_t)window->x && x < (int32_t)(window->x + window->w) &&
           y >= (int32_t)window->y && y < (int32_t)(window->y + window->h);
}

int gui_hit_test(gui_desktop_t *desktop, int32_t x, int32_t y) {
    if (desktop == 0) {
        return GUI_NO_WINDOW;
    }
    for (int32_t i = (int32_t)GUI_MAX_WINDOWS - 1; i >= 0; i--) {
        gui_window_t *window = &desktop->windows[i];
        if (gui_window_contains(window, x, y)) {
            return (int)i;
        }
    }
    return GUI_NO_WINDOW;
}

void gui_get_cursor(gui_desktop_t *desktop, int32_t *x, int32_t *y) {
    if (desktop == 0) {
        return;
    }
    if (x != 0) {
        *x = desktop->cursor.x;
    }
    if (y != 0) {
        *y = desktop->cursor.y;
    }
}

void gui_set_cursor(gui_desktop_t *desktop, int32_t x, int32_t y) {
    if (desktop == 0 || desktop->fb == 0) {
        return;
    }
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if ((uint32_t)x >= desktop->fb->width) {
        x = (int32_t)desktop->fb->width - 1;
    }
    if ((uint32_t)y >= desktop->fb->height) {
        y = (int32_t)desktop->fb->height - 1;
    }
    desktop->cursor.prev_x = desktop->cursor.x;
    desktop->cursor.prev_y = desktop->cursor.y;
    desktop->cursor.x = x;
    desktop->cursor.y = y;
}

void gui_cursor_move(gui_desktop_t *desktop, int32_t dx, int32_t dy) {
    if (desktop == 0) {
        return;
    }
    gui_set_cursor(desktop, desktop->cursor.x + dx, desktop->cursor.y + dy);
}

void gui_drag_start(gui_desktop_t *desktop, uint32_t window_id,
                    int32_t off_x, int32_t off_y) {
    if (desktop == 0 || window_id >= GUI_MAX_WINDOWS ||
        desktop->windows[window_id].used == 0) {
        return;
    }
    desktop->drag_window_id = window_id;
    desktop->drag_off_x = off_x;
    desktop->drag_off_y = off_y;
}

void gui_drag_update(gui_desktop_t *desktop, int32_t cursor_x,
                     int32_t cursor_y) {
    uint32_t id;
    gui_window_t *window;
    int32_t new_x;
    int32_t new_y;

    if (desktop == 0 || desktop->drag_window_id == GUI_NO_WINDOW) {
        return;
    }
    id = desktop->drag_window_id;
    window = &desktop->windows[id];
    if (window->used == 0) {
        desktop->drag_window_id = GUI_NO_WINDOW;
        return;
    }
    new_x = cursor_x - desktop->drag_off_x;
    new_y = cursor_y - desktop->drag_off_y;
    /* Clamp only the negative side. Letting the window run off the right
     * or bottom edge during a drag is harmless: the desktop redraw simply
     * skips out-of-bounds pixels. */
    if (new_x < 0) {
        new_x = 0;
    }
    if (new_y < 0) {
        new_y = 0;
    }
    window->x = (uint32_t)new_x;
    window->y = (uint32_t)new_y;
}

void gui_drag_end(gui_desktop_t *desktop) {
    if (desktop == 0) {
        return;
    }
    desktop->drag_window_id = GUI_NO_WINDOW;
}

void gui_cursor_button(gui_desktop_t *desktop, uint32_t button,
                       uint32_t pressed) {
    if (desktop == 0 || button > 2U) {
        return;
    }
    uint8_t mask = (uint8_t)(1U << button);
    if (pressed != 0) {
        desktop->cursor.buttons_mask = (uint8_t)(desktop->cursor.buttons_mask |
                                                 mask);
    } else {
        desktop->cursor.buttons_mask =
            (uint8_t)(desktop->cursor.buttons_mask & (uint8_t)(~mask));
    }
    /* Only the left button raises the window under the cursor. Right and
     * middle clicks are recorded in the mask for future use but do not
     * change focus today. */
    if (button == 0U && pressed != 0) {
        int32_t hit = gui_hit_test(desktop, desktop->cursor.x,
                                   desktop->cursor.y);
        if ((int32_t)GUI_NO_WINDOW != hit && hit >= 0) {
            gui_focus_window(desktop, (uint32_t)hit);
        }
    }
}

int gui_init(gui_desktop_t *desktop, fb_t *fb, uint32_t background_color) {
    if (desktop == 0 || fb == 0 || fb->pixels == 0) {
        return -1;
    }

    desktop->fb = fb;
    desktop->background_color = background_color;
    desktop->focused_window_id = GUI_NO_WINDOW;
    desktop->drag_window_id = GUI_NO_WINDOW;
    desktop->drag_off_x = 0;
    desktop->drag_off_y = 0;
    desktop->cursor.x = (int32_t)(fb->width / 2U);
    desktop->cursor.y = (int32_t)(fb->height / 2U);
    desktop->cursor.prev_x = desktop->cursor.x;
    desktop->cursor.prev_y = desktop->cursor.y;
    desktop->cursor.buttons_mask = 0;
    desktop->cursor.visible = 1;
    for (uint32_t i = 0; i < GUI_MAX_WINDOWS; i++) {
        desktop->windows[i].x = 0;
        desktop->windows[i].y = 0;
        desktop->windows[i].w = 0;
        desktop->windows[i].h = 0;
        desktop->windows[i].bg_color = 0;
        desktop->windows[i].border_color = 0;
        desktop->windows[i].key_count = 0;
        desktop->windows[i].last_key = '\0';
        desktop->windows[i].owner_pid = GUI_NO_OWNER;
        desktop->windows[i].event_head = 0;
        desktop->windows[i].event_tail = 0;
        desktop->windows[i].event_count = 0;
        for (uint32_t j = 0; j < GUI_TITLE_LEN; j++) {
            desktop->windows[i].title[j] = '\0';
        }
        desktop->windows[i].used = 0;
    }

    return 0;
}

int gui_create_window(gui_desktop_t *desktop, uint32_t x, uint32_t y,
                      uint32_t w, uint32_t h, uint32_t bg_color,
                      uint32_t border_color, uint32_t *window_id) {
    return gui_create_window_for_pid(desktop, GUI_NO_OWNER, x, y, w, h,
                                     bg_color, border_color, 0, window_id);
}

int gui_create_window_for_pid(gui_desktop_t *desktop, uint32_t owner_pid,
                              uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                              uint32_t bg_color, uint32_t border_color,
                              const char *title, uint32_t *window_id) {
    if (desktop == 0 || desktop->fb == 0 || w < 2U || h < 2U ||
        x >= desktop->fb->width || y >= desktop->fb->height) {
        return -1;
    }

    for (uint32_t i = 0; i < GUI_MAX_WINDOWS; i++) {
        gui_window_t *window = &desktop->windows[i];

        if (window->used == 0) {
            window->x = x;
            window->y = y;
            window->w = w;
            window->h = h;
            window->bg_color = bg_color;
            window->border_color = border_color;
            window->key_count = 0;
            window->last_key = '\0';
            window->owner_pid = owner_pid;
            window->event_head = 0;
            window->event_tail = 0;
            window->event_count = 0;
            for (uint32_t j = 0; j < GUI_TITLE_LEN; j++) {
                window->title[j] = '\0';
            }
            if (title != 0) {
                for (uint32_t j = 0; j + 1U < GUI_TITLE_LEN; j++) {
                    if (title[j] == '\0') {
                        break;
                    }
                    window->title[j] = title[j];
                }
            }
            window->used = 1;
            if (desktop->focused_window_id == GUI_NO_WINDOW) {
                desktop->focused_window_id = i;
            }
            if (window_id != 0) {
                *window_id = i;
            }
            return 0;
        }
    }

    return -1;
}

int gui_destroy_window(gui_desktop_t *desktop, uint32_t window_id) {
    if (desktop == 0 || window_id >= GUI_MAX_WINDOWS ||
        desktop->windows[window_id].used == 0) {
        return -1;
    }
    gui_window_t *window = &desktop->windows[window_id];
    window->x = 0;
    window->y = 0;
    window->w = 0;
    window->h = 0;
    window->bg_color = 0;
    window->border_color = 0;
    window->key_count = 0;
    window->last_key = '\0';
    window->owner_pid = GUI_NO_OWNER;
    window->event_head = 0;
    window->event_tail = 0;
    window->event_count = 0;
    window->used = 0;
    for (uint32_t j = 0; j < GUI_TITLE_LEN; j++) {
        window->title[j] = '\0';
    }
    if (desktop->focused_window_id == window_id) {
        desktop->focused_window_id = GUI_NO_WINDOW;
        for (uint32_t i = 0; i < GUI_MAX_WINDOWS; i++) {
            if (desktop->windows[i].used != 0) {
                desktop->focused_window_id = i;
                break;
            }
        }
    }
    return 0;
}

int gui_set_window_title(gui_desktop_t *desktop, uint32_t window_id,
                         const char *title) {
    if (desktop == 0 || window_id >= GUI_MAX_WINDOWS ||
        desktop->windows[window_id].used == 0) {
        return -1;
    }
    gui_window_t *window = &desktop->windows[window_id];
    for (uint32_t j = 0; j < GUI_TITLE_LEN; j++) {
        window->title[j] = '\0';
    }
    if (title == 0) {
        return 0;
    }
    for (uint32_t j = 0; j + 1U < GUI_TITLE_LEN; j++) {
        if (title[j] == '\0') {
            break;
        }
        window->title[j] = title[j];
    }
    return 0;
}

int gui_window_draw_text(gui_desktop_t *desktop, uint32_t window_id,
                         int32_t x, int32_t y, const char *text,
                         uint32_t color) {
    if (desktop == 0 || window_id >= GUI_MAX_WINDOWS ||
        desktop->windows[window_id].used == 0) {
        return -1;
    }
    gui_window_t *window = &desktop->windows[window_id];
    if (text == 0) {
        return -1;
    }
    /* Clip to window bounds */
    int32_t clip_x0 = 0, clip_y0 = 0;
    int32_t clip_x1 = (int32_t)window->w;
    int32_t clip_y1 = (int32_t)window->h;
    if (x < clip_x0) {
        x = clip_x0;
    }
    if (y < clip_y0) {
        y = clip_y0;
    }
    if (x >= clip_x1 || y >= clip_y1) {
        return 0;
    }
    /* We draw on the global framebuffer with the window's offset. */
    uint32_t abs_x = window->x + (uint32_t)x;
    uint32_t abs_y = window->y + (uint32_t)y;
    if (abs_x >= desktop->fb->width || abs_y >= desktop->fb->height) {
        return 0;
    }
    /* font_draw_text doesn't clip; we have to draw into the fb manually
     * by leveraging the existing font primitive. We save/restore the fb
     * and let the global fb be the destination.
     */
    (void)clip_x1;
    (void)clip_y1;
    font_draw_text(desktop->fb, abs_x, abs_y, text, color);
    return 0;
}

int gui_window_draw_rect(gui_desktop_t *desktop, uint32_t window_id,
                         int32_t x, int32_t y, uint32_t w, uint32_t h,
                         uint32_t color) {
    if (desktop == 0 || window_id >= GUI_MAX_WINDOWS ||
        desktop->windows[window_id].used == 0) {
        return -1;
    }
    gui_window_t *window = &desktop->windows[window_id];
    if (w == 0 || h == 0) {
        return 0;
    }
    int32_t wx0 = (int32_t)window->x;
    int32_t wy0 = (int32_t)window->y;
    int32_t wx1 = wx0 + (int32_t)window->w;
    int32_t wy1 = wy0 + (int32_t)window->h;
    int32_t x0 = wx0 + x;
    int32_t y0 = wy0 + y;
    int32_t x1 = x0 + (int32_t)w;
    int32_t y1 = y0 + (int32_t)h;
    if (x0 < wx0) {
        x0 = wx0;
    }
    if (y0 < wy0) {
        y0 = wy0;
    }
    if (x1 > wx1) {
        x1 = wx1;
    }
    if (y1 > wy1) {
        y1 = wy1;
    }
    if (x1 <= x0 || y1 <= y0) {
        return 0;
    }
    fb_fillrect(desktop->fb, (uint32_t)x0, (uint32_t)y0,
                (uint32_t)(x1 - x0), (uint32_t)(y1 - y0), color);
    return 0;
}

int gui_window_clear(gui_desktop_t *desktop, uint32_t window_id,
                     uint32_t color) {
    if (desktop == 0 || window_id >= GUI_MAX_WINDOWS ||
        desktop->windows[window_id].used == 0) {
        return -1;
    }
    gui_window_t *window = &desktop->windows[window_id];
    fb_fillrect(desktop->fb, window->x + 1U, window->y + 1U,
                window->w - 2U, window->h - 2U, color);
    return 0;
}

int gui_window_push_event(gui_window_t *window, uint32_t type,
                          int32_t data1, int32_t data2) {
    if (window == 0 || window->used == 0) {
        return -1;
    }
    if (window->event_count >= GUI_EVENT_QUEUE_SIZE) {
        return -1;
    }
    window->events[window->event_tail].type = type;
    window->events[window->event_tail].data1 = data1;
    window->events[window->event_tail].data2 = data2;
    window->event_tail = (window->event_tail + 1U) % GUI_EVENT_QUEUE_SIZE;
    window->event_count++;
    return 0;
}

int gui_window_pop_event(gui_window_t *window, gui_event_t *out) {
    if (window == 0 || window->used == 0 || out == 0) {
        return -1;
    }
    if (window->event_count == 0) {
        return -1;
    }
    *out = window->events[window->event_head];
    window->event_head = (window->event_head + 1U) % GUI_EVENT_QUEUE_SIZE;
    window->event_count--;
    return 0;
}

int gui_dispatch_input(gui_desktop_t *desktop, const input_event_t *event) {
    if (desktop == 0 || event == 0) {
        return -1;
    }
    switch (event->type) {
    case INPUT_EVENT_KEY_PRESS:
    case INPUT_EVENT_KEY_RELEASE: {
        /* Deliver to focused window's owner, or to all windows if none
         * is focused. */
        int delivered = 0;
        if (desktop->focused_window_id != GUI_NO_WINDOW) {
            gui_window_t *window =
                &desktop->windows[desktop->focused_window_id];
            if (gui_window_push_event(window,
                                      event->type == INPUT_EVENT_KEY_PRESS
                                          ? GUI_EVENT_KEY_PRESS
                                          : GUI_EVENT_KEY_RELEASE,
                                      (int32_t)event->data.key.key,
                                      0) == 0) {
                delivered = 1;
            }
        }
        if (!delivered) {
            for (uint32_t i = 0; i < GUI_MAX_WINDOWS; i++) {
                if (desktop->windows[i].used == 0) {
                    continue;
                }
                if (gui_window_push_event(&desktop->windows[i],
                                          event->type == INPUT_EVENT_KEY_PRESS
                                              ? GUI_EVENT_KEY_PRESS
                                              : GUI_EVENT_KEY_RELEASE,
                                          (int32_t)event->data.key.key,
                                          0) == 0) {
                    delivered = 1;
                    break;
                }
            }
        }
        return delivered ? 0 : -1;
    }
    case INPUT_EVENT_MOUSE_MOVE: {
        /* Push to the topmost window under the cursor. */
        int32_t hit = gui_hit_test(desktop, desktop->cursor.x,
                                   desktop->cursor.y);
        if (hit == (int32_t)GUI_NO_WINDOW || hit < 0) {
            return 0;
        }
        gui_window_t *window = &desktop->windows[hit];
        return gui_window_push_event(window, GUI_EVENT_MOUSE_MOVE,
                                    desktop->cursor.x, desktop->cursor.y);
    }
    default:
        return -1;
    }
}

int gui_move_window(gui_desktop_t *desktop, uint32_t window_id, uint32_t x,
                    uint32_t y) {
    gui_window_t *window;

    if (desktop == 0 || desktop->fb == 0 || window_id >= GUI_MAX_WINDOWS ||
        x >= desktop->fb->width || y >= desktop->fb->height ||
        desktop->windows[window_id].used == 0) {
        return -1;
    }

    window = &desktop->windows[window_id];
    window->x = x;
    window->y = y;
    return 0;
}

int gui_focus_window(gui_desktop_t *desktop, uint32_t window_id) {
    if (desktop == 0 || window_id >= GUI_MAX_WINDOWS ||
        desktop->windows[window_id].used == 0) {
        return -1;
    }

    desktop->focused_window_id = window_id;
    return 0;
}

int gui_focus_window_ensure(gui_desktop_t *desktop) {
    if (desktop == 0) {
        return -1;
    }
    if (desktop->focused_window_id != GUI_NO_WINDOW &&
        desktop->focused_window_id < GUI_MAX_WINDOWS &&
        desktop->windows[desktop->focused_window_id].used != 0) {
        return 0;
    }
    for (uint32_t i = 0; i < GUI_MAX_WINDOWS; i++) {
        if (desktop->windows[i].used != 0) {
            desktop->focused_window_id = i;
            return 0;
        }
    }
    return -1;
}

int gui_send_key(gui_desktop_t *desktop, char key) {
    gui_window_t *window;

    if (desktop == 0 || desktop->focused_window_id >= GUI_MAX_WINDOWS ||
        desktop->windows[desktop->focused_window_id].used == 0) {
        return -1;
    }

    window = &desktop->windows[desktop->focused_window_id];
    window->last_key = key;
    window->key_count++;
    return 0;
}

static void gui_draw_window(fb_t *fb, const gui_desktop_t *desktop,
                            uint32_t index, const gui_window_t *window) {
    uint32_t border;

    if (fb == 0 || window == 0 || window->used == 0) {
        return;
    }

    /* Focus visualization: bright border on the focused window so the
     * user can see which one keyboard input would target. */
    border = window->border_color;
    if (desktop != 0 && desktop->focused_window_id == index) {
        border = 0xffe0e8f0U;
    }

    fb_fillrect(fb, window->x, window->y, window->w, window->h, border);
    if (window->w > 2U && window->h > 2U) {
        fb_fillrect(fb, window->x + 1U, window->y + 1U,
                    window->w - 2U, window->h - 2U, window->bg_color);
    }
}

/* Linear interpolate two ARGB colors: at t=0 returns top, at t=denom-1
 * returns bottom. Uses 8-bit fixed-point per channel. */
static uint32_t gui_blend_color(uint32_t top, uint32_t bottom,
                               uint32_t num, uint32_t denom) {
    uint32_t tr = (top >> 16) & 0xffU;
    uint32_t tg = (top >> 8) & 0xffU;
    uint32_t tb = top & 0xffU;
    uint32_t br = (bottom >> 16) & 0xffU;
    uint32_t bg = (bottom >> 8) & 0xffU;
    uint32_t bb = bottom & 0xffU;
    if (denom == 0) {
        return top;
    }
    if (num >= denom) {
        return (top & 0xff000000U) | (br << 16) | (bg << 8) | bb;
    }
    uint32_t r = (uint32_t)((int32_t)tr +
                            (((int32_t)br - (int32_t)tr) *
                             (int32_t)num) / (int32_t)denom);
    uint32_t g = (uint32_t)((int32_t)tg +
                            (((int32_t)bg - (int32_t)tg) *
                             (int32_t)num) / (int32_t)denom);
    uint32_t b = (uint32_t)((int32_t)tb +
                            (((int32_t)bb - (int32_t)tb) *
                             (int32_t)num) / (int32_t)denom);
    return (top & 0xff000000U) | (r << 16) | (g << 8) | b;
}

void gui_draw(gui_desktop_t *desktop) {
    uint32_t bottom_color;
    uint32_t height;

    if (desktop == 0 || desktop->fb == 0) {
        return;
    }

    /* Vertical gradient: top is background_color, bottom is a darker
     * shade for visual depth. Cheap enough on QEMU at 640x480 because we
     * issue one fillrect per row. */
    height = desktop->fb->height;
    bottom_color = gui_blend_color(desktop->background_color, 0xff000000U,
                                   3U, 4U);
    for (uint32_t row = 0; row < height; row++) {
        uint32_t color = gui_blend_color(desktop->background_color,
                                         bottom_color, row, height - 1U);
        fb_fillrect(desktop->fb, 0, row, desktop->fb->width, 1U, color);
    }

    for (uint32_t i = 0; i < GUI_MAX_WINDOWS; i++) {
        gui_draw_window(desktop->fb, desktop, i, &desktop->windows[i]);
    }
    if (desktop->cursor.visible) {
        gui_draw_cursor(desktop->fb, desktop->cursor.x, desktop->cursor.y);
    }
}

static fb_t g_demo_fb;
static gui_desktop_t g_demo_desktop;
static uint8_t g_demo_active;
static uint8_t g_demo_dirty;

void gui_draw_render(fb_t *fb, void *context) {
    (void)context;
    if (g_demo_active == 0) {
        return;
    }
    g_demo_fb = *fb;
    g_demo_desktop.fb = &g_demo_fb;
    gui_draw(&g_demo_desktop);
}

gui_desktop_t *gui_demo_desktop(void) {
    if (g_demo_active == 0) {
        return 0;
    }
    return &g_demo_desktop;
}

int gui_demo_is_dirty(void) {
    return g_demo_dirty != 0 ? 1 : 0;
}

void gui_demo_clear_dirty(void) {
    g_demo_dirty = 0;
}

void gui_demo_request_redraw(void) {
    g_demo_dirty = 1;
}

#define GUI_BTN_LEFT  0x110U
#define GUI_BTN_RIGHT 0x111U
/* The above two macros are retained as documentation of the Linux codes
 * the virtio-input driver maps to INPUT_EVENT_MOUSE_BUTTON. The GUI layer
 * no longer inspects KEY_PRESS codes for buttons. */

int gui_demo_handle_input(const input_event_t *event) {
    if (g_demo_active == 0 || event == 0) {
        return -1;
    }

    /* Mouse buttons and motion also affect the cursor itself. */
    switch (event->type) {
    case INPUT_EVENT_MOUSE_MOVE:
        gui_cursor_move(&g_demo_desktop, event->data.mouse_move.dx,
                         event->data.mouse_move.dy);
        if (g_demo_desktop.drag_window_id != GUI_NO_WINDOW) {
            gui_drag_update(&g_demo_desktop, g_demo_desktop.cursor.x,
                            g_demo_desktop.cursor.y);
        }
        gui_dispatch_input(&g_demo_desktop, event);
        g_demo_dirty = 1;
        return 0;
    case INPUT_EVENT_MOUSE_BUTTON:
        gui_cursor_button(&g_demo_desktop,
                          event->data.mouse_button.button,
                          event->data.mouse_button.pressed);
        if (event->data.mouse_button.button == 0U) {
            if (event->data.mouse_button.pressed != 0U) {
                /* Left press: focus is already set by gui_cursor_button.
                 * Start a drag and deliver a click on the topmost window. */
                int32_t hit = gui_hit_test(&g_demo_desktop,
                                           g_demo_desktop.cursor.x,
                                           g_demo_desktop.cursor.y);
                if (hit != (int32_t)GUI_NO_WINDOW && hit >= 0) {
                    gui_window_t *window =
                        &g_demo_desktop.windows[hit];
                    gui_drag_start(&g_demo_desktop, (uint32_t)hit,
                                   g_demo_desktop.cursor.x -
                                       (int32_t)window->x,
                                   g_demo_desktop.cursor.y -
                                       (int32_t)window->y);
                    gui_window_push_event(window, GUI_EVENT_MOUSE_CLICK,
                                          g_demo_desktop.cursor.x,
                                          g_demo_desktop.cursor.y);
                }
            }
            if (event->data.mouse_button.pressed == 0U) {
                gui_drag_end(&g_demo_desktop);
            }
        }
        g_demo_dirty = 1;
        return 0;
    case INPUT_EVENT_KEY_PRESS:
        /* Non-button key: route to focused window as KEY_PRESS. */
        gui_focus_window_ensure(&g_demo_desktop);
        if (g_demo_desktop.focused_window_id != GUI_NO_WINDOW) {
            gui_window_t *window =
                &g_demo_desktop.windows[g_demo_desktop.focused_window_id];
            gui_window_push_event(window, GUI_EVENT_KEY_PRESS,
                                  (int32_t)event->data.key.key, 0);
        }
        g_demo_dirty = 1;
        return 0;
    case INPUT_EVENT_KEY_RELEASE:
        if (g_demo_desktop.focused_window_id != GUI_NO_WINDOW) {
            gui_window_t *window =
                &g_demo_desktop.windows[g_demo_desktop.focused_window_id];
            gui_window_push_event(window, GUI_EVENT_KEY_RELEASE,
                                  (int32_t)event->data.key.key, 0);
        }
        g_demo_dirty = 1;
        return 0;
    default:
        return -1;
    }
}

void gui_draw_demo(fb_t *fb, void *context) {
    uint32_t first = GUI_NO_WINDOW;
    uint32_t second = GUI_NO_WINDOW;

    (void)context;
    g_demo_active = 0;
    g_demo_dirty = 0;

    if (fb == 0) {
        return;
    }

    g_demo_fb = *fb;
    if (gui_init(&g_demo_desktop, &g_demo_fb, 0xff202428U) != 0) {
        return;
    }

    (void)gui_create_window(&g_demo_desktop, 72, 64, 320, 220, 0xff2f6fedU,
                            0xffd8e4ffU, &first);
    (void)gui_create_window(&g_demo_desktop, 220, 150, 340, 230, 0xff38a169U,
                            0xfffff4c2U, &second);
    (void)gui_focus_window(&g_demo_desktop, second);
    gui_draw(&g_demo_desktop);
    font_draw_text(fb, 94, 86, "KOLIBRI ARM", 0xffffffffU);
    font_draw_text(fb, 242, 172, "FAT32 IPC", 0xff101010U);
    (void)first;
    g_demo_active = 1;
}

int gui_demo_send_key(char key) {
    if (g_demo_active == 0) {
        return -1;
    }

    return gui_send_key(&g_demo_desktop, key);
}
