#include "kernel/gui.h"

#include <stdint.h>

#include "kernel/font.h"
#include "kernel/mm/kheap.h"
#include "kernel/process.h"

static fb_t g_gui_fb;
static gui_desktop_t g_gui_desktop;
static uint8_t g_gui_active;
static uint8_t g_gui_dirty;

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

static int gui_str_eq(const char *a, const char *b) {
    uint32_t i = 0;
    if (a == 0 || b == 0) {
        return 0;
    }
    for (;;) {
        if (a[i] != b[i]) {
            return 0;
        }
        if (a[i] == '\0') {
            return 1;
        }
        i++;
    }
}

static uint32_t gui_blend_color(uint32_t top, uint32_t bottom,
                                uint32_t num, uint32_t denom) {
    uint32_t tr = (top >> 16) & 0xffU;
    uint32_t tg = (top >> 8) & 0xffU;
    uint32_t tb = top & 0xffU;
    uint32_t br = (bottom >> 16) & 0xffU;
    uint32_t bg = (bottom >> 8) & 0xffU;
    uint32_t bb = bottom & 0xffU;
    uint32_t r;
    uint32_t g;
    uint32_t b;

    if (denom == 0U) {
        return top;
    }
    if (num >= denom) {
        return (top & 0xff000000U) | (br << 16) | (bg << 8) | bb;
    }

    r = (uint32_t)((int32_t)tr +
                   (((int32_t)br - (int32_t)tr) * (int32_t)num) /
                       (int32_t)denom);
    g = (uint32_t)((int32_t)tg +
                   (((int32_t)bg - (int32_t)tg) * (int32_t)num) /
                       (int32_t)denom);
    b = (uint32_t)((int32_t)tb +
                   (((int32_t)bb - (int32_t)tb) * (int32_t)num) /
                       (int32_t)denom);

    return (top & 0xff000000U) | (r << 16) | (g << 8) | b;
}

static uint32_t gui_window_effective_flags(const gui_window_t *window) {
    if (window == 0 || window->used == 0) {
        return 0;
    }

    return window->flags;
}

static void gui_apply_builtin_policy(gui_window_t *window) {
    if (window == 0 || window->used == 0) {
        return;
    }

    /* Until userland has a stable SET_FLAGS syscall, classify the built-in
     * panel by its registered title at creation time and store the result in
     * the real window policy field. Policy checks never special-case the
     * title again after this point. */
    if (gui_str_eq(window->title, "panel")) {
        window->flags |= GUI_WINDOW_DOCK | GUI_WINDOW_NO_FOCUS |
                         GUI_WINDOW_NO_DRAG | GUI_WINDOW_SKIP_TASKBAR;
    }
}

static void gui_draw_cursor(fb_t *fb, int32_t x, int32_t y, uint8_t shape) {
    const uint8_t (*bitmap)[GUI_CURSOR_W] =
        (shape == GUI_CURSOR_HAND) ? g_cursor_hand : g_cursor;

    for (uint32_t row = 0; row < GUI_CURSOR_H; row++) {
        for (uint32_t col = 0; col < GUI_CURSOR_W; col++) {
            uint8_t v = bitmap[row][col];
            if (v == 0U) {
                continue;
            }
            fb_putpixel(fb, (uint32_t)(x + (int32_t)col),
                        (uint32_t)(y + (int32_t)row),
                        v == 1U ? 0xff101010U : 0xffe0e0e0U);
        }
    }
}

static int gui_close_box_rect(const gui_window_t *window, uint32_t *out_x,
                              uint32_t *out_y, uint32_t *out_w,
                              uint32_t *out_h) {
    uint32_t bh;
    uint32_t bw;

    if (window == 0 || window->used == 0 || window->title_h == 0U ||
        window->title[0] == '\0' ||
        window->title_h < GUI_CLOSE_BTN_MIN_TITLE_H) {
        return 0;
    }

    bh = window->title_h - 2U * GUI_CLOSE_BTN_PAD;
    if (bh < 4U) {
        return 0;
    }

    bw = bh < GUI_CLOSE_BTN_W ? bh : GUI_CLOSE_BTN_W;
    if (window->w < bw + 2U * GUI_CLOSE_BTN_PAD) {
        return 0;
    }

    *out_x = window->x + window->w - bw - GUI_CLOSE_BTN_PAD;
    *out_y = window->y + GUI_CLOSE_BTN_PAD;
    *out_w = bw;
    *out_h = bh;
    return 1;
}

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

