#include "kernel/gui.h"

#include <stdint.h>

#include "kernel/font.h"
#include "kernel/mm/kheap.h"
#include "kernel/process.h"
#include "uart/pl011.h"

/*
 * The kernel has exactly one GUI desktop. The static globals below hold
 * its state so the rest of the kernel can call into the GUI without
 * threading a context pointer through every path. They are defined here
 * (above every function that uses them) so the damage-tracking helpers
 * in gui_request_redraw can read g_gui_dirty without a forward decl.
 */
static fb_t g_gui_fb;
static gui_desktop_t g_gui_desktop;
static uint8_t g_gui_active;
static uint8_t g_gui_dirty;

/*
 * 16x16 arrow cursor.
 * 0 = transparent, 1 = black outline, 2 = white fill.
 */
static const uint8_t g_cursor[GUI_CURSOR_H][GUI_CURSOR_W] = {
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0},
    {1, 2, 1, 0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0},
    {1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0},
};

/*
 * 16x16 hand cursor for clickable window decorations.
 * 0 = transparent, 1 = black outline, 2 = white fill.
 */
static const uint8_t g_cursor_hand[GUI_CURSOR_H][GUI_CURSOR_W] = {
    {0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 1, 2, 2, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 1, 2, 2, 1, 1, 2, 2, 1, 0, 0, 0, 0, 0},
    {0, 0, 0, 1, 2, 2, 1, 2, 2, 2, 1, 1, 0, 0, 0, 0},
    {0, 0, 0, 1, 2, 2, 1, 2, 2, 2, 1, 2, 1, 0, 0, 0},
    {0, 0, 1, 1, 2, 2, 1, 2, 2, 2, 1, 2, 1, 0, 0, 0},
    {0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 1, 2, 1, 0, 0, 0},
    {0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0},
    {0, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0},
    {0, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0},
    {0, 0, 0, 1, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0},
    {0, 0, 0, 1, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 1, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0},
};

static int gui_close_box_rect(const gui_window_t *window, uint32_t *out_x,
                              uint32_t *out_y, uint32_t *out_w,
                              uint32_t *out_h);
static void gui_refresh_cursor_shape(gui_desktop_t *desktop);

