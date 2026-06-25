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

/*
 * Min / max / close boxes are siblings along the right edge of the
 * title bar. Close sits at the right edge, minimise one button-width
 * to its left, maximise between them. We compute the right edge of
 * the row once and step backwards so the helpers stay in sync if the
 * button width changes.
 */
static int gui_min_max_btn_row(const gui_window_t *window, uint32_t *out_x,
                               uint32_t *out_y, uint32_t *out_w,
                               uint32_t *out_h) {
    uint32_t bw;
    uint32_t bh;

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
    if (window->w < 3U * bw + 4U * GUI_CLOSE_BTN_PAD) {
        return 0;
    }
    *out_w = bw;
    *out_h = bh;
    *out_y = window->y + GUI_CLOSE_BTN_PAD;
    /* Right edge of the row, leaving room for all three buttons plus
     * their internal padding gaps. */
    *out_x = window->x + window->w - bw - GUI_CLOSE_BTN_PAD;
    return 1;
}

int gui_minimize_button_rect(const gui_window_t *window, uint32_t *out_x,
                             uint32_t *out_y, uint32_t *out_w,
                             uint32_t *out_h) {
    uint32_t right_x;
    if (!gui_min_max_btn_row(window, &right_x, out_y, out_w, out_h)) {
        return 0;
    }
    /* Two buttons to the left of close. */
    *out_x = right_x - 2U * (*out_w) - 2U * GUI_CLOSE_BTN_PAD;
    return 1;
}