int gui_window_contains(gui_window_t *window, int32_t x, int32_t y) {
    if (window == 0 || window->used == 0) {
        return 0;
    }

    return x >= (int32_t)window->x &&
           x < (int32_t)(window->x + window->w) &&
           y >= (int32_t)window->y &&
           y < (int32_t)(window->y + window->h);
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
    int32_t prev_x;
    int32_t prev_y;
    int32_t ux0;
    int32_t uy0;
    int32_t ux1;
    int32_t uy1;

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

    prev_x = desktop->cursor.x;
    prev_y = desktop->cursor.y;
    desktop->cursor.prev_x = prev_x;
    desktop->cursor.prev_y = prev_y;
    desktop->cursor.x = x;
    desktop->cursor.y = y;
    gui_refresh_cursor_shape(desktop);

    ux0 = prev_x < x ? prev_x : x;
    uy0 = prev_y < y ? prev_y : y;
    ux1 = (prev_x + (int32_t)GUI_CURSOR_W) > (x + (int32_t)GUI_CURSOR_W)
              ? prev_x + (int32_t)GUI_CURSOR_W
              : x + (int32_t)GUI_CURSOR_W;
    uy1 = (prev_y + (int32_t)GUI_CURSOR_H) > (y + (int32_t)GUI_CURSOR_H)
              ? prev_y + (int32_t)GUI_CURSOR_H
              : y + (int32_t)GUI_CURSOR_H;
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
    gui_window_t *window;
    uint32_t flags;

    if (desktop == 0 || window_id >= GUI_MAX_WINDOWS ||
        desktop->windows[window_id].used == 0) {
        return;
    }

    window = &desktop->windows[window_id];
    flags = gui_window_effective_flags(window);
    if ((flags & GUI_WINDOW_NO_DRAG) != 0U) {
        return;
    }

    if (window->title_h > 0U &&
        (off_y < 0 || (uint32_t)off_y >= window->title_h)) {
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
    int32_t old_x;
    int32_t old_y;
    int32_t old_w;
    int32_t old_h;
    int32_t ux0;
    int32_t uy0;
    int32_t ux1;
    int32_t uy1;

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

    ux0 = old_x < new_x ? old_x : new_x;
    uy0 = old_y < new_y ? old_y : new_y;
    ux1 = old_x + old_w > new_x + old_w ? old_x + old_w : new_x + old_w;
    uy1 = old_y + old_h > new_y + old_h ? old_y + old_h : new_y + old_h;
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
    uint8_t mask;
    int32_t hit;

    if (desktop == 0 || button > 2U) {
        return;
    }

    mask = (uint8_t)(1U << button);
    if (pressed != 0U) {
        desktop->cursor.buttons_mask =
            (uint8_t)(desktop->cursor.buttons_mask | mask);
    } else {
        desktop->cursor.buttons_mask =
            (uint8_t)(desktop->cursor.buttons_mask & (uint8_t)(~mask));
    }

    if (button != 0U || pressed == 0U) {
        return;
    }

    hit = gui_hit_test(desktop, desktop->cursor.x, desktop->cursor.y);
    if (hit != (int32_t)GUI_NO_WINDOW && hit >= 0) {
        (void)gui_focus_window(desktop, (uint32_t)hit);
    } else {
        desktop->focused_window_id = GUI_NO_WINDOW;
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
    desktop->damage_full = 1;

    for (uint32_t i = 0; i < GUI_MAX_WINDOWS; i++) {
        desktop->windows[i].x = 0;
        desktop->windows[i].y = 0;
        desktop->windows[i].w = 0;
        desktop->windows[i].h = 0;
        desktop->windows[i].bg_color = 0;
        desktop->windows[i].border_color = 0;
        desktop->windows[i].owner_pid = GUI_NO_OWNER;
        desktop->windows[i].z = 0;
        desktop->windows[i].flags = 0;
        desktop->windows[i].title_h = 0;
        for (uint32_t j = 0; j < GUI_TITLE_LEN; j++) {
            desktop->windows[i].title[j] = '\0';
        }
        desktop->windows[i].event_head = 0;
        desktop->windows[i].event_tail = 0;
        desktop->windows[i].event_count = 0;
        desktop->windows[i].used = 0;
        desktop->windows[i].owner_drawn = 0;
        desktop->windows[i].backing = 0;
        desktop->windows[i].backing_capacity = 0;
    }

    return 0;
}

int gui_window_ensure_backing(gui_window_t *window) {
    uint32_t content_h;
    uint32_t needed_bytes;
    fb_t fb;

    if (window == 0 || window->w == 0U || window->h == 0U) {
        return -1;
    }

    content_h = window->h > window->title_h ? window->h - window->title_h : 0U;
    if (content_h == 0U) {
        return -1;
    }

    needed_bytes = window->w * content_h * sizeof(uint32_t);
    if (window->backing != 0 && window->backing_capacity >= needed_bytes) {
        return 0;
    }

    if (window->backing != 0) {
        window->backing = 0;
        window->backing_capacity = 0;
    }

    window->backing = (uint32_t *)kmalloc((unsigned long)needed_bytes);
    if (window->backing == 0) {
        return -1;
    }
    window->backing_capacity = needed_bytes;

    fb = backing_fb_for(window);
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
        if (window->used != 0) {
            continue;
        }

        window->x = x;
        window->y = y;
        window->w = w;
        window->h = h;
        window->bg_color = bg_color;
        window->border_color = border_color;
        window->owner_pid = owner_pid;
        window->z = desktop->next_z++;
        window->flags = 0;
        window->title_h = 0;
        window->event_head = 0;
        window->event_tail = 0;
        window->event_count = 0;
        window->owner_drawn = 0;
        window->backing = 0;
        window->backing_capacity = 0;
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
        gui_apply_builtin_policy(window);

        if (desktop->focused_window_id == GUI_NO_WINDOW &&
            (window->flags & GUI_WINDOW_NO_FOCUS) == 0U) {
            desktop->focused_window_id = i;
        }
        if (window_id != 0) {
            *window_id = i;
        }
        gui_refresh_cursor_shape(desktop);
        gui_damage_add(desktop, (int32_t)x, (int32_t)y, (int32_t)w,
                       (int32_t)h);
        return 0;
    }

    return -1;
}

int gui_destroy_window(gui_desktop_t *desktop, uint32_t window_id) {
    gui_window_t *window;
    int32_t dx;
    int32_t dy;
    int32_t dw;
    int32_t dh;

    if (desktop == 0 || window_id >= GUI_MAX_WINDOWS ||
        desktop->windows[window_id].used == 0) {
        return -1;
    }

    window = &desktop->windows[window_id];
    dx = (int32_t)window->x;
    dy = (int32_t)window->y;
    dw = (int32_t)window->w;
    dh = (int32_t)window->h;

    gui_window_free_backing(window);
    window->x = 0;
    window->y = 0;
    window->w = 0;
    window->h = 0;
    window->bg_color = 0;
    window->border_color = 0;
    window->owner_pid = GUI_NO_OWNER;
    window->z = 0;
    window->flags = 0;
    window->title_h = 0;
    window->event_head = 0;
    window->event_tail = 0;
    window->event_count = 0;
    window->used = 0;
    window->owner_drawn = 0;
    for (uint32_t j = 0; j < GUI_TITLE_LEN; j++) {
        window->title[j] = '\0';
    }

    if (desktop->focused_window_id == window_id) {
        desktop->focused_window_id = GUI_NO_WINDOW;
        (void)gui_focus_window_ensure(desktop);
    }
    gui_refresh_cursor_shape(desktop);
    gui_damage_add(desktop, dx, dy, dw, dh);
    return 0;
}

int gui_set_window_title(gui_desktop_t *desktop, uint32_t window_id,
                         const char *title) {
    gui_window_t *window;

    if (desktop == 0 || window_id >= GUI_MAX_WINDOWS ||
        desktop->windows[window_id].used == 0) {
        return -1;
    }

    window = &desktop->windows[window_id];
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
    gui_apply_builtin_policy(window);
    gui_refresh_cursor_shape(desktop);
    if (window->title_h > 0U) {
        gui_damage_add(desktop, (int32_t)window->x, (int32_t)window->y,
                       (int32_t)window->w, (int32_t)window->title_h);
    }
    return 0;
}

int gui_set_window_title_bar(gui_desktop_t *desktop, uint32_t window_id,
                             uint32_t title_h) {
    gui_window_t *window;

    if (desktop == 0 || window_id >= GUI_MAX_WINDOWS ||
        desktop->windows[window_id].used == 0) {
        return -1;
    }

    window = &desktop->windows[window_id];
    if (title_h >= window->h) {
        return -1;
    }

    window->title_h = title_h;
    gui_refresh_cursor_shape(desktop);
    gui_damage_add(desktop, (int32_t)window->x, (int32_t)window->y,
                   (int32_t)window->w, (int32_t)window->h);
    return 0;
}

int gui_set_window_flags(gui_desktop_t *desktop, uint32_t window_id,
                         uint32_t flags) {
    gui_window_t *window;
    uint32_t old_flags;

    if (desktop == 0 || window_id >= GUI_MAX_WINDOWS ||
        desktop->windows[window_id].used == 0) {
        return -1;
    }

    window = &desktop->windows[window_id];
    old_flags = window->flags;
    window->flags = flags;
    if ((flags & GUI_WINDOW_NO_FOCUS) != 0U &&
        desktop->focused_window_id == window_id) {
        desktop->focused_window_id = GUI_NO_WINDOW;
        (void)gui_focus_window_ensure(desktop);
    }
    if (((old_flags ^ flags) & (GUI_WINDOW_NO_FOCUS | GUI_WINDOW_NO_DRAG |
                                GUI_WINDOW_DOCK)) != 0U) {
        gui_refresh_cursor_shape(desktop);
        gui_damage_add(desktop, (int32_t)window->x, (int32_t)window->y,
                       (int32_t)window->w, (int32_t)window->h);
    }
    return 0;
}

int gui_window_draw_text(gui_desktop_t *desktop, uint32_t window_id,
                         int32_t x, int32_t y, const char *text,
                         uint32_t color) {
    gui_window_t *window;
    uint32_t content_h;
    uint32_t text_w;
    int32_t dw;
    int32_t dh;
    fb_t fb;

    if (desktop == 0 || window_id >= GUI_MAX_WINDOWS ||
        desktop->windows[window_id].used == 0 || text == 0) {
        return -1;
    }

    window = &desktop->windows[window_id];
    if (gui_window_ensure_backing(window) != 0) {
        return -1;
    }
    window->owner_drawn = 1;

    content_h = window->h > window->title_h ? window->h - window->title_h : 0U;
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if ((uint32_t)x >= window->w || (uint32_t)y >= content_h) {
        return 0;
    }

    fb = backing_fb_for(window);
    font_draw_text(&fb, (uint32_t)x, (uint32_t)y, text, color);
    text_w = font_text_width(text);
    dw = (int32_t)text_w;
    dh = (int32_t)FONT_GLYPH_HEIGHT;
    if (x + dw > (int32_t)window->w) {
        dw = (int32_t)window->w - x;
    }
    if (y + dh > (int32_t)content_h) {
        dh = (int32_t)content_h - y;
    }
    gui_damage_add(desktop, (int32_t)window->x + x,
                   (int32_t)window->y + (int32_t)window->title_h + y,
                   dw, dh);
    return 0;
}

int gui_window_draw_rect(gui_desktop_t *desktop, uint32_t window_id,
                         int32_t x, int32_t y, uint32_t w, uint32_t h,
                         uint32_t color) {
    gui_window_t *window;
    uint32_t content_h;
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;
    fb_t fb;

    if (desktop == 0 || window_id >= GUI_MAX_WINDOWS ||
        desktop->windows[window_id].used == 0) {
        return -1;
    }
    if (w == 0U || h == 0U) {
        return 0;
    }

    window = &desktop->windows[window_id];
    if (gui_window_ensure_backing(window) != 0) {
        return -1;
    }
    window->owner_drawn = 1;

    content_h = window->h > window->title_h ? window->h - window->title_h : 0U;
    x0 = x;
    y0 = y;
    x1 = x0 + (int32_t)w;
    y1 = y0 + (int32_t)h;
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > (int32_t)window->w) {
        x1 = (int32_t)window->w;
    }
    if (y1 > (int32_t)content_h) {
        y1 = (int32_t)content_h;
    }
    if (x1 <= x0 || y1 <= y0) {
        return 0;
    }

    fb = backing_fb_for(window);
    fb_fillrect(&fb, (uint32_t)x0, (uint32_t)y0,
                (uint32_t)(x1 - x0), (uint32_t)(y1 - y0), color);
    gui_damage_add(desktop, (int32_t)window->x + x0,
                   (int32_t)window->y + (int32_t)window->title_h + y0,
                   x1 - x0, y1 - y0);
    return 0;
}