static void gui_draw_cursor(fb_t *fb, int32_t x, int32_t y, uint8_t shape) {
    const uint8_t (*bitmap)[GUI_CURSOR_W] =
        (shape == GUI_CURSOR_HAND) ? g_cursor_hand : g_cursor;

    for (uint32_t row = 0; row < GUI_CURSOR_H; row++) {
        for (uint32_t col = 0; col < GUI_CURSOR_W; col++) {
            uint8_t v = bitmap[row][col];
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
    uint32_t best_z = 0;
    int best = (int)GUI_NO_WINDOW;

    if (desktop == 0) {
        return GUI_NO_WINDOW;
    }
    for (uint32_t i = 0; i < GUI_MAX_WINDOWS; i++) {
        gui_window_t *window = &desktop->windows[i];
        if (gui_window_contains(window, x, y)) {
            if (best == (int)GUI_NO_WINDOW || window->z >= best_z) {
                best = (int)i;
                best_z = window->z;
            }
        }
    }
    return best;
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

int gui_set_cursor_shape(gui_desktop_t *desktop, uint32_t shape) {
    if (desktop == 0 ||
        (shape != GUI_CURSOR_ARROW && shape != GUI_CURSOR_HAND)) {
        return -1;
    }

    desktop->cursor.shape = (uint8_t)shape;
    return 0;
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
    int32_t prev_x = desktop->cursor.x;
    int32_t prev_y = desktop->cursor.y;
    desktop->cursor.prev_x = prev_x;
    desktop->cursor.prev_y = prev_y;
    desktop->cursor.x = x;
    desktop->cursor.y = y;
    gui_refresh_cursor_shape(desktop);
    /* Damage the union of the old and new cursor rectangles so the
     * compositor redraws both the previous and current positions. The
     * 16x16 cursor bitmap is small; one merged rect is enough. */
    int32_t ux0 = prev_x < x ? prev_x : x;
    int32_t uy0 = prev_y < y ? prev_y : y;
    int32_t ux1 = (prev_x + (int32_t)GUI_CURSOR_W) > (x + (int32_t)GUI_CURSOR_W)
                      ? (prev_x + (int32_t)GUI_CURSOR_W)
                      : (x + (int32_t)GUI_CURSOR_W);
    int32_t uy1 = (prev_y + (int32_t)GUI_CURSOR_H) > (y + (int32_t)GUI_CURSOR_H)
                      ? (prev_y + (int32_t)GUI_CURSOR_H)
                      : (y + (int32_t)GUI_CURSOR_H);
    gui_damage_add(desktop, ux0, uy0, ux1 - ux0, uy1 - uy0);
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
    int32_t old_x, old_y, old_w, old_h;

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
    old_x = (int32_t)window->x;
    old_y = (int32_t)window->y;
    old_w = (int32_t)window->w;
    old_h = (int32_t)window->h;
    window->x = (uint32_t)new_x;
    window->y = (uint32_t)new_y;
    gui_refresh_cursor_shape(desktop);
    /* Drag: damage the union of the old and new window rects so the
     * gradient and any windows underneath repaint at both positions. */
    int32_t new_x1 = new_x + old_w;
    int32_t new_y1 = new_y + old_h;
    int32_t old_x1 = old_x + old_w;
    int32_t old_y1 = old_y + old_h;
    int32_t ux0 = old_x < new_x ? old_x : new_x;
    int32_t uy0 = old_y < new_y ? old_y : new_y;
    int32_t ux1 = old_x1 > new_x1 ? old_x1 : new_x1;
    int32_t uy1 = old_y1 > new_y1 ? old_y1 : new_y1;
    gui_damage_add(desktop, ux0, uy0, ux1 - ux0, uy1 - uy0);
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
    /* Only the left button affects focus. A left click on a window raises
     * it; a left click on the desktop clears focus. Right and middle clicks
     * are recorded in the mask for future use but do not change focus. */
    if (button == 0U && pressed != 0) {
        int32_t hit = gui_hit_test(desktop, desktop->cursor.x,
                                   desktop->cursor.y);
        if ((int32_t)GUI_NO_WINDOW != hit && hit >= 0) {
            gui_focus_window(desktop, (uint32_t)hit);
        } else {
            desktop->focused_window_id = GUI_NO_WINDOW;
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
    desktop->next_z = 1;
    desktop->drag_window_id = GUI_NO_WINDOW;
    desktop->drag_off_x = 0;
    desktop->drag_off_y = 0;
    desktop->cursor.x = (int32_t)(fb->width / 2U);
    desktop->cursor.y = (int32_t)(fb->height / 2U);
    desktop->cursor.prev_x = desktop->cursor.x;
    desktop->cursor.prev_y = desktop->cursor.y;
    desktop->cursor.buttons_mask = 0;
    desktop->cursor.visible = 1;
    desktop->cursor.shape = GUI_CURSOR_ARROW;
    desktop->damage_count = 0;
    desktop->damage_full = 0;
    for (uint32_t i = 0; i < GUI_MAX_WINDOWS; i++) {
        desktop->windows[i].x = 0;
        desktop->windows[i].y = 0;
        desktop->windows[i].w = 0;
        desktop->windows[i].h = 0;
        desktop->windows[i].bg_color = 0;
        desktop->windows[i].border_color = 0;
        desktop->windows[i].owner_pid = GUI_NO_OWNER;
        desktop->windows[i].z = 0;
        desktop->windows[i].title_h = 0;
        desktop->windows[i].event_head = 0;
        desktop->windows[i].event_tail = 0;
        desktop->windows[i].event_count = 0;
        for (uint32_t j = 0; j < GUI_TITLE_LEN; j++) {
            desktop->windows[i].title[j] = '\0';
        }
        desktop->windows[i].used = 0;
        desktop->windows[i].owner_drawn = 0;
        desktop->windows[i].backing = 0;
        desktop->windows[i].backing_capacity = 0;
    }

    return 0;
}

/*
 * backing_fb_for: synthesize an fb_t that points at the window's
 * backing buffer in content-local coordinates (0,0 = top-left of the
 * content area below the title bar). Used as a transient destination
 * by the existing font and fb primitives so app drawing writes into
 * the backing buffer instead of the live framebuffer.
 */
static fb_t backing_fb_for(const gui_window_t *window) {
    fb_t fb;
    uint32_t content_h = window->h > window->title_h
                             ? window->h - window->title_h
                             : 0U;
    fb.pixels = window->backing;
    fb.width = window->w;
    fb.height = content_h;
    fb.stride_pixels = window->w;
    return fb;
}

/*
 * gui_window_ensure_backing: lazily allocate the per-window content
 * backing buffer on the first owner draw. The backing covers only the
 * content area (excluding the kernel-drawn title bar) and is sized
 * w * (h - title_h) BGRA pixels. Returns 0 on success, -1 if the
 * allocator refuses or the window has no owner.
 */
int gui_window_ensure_backing(gui_window_t *window) {
    uint32_t content_h;
    uint32_t needed_bytes;

    if (window == 0 || window->w == 0 || window->h == 0) {
        return -1;
    }

    content_h = window->h > window->title_h
                    ? window->h - window->title_h
                    : 0U;
    if (content_h == 0U) {
        return -1;
    }

    needed_bytes = window->w * content_h * sizeof(uint32_t);
    if (window->backing != 0 && window->backing_capacity >= needed_bytes) {
        return 0;
    }

    if (window->backing != 0) {
        /*
         * Re-allocation only happens if the new size would exceed
         * the previous capacity; window size is fixed at creation so
         * this branch is defensive and should not normally fire.
         */
        window->backing = 0;
        window->backing_capacity = 0;
    }

    /*
     * Try the kernel heap first; if that fails, fall back to a static
     * pool so a single oversized window does not kill the rest of
     * the desktop. The static pool is small (one window's worth) so
     * a backing shortage is recoverable.
     */
    window->backing = (uint32_t *)kmalloc((unsigned long)needed_bytes);
    if (window->backing == 0) {
        return -1;
    }
    window->backing_capacity = needed_bytes;

    /*
     * Initialise the backing to the window's bg_color so the first
     * blit does not flash through whatever the heap happened to
     * contain before. Apps that want a transparent background can
     * overwrite it with their own clear rect.
     */
    fb_t fb = backing_fb_for(window);
    fb_fillrect(&fb, 0, 0, window->w, content_h, window->bg_color);

    return 0;
}

void gui_window_free_backing(gui_window_t *window) {
    if (window == 0 || window->backing == 0) {
        return;
    }
    kfree(window->backing);
    window->backing = 0;
    window->backing_capacity = 0;
    window->owner_drawn = 0;
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
            window->owner_pid = owner_pid;
            window->z = desktop->next_z++;
            window->title_h = 0;
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
            window->owner_drawn = 0;
            if (desktop->focused_window_id == GUI_NO_WINDOW) {
                desktop->focused_window_id = i;
            }
            if (window_id != 0) {
                *window_id = i;
            }
            gui_refresh_cursor_shape(desktop);
            /* New window appears: invalidate its full rect (title bar
             * plus content) so the next compositor pass paints it. */
            gui_damage_add(desktop, (int32_t)window->x, (int32_t)window->y,
                           (int32_t)window->w, (int32_t)window->h);
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
    /* Capture the bounding rect before zeroing the window so the
     * compositor repaints the region the window occupied. */
    int32_t dx = (int32_t)window->x;
    int32_t dy = (int32_t)window->y;
    int32_t dw = (int32_t)window->w;
    int32_t dh = (int32_t)window->h;
    gui_window_free_backing(window);
    window->x = 0;
    window->y = 0;
    window->w = 0;
    window->h = 0;
    window->bg_color = 0;
    window->border_color = 0;
    window->owner_pid = GUI_NO_OWNER;
    window->z = 0;
    window->event_head = 0;
    window->event_tail = 0;
    window->event_count = 0;
    window->used = 0;
    for (uint32_t j = 0; j < GUI_TITLE_LEN; j++) {
        window->title[j] = '\0';
    }
    if (desktop->focused_window_id == window_id) {
        desktop->focused_window_id = GUI_NO_WINDOW;
        uint32_t best_z = 0;
        for (uint32_t i = 0; i < GUI_MAX_WINDOWS; i++) {
            if (desktop->windows[i].used != 0 &&
                desktop->windows[i].z >= best_z) {
                desktop->focused_window_id = i;
                best_z = desktop->windows[i].z;
            }
        }
    }
    gui_refresh_cursor_shape(desktop);
    /* Repaint the area the window used to occupy so the desktop gradient
     * and any windows underneath show through. */
    gui_damage_add(desktop, dx, dy, dw, dh);
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
        gui_refresh_cursor_shape(desktop);
        return 0;
    }
    for (uint32_t j = 0; j + 1U < GUI_TITLE_LEN; j++) {
        if (title[j] == '\0') {
            break;
        }
        window->title[j] = title[j];
    }
    gui_refresh_cursor_shape(desktop);
    /* Only the title bar pixels change; the rest of the window is
     * unaffected. Dirty just the bar so the next redraw is small. */
    if (window->title_h > 0U) {
        gui_damage_add(desktop, (int32_t)window->x, (int32_t)window->y,
                       (int32_t)window->w, (int32_t)window->title_h);
    }
    return 0;
}

int gui_set_window_title_bar(gui_desktop_t *desktop, uint32_t window_id,
                             uint32_t title_h) {
    if (desktop == 0 || window_id >= GUI_MAX_WINDOWS ||
        desktop->windows[window_id].used == 0) {
        return -1;
    }
    /* Refuse a title bar taller than the window itself. The bar must
     * leave at least 1 px of content underneath it, otherwise owner
     * drawing has nowhere to go and the bar would be a regression
     * versus no-bar mode. */
    gui_window_t *window = &desktop->windows[window_id];
    if (title_h >= window->h) {
        return -1;
    }
    uint32_t old_title_h = window->title_h;
    window->title_h = title_h;
    gui_refresh_cursor_shape(desktop);
    /* Title bar height changed: dirty the whole window so the bar
     * relayouts and any content that moved gets re-blitted. */
    (void)old_title_h;
    gui_damage_add(desktop, (int32_t)window->x, (int32_t)window->y,
                   (int32_t)window->w, (int32_t)window->h);
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
    if (gui_window_ensure_backing(window) != 0) {
        return -1;
    }
    window->owner_drawn = 1;
    /*
     * Content coordinates are 0-based; the backing buffer origin sits
     * at (0,0) of the content area so we draw at (x, y) directly.
     * The compositor adds title_h when blitting the backing onto the
     * framebuffer.
     */
    uint32_t content_h = window->h > window->title_h
                             ? window->h - window->title_h
                             : 0U;
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if ((uint32_t)x >= window->w || (uint32_t)y >= content_h) {
        return 0;
    }
    fb_t fb = backing_fb_for(window);
    font_draw_text(&fb, (uint32_t)x, (uint32_t)y, text, color);
    /*
     * Push a tight damage rect that covers the rendered text. The width
     * uses font_text_width so a short line does not invalidate the
     * whole row. Y is clipped to content_h; the y+GLYPH_HEIGHT band can
     * be slightly larger than content (descender) but is still well
     * below full-screen.
     */
    uint32_t text_w = font_text_width(text);
    int32_t dx = (int32_t)window->x + x;
    int32_t dy = (int32_t)window->y + (int32_t)window->title_h + y;
    int32_t dw = (int32_t)text_w;
    int32_t dh = (int32_t)FONT_GLYPH_HEIGHT;
    if (dx + dw > (int32_t)(window->x + window->w)) {
        dw = (int32_t)(window->x + window->w) - dx;
    }
    if (dy + dh > (int32_t)(window->y + window->h)) {
        dh = (int32_t)(window->y + window->h) - dy;
    }
    gui_damage_add(desktop, dx, dy, dw, dh);
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
    if (gui_window_ensure_backing(window) != 0) {
        return -1;
    }
    window->owner_drawn = 1;
    uint32_t content_h = window->h > window->title_h
                             ? window->h - window->title_h
                             : 0U;
    int32_t x0 = x;
    int32_t y0 = y;
    int32_t x1 = x0 + (int32_t)w;
    int32_t y1 = y0 + (int32_t)h;
    int32_t cx1 = (int32_t)window->w;
    int32_t cy1 = (int32_t)content_h;
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > cx1) {
        x1 = cx1;
    }
    if (y1 > cy1) {
        y1 = cy1;
    }
    if (x1 <= x0 || y1 <= y0) {
        return 0;
    }
    fb_t fb = backing_fb_for(window);
    fb_fillrect(&fb, (uint32_t)x0, (uint32_t)y0,
                (uint32_t)(x1 - x0), (uint32_t)(y1 - y0), color);
    /* Push the content rect as damage in framebuffer coords. */
    gui_damage_add(desktop,
                   (int32_t)window->x + x0,
                   (int32_t)window->y + (int32_t)window->title_h + y0,
                   x1 - x0, y1 - y0);
    return 0;
}

int gui_window_clear(gui_desktop_t *desktop, uint32_t window_id,
                     uint32_t color) {
    if (desktop == 0 || window_id >= GUI_MAX_WINDOWS ||
        desktop->windows[window_id].used == 0) {
        return -1;
    }
    gui_window_t *window = &desktop->windows[window_id];
    if (gui_window_ensure_backing(window) != 0) {
        return -1;
    }
    window->owner_drawn = 1;
    uint32_t content_h = window->h > window->title_h
                             ? window->h - window->title_h
                             : 0U;
    fb_t fb = backing_fb_for(window);
    fb_fillrect(&fb, 0, 0, window->w, content_h, color);
    /* Clear invalidates the entire content area below the title bar. */
    gui_damage_add(desktop,
                   (int32_t)window->x,
                   (int32_t)window->y + (int32_t)window->title_h,
                   (int32_t)window->w, (int32_t)content_h);
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
    int32_t old_x, old_y, old_w, old_h;

    if (desktop == 0 || desktop->fb == 0 || window_id >= GUI_MAX_WINDOWS ||
        x >= desktop->fb->width || y >= desktop->fb->height ||
        desktop->windows[window_id].used == 0) {
        return -1;
    }

    window = &desktop->windows[window_id];
    old_x = (int32_t)window->x;
    old_y = (int32_t)window->y;
    old_w = (int32_t)window->w;
    old_h = (int32_t)window->h;
    window->x = x;
    window->y = y;
    gui_refresh_cursor_shape(desktop);
    /* Move: damage the union of the old and new positions so both the
     * old rectangle (now background) and the new rectangle (new window
     * location) get repainted. Damage coalescing will merge them into a
     * single rect when they overlap. */
    int32_t new_x1 = (int32_t)x + old_w;
    int32_t new_y1 = (int32_t)y + old_h;
    int32_t old_x1 = old_x + old_w;
    int32_t old_y1 = old_y + old_h;
    int32_t ux0 = old_x < (int32_t)x ? old_x : (int32_t)x;
    int32_t uy0 = old_y < (int32_t)y ? old_y : (int32_t)y;
    int32_t ux1 = old_x1 > new_x1 ? old_x1 : new_x1;
    int32_t uy1 = old_y1 > new_y1 ? old_y1 : new_y1;
    gui_damage_add(desktop, ux0, uy0, ux1 - ux0, uy1 - uy0);
    return 0;
}

int gui_focus_window(gui_desktop_t *desktop, uint32_t window_id) {
    if (desktop == 0 || window_id >= GUI_MAX_WINDOWS ||
        desktop->windows[window_id].used == 0) {
        return -1;
    }

    uint32_t prev = desktop->focused_window_id;
    desktop->focused_window_id = window_id;
    desktop->windows[window_id].z = desktop->next_z++;
    gui_refresh_cursor_shape(desktop);
    /* Focus only changes the border colour. Dirty a 1-px ring around the
     * previously focused and the newly focused windows so the border
     * repaints without touching window content. We approximate the
     * border as the full window rect; the border redraw is cheap. */
    if (prev != GUI_NO_WINDOW && prev < GUI_MAX_WINDOWS &&
        desktop->windows[prev].used != 0) {
        gui_window_t *w = &desktop->windows[prev];
        gui_damage_add(desktop, (int32_t)w->x, (int32_t)w->y,
                       (int32_t)w->w, (int32_t)w->h);
    }
    gui_window_t *w = &desktop->windows[window_id];
    gui_damage_add(desktop, (int32_t)w->x, (int32_t)w->y,
                   (int32_t)w->w, (int32_t)w->h);
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

uint32_t gui_window_for_pid(gui_desktop_t *desktop, uint32_t owner_pid,
                            uint32_t index) {
    if (desktop == 0) {
        return GUI_NO_WINDOW;
    }
    /* GUI_NO_OWNER is the sentinel for "no pid"; never enumerate
     * ownerless kernel-created windows under it. */
    if (owner_pid == GUI_NO_OWNER) {
        return GUI_NO_WINDOW;
    }
    uint32_t found = 0;
    for (uint32_t i = 0; i < GUI_MAX_WINDOWS; i++) {
        const gui_window_t *window = &desktop->windows[i];
        if (window->used == 0 || window->owner_pid != owner_pid) {
            continue;
        }
        if (found == index) {
            return i;
        }
        found++;
    }
    return GUI_NO_WINDOW;
}

/* Forward declare the color blender; it lives below gui_draw_window but
 * the title-bar code path inside that function already needs it. */
static uint32_t gui_blend_color(uint32_t top, uint32_t bottom,
                                uint32_t num, uint32_t denom);

/* Compute the close-button rect for a window with a kernel-drawn title
 * bar. Returns 1 and fills the out-params when the bar is tall enough
 * and wide enough; returns 0 otherwise. The button sits flush right
 * inside the bar with GUI_CLOSE_BTN_PAD pixels of padding on every
 * side. When the title_h is smaller than the requested box height, the
 * helper clamps the box to fit. The minimum title_h gate
 * (GUI_CLOSE_BTN_MIN_TITLE_H) keeps the box from colliding with the
 * title text in thin title bars. */
static int gui_close_box_rect(const gui_window_t *window, uint32_t *out_x,
                              uint32_t *out_y, uint32_t *out_w,
                              uint32_t *out_h) {
    if (window == 0 || window->used == 0 || window->title_h == 0U ||
        window->title[0] == '\0' ||
        window->title_h < GUI_CLOSE_BTN_MIN_TITLE_H) {
        return 0;
    }
    uint32_t bh = window->title_h - 2U * GUI_CLOSE_BTN_PAD;
    if (bh < 4U) {
        return 0;
    }
    uint32_t bw = (bh < GUI_CLOSE_BTN_W) ? bh : GUI_CLOSE_BTN_W;
    if (window->w < bw + 2U * GUI_CLOSE_BTN_PAD) {
        return 0;
    }
    *out_x = window->x + window->w - bw - GUI_CLOSE_BTN_PAD;
    *out_y = window->y + GUI_CLOSE_BTN_PAD;
    *out_w = bw;
    *out_h = bh;
    return 1;
}

static void gui_refresh_cursor_shape(gui_desktop_t *desktop) {
    int32_t hit;
    gui_window_t *window;

    if (desktop == 0) {
        return;
    }

    desktop->cursor.shape = GUI_CURSOR_ARROW;
    hit = gui_hit_test(desktop, desktop->cursor.x, desktop->cursor.y);
    if (hit == (int32_t)GUI_NO_WINDOW || hit < 0) {
        return;
    }

    window = &desktop->windows[hit];
    if (window->title_h > 0U && window->title[0] != '\0' &&
        desktop->cursor.y >= (int32_t)window->y &&
        desktop->cursor.y < (int32_t)(window->y + window->title_h)) {
        desktop->cursor.shape = GUI_CURSOR_HAND;
        return;
    }

    if (window->title_h > 0U) {
        uint32_t cb_x = 0, cb_y = 0, cb_w = 0, cb_h = 0;
        if (gui_close_box_rect(window, &cb_x, &cb_y, &cb_w, &cb_h) &&
            desktop->cursor.x >= (int32_t)cb_x &&
            desktop->cursor.x < (int32_t)(cb_x + cb_w) &&
            desktop->cursor.y >= (int32_t)cb_y &&
            desktop->cursor.y < (int32_t)(cb_y + cb_h)) {
            desktop->cursor.shape = GUI_CURSOR_HAND;
        }
    }
}

static int gui_window_owner_dead(const gui_window_t *window) {
    const process_t *owner;

    if (window == 0 || window->used == 0 ||
        window->owner_pid == GUI_NO_OWNER) {
        return 0;
    }

    owner = process_find(window->owner_pid);
    return owner == 0 || owner->state == PROCESS_ZOMBIE ||
           owner->state == PROCESS_UNUSED;
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

    if (window->owner_drawn == 0) {
        /* Default path: fill the window with border then paint bg_color
         * over the interior. Used by windows that have not received any
         * drawing from their EL0 owner. */
        fb_fillrect(fb, window->x, window->y, window->w, window->h, border);
        if (window->w > 2U && window->h > 2U) {
            fb_fillrect(fb, window->x + 1U, window->y + 1U,
                        window->w - 2U, window->h - 2U, window->bg_color);
        }
    } else {
        /*
         * Owner-drawn path: blit the backing buffer first (it is the
         * source of truth for the window's content), then paint the
         * 1px border on top so the border survives regardless of what
         * the owner drew into (0, 0). This is what makes drag / focus
         * changes / resize look correct: the content follows the
         * window because we always re-blit at the current (x, y).
         */
        if (window->backing != 0) {
            uint32_t content_h = window->h > window->title_h
                                     ? window->h - window->title_h
                                     : 0U;
            uint32_t blit_y = window->y + window->title_h;
            uint32_t blit_h = content_h;
            if (window->title_h == 0U) {
                blit_y = window->y;
                blit_h = window->h;
            }
            fb_blit(fb, window->x, blit_y, window->backing,
                     window->w, blit_h);
        }
        fb_fillrect(fb, window->x, window->y, window->w, 1U, border);
        if (window->h > 1U) {
            fb_fillrect(fb, window->x,
                        window->y + window->h - 1U, window->w, 1U, border);
        }
        if (window->h > 2U) {
            fb_fillrect(fb, window->x, window->y + 1U, 1U,
                        window->h - 2U, border);
            if (window->w > 1U) {
                fb_fillrect(fb, window->x + window->w - 1U,
                            window->y + 1U, 1U, window->h - 2U, border);
            }
        }
    }

    /* Title bar: drawn on top of the bg/border pass so the title text is
     * always visible. When title_h is 0 (no bar requested) or the title
     * string is empty, we leave the window alone and let the owner draw
     * from y=0. When title_h > 0, the title bar replaces the top
     * title_h pixels of the window content. */
    if (window->title_h > 0U && window->title[0] != '\0') {
        uint32_t bar_color = gui_blend_color(border, 0xff000000U, 2U, 3U);
        fb_fillrect(fb, window->x, window->y, window->w, window->title_h,
                    bar_color);
        if (window->h > window->title_h + 1U) {
            fb_fillrect(fb, window->x, window->y + window->title_h,
                        window->w, 1U, border);
        }
        /* Center the font glyph vertically inside the bar with 2 px left
         * padding for the text. Clip to the bar height so the glyph never
         * bleeds into the content area. */
        uint32_t text_y = window->y + (window->title_h > FONT_GLYPH_HEIGHT
                                       ? (window->title_h - FONT_GLYPH_HEIGHT) /
                                             2U
                                       : 0U);
        font_draw_text_clipped(fb, window->x + 2U, text_y, window->title_h,
                               window->title, 0xfff0f4f8U);
        /* Close button: small red box with an X inside, flush right.
         * Only drawn when gui_close_box_rect accepts the geometry. The
         * click path in gui_handle_input mirrors this helper, so a
         * visible close box always intercepts a left-click. */
        uint32_t cb_x = 0, cb_y = 0, cb_w = 0, cb_h = 0;
        if (gui_close_box_rect(window, &cb_x, &cb_y, &cb_w, &cb_h)) {
            uint32_t cb_bg = (desktop != 0 &&
                              desktop->focused_window_id == index)
                                 ? 0xffe04a4aU
                                 : 0xff8a3030U;
            fb_fillrect(fb, cb_x, cb_y, cb_w, cb_h, cb_bg);
            uint32_t x_color = 0xfff8f8f8U;
            fb_draw_line(fb, (int32_t)(cb_x + 2U),
                         (int32_t)(cb_y + 2U),
                         (int32_t)(cb_x + cb_w - 3U),
                         (int32_t)(cb_y + cb_h - 3U), x_color);
            fb_draw_line(fb, (int32_t)(cb_x + cb_w - 3U),
                         (int32_t)(cb_y + 2U),
                         (int32_t)(cb_x + 2U),
                         (int32_t)(cb_y + cb_h - 3U), x_color);
        }
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

/* Find the next used window covering (x, row) whose left edge is at or
 * after x. Returns the window index or GUI_MAX_WINDOWS if none. */
static uint32_t gui_next_window_at_or_after(const gui_desktop_t *desktop,
                                             uint32_t row, uint32_t x) {
    uint32_t best = GUI_MAX_WINDOWS;
    uint32_t best_x = UINT32_MAX;
    for (uint32_t i = 0; i < GUI_MAX_WINDOWS; i++) {
        const gui_window_t *w = &desktop->windows[i];
        if (w->used == 0) {
            continue;
        }
        if (w->y > row || (w->y + w->h) <= row) {
            continue;
        }
        if (w->x + w->w <= x) {
            continue;
        }
        if (w->x >= x && w->x < best_x) {
            best = i;
            best_x = w->x;
        }
    }
    return best;
}

static uint32_t gui_next_window_above_z(const gui_desktop_t *desktop,
                                        uint32_t min_z) {
    uint32_t best = GUI_MAX_WINDOWS;
    uint32_t best_z = UINT32_MAX;

    for (uint32_t i = 0; i < GUI_MAX_WINDOWS; i++) {
        const gui_window_t *window = &desktop->windows[i];
        if (window->used == 0 || window->z <= min_z) {
            continue;
        }
        if (window->z < best_z) {
            best = i;
            best_z = window->z;
        }
    }

    return best;
}

void gui_draw(gui_desktop_t *desktop) {
    if (desktop == 0 || desktop->fb == 0) {
        return;
    }

    /* "Full" sentinel: take the cheap route of repainting the entire
     * framebuffer. The damage list is ignored because the cost of
     * walking many small rects would exceed the cost of one full pass
     * once the bursts accumulate. */
    if (desktop->damage_full) {
        uint32_t bottom_color;
        uint32_t height;
        uint32_t row;
        uint32_t last_z;

        height = desktop->fb->height;
        bottom_color = gui_blend_color(desktop->background_color,
                                       0xff000000U, 3U, 4U);
        for (row = 0; row < height; row++) {
            uint32_t x = 0;
            while (x < desktop->fb->width) {
                uint32_t w = gui_next_window_at_or_after(desktop, row, x);
                uint32_t next;
                if (w == GUI_MAX_WINDOWS) {
                    next = desktop->fb->width;
                } else {
                    next = desktop->windows[w].x;
                }
                if (next > x) {
                    uint32_t color = gui_blend_color(
                        desktop->background_color, bottom_color, row,
                        height - 1U);
                    fb_fillrect(desktop->fb, x, row, next - x, 1U, color);
                }
                if (w == GUI_MAX_WINDOWS) {
                    break;
                }
                x = desktop->windows[w].x + desktop->windows[w].w;
            }
        }
        last_z = 0;
        for (;;) {
            uint32_t i = gui_next_window_above_z(desktop, last_z);
            if (i == GUI_MAX_WINDOWS) {
                break;
            }
            gui_draw_window(desktop->fb, desktop, i, &desktop->windows[i]);
            last_z = desktop->windows[i].z;
        }
        if (desktop->cursor.visible) {
            gui_draw_cursor(desktop->fb, desktop->cursor.x,
                            desktop->cursor.y, desktop->cursor.shape);
        }
        return;
    }

    /* Partial-redraw path: walk the damage list and repaint each rect.
     * The gradient is painted without skipping window spans (it is
     * cheap; one fillrect per row) and the windows are then repainted
     * in z-order, which overdraws where they overlap. The visual
     * result matches the full path; the per-row cost is proportional
     * to the damage area instead of the framebuffer size. */
    for (uint32_t i = 0; i < desktop->damage_count; i++) {
        const damage_rect_t *r = &desktop->damage_rects[i];
        int32_t x0 = r->x;
        int32_t y0 = r->y;
        int32_t x1 = r->x + r->w;
        int32_t y1 = r->y + r->h;
        if (x0 < 0) {
            x0 = 0;
        }
        if (y0 < 0) {
            y0 = 0;
        }
        if (x1 > (int32_t)desktop->fb->width) {
            x1 = (int32_t)desktop->fb->width;
        }
        if (y1 > (int32_t)desktop->fb->height) {
            y1 = (int32_t)desktop->fb->height;
        }
        if (x1 <= x0 || y1 <= y0) {
            continue;
        }
        uint32_t height = desktop->fb->height;
        uint32_t bottom_color = gui_blend_color(desktop->background_color,
                                                0xff000000U, 3U, 4U);
        for (int32_t row = y0; row < y1; row++) {
            uint32_t color = gui_blend_color(desktop->background_color,
                                             bottom_color, (uint32_t)row,
                                             height - 1U);
            fb_fillrect(desktop->fb, (uint32_t)x0, (uint32_t)row,
                        (uint32_t)(x1 - x0), 1U, color);
        }
        uint32_t last_z = 0;
        for (;;) {
            uint32_t wi = gui_next_window_above_z(desktop, last_z);
            if (wi == GUI_MAX_WINDOWS) {
                break;
            }
            gui_draw_window(desktop->fb, desktop, wi, &desktop->windows[wi]);
            last_z = desktop->windows[wi].z;
        }
        if (desktop->cursor.visible) {
            int32_t cx0 = desktop->cursor.x;
            int32_t cy0 = desktop->cursor.y;
            int32_t cx1 = cx0 + (int32_t)GUI_CURSOR_W;
            int32_t cy1 = cy0 + (int32_t)GUI_CURSOR_H;
            if (cx1 > x0 && cx0 < x1 && cy1 > y0 && cy0 < y1) {
                gui_draw_cursor(desktop->fb, desktop->cursor.x,
                                desktop->cursor.y, desktop->cursor.shape);
            }
        }
    }
}

void gui_render(fb_t *fb, void *context) {
    (void)context;
    if (g_gui_active == 0) {
        return;
    }
    g_gui_fb = *fb;
    g_gui_desktop.fb = &g_gui_fb;
    gui_draw(&g_gui_desktop);
}

gui_desktop_t *gui_desktop(void) {
    if (g_gui_active == 0) {
        return 0;
    }
    return &g_gui_desktop;
}

int gui_is_dirty(void) {
    return g_gui_dirty != 0 ? 1 : 0;
}

void gui_clear_dirty(void) {
    g_gui_dirty = 0;
    if (g_gui_active) {
        gui_damage_clear(&g_gui_desktop);
    }
}

void gui_request_redraw(void) {
    g_gui_dirty = 1;
    if (g_gui_active) {
        gui_damage_add_full(&g_gui_desktop);
    }
}

/*
 * gui_damage_add: insert a framebuffer-coords rect into the desktop's damage
 * list. The rect is clipped to the framebuffer; zero-area rects are dropped.
 * Existing overlapping/adjacent entries are merged in place. When the list
 * fills, we collapse everything to a single "full" sentinel so future adds
 * become O(1) and the next gui_draw degrades to a full repaint.
 */
void gui_damage_add(gui_desktop_t *desktop, int32_t x, int32_t y,
                    int32_t w, int32_t h) {
    if (desktop == 0 || desktop->fb == 0) {
        return;
    }
    if (desktop->damage_full) {
        return;
    }
    if (w <= 0 || h <= 0) {
        return;
    }
    int32_t fb_w = (int32_t)desktop->fb->width;
    int32_t fb_h = (int32_t)desktop->fb->height;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x >= fb_w || y >= fb_h) {
        return;
    }
    if (x + w > fb_w) {
        w = fb_w - x;
    }
    if (y + h > fb_h) {
        h = fb_h - y;
    }
    if (w <= 0 || h <= 0) {
        return;
    }
    /* Fast path: a rect that covers the whole framebuffer collapses to
     * the sentinel and short-circuits future adds. */
    if (x == 0 && y == 0 && w == fb_w && h == fb_h) {
        desktop->damage_full = 1;
        desktop->damage_count = 0;
        return;
    }
    /* Merge pass: scan existing rects, absorbing any that overlap or touch
     * the new one and growing the new rect to cover them. Remove the
     * absorbed entries so the list stays compact. */
    for (uint32_t i = 0; i < desktop->damage_count; ) {
        damage_rect_t *r = &desktop->damage_rects[i];
        int32_t ax1 = x + w;
        int32_t ay1 = y + h;
        int32_t bx1 = r->x + r->w;
        int32_t by1 = r->y + r->h;
        if (x <= bx1 && ax1 >= r->x && y <= by1 && ay1 >= r->y) {
            /* Overlap or touch: unify. */
            int32_t nx0 = x < r->x ? x : r->x;
            int32_t ny0 = y < r->y ? y : r->y;
            int32_t nx1 = ax1 > bx1 ? ax1 : bx1;
            int32_t ny1 = ay1 > by1 ? ay1 : by1;
            x = nx0;
            y = ny0;
            w = nx1 - nx0;
            h = ny1 - ny0;
            /* Drop the absorbed entry by shifting the tail down. */
            for (uint32_t k = i; k + 1U < desktop->damage_count; k++) {
                desktop->damage_rects[k] = desktop->damage_rects[k + 1U];
            }
            desktop->damage_count--;
            /* Do not advance i: re-check the new occupant at slot i. */
            continue;
        }
        i++;
    }
    if (desktop->damage_count >= GUI_DAMAGE_MAX) {
        desktop->damage_full = 1;
        desktop->damage_count = 0;
        return;
    }
    desktop->damage_rects[desktop->damage_count].x = x;
    desktop->damage_rects[desktop->damage_count].y = y;
    desktop->damage_rects[desktop->damage_count].w = w;
    desktop->damage_rects[desktop->damage_count].h = h;
    desktop->damage_count++;
    g_gui_dirty = 1;
}

void gui_damage_add_full(gui_desktop_t *desktop) {
    if (desktop == 0) {
        return;
    }
    desktop->damage_full = 1;
    desktop->damage_count = 0;
    g_gui_dirty = 1;
}

void gui_damage_clear(gui_desktop_t *desktop) {
    if (desktop == 0) {
        return;
    }
    desktop->damage_full = 0;
    desktop->damage_count = 0;
}

int gui_handle_input(const input_event_t *event) {
    if (g_gui_active == 0 || event == 0) {
        return -1;
    }

    /* Mouse buttons and motion also affect the cursor itself. */
    switch (event->type) {
    case INPUT_EVENT_MOUSE_MOVE:
        gui_cursor_move(&g_gui_desktop, event->data.mouse_move.dx,
                         event->data.mouse_move.dy);
        if (g_gui_desktop.drag_window_id != GUI_NO_WINDOW) {
            gui_drag_update(&g_gui_desktop, g_gui_desktop.cursor.x,
                            g_gui_desktop.cursor.y);
        }
        gui_dispatch_input(&g_gui_desktop, event);
        return 0;
    case INPUT_EVENT_MOUSE_BUTTON:
        gui_cursor_button(&g_gui_desktop,
                          event->data.mouse_button.button,
                          event->data.mouse_button.pressed);
        if (event->data.mouse_button.button == 0U) {
            if (event->data.mouse_button.pressed != 0U) {
                /* Left press: focus is already set by gui_cursor_button.
                 * If the click lands inside the kernel-drawn close box
                 * of the focused window, push GUI_EVENT_CLOSE and skip
                 * both the drag start and the generic click delivery.
                 * Otherwise start a drag and deliver a click on the
                 * topmost window. */
                int32_t hit = gui_hit_test(&g_gui_desktop,
                                           g_gui_desktop.cursor.x,
                                           g_gui_desktop.cursor.y);
                if (hit != (int32_t)GUI_NO_WINDOW && hit >= 0) {
                    gui_window_t *window =
                        &g_gui_desktop.windows[hit];
                    uint32_t cb_x = 0, cb_y = 0, cb_w = 0, cb_h = 0;
                    if (gui_close_box_rect(window, &cb_x, &cb_y, &cb_w,
                                           &cb_h) &&
                        g_gui_desktop.cursor.x >= (int32_t)cb_x &&
                        g_gui_desktop.cursor.x <
                            (int32_t)(cb_x + cb_w) &&
                        g_gui_desktop.cursor.y >= (int32_t)cb_y &&
                        g_gui_desktop.cursor.y <
                            (int32_t)(cb_y + cb_h)) {
                        if (gui_window_owner_dead(window)) {
                            (void)gui_destroy_window(&g_gui_desktop,
                                                     (uint32_t)hit);
                        } else {
                            (void)gui_window_push_event(window,
                                                        GUI_EVENT_CLOSE, 0, 0);
                        }
                    } else {
                        gui_drag_start(&g_gui_desktop, (uint32_t)hit,
                                       g_gui_desktop.cursor.x -
                                           (int32_t)window->x,
                                       g_gui_desktop.cursor.y -
                                           (int32_t)window->y);
                        gui_window_push_event(window,
                                              GUI_EVENT_MOUSE_CLICK,
                                              g_gui_desktop.cursor.x,
                                              g_gui_desktop.cursor.y);
                    }
                }
            }
            if (event->data.mouse_button.pressed == 0U) {
                gui_drag_end(&g_gui_desktop);
            }
        }
        return 0;
    case INPUT_EVENT_KEY_PRESS:
        /* Non-button key: route to focused window as KEY_PRESS. */
        gui_focus_window_ensure(&g_gui_desktop);
        if (g_gui_desktop.focused_window_id != GUI_NO_WINDOW) {
            gui_window_t *window =
                &g_gui_desktop.windows[g_gui_desktop.focused_window_id];
            gui_window_push_event(window, GUI_EVENT_KEY_PRESS,
                                  (int32_t)event->data.key.key, 0);
        }
        return 0;
    case INPUT_EVENT_KEY_RELEASE:
        if (g_gui_desktop.focused_window_id != GUI_NO_WINDOW) {
            gui_window_t *window =
                &g_gui_desktop.windows[g_gui_desktop.focused_window_id];
            gui_window_push_event(window, GUI_EVENT_KEY_RELEASE,
                                  (int32_t)event->data.key.key, 0);
        }
        return 0;
    default:
        return -1;
    }
}

void gui_init_for_framebuffer(fb_t *fb, void *context) {
    (void)context;

    g_gui_active = 0;
    g_gui_dirty = 0;

    if (fb == 0) {
        return;
    }

    g_gui_fb = *fb;
    if (gui_init(&g_gui_desktop, &g_gui_fb, 0xff202428U) != 0) {
        return;
    }

    /* The desktop starts empty: the panel taskbar (programs/apps/panel.S)
     * is the first userland process and creates its own window via
     * sys_window_create. Per-app launchers, running-app entries, and
     * any user-spawned windows build on top of that empty gradient.
     * The two demo rectangles that used to live here made the screen
     * look like a demo instead of a real desktop; the alpha rule is to
     * leave the surface empty until the panel paints. */
    gui_draw(&g_gui_desktop);
    g_gui_active = 1;
}
