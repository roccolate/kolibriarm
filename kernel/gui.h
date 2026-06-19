#ifndef KOLIBRIARM_KERNEL_GUI_H
#define KOLIBRIARM_KERNEL_GUI_H

#include <stdint.h>

#include "fb/fb.h"
#include "input/input.h"

#define GUI_MAX_WINDOWS      16U
#define GUI_NO_WINDOW        0xffffffffU
#define GUI_CURSOR_W         12
#define GUI_CURSOR_H         12
#define GUI_TITLE_LEN        32U
/* Kernel-drawn close button inside the title bar. The box is only
 * rendered (and only intercepts clicks) when the window has a
 * title_h large enough to fit it without crowding the title text. */
#define GUI_CLOSE_BTN_W      10U
#define GUI_CLOSE_BTN_PAD     2U
#define GUI_CLOSE_BTN_MIN_TITLE_H 10U
/* Default kernel-drawn title bar height when an app requests one. Fits
 * the 5x7 bitmap font (7 px glyph) plus 5 px of vertical padding. */
#define GUI_TITLE_BAR_H      12U
#define GUI_EVENT_QUEUE_SIZE 32U
#define GUI_NO_OWNER         0xffffffffU

#define GUI_EVENT_KEY_PRESS   1U
#define GUI_EVENT_KEY_RELEASE 2U
#define GUI_EVENT_MOUSE_CLICK 3U
#define GUI_EVENT_MOUSE_MOVE  4U
#define GUI_EVENT_RESIZE      5U
#define GUI_EVENT_CLOSE        6U

typedef struct {
    uint32_t type;
    int32_t  data1;
    int32_t  data2;
} gui_event_t;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
    uint32_t bg_color;
    uint32_t border_color;
    uint32_t owner_pid;
    /* Kernel-drawn title bar height in pixels. 0 means no title bar.
     * When set, the kernel paints a solid bar at the top of the window
     * and draws the title text inside it during gui_draw_window. Owner
     * drawing via SYS_WINDOW_DRAW_RECT/TEXT has its y coordinate shifted
     * down by title_h so apps keep a clean 0-based content coordinate
     * space below the bar. */
    uint32_t title_h;
    char title[GUI_TITLE_LEN];
    gui_event_t events[GUI_EVENT_QUEUE_SIZE];
    uint32_t event_head;
    uint32_t event_tail;
    uint32_t event_count;
    uint8_t used;
    /* Set when an EL0 owner has drawn into this window via
     * SYS_WINDOW_DRAW_RECT or SYS_WINDOW_DRAW_TEXT. When set, the kernel
     * compositor skips the bg_color fillrect on redraw so the owner's
     * drawing survives. The owner is responsible for painting its own
     * background the first time. */
    uint8_t owner_drawn;
} gui_window_t;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t prev_x;
    int32_t prev_y;
    uint8_t buttons_mask;
    uint8_t visible;
} gui_cursor_t;

/* Bitmask values for gui_cursor_t::buttons_mask. The same numbering is used
 * by input_event_t::mouse_button::button so the GUI does not need to know
 * about Linux input codes. */
#define GUI_BUTTON_LEFT   (1U << 0)
#define GUI_BUTTON_RIGHT  (1U << 1)
#define GUI_BUTTON_MIDDLE (1U << 2)

typedef struct {
    fb_t *fb;
    uint32_t background_color;
    uint32_t focused_window_id;
    /* Drag state. drag_window_id == GUI_NO_WINDOW when no drag is active.
     * drag_off_x/off_y is the cursor offset from the window's top-left at
     * the start of the drag; while dragging, gui_drag_update moves the
     * window to keep that offset constant under the cursor. */
    uint32_t drag_window_id;
    int32_t drag_off_x;
    int32_t drag_off_y;
    gui_window_t windows[GUI_MAX_WINDOWS];
    gui_cursor_t cursor;
} gui_desktop_t;

int gui_init(gui_desktop_t *desktop, fb_t *fb, uint32_t background_color);
int gui_create_window(gui_desktop_t *desktop, uint32_t x, uint32_t y,
                      uint32_t w, uint32_t h, uint32_t bg_color,
                      uint32_t border_color, uint32_t *window_id);
int gui_create_window_for_pid(gui_desktop_t *desktop, uint32_t owner_pid,
                              uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                              uint32_t bg_color, uint32_t border_color,
                              const char *title, uint32_t *window_id);
int gui_destroy_window(gui_desktop_t *desktop, uint32_t window_id);
int gui_set_window_title(gui_desktop_t *desktop, uint32_t window_id,
                         const char *title);
int gui_set_window_title_bar(gui_desktop_t *desktop, uint32_t window_id,
                             uint32_t title_h);
int gui_window_draw_text(gui_desktop_t *desktop, uint32_t window_id,
                         int32_t x, int32_t y, const char *text,
                         uint32_t color);
int gui_window_draw_rect(gui_desktop_t *desktop, uint32_t window_id,
                         int32_t x, int32_t y, uint32_t w, uint32_t h,
                         uint32_t color);
int gui_window_clear(gui_desktop_t *desktop, uint32_t window_id,
                     uint32_t color);
int gui_window_push_event(gui_window_t *window, uint32_t type,
                          int32_t data1, int32_t data2);
int gui_window_pop_event(gui_window_t *window, gui_event_t *out);
int gui_move_window(gui_desktop_t *desktop, uint32_t window_id, uint32_t x,
                    uint32_t y);
int gui_focus_window(gui_desktop_t *desktop, uint32_t window_id);
int gui_focus_window_ensure(gui_desktop_t *desktop);
/* Find the index-th used window owned by owner_pid. Returns the window
 * id on success or GUI_NO_WINDOW when no more windows exist for that
 * pid. Lets the desktop taskbar raise a window without knowing the
 * id in advance. */
uint32_t gui_window_for_pid(gui_desktop_t *desktop, uint32_t owner_pid,
                            uint32_t index);
int gui_hit_test(gui_desktop_t *desktop, int32_t x, int32_t y);
int gui_window_contains(gui_window_t *window, int32_t x, int32_t y);
int gui_dispatch_input(gui_desktop_t *desktop, const input_event_t *event);
void gui_get_cursor(gui_desktop_t *desktop, int32_t *x, int32_t *y);
void gui_set_cursor(gui_desktop_t *desktop, int32_t x, int32_t y);
void gui_cursor_button(gui_desktop_t *desktop, uint32_t button,
                       uint32_t pressed);
void gui_cursor_move(gui_desktop_t *desktop, int32_t dx, int32_t dy);
void gui_drag_start(gui_desktop_t *desktop, uint32_t window_id,
                    int32_t off_x, int32_t off_y);
void gui_drag_update(gui_desktop_t *desktop, int32_t cursor_x,
                     int32_t cursor_y);
void gui_drag_end(gui_desktop_t *desktop);
static inline int gui_drag_active(const gui_desktop_t *desktop) {
    return (desktop != 0 && desktop->drag_window_id != GUI_NO_WINDOW) ? 1 : 0;
}
void gui_draw(gui_desktop_t *desktop);

/* The kernel has exactly one GUI: a single desktop with a fixed pool of
 * windows, a single cursor, and a single dirty flag. These are the
 * entry points the kernel uses from the timer tick and the input/svc
 * paths; tests can also drive them directly. */
void gui_render(fb_t *fb, void *context);
void gui_init_for_framebuffer(fb_t *fb, void *context);
gui_desktop_t *gui_desktop(void);
int gui_handle_input(const input_event_t *event);
int gui_is_dirty(void);
void gui_clear_dirty(void);
void gui_request_redraw(void);

#endif