int gui_window_clear(gui_desktop_t *desktop, uint32_t window_id,
                     uint32_t color) {
    gui_window_t *window;
    uint32_t content_h;
    fb_t fb;

    if (desktop == 0 || window_id >= GUI_MAX_WINDOWS ||
        desktop->windows[window_id].used == 0) {
        return -1;
    }

    window = &desktop->windows[window_id];
    if (gui_window_ensure_backing(window) != 0) {
        return -1;
    }
    window->owner_drawn = 1;
    content_h = window->h > window->title_h ? window->h - window->title_h : 0U;
    fb = backing_fb_for(window);
    fb_fillrect(&fb, 0, 0, window->w, content_h, color);
    gui_damage_add(desktop, (int32_t)window->x,
                   (int32_t)window->y + (int32_t)window->title_h,
                   (int32_t)window->w, (int32_t)content_h);
    return 0;
}

int gui_window_push_event(gui_window_t *window, uint32_t type,
                          int32_t data1, int32_t data2) {
    if (window == 0 || window->used == 0) {
        return -1;
    }

    if (type == GUI_EVENT_MOUSE_MOVE && window->event_count > 0U) {
        uint32_t prev = window->event_tail == 0U
                            ? GUI_EVENT_QUEUE_SIZE - 1U
                            : window->event_tail - 1U;
        if (window->events[prev].type == GUI_EVENT_MOUSE_MOVE) {
            window->events[prev].data1 = data1;
            window->events[prev].data2 = data2;
            return 0;
        }
    }

    if (window->event_count >= GUI_EVENT_QUEUE_SIZE) {
        if (type == GUI_EVENT_MOUSE_MOVE) {
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
    return 0;
}

int gui_window_pop_event(gui_window_t *window, gui_event_t *out) {
    if (window == 0 || window->used == 0 || out == 0) {
        return -1;
    }
    if (window->event_count == 0U) {
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
    case INPUT_EVENT_KEY_RELEASE:
        if (desktop->focused_window_id != GUI_NO_WINDOW &&
            desktop->focused_window_id < GUI_MAX_WINDOWS &&
            desktop->windows[desktop->focused_window_id].used != 0) {
            return gui_window_push_event(
                &desktop->windows[desktop->focused_window_id],
                event->type == INPUT_EVENT_KEY_PRESS ? GUI_EVENT_KEY_PRESS
                                                     : GUI_EVENT_KEY_RELEASE,
                (int32_t)event->data.key.key, 0);
        }
        return 0;
    case INPUT_EVENT_MOUSE_MOVE: {
        int32_t hit = gui_hit_test(desktop, desktop->cursor.x,
                                   desktop->cursor.y);
        if (hit == (int32_t)GUI_NO_WINDOW || hit < 0) {
            return 0;
        }
        return gui_window_push_event(&desktop->windows[hit],
                                     GUI_EVENT_MOUSE_MOVE,
                                     desktop->cursor.x, desktop->cursor.y);
    }
    default:
        return -1;
    }
}

int gui_move_window(gui_desktop_t *desktop, uint32_t window_id, uint32_t x,
                    uint32_t y) {
    gui_window_t *window;
    int32_t old_x;
    int32_t old_y;
    int32_t old_w;
    int32_t old_h;
    int32_t ux0;
    int32_t uy0;
    int32_t ux1;
    int32_t uy1;

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

    ux0 = old_x < (int32_t)x ? old_x : (int32_t)x;
    uy0 = old_y < (int32_t)y ? old_y : (int32_t)y;
    ux1 = old_x + old_w > (int32_t)x + old_w ? old_x + old_w
                                              : (int32_t)x + old_w;
    uy1 = old_y + old_h > (int32_t)y + old_h ? old_y + old_h
                                              : (int32_t)y + old_h;
    gui_damage_add(desktop, ux0, uy0, ux1 - ux0, uy1 - uy0);
    return 0;
}

int gui_focus_window(gui_desktop_t *desktop, uint32_t window_id) {
    uint32_t prev;
    gui_window_t *window;

    if (desktop == 0 || window_id >= GUI_MAX_WINDOWS ||
        desktop->windows[window_id].used == 0) {
        return -1;
    }

    window = &desktop->windows[window_id];
    if ((gui_window_effective_flags(window) & GUI_WINDOW_NO_FOCUS) != 0U) {
        gui_damage_add(desktop, (int32_t)window->x, (int32_t)window->y,
                       (int32_t)window->w, (int32_t)window->h);
        return 0;
    }

    prev = desktop->focused_window_id;
    desktop->focused_window_id = window_id;
    window->z = desktop->next_z++;
    gui_refresh_cursor_shape(desktop);

    if (prev != GUI_NO_WINDOW && prev < GUI_MAX_WINDOWS &&
        desktop->windows[prev].used != 0) {
        gui_window_t *old = &desktop->windows[prev];
        gui_damage_add(desktop, (int32_t)old->x, (int32_t)old->y,
                       (int32_t)old->w, (int32_t)old->h);
    }
    gui_damage_add(desktop, (int32_t)window->x, (int32_t)window->y,
                   (int32_t)window->w, (int32_t)window->h);
    return 0;
}

int gui_focus_window_ensure(gui_desktop_t *desktop) {
    if (desktop == 0) {
        return -1;
    }
    if (desktop->focused_window_id != GUI_NO_WINDOW &&
        desktop->focused_window_id < GUI_MAX_WINDOWS &&
        desktop->windows[desktop->focused_window_id].used != 0 &&
        (desktop->windows[desktop->focused_window_id].flags &
         GUI_WINDOW_NO_FOCUS) == 0U) {
        return 0;
    }

    for (uint32_t i = 0; i < GUI_MAX_WINDOWS; i++) {
        if (desktop->windows[i].used != 0 &&
            (desktop->windows[i].flags & GUI_WINDOW_NO_FOCUS) == 0U) {
            desktop->focused_window_id = i;
            return 0;
        }
    }

    desktop->focused_window_id = GUI_NO_WINDOW;
    return -1;
}

uint32_t gui_window_for_pid(gui_desktop_t *desktop, uint32_t owner_pid,
                            uint32_t index) {
    uint32_t found = 0;

    if (desktop == 0 || owner_pid == GUI_NO_OWNER) {
        return GUI_NO_WINDOW;
    }

    for (uint32_t i = 0; i < GUI_MAX_WINDOWS; i++) {
        const gui_window_t *window = &desktop->windows[i];
        if (window->used == 0 || window->owner_pid != owner_pid ||
            (window->flags & GUI_WINDOW_SKIP_TASKBAR) != 0U) {
            continue;
        }
        if (found == index) {
            return i;
        }
        found++;
    }

    return GUI_NO_WINDOW;
}

static void gui_draw_window(fb_t *fb, const gui_desktop_t *desktop,
                            uint32_t index, const gui_window_t *window) {
    uint32_t border;

    if (fb == 0 || window == 0 || window->used == 0) {
        return;
    }

    border = window->border_color;
    if (desktop != 0 && desktop->focused_window_id == index &&
        (window->flags & GUI_WINDOW_NO_FOCUS) == 0U) {
        border = 0xffe0e8f0U;
    }

    if (window->owner_drawn != 0 && window->backing != 0) {
        uint32_t content_h = window->h > window->title_h
                                 ? window->h - window->title_h
                                 : 0U;
        fb_blit(fb, window->x, window->y + window->title_h,
                window->backing, window->w, content_h);
    } else {
        fb_fillrect(fb, window->x, window->y, window->w, window->h, border);
        if (window->w > 2U && window->h > 2U) {
            fb_fillrect(fb, window->x + 1U, window->y + 1U,
                        window->w - 2U, window->h - 2U,
                        window->bg_color);
        }
    }

    fb_fillrect(fb, window->x, window->y, window->w, 1U, border);
    if (window->h > 1U) {
        fb_fillrect(fb, window->x, window->y + window->h - 1U,
                    window->w, 1U, border);
    }
    if (window->h > 2U) {
        fb_fillrect(fb, window->x, window->y + 1U, 1U,
                    window->h - 2U, border);
        if (window->w > 1U) {
            fb_fillrect(fb, window->x + window->w - 1U,
                        window->y + 1U, 1U, window->h - 2U, border);
        }
    }

    if (window->title_h > 0U && window->title[0] != '\0') {
        uint32_t bar_color = gui_blend_color(border, 0xff000000U, 2U, 3U);
        uint32_t text_y = window->y +
                          (window->title_h > FONT_GLYPH_HEIGHT
                               ? (window->title_h - FONT_GLYPH_HEIGHT) / 2U
                               : 0U);
        uint32_t cb_x = 0, cb_y = 0, cb_w = 0, cb_h = 0;

        fb_fillrect(fb, window->x, window->y, window->w, window->title_h,
                    bar_color);
        if (window->h > window->title_h + 1U) {
            fb_fillrect(fb, window->x, window->y + window->title_h,
                        window->w, 1U, border);
        }
        font_draw_text_clipped(fb, window->x + 2U, text_y, window->title_h,
                               window->title, 0xfff0f4f8U);
        if (gui_close_box_rect(window, &cb_x, &cb_y, &cb_w, &cb_h)) {
            uint32_t cb_bg = desktop != 0 && desktop->focused_window_id == index
                                 ? 0xffe04a4aU
                                 : 0xff8a3030U;
            fb_fillrect(fb, cb_x, cb_y, cb_w, cb_h, cb_bg);
            fb_draw_line(fb, (int32_t)(cb_x + 2U), (int32_t)(cb_y + 2U),
                         (int32_t)(cb_x + cb_w - 3U),
                         (int32_t)(cb_y + cb_h - 3U), 0xfff8f8f8U);
            fb_draw_line(fb, (int32_t)(cb_x + cb_w - 3U),
                         (int32_t)(cb_y + 2U), (int32_t)(cb_x + 2U),
                         (int32_t)(cb_y + cb_h - 3U), 0xfff8f8f8U);
        }
    }
}

void gui_draw(gui_desktop_t *desktop) {
    uint32_t height;
    uint32_t bottom_color;
    uint32_t last_z;

    if (desktop == 0 || desktop->fb == 0) {
        return;
    }

    height = desktop->fb->height;
    bottom_color = gui_blend_color(desktop->background_color, 0xff000000U,
                                   3U, 4U);
    for (uint32_t row = 0; row < height; row++) {
        uint32_t color = gui_blend_color(desktop->background_color,
                                         bottom_color, row, height - 1U);
        fb_fillrect(desktop->fb, 0, row, desktop->fb->width, 1U, color);
    }

    last_z = 0;
    for (;;) {
        uint32_t best = GUI_MAX_WINDOWS;
        uint32_t best_z = UINT32_MAX;
        for (uint32_t i = 0; i < GUI_MAX_WINDOWS; i++) {
            const gui_window_t *window = &desktop->windows[i];
            if (window->used == 0 || window->z <= last_z) {
                continue;
            }
            if (window->z < best_z) {
                best = i;
                best_z = window->z;
            }
        }
        if (best == GUI_MAX_WINDOWS) {
            break;
        }
        gui_draw_window(desktop->fb, desktop, best,
                        &desktop->windows[best]);
        last_z = best_z;
    }

    if (desktop->cursor.visible != 0U) {
        gui_draw_cursor(desktop->fb, desktop->cursor.x, desktop->cursor.y,
                        desktop->cursor.shape);
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
    return g_gui_dirty != 0U ? 1 : 0;
}

void gui_clear_dirty(void) {
    g_gui_dirty = 0;
    if (g_gui_active != 0U) {
        gui_damage_clear(&g_gui_desktop);
    }
}

void gui_request_redraw(void) {
    g_gui_dirty = 1;
    if (g_gui_active != 0U) {
        gui_damage_add_full(&g_gui_desktop);
    }
}

void gui_damage_add(gui_desktop_t *desktop, int32_t x, int32_t y,
                    int32_t w, int32_t h) {
    if (desktop == 0 || desktop->fb == 0 || w <= 0 || h <= 0) {
        return;
    }

    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x >= (int32_t)desktop->fb->width ||
        y >= (int32_t)desktop->fb->height) {
        return;
    }
    if (x + w > (int32_t)desktop->fb->width) {
        w = (int32_t)desktop->fb->width - x;
    }
    if (y + h > (int32_t)desktop->fb->height) {
        h = (int32_t)desktop->fb->height - y;
    }
    if (w <= 0 || h <= 0) {
        return;
    }

    desktop->damage_full = 1;
    desktop->damage_count = 0;
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

    switch (event->type) {
    case INPUT_EVENT_MOUSE_MOVE:
        gui_cursor_move(&g_gui_desktop, event->data.mouse_move.dx,
                        event->data.mouse_move.dy);
        if (g_gui_desktop.drag_window_id != GUI_NO_WINDOW) {
            gui_drag_update(&g_gui_desktop, g_gui_desktop.cursor.x,
                            g_gui_desktop.cursor.y);
        }
        (void)gui_dispatch_input(&g_gui_desktop, event);
        return 0;
    case INPUT_EVENT_MOUSE_BUTTON:
        gui_cursor_button(&g_gui_desktop,
                          event->data.mouse_button.button,
                          event->data.mouse_button.pressed);
        if (event->data.mouse_button.button == 0U) {
            if (event->data.mouse_button.pressed != 0U) {
                int32_t hit = gui_hit_test(&g_gui_desktop,
                                           g_gui_desktop.cursor.x,
                                           g_gui_desktop.cursor.y);
                if (hit != (int32_t)GUI_NO_WINDOW && hit >= 0) {
                    gui_window_t *window = &g_gui_desktop.windows[hit];
                    uint32_t cb_x = 0, cb_y = 0, cb_w = 0, cb_h = 0;
                    if (gui_close_box_rect(window, &cb_x, &cb_y, &cb_w,
                                           &cb_h) &&
                        g_gui_desktop.cursor.x >= (int32_t)cb_x &&
                        g_gui_desktop.cursor.x < (int32_t)(cb_x + cb_w) &&
                        g_gui_desktop.cursor.y >= (int32_t)cb_y &&
                        g_gui_desktop.cursor.y < (int32_t)(cb_y + cb_h)) {
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
                        (void)gui_window_push_event(window,
                                                    GUI_EVENT_MOUSE_CLICK,
                                                    g_gui_desktop.cursor.x,
                                                    g_gui_desktop.cursor.y);
                    }
                }
            } else {
                gui_drag_end(&g_gui_desktop);
            }
        }
        return 0;
    case INPUT_EVENT_KEY_PRESS:
        (void)gui_focus_window_ensure(&g_gui_desktop);
        if (g_gui_desktop.focused_window_id != GUI_NO_WINDOW) {
            gui_window_t *window =
                &g_gui_desktop.windows[g_gui_desktop.focused_window_id];
            (void)gui_window_push_event(window, GUI_EVENT_KEY_PRESS,
                                        (int32_t)event->data.key.key, 0);
        }
        return 0;
    case INPUT_EVENT_KEY_RELEASE:
        if (g_gui_desktop.focused_window_id != GUI_NO_WINDOW) {
            gui_window_t *window =
                &g_gui_desktop.windows[g_gui_desktop.focused_window_id];
            (void)gui_window_push_event(window, GUI_EVENT_KEY_RELEASE,
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
    gui_draw(&g_gui_desktop);
    gui_damage_clear(&g_gui_desktop);
    g_gui_active = 1;
}