int gui_maximize_button_rect(const gui_window_t *window, uint32_t *out_x,
                             uint32_t *out_y, uint32_t *out_w,
                             uint32_t *out_h) {
    uint32_t right_x;
    if (!gui_min_max_btn_row(window, &right_x, out_y, out_w, out_h)) {
        return 0;
    }
    /* One button to the left of close. */
    *out_x = right_x - (*out_w) - GUI_CLOSE_BTN_PAD;
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
    int32_t content_x;
    int32_t content_y;

    if (desktop == 0) {
        return;
    }

    desktop->cursor.shape = GUI_CURSOR_ARROW;
    hit = gui_hit_test(desktop, desktop->cursor.x, desktop->cursor.y);
    if (hit == (int32_t)GUI_NO_WINDOW || hit < 0) {
        return;
    }

    window = &desktop->windows[hit];

    /* Per-window cursor-shape regions win over the title-bar default.
     * Walk the slots in ascending order; the first region whose
     * content-local rect contains the cursor sets the shape. This
     * lets apps override the cursor for clickable widgets drawn
     * inside the window (launcher buttons, scroll bars, etc.) without
     * touching the global SYS_CURSOR_SET_SHAPE. */
    content_x = desktop->cursor.x - (int32_t)window->x;
    content_y = desktop->cursor.y - ((int32_t)window->y +
                                     (int32_t)window->title_h);
    for (uint32_t i = 0; i < GUI_MAX_CURSOR_REGIONS; i++) {
        if (window->cursor_regions[i].used == 0U) {
            continue;
        }
        int32_t rx = window->cursor_regions[i].x;
        int32_t ry = window->cursor_regions[i].y;
        int32_t rw = window->cursor_regions[i].w;
        int32_t rh = window->cursor_regions[i].h;
        if (content_x >= rx && content_x < rx + rw &&
            content_y >= ry && content_y < ry + rh) {
            desktop->cursor.shape =
                (uint8_t)window->cursor_regions[i].shape;
            return;
        }
    }

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

const gui_window_t *gui_window_lookup(const gui_desktop_t *desktop,
                                      uint32_t window_id) {
    if (desktop == 0 || window_id >= GUI_MAX_WINDOWS) {
        return 0;
    }
    if (desktop->windows[window_id].used == 0U) {
        return 0;
    }
    return &desktop->windows[window_id];
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

int gui_register_cursor_region(gui_desktop_t *desktop, uint32_t window_id,
                               uint32_t slot, int32_t x, int32_t y,
                               uint32_t w, uint32_t h, uint32_t shape) {
    gui_window_t *window;

    if (desktop == 0 || window_id >= GUI_MAX_WINDOWS ||
        slot >= GUI_MAX_CURSOR_REGIONS) {
        return -1;
    }
    window = &desktop->windows[window_id];
    if (window->used == 0U) {
        return -1;
    }

    if (shape == GUI_CURSOR_REGION_DELETE) {
        window->cursor_regions[slot].used = 0U;
        window->cursor_regions[slot].x = 0;
        window->cursor_regions[slot].y = 0;
        window->cursor_regions[slot].w = 0;
        window->cursor_regions[slot].h = 0;
        window->cursor_regions[slot].shape = 0;
        if (window_id == desktop->focused_window_id) {
            gui_refresh_cursor_shape(desktop);
        }
        return 0;
    }

    if (shape != GUI_CURSOR_ARROW && shape != GUI_CURSOR_HAND) {
        return -1;
    }

    window->cursor_regions[slot].x = x;
    window->cursor_regions[slot].y = y;
    window->cursor_regions[slot].w = (int32_t)w;
    window->cursor_regions[slot].h = (int32_t)h;
    window->cursor_regions[slot].shape = shape;
    window->cursor_regions[slot].used = 1U;

    if (window_id == desktop->focused_window_id) {
        gui_refresh_cursor_shape(desktop);
    }
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
        for (uint32_t j = 0; j < GUI_MAX_CURSOR_REGIONS; j++) {
            window->cursor_regions[j].x = 0;
            window->cursor_regions[j].y = 0;
            window->cursor_regions[j].w = 0;
            window->cursor_regions[j].h = 0;
            window->cursor_regions[j].shape = 0;
            window->cursor_regions[j].used = 0;
        }
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
        window->minimized = 0U;
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
    for (uint32_t j = 0; j < GUI_MAX_CURSOR_REGIONS; j++) {
        window->cursor_regions[j].x = 0;
        window->cursor_regions[j].y = 0;
        window->cursor_regions[j].w = 0;
        window->cursor_regions[j].h = 0;
        window->cursor_regions[j].shape = 0;
        window->cursor_regions[j].used = 0;
    }
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

/*
 * Destroy every window whose owner_pid matches `pid`. Called when
 * the process becomes a zombie so the desktop does not accumulate
 * stale windows the user cannot close (close buttons need a live
 * owner to deliver GUI_EVENT_CLOSE; the kernel only destroys
 * windows of dead owners on a click). Without this, a long
 * session that spawns and exits many apps would fill the desktop's
 * 16-window pool with ghosts. Idempotent: GUI_NO_OWNER (no match)
 * or a pid with no windows is a no-op.
 */
void gui_destroy_windows_for_pid(gui_desktop_t *desktop, uint32_t pid) {
    if (desktop == 0 || pid == GUI_NO_OWNER) {
        return;
    }
    /*
     * Walk back-to-front so a destruction does not shift the
     * indices we still need to inspect. gui_destroy_window zeroes
     * the slot, so forward iteration would skip windows.
     */
    for (uint32_t i = GUI_MAX_WINDOWS; i-- > 0;) {
        if (desktop->windows[i].used != 0 &&
            desktop->windows[i].owner_pid == pid) {
            (void)gui_destroy_window(desktop, i);
        }
    }
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

int gui_resize_window(gui_desktop_t *desktop, uint32_t window_id, uint32_t x,
                      uint32_t y, uint32_t w, uint32_t h) {
    gui_window_t *window;
    int32_t old_x;
    int32_t old_y;
    int32_t new_x;
    int32_t new_y;
    int32_t old_w;
    int32_t old_h;
    int32_t ux0;
    int32_t uy0;
    int32_t ux1;
    int32_t uy1;
    int32_t size_changed;

    if (desktop == 0 || desktop->fb == 0 || window_id >= GUI_MAX_WINDOWS ||
        w < 2U || h < 2U ||
        x >= desktop->fb->width || y >= desktop->fb->height ||
        x + w > desktop->fb->width || y + h > desktop->fb->height ||
        desktop->windows[window_id].used == 0) {
        return -1;
    }

    window = &desktop->windows[window_id];
    old_x = (int32_t)window->x;
    old_y = (int32_t)window->y;
    old_w = (int32_t)window->w;
    old_h = (int32_t)window->h;
    new_x = (int32_t)x;
    new_y = (int32_t)y;
    size_changed = (old_w != (int32_t)w || old_h != (int32_t)h) ? 1 : 0;

    window->x = x;
    window->y = y;
    window->w = w;
    window->h = h;
    gui_refresh_cursor_shape(desktop);

    if (size_changed != 0) {
        /* The backing buffer is sized in window pixels; gui_window_ensure_backing
         * keeps the existing allocation if it is large enough and reallocates
         * otherwise. The new backing is cleared to bg_color so the owner
         * does not see stale content from the previous size before it can
         * repaint. */
        (void)gui_window_ensure_backing(window);
        /* Tell the owner the new dimensions. The event lands on the owner's
         * queue; apps that ignore it still get a fresh blank content area
         * to draw into. */
        (void)gui_window_push_event(window, GUI_EVENT_RESIZE,
                                    (int32_t)w, (int32_t)h);
    }

    ux0 = old_x < new_x ? old_x : new_x;
    uy0 = old_y < new_y ? old_y : new_y;
    ux1 = old_x + old_w > new_x + (int32_t)w ? old_x + old_w
                                              : new_x + (int32_t)w;
    uy1 = old_y + old_h > new_y + (int32_t)h ? old_y + old_h
                                              : new_y + (int32_t)h;
    gui_damage_add(desktop, ux0, uy0, ux1 - ux0, uy1 - uy0);
    return 0;
}

int gui_window_get_bounds(const gui_window_t *window, uint32_t *out_x,
                          uint32_t *out_y, uint32_t *out_w, uint32_t *out_h) {
    if (window == 0 || window->used == 0) {
        return -1;
    }
    if (out_x != 0) {
        *out_x = window->x;
    }
    if (out_y != 0) {
        *out_y = window->y;
    }
    if (out_w != 0) {
        *out_w = window->w;
    }
    if (out_h != 0) {
        *out_h = window->h;
    }
    return 0;
}

int gui_window_minimize(gui_desktop_t *desktop, uint32_t window_id) {
    gui_window_t *window;

    if (desktop == 0 || window_id >= GUI_MAX_WINDOWS) {
        return -1;
    }
    window = &desktop->windows[window_id];
    if (window->used == 0 || window->minimized != 0U) {
        return -1;
    }
    window->minimized = 1U;
    /* The owner learns through GUI_EVENT_MINIMIZE on its event queue
     * — the kernel does not invent a size for the app to wake up into,
     * the app decides what "minimised" means for itself (pause work,
     * drop cached layouts, etc.). */
    (void)gui_window_push_event(window, GUI_EVENT_MINIMIZE, 0, 0);
    gui_request_redraw();
    return 0;
}

int gui_window_restore(gui_desktop_t *desktop, uint32_t window_id) {
    gui_window_t *window;

    if (desktop == 0 || window_id >= GUI_MAX_WINDOWS) {
        return -1;
    }
    window = &desktop->windows[window_id];
    if (window->used == 0 || window->minimized == 0U) {
        return -1;
    }
    window->minimized = 0U;
    /* Raise the window so it lands on top after restore, matching
     * the focus semantics the panel already uses for non-minimised
     * clicks. NO_FOCUS keeps docks (which can't be focused) where
     * they were. */
    if ((gui_window_effective_flags(window) & GUI_WINDOW_NO_FOCUS) == 0U) {
        desktop->focused_window_id = window_id;
    }
    window->z = desktop->next_z++;
    (void)gui_window_push_event(window, GUI_EVENT_MAXIMIZE, 0, 0);
    gui_request_redraw();
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
    /* Minimised windows are completely off-screen. The panel greys the
     * matching running-apps slot and clicking it calls
     * gui_window_restore, which clears this flag and re-runs this
     * draw path with the window visible. */
    if (window->minimized != 0U) {
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
        /* Min / max / close siblings along the right edge. They are
         * only drawn when the title bar is tall enough to fit three
         * buttons side by side; smaller title bars fall back to the
         * close-only layout. */
        uint32_t mb_x = 0, mb_y = 0, mb_w = 0, mb_h = 0;
        uint32_t xb_x = 0, xb_y = 0, xb_w = 0, xb_h = 0;
        uint32_t min_bg = desktop != 0 && desktop->focused_window_id == index
                              ? 0xffa0a8b8U
                              : 0xff586070U;
        uint32_t max_bg = desktop != 0 && desktop->focused_window_id == index
                              ? 0xffa0a8b8U
                              : 0xff586070U;
        uint32_t cls_bg = desktop != 0 && desktop->focused_window_id == index
                              ? 0xffe04a4aU
                              : 0xff8a3030U;
        if (gui_minimize_button_rect(window, &mb_x, &mb_y, &mb_w, &mb_h)) {
            fb_fillrect(fb, mb_x, mb_y, mb_w, mb_h, min_bg);
            /* A single horizontal bar at the bottom of the box reads
             * as a minimise glyph at this resolution. */
            fb_fillrect(fb, (int32_t)(mb_x + 2U),
                        (int32_t)(mb_y + mb_h - 3U),
                        (int32_t)(mb_w - 4U), 1U, 0xfff0f4f8U);
        }
        if (gui_maximize_button_rect(window, &xb_x, &xb_y, &xb_w, &xb_h)) {
            fb_fillrect(fb, xb_x, xb_y, xb_w, xb_h, max_bg);
            /* A hollow square outline reads as a maximise glyph. */
            fb_draw_rect(fb, (int32_t)(xb_x + 2U), (int32_t)(xb_y + 2U),
                         (int32_t)(xb_w - 4U), (int32_t)(xb_h - 4U),
                         0xfff0f4f8U);
        }
        if (gui_close_box_rect(window, &cb_x, &cb_y, &cb_w, &cb_h)) {
            fb_fillrect(fb, cb_x, cb_y, cb_w, cb_h, cls_bg);
            fb_draw_line(fb, (int32_t)(cb_x + 2U), (int32_t)(cb_y + 2U),
                         (int32_t)(cb_x + cb_w - 3U),
                         (int32_t)(cb_y + cb_h - 3U), 0xfff8f8f8U);
            fb_draw_line(fb, (int32_t)(cb_x + cb_w - 3U),
                         (int32_t)(cb_y + 2U), (int32_t)(cb_x + 2U),
                         (int32_t)(cb_y + cb_h - 3U), 0xfff8f8f8U);
        }
    }
}

/*
 * Find the next used window covering (x, row) whose left edge is at or
 * after x. Returns the window index or GUI_MAX_WINDOWS if none. Used by
 * the full-redraw path to skip window spans when painting the gradient.
 */
static uint32_t gui_next_window_at_or_after(const gui_desktop_t *desktop,
                                            uint32_t row, uint32_t x) {
    uint32_t best = GUI_MAX_WINDOWS;
    uint32_t best_x = UINT32_MAX;
    for (uint32_t i = 0; i < GUI_MAX_WINDOWS; i++) {
        const gui_window_t *w = &desktop->windows[i];
        if (w->used == 0 || w->minimized != 0U) {
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
        if (window->used == 0 || window->minimized != 0U ||
            window->z <= min_z) {
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
                    uint32_t bx_x = 0, bx_y = 0, bx_w = 0, bx_h = 0;
                    int32_t cx = g_gui_desktop.cursor.x;
                    int32_t cy = g_gui_desktop.cursor.y;
                    int button = 0;
                    if (gui_minimize_button_rect(window, &bx_x, &bx_y, &bx_w,
                                                  &bx_h) &&
                        cx >= (int32_t)bx_x &&
                        cx <  (int32_t)(bx_x + bx_w) &&
                        cy >= (int32_t)bx_y &&
                        cy <  (int32_t)(bx_y + bx_h)) {
                        button = 1; /* minimise */
                    } else if (gui_maximize_button_rect(window, &bx_x, &bx_y,
                                                       &bx_w, &bx_h) &&
                               cx >= (int32_t)bx_x &&
                               cx <  (int32_t)(bx_x + bx_w) &&
                               cy >= (int32_t)bx_y &&
                               cy <  (int32_t)(bx_y + bx_h)) {
                        button = 2; /* maximise */
                    } else if (gui_close_box_rect(window, &bx_x, &bx_y, &bx_w,
                                                   &bx_h) &&
                               cx >= (int32_t)bx_x &&
                               cx <  (int32_t)(bx_x + bx_w) &&
                               cy >= (int32_t)bx_y &&
                               cy <  (int32_t)(bx_y + bx_h)) {
                        button = 3; /* close */
                    }
                    if (button == 3) {
                        if (gui_window_owner_dead(window)) {
                            (void)gui_destroy_window(&g_gui_desktop,
                                                     (uint32_t)hit);
                        } else {
                            (void)gui_window_push_event(window,
                                                        GUI_EVENT_CLOSE, 0, 0);
                        }
                    } else if (button == 1) {
                        (void)gui_window_minimize(&g_gui_desktop,
                                                  (uint32_t)hit);
                    } else if (button == 2) {
                        (void)gui_window_push_event(window,
                                                    GUI_EVENT_MAXIMIZE, 0, 0);
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
